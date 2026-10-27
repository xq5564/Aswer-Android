// AI 屏幕助手主程序
// 运行流程：初始化 Surface/EGL/ImGui -> 监听音量- -> 截图 -> AI 请求(工作线程) -> 角落绘制答案
// 用法: ai_overlay [--config /path/config.json]

#include <atomic>
#include <thread>
#include <string>
#include <cstring>
#include <unistd.h>
#include <climits>

// ANativeWindowCreator 需要的外部符号定义（系统大版本号，由 ApiResolver 填充）
namespace android::anative_window_creator::detail::compat {
size_t SystemVersion = 0;
}

#include "config.h"
#include "input_watcher.h"
#include "touch_watcher.h"
#include "screenshot.h"
#include "ai_client.h"
#include "overlay.h"

namespace {

enum class State { Idle, Capturing, Asking, Done };

std::atomic<State> g_state{State::Idle};
std::string g_answer;
std::string g_status;
std::atomic<bool> g_workerBusy{false};

void Worker(AppConfig cfg) {
    g_workerBusy = true;
    g_answer.clear();

    g_state = State::Capturing;
    g_status = "正在截图...";
    if (!CaptureScreen(cfg.screenshot)) {
        g_status.clear();
        g_answer = "[错误] 截图失败，请确认以 shell/root 运行";
        g_state = State::Done;
        g_workerBusy = false;
        return;
    }

    g_state = State::Asking;
    g_status = "正在请求 AI...";
    LOGI("开始 AI 请求...");
    g_answer = ai::AskWithImage(cfg, cfg.screenshot);
    LOGI("AI 返回 (%zu 字符): %.80s", g_answer.size(), g_answer.c_str());

    g_status.clear();
    g_state = State::Done;
    g_workerBusy = false;
}

} // namespace

// 获取程序自身所在目录（跟随二进制文件，而不是写死路径）
static std::string GetExeDir() {
    char buf[PATH_MAX] = {0};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "/data/local/tmp";
    std::string p(buf, n);
    auto pos = p.find_last_of('/');
    return pos == std::string::npos ? "/data/local/tmp" : p.substr(0, pos);
}

int main(int argc, char** argv) {
    const std::string exeDir = GetExeDir();
    std::string configPath = exeDir + "/config.json";  // 默认与程序同级
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc)
            configPath = argv[++i];
    }

    AppConfig cfg;
    // 截图与字体默认也放到程序同目录
    cfg.screenshot = exeDir + "/shot.png";
    cfg.font_path  = exeDir + "/font.ttf";
    LoadConfig(configPath, cfg);
    // 配置文件中相对路径（非 / 开头）一律基于程序目录解析
    auto resolvePath = [&](std::string& p) {
        if (!p.empty() && p.front() != '/') p = exeDir + "/" + p;
    };
    resolvePath(cfg.screenshot);
    resolvePath(cfg.font_path);

    LOGI("AI Overlay 启动, config=%s", configPath.c_str());

    Overlay overlay;
    if (!overlay.Init()) {
        LOGE("渲染层初始化失败，退出");
        return 1;
    }
    overlay.LoadFont(cfg, exeDir);

    InputWatcher input;
    input.Start();
    TouchWatcher touch;
    touch.Start(overlay.ScreenWidth(), overlay.ScreenHeight());

    LOGI("就绪。按下音量- 触发 截图 -> AI -> 显示答案");

    bool running = true;
    bool uiVisible = true;
    while (running) {
        // 长按音量- 5 秒：优雅退出
        if (input.ExitRequested()) {
            LOGI("正在退出程序...");
            running = false;
        }
        // 音量+：切换 UI 显示/隐藏
        if (input.volumeUpPressed.exchange(false)) {
            uiVisible = !uiVisible;
            LOGI("UI %s（音量+ 切换）", uiVisible ? "显示" : "隐藏");
            if (!uiVisible) {
                g_answer.clear();
                g_status.clear();
                g_state = State::Idle;
            }
        }

        // 消费音量- 事件
        if (input.volumeDownPressed.exchange(false)) {
            if (!uiVisible) {
                uiVisible = true;   // 隐藏状态下按音量- 也可唤醒 UI
                LOGI("UI 显示（音量- 唤醒）");
            } else if (!g_workerBusy.load()) {
                LOGI("触发截图 + AI 请求");
                std::thread(Worker, cfg).detach();
            } else if (g_state == State::Done) {
                // 已有结果时再次按下 -> 清除答案
                g_answer.clear();
                g_state = State::Idle;
            }
        }

        // 触摸 -> ImGui（在 NewFrame 之前喂入）
        TouchState ts;
        if (touch.Poll(ts))
            overlay.FeedTouch(ts.x, ts.y, ts.down);

        overlay.NewFrame();
        // 常驻窗口：无任务时显示待机提示；音量+ 隐藏后不绘制任何内容
        std::string status = g_status;
        std::string answer = g_answer;
        if (uiVisible) {
            if (status.empty() && answer.empty())
                status = "待机中（音量- 提问 / 音量+ 隐藏）";
            overlay.DrawAnswerWindow(cfg, status, answer, true);
        }
        overlay.Render();

        usleep(16000);  // ~60fps
    }

    input.Stop();
    touch.Stop();
    overlay.Shutdown();
    return 0;
}
