#pragma once
// 截图：调用系统 screencap 命令（shell/root 权限），输出 PNG 到指定路径

#include <string>
#include <cstdio>
#include <sys/stat.h>
#include "config.h"

inline bool CaptureScreen(const std::string& outPath) {
    // 确保输出目录存在
    auto pos = outPath.find_last_of('/');
    if (pos != std::string::npos) {
        std::string dir = outPath.substr(0, pos);
        std::string mk = "mkdir -p '" + dir + "'";
        system(mk.c_str());
    }

    // 捕获 screencap 的错误输出，便于诊断
    std::string cmd = "screencap -p '" + outPath + "' 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOGE("无法执行 screencap");
        return false;
    }
    char buf[256];
    std::string errOut;
    while (fgets(buf, sizeof(buf), pipe)) errOut += buf;
    int ret = pclose(pipe);
    if (ret != 0) {
        LOGE("screencap 执行失败 ret=%d: %s", ret, errOut.c_str());
        return false;
    }
    struct stat st{};
    if (stat(outPath.c_str(), &st) != 0 || st.st_size < 1024) {
        LOGE("截图文件无效: %s", outPath.c_str());
        return false;
    }
    LOGI("截图成功: %s (%lld bytes)", outPath.c_str(), (long long)st.st_size);
    return true;
}
