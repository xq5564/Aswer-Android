#pragma once
// 音量键监听：直接读取 /dev/input/event* 中的 EV_KEY / KEY_VOLUMEDOWN
// 需要 root（或 shell）权限才能读取输入设备节点

#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <linux/input.h>
#include "config.h"

class InputWatcher {
public:
    std::atomic<bool> volumeDownPressed{false};
    std::atomic<bool> volumeUpPressed{false};   // 音量+：切换 UI 显示/隐藏
    std::atomic<bool> exitRequested{false};     // 长按音量- 5秒：退出程序

    bool ExitRequested() const { return exitRequested.load(); }

    void Start() {
        running_ = true;
        thread_ = std::thread(&InputWatcher::Loop, this);
    }

    void Stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point volDownPressTime{};
    bool volDownHeld_ = false;

    static bool IsVolumeKeyDevice(int fd) {
        unsigned long keyBits[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) return false;
        auto testBit = [&](int key) {
            return (keyBits[key / (8 * sizeof(unsigned long))] >> (key % (8 * sizeof(unsigned long)))) & 1;
        };
        return testBit(KEY_VOLUMEDOWN) || testBit(KEY_VOLUMEUP);
    }

    void Loop() {
        // 打开所有上报音量键的输入设备
        std::vector<int> fds;
        DIR* dir = opendir("/dev/input");
        if (dir) {
            while (auto* ent = readdir(dir)) {
                if (strncmp(ent->d_name, "event", 5) != 0) continue;
                std::string path = std::string("/dev/input/") + ent->d_name;
                int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
                if (fd < 0) continue;
                if (IsVolumeKeyDevice(fd)) {
                    fds.push_back(fd);
                    LOGI("监听输入设备: %s", path.c_str());
                } else {
                    close(fd);
                }
            }
            closedir(dir);
        }
        if (fds.empty()) {
            LOGE("未找到任何含音量键的输入设备（权限不足？需要 root/shell 运行）");
            return;
        }

        std::vector<pollfd> pfds(fds.size());
        while (running_) {
            for (size_t i = 0; i < fds.size(); i++) {
                pfds[i].fd = fds[i];
                pfds[i].events = POLLIN;
                pfds[i].revents = 0;
            }
            int ret = poll(pfds.data(), pfds.size(), 200);
            if (ret <= 0) continue;
            for (auto& p : pfds) {
                if (!(p.revents & POLLIN)) continue;
                input_event ev{};
                while (read(p.fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ev.type != EV_KEY) continue;
                    if (ev.code == KEY_VOLUMEUP && ev.value == 1) {
                        volumeUpPressed = true;    // 主循环消费
                        continue;
                    }
                    if (ev.code == KEY_VOLUMEDOWN) {
                        auto now = std::chrono::steady_clock::now();
                        if (ev.value == 1 && !volDownHeld_) {          // 按下
                            volDownHeld_ = true;
                            volDownPressTime = now;
                        } else if (ev.value == 0 && volDownHeld_) {     // 松开
                            volDownHeld_ = false;
                            auto held = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           now - volDownPressTime).count();
                            if (held >= 5000) {
                                LOGI("音量- 长按 %lldms，请求退出", (long long)held);
                                exitRequested = true;                  // 长按 >= 5s -> 退出
                            } else {
                                volumeDownPressed = true;               // 短按 -> 提问
                            }
                        }
                        // ev.value == 2 为自动重复，忽略
                    }
                }
            }
        }
        for (int fd : fds) close(fd);
    }
};
