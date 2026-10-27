# Aswer-Android — C++ ELF + ImGui AI 屏幕助手

纯 C++ 编写的 **Android 原生 ELF 可执行程序**（无 Java、无 APK）：

> 按下 **音量- 键** → 截取当前屏幕 → 将图片发送给 AI（OpenAI 兼容视觉接口，API Key 自行配置）→ 通过 **ImGui + EGL/GLES3** 在屏幕角落以透明悬浮窗绘制答案。

## 功能流程

```
/dev/input 监听 KEY_VOLUMEDOWN
        │
        ▼
screencap -p 截图（PNG）
        │
        ▼
Base64 编码 → POST {base_url}/v1/chat/completions（图片 + 提示词）
        │
        ▼
ImGui 在屏幕角落绘制半透明答案窗口（再次按下音量- 隐藏）
```

## 目录结构

```
├── CMakeLists.txt          构建脚本（NDK 工具链 + mbedTLS）
├── build.sh                一键构建（自动下载依赖）
├── config.json             运行配置（API Key / 模型 / 提示词 / 窗口位置）
├── src/
│   ├── main.cpp            主循环与状态机
│   ├── config.h            配置加载（nlohmann/json）
│   ├── input_watcher.h     /dev/input 音量键监听
│   ├── screenshot.h        screencap 截图封装
│   ├── ai_client.h         HTTPS AI 请求（cpp-httplib + mbedTLS）
│   └── overlay.h           Surface + EGL + ImGui 渲染层
└── third_party/
    ├── ANativeWindowCreator.h   免 Java 创建 Surface（MIT, Bzi-Han/AndroidSurfaceImgui）
    ├── imgui_core/ imgui_backends/   Dear ImGui v1.91.5
    ├── json.hpp / httplib.h          单头文件依赖
    └── mbedtls/                    HTTPS 加密库
```

## 编译

```bash
./build.sh /path/to/android-ndk-r27   # 或设置 ANDROID_NDK_HOME
```

产物：`build/aswer`（arm64-v8a ELF）

## 部署与运行

```bash
adb push build/aswer /data/local/tmp/aswer/
adb push config.json /data/local/tmp/aswer/
# 可选：推送中文字体（否则中文显示为方块）
adb push /path/to/中文字体.ttf /data/local/tmp/aswer/font.ttf

# 需要 root 或 shell 身份（读取输入设备/截屏/创建 Surface 均需要特权）
adb shell
su
cd /data/local/tmp/aswer && ./aswer
# 或: ./aswer --config /sdcard/my_config.json
```

日志查看：`adb logcat -s AiOverlay`

## 配置说明（config.json）

| 字段 | 说明 | 默认 |
|---|---|---|
| `base_url` | OpenAI 兼容接口地址（不含 /v1） | `https://api.openai.com` |
| `api_key` | **必填**，你的 API Key | - |
| `model` | 需支持图片输入的视觉模型 | `gpt-4o-mini` |
| `prompt` | 随截图一起发送的提示词 | 描述并解答截图内容 |
| `corner` | 窗口位置 0左上/1右上/2左下/3右下 | 1 |
| `opacity` | 窗口不透明度 0~1 | 0.85 |
| `font_size` | 字号（px） | 32 |
| `font_path` | 中文字体路径（可选） | - |
| `timeout_ms` | AI 请求超时 | 60000 |

兼容任何 OpenAI Chat Completions 格式的服务（DeepSeek、通义、Moonshot、one-api 中转等），只需修改 `base_url` / `model` / `api_key`。

## 权限要求与限制

- **必须 root（su）或 shell（adb shell）身份运行**：普通 App 进程无权读取 `/dev/input`、执行 `screencap`、直接创建全局 Surface。
- 兼容 Android 9 ~ 16（Surface 创建符号按系统版本自动适配，arm64）。
- 悬浮窗不参与触摸事件分发，不会拦截屏幕操作。
- 首次 HTTPS 握手包含 CA 证书校验，请确保设备系统时间正确。

## 合法使用声明

本项目仅供学习研究与个人设备上的合法用途（如辅助阅读、无障碍增强）。请勿用于考试作弊、侵犯他人隐私或任何违反法律法规及服务条款的场景。
