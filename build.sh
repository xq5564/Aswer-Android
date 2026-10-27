#!/usr/bin/env bash
# 一键构建脚本（Linux/macOS）
# 用法: ./build.sh /path/to/android-ndk
set -e

NDK=${1:-$ANDROID_NDK_HOME}
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "用法: ./build.sh /path/to/android-ndk-r27"
    echo "或设置环境变量 ANDROID_NDK_HOME"
    exit 1
fi

# 首次构建时自动下载依赖（若 third_party 下不存在）
TP=third_party
if [ ! -f $TP/json.hpp ]; then
    curl -L -o $TP/json.hpp https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp
fi
if [ ! -f $TP/httplib.h ]; then
    curl -L -o $TP/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h
fi
if [ ! -d $TP/mbedtls ]; then
    curl -L -o /tmp/mbedtls.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/v3.6.2/mbedtls-3.6.2.tar.bz2
    tar xjf /tmp/mbedtls.tar.bz2 -C $TP && mv $TP/mbedtls-3.6.2 $TP/mbedtls
fi

HOST_TAG=$(uname | tr '[:upper:]' '[:lower:]')-x86_64
cmake -B build -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

echo ""
echo "构建完成: build/aswer"
echo "推送到设备运行:"
echo "  adb push build/aswer /data/local/tmp/aswer/"
echo "  adb push config.json /data/local/tmp/aswer/"
echo "  adb shell 'su -c /data/local/tmp/aswer/aswer'   # 或以 shell 权限直接运行"
