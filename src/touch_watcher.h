#pragma once
// 触摸事件源：root 直读 /dev/input/event*（多点触摸协议 B）
// ABS_MT_SLOT / ABS_MT_TRACKING_ID / ABS_MT_POSITION_X / ABS_MT_POSITION_Y
// BTN_TOUCH + SYN_REPORT -> 归并为单一主触点（喂给 ImGui 用）
// 不注册 InputChannel，系统输入分发不受影响（天然全穿透）

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <linux/input.h>
#include "config.h"

struct TouchState {
    bool  down = false;
    float x = 0, y = 0;   // 已映射到屏幕图层坐标
};

class TouchWatcher {
public:
    // 需要图层尺寸做坐标映射
    void Start(int layerW, int layerH) {
        layerW_ = layerW;
        layerH_ = layerH;
        running_ = true;
        thread_ = std::thread(&TouchWatcher::Loop, this);
    }

    void Stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    // 主渲染线程每帧调用
    bool Poll(TouchState& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lastSeq_ == consumedSeq_) return false;
        consumedSeq_ = lastSeq_;
        out = state_;
        return true;
    }

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    TouchState state_;
    uint64_t lastSeq_ = 0, consumedSeq_ = 0;
    int layerW_ = 1, layerH_ = 1;

    // 协议 B 槽位状态
    static constexpr int MAX_SLOTS = 16;
    struct Slot {
        int32_t trackingId = -1;
        int32_t x = 0, y = 0;
        bool hasXY = false;
    } slots_[MAX_SLOTS];
    int currentSlot_ = 0;

    static bool IsTouchDevice(int fd) {
        unsigned long absBits[(ABS_MAX + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) return false;
        auto has = [&](int abs) {
            return (absBits[abs / (8 * sizeof(unsigned long))] >> (abs % (8 * sizeof(unsigned long)))) & 1;
        };
        return has(ABS_MT_POSITION_X) && has(ABS_MT_POSITION_Y);
    }

    void Loop() {
        struct Range { int min = 0, max = 1; };
        std::vector<int> fds;
        std::vector<Range> rx, ry;

        DIR* dir = opendir("/dev/input");
        if (dir) {
            while (auto* ent = readdir(dir)) {
                if (strncmp(ent->d_name, "event", 5) != 0) continue;
                std::string path = std::string("/dev/input/") + ent->d_name;
                int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
                if (fd < 0) continue;
                if (IsTouchDevice(fd)) {
                    input_absinfo ai{};
                    Range rX{0, 1}, rY{0, 1};
                    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0)
                        rX = {ai.minimum, ai.maximum ? ai.maximum : 1};
                    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0)
                        rY = {ai.minimum, ai.maximum ? ai.maximum : 1};
                    fds.push_back(fd);
                    rx.push_back(rX);
                    ry.push_back(rY);
                    LOGI("监听触摸设备: %s (X:%d-%d Y:%d-%d)", path.c_str(), rX.min, rX.max, rY.min, rY.max);
                } else {
                    close(fd);
                }
            }
            closedir(dir);
        }
        if (fds.empty()) {
            LOGE("未找到触摸屏设备（需要 root 权限）");
            return;
        }

        std::vector<pollfd> pfds(fds.size());
        while (running_) {
            for (size_t i = 0; i < fds.size(); i++) {
                pfds[i].fd = fds[i];
                pfds[i].events = POLLIN;
                pfds[i].revents = 0;
            }

            if (poll(pfds.data(), pfds.size(), 200) <= 0) continue;

            for (size_t i = 0; i < fds.size(); i++) {
                if (!(pfds[i].revents & POLLIN)) continue;
                input_event ev{};
                while (read(fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                    switch (ev.type) {
                        case EV_ABS:
                            switch (ev.code) {
                                case ABS_MT_SLOT: {
                                    int s = ev.value;
                                    if (s >= 0 && s < MAX_SLOTS) currentSlot_ = s;
                                    break;
                                }
                                case ABS_MT_TRACKING_ID:
                                    slots_[currentSlot_].trackingId = ev.value;  // -1 = 离开
                                    break;
                                case ABS_MT_POSITION_X:
                                    slots_[currentSlot_].x = ev.value;
                                    slots_[currentSlot_].hasXY = true;
                                    break;
                                case ABS_MT_POSITION_Y:
                                    slots_[currentSlot_].y = ev.value;
                                    slots_[currentSlot_].hasXY = true;
                                    break;
                            }
                            break;
                        case EV_KEY:
                            if (ev.code == BTN_TOUCH) { /* 状态在 SYN 时统一计算 */ }
                            break;
                        case EV_SYN:
                            if (ev.code == SYN_REPORT) {
                                // 找第一个活跃槽作为主触点
                                bool down = false;
                                float x = 0, y = 0;
                                for (auto& s : slots_) {
                                    if (s.trackingId >= 0 && s.hasXY) {
                                        // 设备坐标 -> 图层坐标
                                        x = (float)(s.x - rx[i].min) * layerW_ / (rx[i].max - rx[i].min);
                                        y = (float)(s.y - ry[i].min) * layerH_ / (ry[i].max - ry[i].min);
                                        down = true;
                                        break;
                                    }
                                }
                                std::lock_guard<std::mutex> lock(mutex_);
                                state_.down = down;
                                state_.x = x;
                                state_.y = y;
                                lastSeq_++;
                            }
                            break;
                    }
                }
            }
        }
        for (int fd : fds) close(fd);
    }
};
