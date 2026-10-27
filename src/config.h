#pragma once
// 全局运行配置：从 JSON 配置文件加载
// 默认读取路径 /data/local/tmp/ai_overlay/config.json（可用 --config 覆盖）

#include <string>
#include <fstream>
#include <android/log.h>
#include "json.hpp"

#define LOG_TAG "AiOverlay"
// 双通道日志：logcat + 终端 stdout（行缓冲，确保实时可见）
#include <cstdio>
#define LOGI(...) do { \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
    printf("[I] " __VA_ARGS__); printf("\n"); fflush(stdout); \
} while (0)
#define LOGE(...) do { \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); \
    fprintf(stderr, "[E] " __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); \
} while (0)

struct AppConfig {
    std::string base_url   = "https://api.openai.com";  // OpenAI 兼容接口地址
    std::string api_key    = "sk-在此填入你的KEY";
    std::string model      = "gpt-4o-mini";             // 需支持视觉(图片输入)的模型
    std::string prompt     = "请描述并解答这张屏幕截图中的内容，回答尽量简洁。";
    int         corner     = 1;         // 悬浮窗位置 0=左上 1=右上 2=左下 3=右下
    float       opacity    = 0.35f;     // 悬浮窗不透明度（0~1，越小越透明）
    int         font_size  = 42;        // 界面字号（中文需放入字体文件，见 README）
    std::string font_path  = "/data/local/tmp/ai_overlay/font.ttf"; // 可选中文字体
    std::string screenshot = "/data/local/tmp/ai_overlay/shot.png";
    int         timeout_ms = 60000;
};

inline bool LoadConfig(const std::string& path, AppConfig& cfg) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOGE("无法打开配置文件: %s，使用默认配置", path.c_str());
        return false;
    }
    try {
        nlohmann::json j;
        f >> j;
        if (j.contains("base_url"))   cfg.base_url   = j["base_url"];
        if (j.contains("api_key"))    cfg.api_key    = j["api_key"];
        if (j.contains("model"))      cfg.model      = j["model"];
        if (j.contains("prompt"))     cfg.prompt     = j["prompt"];
        if (j.contains("corner"))     cfg.corner     = j["corner"];
        if (j.contains("opacity"))    cfg.opacity    = j["opacity"];
        if (j.contains("font_size"))  cfg.font_size  = j["font_size"];
        if (j.contains("font_path"))  cfg.font_path  = j["font_path"];
        if (j.contains("timeout_ms")) cfg.timeout_ms = j["timeout_ms"];
        LOGI("配置已加载: base_url=%s model=%s", cfg.base_url.c_str(), cfg.model.c_str());
        return true;
    } catch (const std::exception& e) {
        LOGE("配置解析失败: %s", e.what());
        return false;
    }
}
