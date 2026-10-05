#pragma once

#include <string>

enum class ScaleMode {
    UNSPECIFIED,
    WINDOW_SIZE,
    SCALE_INT,
    SCALE_FLOAT
};

enum class MouseCaptureMode {
    SEAMLESS,
    ONCLICK
};

enum class ReleaseButton {
    NONE,
    LMB,
    RMB,
    MMB
};

struct MouseReleaseConfig {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    int vk = 0x7B; // 0x7B is VK_F12
    ReleaseButton button = ReleaseButton::NONE;
    std::string str = "f12";
};

struct WrapperConfig {
    ScaleMode scale_mode = ScaleMode::UNSPECIFIED;
    int window_width = 0;
    int window_height = 0;
    int scale_int = 1;
    float scale_float = 1.0f;
    bool aspect_ratio = true;
    bool resizable = true;
    bool vsync = true;
    std::string scaler = "simple";
    std::string shader = "";
    std::string filter = "opengl";
    MouseCaptureMode mouse_capture = MouseCaptureMode::SEAMLESS;
    MouseReleaseConfig mouse_release;
};

class ConfigManager {
public:
    static WrapperConfig load(const std::string& path = "ddraw.ini");
    static void compute_target_size(const WrapperConfig& cfg, int in_w, int in_h, int& out_w, int& out_h);
};
