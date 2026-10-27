#pragma once
// 渲染层：免 Java 创建全屏透明 Surface -> EGL/GLES3 -> ImGui 绘制
// Surface 创建使用 third_party/ANativeWindowCreator.h（MIT, Bzi-Han/AndroidSurfaceImgui）

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "ANativeWindowCreator.h"
#include "config.h"

class Overlay {
public:
    bool Init() {
        // 1. 创建全屏 Surface（无 Activity，直接走 SurfaceComposerClient）
        auto display = android::ANativeWindowCreator::GetDisplayInfo();
        width_ = display.width;
        height_ = display.height;
        if (width_ <= 0 || height_ <= 0) {
            LOGE("获取屏幕分辨率失败");
            return false;
        }
        LOGI("屏幕分辨率: %d x %d", width_, height_);

        window_ = android::ANativeWindowCreator::Create({
            .name = "AiOverlay", .width = width_, .height = height_,
            .skipScreenshot = true});  // eLayerSkipScreenshot：截屏/录屏排除本图层
        if (!window_) {
            LOGE("创建 Surface 失败（需要 shell/root 权限）");
            return false;
        }

        // 2. EGL 初始化
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
            LOGE("eglInitialize 失败");
            return false;
        }
        const EGLint cfgAttrs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        EGLConfig eglConfig;
        EGLint numConfigs = 0;
        if (!eglChooseConfig(display_, cfgAttrs, nullptr, 0, &numConfigs) || numConfigs == 0) {
            LOGE("eglChooseConfig 失败");
            return false;
        }
        if (!eglChooseConfig(display_, cfgAttrs, &eglConfig, 1, &numConfigs)) return false;

        EGLint fmt;
        eglGetConfigAttrib(display_, eglConfig, EGL_NATIVE_VISUAL_ID, &fmt);
        ANativeWindow_setBuffersGeometry(window_, width_, height_, fmt);

        const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, eglConfig, EGL_NO_CONTEXT, ctxAttrs);
        surface_ = eglCreateWindowSurface(display_, eglConfig, window_, nullptr);
        if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE ||
            !eglMakeCurrent(display_, surface_, surface_, context_)) {
            LOGE("EGL 上下文创建失败");
            return false;
        }
        eglSwapInterval(display_, 0);

        // 3. ImGui 初始化
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplOpenGL3_Init("#version 300 es");
        ImGui::GetIO().IniFilename = nullptr;  // 不写 ini 文件
        return true;
    }

    // 加载字体（可选，中文显示需要），在 Init 之后调用
    void LoadFont(const AppConfig& cfg, const std::string& exeDir) {
        auto& io = ImGui::GetIO();

        // 候选列表：配置路径 + 程序目录下的常见命名（含下载器改名变体）
        std::vector<std::string> candidates = {
            cfg.font_path,
            exeDir + "/font.ttf",
            exeDir + "/font.ttc",
            exeDir + "/uming.ttc",
            exeDir + "/font.ttf.bin",
        };

        for (const auto& path : candidates) {
            struct stat st{};
            if (stat(path.c_str(), &st) != 0) continue;   // 不存在，试下一个
            if (st.st_size < 1000) {
                LOGE("字体文件异常 (%lld 字节): %s", (long long)st.st_size, path.c_str());
                continue;
            }

            FILE* f = fopen(path.c_str(), "rb");
            if (!f) {
                LOGE("字体存在但无法打开 (%s): %s", strerror(errno), path.c_str());
                continue;
            }
            fclose(f);

            ImFont* font = io.Fonts->AddFontFromFileTTF(
                path.c_str(), (float)cfg.font_size, nullptr,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (font) {
                LOGI("已加载字体: %s (size=%d, %.1f KB)", path.c_str(),
                     cfg.font_size, st.st_size / 1024.0f);
                return;
            }
            LOGE("字体文件解析失败(stb 无法解码，可能损坏或格式不对): %s", path.c_str());
        }

        io.Fonts->AddFontDefault();
        LOGE("所有候选路径均未成功加载字体，使用内置字体（中文将显示为方块）");
        LOGI("提示: 请确认 font.ttf 与 ai_overlay 在同一目录，且文件名未被改为 font.ttf.bin");
    }

    // 把触摸状态喂给 ImGui（在 NewFrame 之前调用）
    void FeedTouch(float x, float y, bool down) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        if (down != prevDown_) {
            io.AddMouseButtonEvent(0, down);  // 左键 = 主触点
            prevDown_ = down;
        }
    }

    void NewFrame() {
        // 关键：必须手动告知 ImGui 屏幕尺寸，否则 DisplaySize=0，所有内容被裁剪
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width_, (float)height_);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigWindowsMoveFromTitleBarOnly = false;  // 整个窗口可拖动

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
    }

    // 在屏幕角落绘制答案悬浮窗（可拖动：首次出现在 corner 指定位置）
    void DrawAnswerWindow(const AppConfig& cfg, const std::string& status,
                          const std::string& answer, bool visible) {
        float scale = cfg.font_size / 18.0f;
        float winW = width_ * 0.34f;           // 窗口宽度约屏幕 1/3
        float margin = 20.0f * scale;

        // 首次按 corner 定位，之后由用户拖动决定位置
        if (winPos_.x < 0) {
            switch (cfg.corner) {
                case 0: winPos_ = {margin, margin}; break;
                case 2: winPos_ = {margin, height_ * 0.35f}; break;
                case 3: winPos_ = {width_ - winW - margin, height_ * 0.35f}; break;
                default: winPos_ = {width_ - winW - margin, margin}; break;
            }
            LOGI("悬浮窗初始位置: corner=%d pos=(%.0f,%.0f)", cfg.corner, winPos_.x, winPos_.y);
        }
        ImGui::SetNextWindowPos(winPos_, ImGuiCond_Once);
        ImGui::SetNextWindowSize({winW, 0}, ImGuiCond_Always);   // 宽度固定，高度自适应
        ImGui::SetNextWindowBgAlpha(cfg.opacity);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoNav;

        if (ImGui::Begin("AI 助手", nullptr, flags)) {
            if (!status.empty()) {
                ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "%s", status.c_str());
            }
            if (!answer.empty()) {
                ImGui::Separator();
                ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 10);
                ImGui::TextUnformatted(answer.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::End();
        winPos_ = ImGui::GetWindowPos();   // 跟随拖动
    }

    void Render() {
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();

        glViewport(0, 0, width_, height_);

        glClearColor(0, 0, 0, 0);  // 全透明背景
        glClear(GL_COLOR_BUFFER_BIT);

        // 预乘 alpha 合成：BufferQueue 消费端按预乘格式合成，
        // 顶点颜色 rgb *= a，混合方程用 (ONE, ONE_MINUS_SRC_ALPHA)
        PremultiplyVertexColors(drawData);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        ImGui_ImplOpenGL3_RenderDrawData(drawData);

        if (!eglSwapBuffers(display_, surface_)) {
            static bool logged = false;
            if (!logged) { LOGE("eglSwapBuffers 失败: 0x%x", eglGetError()); logged = true; }
        }

        // 首帧记录 GL 状态
        static bool firstFrame = true;
        if (firstFrame) {
            LOGI("首帧渲染完成: GL=%s", (const char*)glGetString(GL_VERSION));
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) LOGE("GL 错误: 0x%x", err);
            firstFrame = false;
        }
    }

    int ScreenWidth() const { return width_; }
    int ScreenHeight() const { return height_; }

    void Shutdown() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            eglTerminate(display_);
        }
        if (window_) android::ANativeWindowCreator::Destroy(window_);
    }

private:
    ANativeWindow* window_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    ImVec2 winPos_{-1, -1};   // 悬浮窗当前位置（跟随拖动）
    bool prevDown_ = false;
    int width_ = 0, height_ = 0;

    // ImGui 顶点颜色 RGBA8 -> 预乘：rgb' = rgb * a / 255
    static void PremultiplyVertexColors(ImDrawData* drawData) {
        for (int n = 0; n < drawData->CmdListsCount; n++) {
            ImDrawList* list = drawData->CmdLists[n];
            for (auto& vtx : list->VtxBuffer) {
                ImU32 c = vtx.col;
                ImU32 a = (c >> 24) & 0xFF;
                if (a == 0xFF) continue;   // 不透明无需处理
                ImU32 r = (c) & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
                r = r * a / 255;
                g = g * a / 255;
                b = b * a / 255;
                vtx.col = (a << 24) | (b << 16) | (g << 8) | r;
            }
        }
    }
};
