#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <glad/glad.h>
#include "config.h"
#include "shader.h"

enum class SurfaceFormat {
    PALETTE8,
    RGB565,
    RGB555,
    RGB24,
    RGB32,
    RGBA32
};

struct ViewportRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int fb_width = 0;
    int fb_height = 0;
};

class Renderer {
public:
    static Renderer& instance();

    bool init(HWND target_hwnd, int game_w, int game_h, const WrapperConfig& cfg);
    void shutdown();

    void update_resolution(int game_w, int game_h);
    void present(const uint8_t* surface_data, int pitch, SurfaceFormat fmt, const uint32_t* palette);

    HWND get_target_hwnd() const { return target_hwnd; }
    HWND get_active_target_hwnd() const;
    HWND get_hwnd() const { return target_hwnd; }
    const WrapperConfig& get_config() const { return config; }
    ViewportRect get_viewport() const;
    int get_game_width() const { return game_width; }
    int get_game_height() const { return game_height; }
    bool is_initialized() const { return initialized; }
    bool is_mouse_scaling_enabled() const;
    void update_window_title();
    std::string get_formatted_window_title() const;
    void set_game_title(const std::string& title);
    const std::string& get_game_title() const { return game_title; }

    void game_to_screen(int game_x, int game_y, long& screen_x, long& screen_y) const;
    void screen_to_game(int screen_x, int screen_y, long& game_x, long& game_y) const;
    void client_to_game(int client_x, int client_y, long& game_x, long& game_y) const;
    void scale_mouse_delta(long& dx, long& dy) const;
    bool check_keyboard_release(UINT uMsg, WPARAM wParam, LPARAM lParam) const;
    bool check_mouse_release(UINT uMsg) const;

private:
    Renderer();
    ~Renderer();

    HWND target_hwnd;
    HDC hdc;
    HGLRC hglrc;

    WrapperConfig config;
    ShaderProgram shader;
    GLuint vao;
    GLuint vbo;
    GLuint texture_id;

    int game_width;
    int game_height;
    int tex_width;
    int tex_height;

    std::string game_title;
    std::string active_shader_name;
    bool is_using_internal_shader;

    std::vector<uint8_t> conversion_buffer;
    ViewportRect current_viewport;
    bool initialized;

    void setup_quad();
    void setup_texture();
    void update_viewport();
};
