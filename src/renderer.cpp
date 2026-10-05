#include "../include/renderer.h"
#include "../include/mouse_hook.h"
#include <iostream>
#include <algorithm>
#include <cstring>

// singleton accessor for global renderer instance
Renderer& Renderer::instance() {
    static Renderer inst;
    return inst;
}

// default constructor for renderer state
Renderer::Renderer()
    : target_hwnd(nullptr), hdc(nullptr), hglrc(nullptr),
      vao(0), vbo(0), texture_id(0),
      game_width(640), game_height(480),
      tex_width(0), tex_height(0), initialized(false) {}

// destructor cleans up all opengl resources and window subclassing
Renderer::~Renderer() {
    shutdown();
}

// retrieves current active popup or target window handle
HWND Renderer::get_active_target_hwnd() const {
    if (!target_hwnd || !IsWindow(target_hwnd)) return nullptr;
    HWND active = GetLastActivePopup(target_hwnd);
    if (active && IsWindow(active)) return active;
    return target_hwnd;
}

static WNDPROC g_orig_wndproc = nullptr;

// custom window procedure to handle input translation, cursor visibility, and hotkeys
static LRESULT CALLBACK WrapperWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    Renderer& r = Renderer::instance();
    switch (uMsg) {
        case WM_SIZE: {
            if (r.is_initialized()) {
                r.update_window_title();
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (r.is_initialized()) {
                if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && MouseHook::is_locked()) {
                    if (r.check_keyboard_release(uMsg, wParam, lParam)) {
                        MouseHook::unlock_mouse();
                        return 0;
                    }
                }
            }
            break;
        }
        case WM_SETCURSOR: {
            // restore standard arrow cursor when hovering over non-client elements (title bar, borders)
            if (LOWORD(lParam) != HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
            }
            if (r.is_initialized()) {
                if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && !MouseHook::is_locked()) {
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
            }
            break;
        }
        case WM_NCMOUSEMOVE: {
            // keep standard cursor visible during non-client mouse movements
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            break;
        }
        case WM_ACTIVATE: {
            MouseHook::subclass_secondary_windows();
            if (LOWORD(wParam) != WA_INACTIVE) {
                if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && MouseHook::is_locked()) {
                    MouseHook::clip_to_viewport();
                } else if (r.get_config().mouse_capture == MouseCaptureMode::SEAMLESS) {
                    MouseHook::release_clip();
                }
            } else {
                MouseHook::release_clip();
            }
            break;
        }
        case WM_SETFOCUS: {
            MouseHook::subclass_secondary_windows();
            if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && MouseHook::is_locked()) {
                MouseHook::clip_to_viewport();
            } else if (r.get_config().mouse_capture == MouseCaptureMode::SEAMLESS) {
                MouseHook::release_clip();
            }
            break;
        }
        case WM_KILLFOCUS: {
            MouseHook::release_clip();
            break;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            if (r.is_initialized()) {
                if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK) {
                    if (MouseHook::is_locked()) {
                        if (r.check_mouse_release(uMsg)) {
                            MouseHook::unlock_mouse();
                            return 0;
                        }
                    } else {
                        MouseHook::lock_mouse();
                    }
                }
                short cx = (short)LOWORD(lParam);
                short cy = (short)HIWORD(lParam);
                long gx = 0, gy = 0;
                r.client_to_game(cx, cy, gx, gy);
                lParam = MAKELPARAM((WORD)gx, (WORD)gy);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (r.is_initialized()) {
                if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && !MouseHook::is_locked()) {
                    return 0;
                }
                short cx = (short)LOWORD(lParam);
                short cy = (short)HIWORD(lParam);
                long gx = 0, gy = 0;
                r.client_to_game(cx, cy, gx, gy);
                lParam = MAKELPARAM((WORD)gx, (WORD)gy);
            }
            break;
        }
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK: {
            if (r.is_initialized()) {
                short cx = (short)LOWORD(lParam);
                short cy = (short)HIWORD(lParam);
                long gx = 0, gy = 0;
                r.client_to_game(cx, cy, gx, gy);
                lParam = MAKELPARAM((WORD)gx, (WORD)gy);
            }
            break;
        }
    }
    if (g_orig_wndproc) {
        return CallWindowProc(g_orig_wndproc, hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// initializes opengl rendering context, window styling, and shaders
bool Renderer::init(HWND target_h, int game_w, int game_h, const WrapperConfig& cfg) {
    if (target_h && IsWindow(target_h)) {
        target_hwnd = target_h;
    }
    if (!target_hwnd || !IsWindow(target_hwnd)) {
        target_hwnd = GetActiveWindow();
        if (!target_hwnd) target_hwnd = GetForegroundWindow();
    }

    if (initialized) {
        if (target_hwnd && IsWindow(target_hwnd) && !IsWindowVisible(target_hwnd)) {
            ShowWindow(target_hwnd, SW_SHOWNORMAL);
        }
        update_resolution(game_w, game_h);
        return true;
    }

    if (!target_hwnd || !IsWindow(target_hwnd)) {
        return false;
    }

    config = cfg;
    game_width = game_w > 0 ? game_w : 640;
    game_height = game_h > 0 ? game_h : 480;

    int target_w = 0, target_h_size = 0;
    ConfigManager::compute_target_size(config, game_width, game_height, target_w, target_h_size);

    if (target_w > 0 && target_h_size > 0) {
        DWORD style = GetWindowLong(target_hwnd, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE);
        if (config.resizable) {
            style |= (WS_THICKFRAME | WS_MAXIMIZEBOX);
        } else {
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }
        SetWindowLong(target_hwnd, GWL_STYLE, style);

        RECT rc = { 0, 0, target_w, target_h_size };
        AdjustWindowRect(&rc, style, FALSE);
        int win_w = rc.right - rc.left;
        int win_h = rc.bottom - rc.top;

        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        int pos_x = (screen_w - win_w) / 2;
        int pos_y = (screen_h - win_h) / 2;
        if (pos_x < 0) pos_x = 0;
        if (pos_y < 0) pos_y = 0;

        SetWindowPos(target_hwnd, HWND_TOP, pos_x, pos_y, win_w, win_h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    }

    if (target_hwnd && IsWindow(target_hwnd) && !g_orig_wndproc) {
        g_orig_wndproc = (WNDPROC)SetWindowLongPtr(target_hwnd, GWLP_WNDPROC, (LONG_PTR)WrapperWndProc);
    }

    hdc = GetDC(target_hwnd);
    if (!hdc) return false;

    PIXELFORMATDESCRIPTOR pfd;
    std::memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int format = ChoosePixelFormat(hdc, &pfd);
    if (format == 0) return false;
    SetPixelFormat(hdc, format, &pfd);

    hglrc = wglCreateContext(hdc);
    if (!hglrc) return false;
    wglMakeCurrent(hdc, hglrc);

    gladLoadGL();

    // attempt to create modern compatibility context
    typedef HGLRC (WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    if (wglCreateContextAttribsARB) {
        int attribs[] = {
            0x2091, 3,          // wgl_context_major_version_arb
            0x2092, 3,          // wgl_context_minor_version_arb
            0x9126, 0x00000002, // wgl_context_profile_mask_arb = wgl_context_compatibility_profile_bit_arb
            0
        };
        HGLRC modern_rc = wglCreateContextAttribsARB(hdc, nullptr, attribs);
        if (modern_rc) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc);
            hglrc = modern_rc;
            wglMakeCurrent(hdc, hglrc);
            gladLoadGL();
        }
    }

    typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC)(int);
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapIntervalEXT) {
        wglSwapIntervalEXT(config.vsync ? 1 : 0);
    }

    setup_quad();
    setup_texture();

    bool nearest = (config.filter == "openglnb");
    is_using_internal_shader = true;
    active_shader_name = "internal";

    // use internal pixel shader if shader line is absent or glsl file cannot be opened
    bool shader_loaded = false;
    if (!config.shader.empty() && config.shader != "none" && config.shader != "off") {
        if (shader.load_dosbox_shader(config.shader, nearest)) {
            shader_loaded = true;
            is_using_internal_shader = false;
            std::string s = config.shader;
            size_t slash = s.find_last_of("\\/");
            if (slash != std::string::npos) {
                s = s.substr(slash + 1);
            }
            std::string s_lower = s;
            for (char& c : s_lower) c = (char)tolower((unsigned char)c);
            size_t dot = s_lower.rfind(".glsl");
            if (dot != std::string::npos && dot + 5 == s.size()) {
                s = s.substr(0, dot);
            }
            active_shader_name = s;
        }
    }

    if (!shader_loaded) {
        shader.load_default_shader();
        is_using_internal_shader = true;
        active_shader_name = "internal";
    }

    initialized = true;
    update_viewport();
    MouseHook::clip_to_viewport();

    if (game_title.empty() && target_hwnd && IsWindow(target_hwnd)) {
        char buf[256] = { 0 };
        GetWindowTextA(target_hwnd, buf, sizeof(buf));
        if (buf[0] != '\0') {
            game_title = buf;
        }
    }
    if (game_title.empty()) {
        char exe_path[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        std::string exe_name = exe_path;
        size_t slash = exe_name.find_last_of("\\/");
        if (slash != std::string::npos) {
            exe_name = exe_name.substr(slash + 1);
        }
        size_t dot = exe_name.rfind('.');
        if (dot != std::string::npos) {
            exe_name = exe_name.substr(0, dot);
        }
        if (!exe_name.empty()) {
            game_title = exe_name;
        }
    }

    update_window_title();
    return true;
}

// releases opengl context, buffers, and restores original window proc
void Renderer::shutdown() {
    if (!initialized) return;

    if (texture_id) {
        glDeleteTextures(1, &texture_id);
        texture_id = 0;
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }

    if (hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hglrc);
        hglrc = nullptr;
    }
    if (target_hwnd && IsWindow(target_hwnd) && g_orig_wndproc) {
        SetWindowLongPtr(target_hwnd, GWLP_WNDPROC, (LONG_PTR)g_orig_wndproc);
        g_orig_wndproc = nullptr;
    }

    initialized = false;
}

// configures vertex array and buffer objects for full-screen quad presentation
void Renderer::setup_quad() {
    float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

// allocates 2d texture and pixel conversion staging buffer
void Renderer::setup_texture() {
    if (texture_id) {
        glDeleteTextures(1, &texture_id);
        texture_id = 0;
    }

    tex_width = game_width;
    tex_height = game_height;

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    GLint filter_mode = (config.filter == "opengl") ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex_width, tex_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    conversion_buffer.resize(tex_width * tex_height * 4);
}

// reallocates texture buffers when game resolution changes
void Renderer::update_resolution(int game_w, int game_h) {
    if (game_w <= 0 || game_h <= 0) return;
    if (game_width == game_w && game_height == game_h) return;

    game_width = game_w;
    game_height = game_h;
    setup_texture();
}

// computes viewport rectangle preserving aspect ratio with letterboxing/pillarboxing
void Renderer::update_viewport() {
    if (!target_hwnd || !IsWindow(target_hwnd)) return;
    RECT rc;
    GetClientRect(target_hwnd, &rc);
    int fb_w = rc.right - rc.left;
    int fb_h = rc.bottom - rc.top;
    if (fb_w <= 0 || fb_h <= 0) return;

    current_viewport.fb_width = fb_w;
    current_viewport.fb_height = fb_h;

    if (config.aspect_ratio) {
        float game_ratio = (float)game_width / (float)game_height;
        int target_w = fb_w;
        int target_h = (int)(fb_w / game_ratio);

        if (target_h > fb_h) {
            target_h = fb_h;
            target_w = (int)(fb_h * game_ratio);
        }

        current_viewport.width = target_w;
        current_viewport.height = target_h;
        current_viewport.x = (fb_w - target_w) / 2;
        current_viewport.y = (fb_h - target_h) / 2;
    } else {
        current_viewport.x = 0;
        current_viewport.y = 0;
        current_viewport.width = fb_w;
        current_viewport.height = fb_h;
    }
}

// converts pixel surface format to 32-bit rgba, uploads texture, and draws quad through shader
void Renderer::present(const uint8_t* surface_data, int pitch, SurfaceFormat fmt, const uint32_t* palette) {
    if (!initialized || !hdc || !hglrc || !surface_data || pitch <= 0 || game_width <= 0 || game_height <= 0) return;

    // ensure all secondary dialog windows remain transparent to hit tests
    MouseHook::subclass_secondary_windows();

    wglMakeCurrent(hdc, hglrc);

    uint32_t* dst = (uint32_t*)conversion_buffer.data();
    for (int y = 0; y < game_height; ++y) {
        const uint8_t* row = surface_data + (y * pitch);
        uint32_t* out_row = dst + (y * game_width);

        if (fmt == SurfaceFormat::PALETTE8) {
            if (palette) {
                for (int x = 0; x < game_width; ++x) {
                    uint8_t idx = row[x];
                    out_row[x] = palette[idx] | 0xFF000000;
                }
            } else {
                for (int x = 0; x < game_width; ++x) {
                    uint8_t idx = row[x];
                    out_row[x] = (0xFF000000) | (idx << 16) | (idx << 8) | idx;
                }
            }
        } else if (fmt == SurfaceFormat::RGB565) {
            const uint16_t* src16 = (const uint16_t*)row;
            for (int x = 0; x < game_width; ++x) {
                uint16_t p = src16[x];
                uint32_t r = ((p >> 11) & 0x1F) * 255 / 31;
                uint32_t g = ((p >> 5) & 0x3F) * 255 / 63;
                uint32_t b = (p & 0x1F) * 255 / 31;
                out_row[x] = (0xFF000000) | (b << 16) | (g << 8) | r;
            }
        } else if (fmt == SurfaceFormat::RGB555) {
            const uint16_t* src16 = (const uint16_t*)row;
            for (int x = 0; x < game_width; ++x) {
                uint16_t p = src16[x];
                uint32_t r = ((p >> 10) & 0x1F) * 255 / 31;
                uint32_t g = ((p >> 5) & 0x1F) * 255 / 31;
                uint32_t b = (p & 0x1F) * 255 / 31;
                out_row[x] = (0xFF000000) | (b << 16) | (g << 8) | r;
            }
        } else if (fmt == SurfaceFormat::RGB24) {
            for (int x = 0; x < game_width; ++x) {
                uint8_t b = row[x * 3 + 0];
                uint8_t g = row[x * 3 + 1];
                uint8_t r = row[x * 3 + 2];
                out_row[x] = (0xFF000000) | (b << 16) | (g << 8) | r;
            }
        } else if (fmt == SurfaceFormat::RGB32 || fmt == SurfaceFormat::RGBA32) {
            const uint32_t* src32 = (const uint32_t*)row;
            for (int x = 0; x < game_width; ++x) {
                out_row[x] = src32[x] | 0xFF000000;
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, game_width, game_height, GL_RGBA, GL_UNSIGNED_BYTE, dst);

    update_viewport();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(current_viewport.x, current_viewport.y, current_viewport.width, current_viewport.height);

    shader.use();
    shader.set_uniform2f("rubyTextureSize", (float)tex_width, (float)tex_height);
    shader.set_uniform2f("rubyInputSize", (float)game_width, (float)game_height);
    shader.set_uniform2f("rubyOutputSize", (float)current_viewport.width, (float)current_viewport.height);
    shader.set_uniform2f("TextureSize", (float)tex_width, (float)tex_height);
    shader.set_uniform2f("InputSize", (float)game_width, (float)game_height);
    shader.set_uniform2f("OutputSize", (float)current_viewport.width, (float)current_viewport.height);
    shader.set_uniform1i("rubyTexture", 0);
    shader.set_uniform1i("Texture", 0);
    shader.set_uniform1i("Source", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // rate limit presentations when vsync is active to prevent excess gpu/cpu spin
    static LARGE_INTEGER last_swap = { 0 };
    static LARGE_INTEGER freq = { 0 };
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    double elapsed_ms = (last_swap.QuadPart > 0) ? ((double)(now.QuadPart - last_swap.QuadPart) * 1000.0 / (double)freq.QuadPart) : 999.0;

    if (config.vsync && elapsed_ms < 8.0) {
        glFlush();
        return;
    }

    SwapBuffers(hdc);
    QueryPerformanceCounter(&last_swap);
}

// returns current viewport coordinates inside framebuffer
ViewportRect Renderer::get_viewport() const {
    return current_viewport;
}

// checks whether window dimensions exceed standard 640x480 resolution requiring mouse scaling
bool Renderer::is_mouse_scaling_enabled() const {
    if (!target_hwnd || !IsWindow(target_hwnd)) return false;
    RECT rc;
    GetClientRect(target_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    return (w > 640 || h > 480);
}

// projects original game pixel coordinates to physical desktop screen coordinates
void Renderer::game_to_screen(int game_x, int game_y, long& screen_x, long& screen_y) const {
    if (!is_mouse_scaling_enabled()) {
        POINT pt = { (LONG)game_x, (LONG)game_y };
        if (target_hwnd && IsWindow(target_hwnd)) {
            POINT origin = { 0, 0 };
            MapWindowPoints(target_hwnd, NULL, &origin, 1);
            pt.x += origin.x;
            pt.y += origin.y;
        }
        screen_x = pt.x;
        screen_y = pt.y;
        return;
    }

    if (current_viewport.width <= 0 || current_viewport.height <= 0 || game_width <= 0 || game_height <= 0) {
        screen_x = game_x;
        screen_y = game_y;
        return;
    }

    float vx = (float)current_viewport.x + ((float)game_x * (float)current_viewport.width / (float)game_width);
    float vy = (float)current_viewport.y + ((float)game_y * (float)current_viewport.height / (float)game_height);

    POINT pt = { (LONG)(vx + 0.5f), (LONG)(vy + 0.5f) };
    if (target_hwnd && IsWindow(target_hwnd)) {
        POINT origin = { 0, 0 };
        MapWindowPoints(target_hwnd, NULL, &origin, 1);
        pt.x += origin.x;
        pt.y += origin.y;
    }
    screen_x = pt.x;
    screen_y = pt.y;
}

// translates window client coordinates into clamped game pixel coordinates
void Renderer::client_to_game(int client_x, int client_y, long& game_x, long& game_y) const {
    if (!is_mouse_scaling_enabled()) {
        game_x = std::max(0L, std::min((long)game_width - 1, (long)client_x));
        game_y = std::max(0L, std::min((long)game_height - 1, (long)client_y));
        return;
    }

    if (current_viewport.width <= 0 || current_viewport.height <= 0 || game_width <= 0 || game_height <= 0) {
        game_x = client_x;
        game_y = client_y;
        return;
    }

    int rel_x = client_x - current_viewport.x;
    int rel_y = client_y - current_viewport.y;

    float gx = (float)rel_x * (float)game_width / (float)current_viewport.width;
    float gy = (float)rel_y * (float)game_height / (float)current_viewport.height;

    game_x = std::max(0L, std::min((long)game_width - 1, (long)(gx + 0.5f)));
    game_y = std::max(0L, std::min((long)game_height - 1, (long)(gy + 0.5f)));
}

// translates global screen coordinates into game pixel coordinates
void Renderer::screen_to_game(int screen_x, int screen_y, long& game_x, long& game_y) const {
    if (current_viewport.width <= 0 || current_viewport.height <= 0 || game_width <= 0 || game_height <= 0) {
        game_x = screen_x;
        game_y = screen_y;
        return;
    }

    int client_x = screen_x;
    int client_y = screen_y;
    if (target_hwnd && IsWindow(target_hwnd)) {
        POINT origin = { 0, 0 };
        MapWindowPoints(target_hwnd, NULL, &origin, 1);
        client_x -= origin.x;
        client_y -= origin.y;
    }

    client_to_game(client_x, client_y, game_x, game_y);
}

// scales directinput relative motion deltas to match scaled window resolution when enlarged
void Renderer::scale_mouse_delta(long& dx, long& dy) const {
    if (!is_mouse_scaling_enabled()) return;
    if (current_viewport.width <= 0 || current_viewport.height <= 0) return;

    float sx = (float)game_width / (float)current_viewport.width;
    float sy = (float)game_height / (float)current_viewport.height;

    dx = (long)(dx * sx);
    dy = (long)(dy * sy);
}

// formats window title with game name, window dimensions, shader name, and release hotkey
std::string Renderer::get_formatted_window_title() const {
    if (!target_hwnd || !IsWindow(target_hwnd)) return "";
    RECT rc;
    GetClientRect(target_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    std::string title;
    if (!game_title.empty()) {
        title = game_title + ": ";
    }
    title += std::to_string(w) + "x" + std::to_string(h) + " - " + active_shader_name;
    if (config.mouse_capture == MouseCaptureMode::ONCLICK) {
        title += " - mouse release = " + config.mouse_release.str;
    }
    return title;
}

// updates original game title and refreshes window title bar
void Renderer::set_game_title(const std::string& title) {
    if (title.empty()) return;
    if (title.find(" - ") != std::string::npos && title.find("x") != std::string::npos) {
        // avoid self-overwriting with already formatted wrapper title
        return;
    }
    game_title = title;
    update_window_title();
}

// updates wrapper window title bar
void Renderer::update_window_title() {
    if (!target_hwnd || !IsWindow(target_hwnd)) return;
    std::string title = get_formatted_window_title();
    if (!title.empty()) {
        SetWindowTextA(target_hwnd, title.c_str());
    }
}

// checks whether pressed key matches configured mouse release combination
bool Renderer::check_keyboard_release(UINT uMsg, WPARAM wParam, LPARAM lParam) const {
    if (config.mouse_capture != MouseCaptureMode::ONCLICK) return false;
    const MouseReleaseConfig& rel = config.mouse_release;
    if (rel.button != ReleaseButton::NONE) return false;

    if (rel.vk != 0 && (int)wParam != rel.vk) return false;

    if (rel.ctrl) {
        if (!(GetKeyState(VK_CONTROL) & 0x8000)) return false;
    }
    if (rel.alt) {
        if (!(GetKeyState(VK_MENU) & 0x8000)) return false;
    }
    if (rel.shift) {
        if (!(GetKeyState(VK_SHIFT) & 0x8000)) return false;
    }

    return true;
}

// checks whether clicked mouse button matches configured mouse release combination
bool Renderer::check_mouse_release(UINT uMsg) const {
    if (config.mouse_capture != MouseCaptureMode::ONCLICK) return false;
    const MouseReleaseConfig& rel = config.mouse_release;
    if (rel.button == ReleaseButton::NONE) return false;

    if (rel.ctrl) {
        if (!(GetKeyState(VK_CONTROL) & 0x8000)) return false;
    }
    if (rel.alt) {
        if (!(GetKeyState(VK_MENU) & 0x8000)) return false;
    }
    if (rel.shift) {
        if (!(GetKeyState(VK_SHIFT) & 0x8000)) return false;
    }

    if (rel.button == ReleaseButton::LMB && (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK)) return true;
    if (rel.button == ReleaseButton::RMB && (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONDBLCLK)) return true;
    if (rel.button == ReleaseButton::MMB && (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONDBLCLK)) return true;

    return false;
}
