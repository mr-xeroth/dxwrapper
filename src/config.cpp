#include "../include/config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// trims whitespace from leading and trailing ends of a string
static inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// converts string to lower-case characters
static inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// removes byte order mark (bom) from utf-8 encoded files if present
static inline std::string strip_bom(std::string s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        return s.substr(3);
    }
    return s;
}

// cleans configuration value string by removing comments, quotes, and normalizing paths
static inline std::string clean_val(std::string v) {
    size_t comment_pos = v.find_first_of("#;");
    if (comment_pos != std::string::npos) {
        v = v.substr(0, comment_pos);
    }
    v = trim(v);
    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
        v = v.substr(1, v.size() - 2);
        v = trim(v);
    }
    for (char& c : v) {
        if (c == '/') c = '\\';
    }
    return v;
}

// parses mouse release hotkey string into key, button, and modifier flags
static MouseReleaseConfig parse_mouse_release(const std::string& input) {
    MouseReleaseConfig rel;
    if (input.empty()) {
        rel.vk = 0x7B; // vk_f12 default
        rel.str = "f12";
        return rel;
    }

    std::string s = to_lower(trim(input));
    rel.str = s;
    rel.ctrl = false;
    rel.alt = false;
    rel.shift = false;
    rel.vk = 0;
    rel.button = ReleaseButton::NONE;

    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, '+')) {
        token = trim(token);
        if (token.empty()) continue;

        if (token == "ctrl" || token == "control") {
            rel.ctrl = true;
        } else if (token == "alt" || token == "menu") {
            rel.alt = true;
        } else if (token == "shift") {
            rel.shift = true;
        } else if (token == "lmb" || token == "left_mouse" || token == "mouse1") {
            rel.button = ReleaseButton::LMB;
            rel.vk = 0;
        } else if (token == "rmb" || token == "right_mouse" || token == "mouse2") {
            rel.button = ReleaseButton::RMB;
            rel.vk = 0;
        } else if (token == "mmb" || token == "middle_mouse" || token == "mouse3") {
            rel.button = ReleaseButton::MMB;
            rel.vk = 0;
        } else if (token.size() >= 2 && token[0] == 'f' && std::isdigit(token[1])) {
            int num = std::atoi(token.c_str() + 1);
            if (num >= 1 && num <= 24) {
                rel.vk = 0x70 + (num - 1); // vk_f1 is 0x70
            }
        } else if (token.size() == 1) {
            char c = token[0];
            if (c >= 'a' && c <= 'z') {
                rel.vk = 'A' + (c - 'a');
            } else if (c >= '0' && c <= '9') {
                rel.vk = '0' + (c - '0');
            }
        } else if (token == "space") {
            rel.vk = 0x20; // vk_space
        } else if (token == "esc" || token == "escape") {
            rel.vk = 0x1B; // vk_escape
        } else if (token == "tab") {
            rel.vk = 0x09; // vk_tab
        } else if (token == "enter" || token == "return") {
            rel.vk = 0x0D; // vk_return
        } else if (token == "backspace" || token == "bksp") {
            rel.vk = 0x08; // vk_back
        } else if (token == "insert" || token == "ins") {
            rel.vk = 0x2D; // vk_insert
        } else if (token == "delete" || token == "del") {
            rel.vk = 0x2E; // vk_delete
        } else if (token == "home") {
            rel.vk = 0x24; // vk_home
        } else if (token == "end") {
            rel.vk = 0x23; // vk_end
        } else if (token == "pageup" || token == "pgup") {
            rel.vk = 0x21; // vk_prior
        } else if (token == "pagedown" || token == "pgdn") {
            rel.vk = 0x22; // vk_next
        } else if (token == "pause") {
            rel.vk = 0x13; // vk_pause
        }
    }

    if (rel.vk == 0 && rel.button == ReleaseButton::NONE) {
        rel.vk = 0x7B; // vk_f12 fallback
        rel.str = "f12";
    }

    return rel;
}

// writes minimalistic default ddraw.ini when configuration file is absent
static void write_default_ini(const std::string& path) {
    std::ofstream out(path);
    if (out.is_open()) {
        out << "window_size = default\n";
        out << "aspect_ratio = true\n";
        out << "resizable = true\n";
        out << "vsync = true\n";
        out << "filter = opengl\n";
        out << "mouse_capture = seamless\n";
    }
}

// loads and parses configuration settings from ddraw.ini file, creating default if absent
WrapperConfig ConfigManager::load(const std::string& path) {
    WrapperConfig cfg;
    std::ifstream file(path);
    if (!file.is_open() && path != "ddraw.ini") {
        file.open("ddraw.ini");
    }
    if (!file.is_open()) {
        std::string target_path = path.empty() ? "ddraw.ini" : path;
        write_default_ini(target_path);
        file.open(target_path);
        if (!file.is_open()) {
            return cfg;
        }
    }

    std::string line;
    bool first_line = true;
    while (std::getline(file, line)) {
        if (first_line) {
            line = strip_bom(line);
            first_line = false;
        }
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = to_lower(trim(line.substr(0, eq_pos)));
        std::string raw_val = line.substr(eq_pos + 1);
        std::string val = clean_val(raw_val);
        std::string val_lower = to_lower(val);

        if (key == "window_size" || key == "size" || key == "resolution") {
            if (cfg.scale_mode == ScaleMode::UNSPECIFIED) {
                if (val_lower == "default" || val_lower == "native" || val_lower == "auto" || val_lower == "game") {
                    cfg.window_width = 640;
                    cfg.window_height = 480;
                    cfg.scale_mode = ScaleMode::WINDOW_SIZE;
                } else {
                    int w = 0, h = 0;
                    if (sscanf(val.c_str(), "%dx%d", &w, &h) == 2 ||
                        sscanf(val.c_str(), "%d,%d", &w, &h) == 2 ||
                        sscanf(val.c_str(), "%d %d", &w, &h) == 2) {
                        if (w > 0 && h > 0) {
                            cfg.window_width = w;
                            cfg.window_height = h;
                            cfg.scale_mode = ScaleMode::WINDOW_SIZE;
                        }
                    }
                }
            }
        } else if (key == "scale_int" || key == "integer_scale") {
            if (cfg.scale_mode == ScaleMode::UNSPECIFIED) {
                int s = std::atoi(val.c_str());
                if (s > 0) {
                    cfg.scale_int = s;
                    cfg.scale_mode = ScaleMode::SCALE_INT;
                }
            }
        } else if (key == "scale_float" || key == "scale") {
            if (cfg.scale_mode == ScaleMode::UNSPECIFIED) {
                float f = (float)std::atof(val.c_str());
                if (f > 0.0f) {
                    cfg.scale_float = f;
                    cfg.scale_mode = ScaleMode::SCALE_FLOAT;
                }
            }
        } else if (key == "aspect_ratio" || key == "keep_aspect_ratio") {
            cfg.aspect_ratio = (val_lower == "true" || val_lower == "1" || val_lower == "yes" || val_lower == "on");
        } else if (key == "resizable") {
            cfg.resizable = (val_lower == "true" || val_lower == "1" || val_lower == "yes" || val_lower == "on");
        } else if (key == "vsync") {
            cfg.vsync = (val_lower == "true" || val_lower == "1" || val_lower == "yes" || val_lower == "on");
        } else if (key == "scaler") {
            cfg.scaler = val_lower;
        } else if (key == "shader") {
            cfg.shader = val;
        } else if (key == "filter") {
            cfg.filter = val_lower;
        } else if (key == "mouse_capture" || key == "capture_mode" || key == "mouse_mode") {
            if (val_lower == "onclick" || val_lower == "click" || val_lower == "lock") {
                cfg.mouse_capture = MouseCaptureMode::ONCLICK;
            } else {
                cfg.mouse_capture = MouseCaptureMode::SEAMLESS;
            }
        } else if (key == "mouse_release" || key == "release_mouse" || key == "release_hotkey") {
            cfg.mouse_release = parse_mouse_release(val);
        }
    }

    return cfg;
}

// calculates target window dimensions according to active scale mode
void ConfigManager::compute_target_size(const WrapperConfig& cfg, int in_w, int in_h, int& out_w, int& out_h) {
    if (in_w <= 0) in_w = 640;
    if (in_h <= 0) in_h = 480;

    switch (cfg.scale_mode) {
        case ScaleMode::WINDOW_SIZE:
            out_w = cfg.window_width > 0 ? cfg.window_width : in_w;
            out_h = cfg.window_height > 0 ? cfg.window_height : in_h;
            break;
        case ScaleMode::SCALE_INT: {
            int factor = cfg.scale_int > 0 ? cfg.scale_int : 1;
            out_w = in_w * factor;
            out_h = in_h * factor;
            break;
        }
        case ScaleMode::SCALE_FLOAT: {
            float factor = cfg.scale_float > 0.0f ? cfg.scale_float : 1.0f;
            out_w = (int)(in_w * factor);
            out_h = (int)(in_h * factor);
            break;
        }
        case ScaleMode::UNSPECIFIED:
        default:
            out_w = in_w;
            out_h = in_h;
            break;
    }
}
