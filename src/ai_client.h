#pragma once
// AI 客户端：截图 -> Base64 -> OpenAI 兼容的视觉对话接口 -> 返回答案文本
// HTTPS 由 cpp-httplib + mbedTLS 提供（编译时定义 CPPHTTPLIB_MBEDTLS_SUPPORT）

#include <string>
#include <fstream>
#include <vector>
#include <sstream>
#include "config.h"
#include "json.hpp"

#include "httplib.h"

namespace ai {

// ---- Base64 编码 ----
inline std::string Base64Encode(const unsigned char* data, size_t len) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(4 * ((len + 2) / 3));
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = data[i] << 16;
        if (i + 1 < len) v |= data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += (i + 1 < len) ? tbl[(v >> 6) & 63] : '=';
        out += (i + 2 < len) ? tbl[v & 63] : '=';
    }
    return out;
}

// 调用视觉模型，返回 AI 的回答文本；失败时返回以 "[错误]" 开头的描述
inline std::string AskWithImage(const AppConfig& cfg, const std::string& imagePath) {
    // 读图片
    std::ifstream f(imagePath, std::ios::binary);
    if (!f) return "[错误] 无法读取截图文件";
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) return "[错误] 截图文件为空";

    std::string dataUri = "data:image/png;base64," + Base64Encode(buf.data(), buf.size());

    // 构造请求 JSON（OpenAI Chat Completions 格式）
    nlohmann::json body = {
        {"model", cfg.model},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", nlohmann::json::array({
                {{"type", "text"}, {"text", cfg.prompt}},
                {{"type", "image_url"}, {"image_url", {{"url", dataUri}}}}
            })}}
        })}
    };

    // 规范化 base_url：去掉末尾斜杠；若已包含 /v1 则请求路径不再重复拼接
    std::string base = cfg.base_url;
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string apiPath = "/v1/chat/completions";
    if (base.size() >= 3 && base.compare(base.size() - 3, 3, "/v1") == 0) {
        base.resize(base.size() - 3);
    }

    httplib::Client cli(base);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(cfg.timeout_ms / 1000, 0);
    cli.set_write_timeout(cfg.timeout_ms / 1000, 0);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + cfg.api_key},
        {"Content-Type", "application/json"}
    };

    LOGI("发送请求: POST %s%s (图片 %zu KB)", base.c_str(), apiPath.c_str(), buf.size() / 1024);
    auto res = cli.Post(apiPath, headers, body.dump(), "application/json");
    if (!res) {
        LOGE("网络请求失败: %s", httplib::to_string(res.error()).c_str());
        return std::string("[错误] 网络请求失败: ") + httplib::to_string(res.error());
    }
    LOGI("收到响应: HTTP %d, %zu 字节", res->status, res->body.size());
    if (res->status != 200) {
        return "[错误] HTTP " + std::to_string(res->status) + ": " +
               res->body.substr(0, 300);
    }

    try {
        auto j = nlohmann::json::parse(res->body);
        return j["choices"][0]["message"]["content"].get<std::string>();
    } catch (const std::exception& e) {
        return std::string("[错误] 解析响应失败: ") + e.what();
    }
}

} // namespace ai
