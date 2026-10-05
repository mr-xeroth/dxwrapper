# AGENTS.md

Guidance and instructions for AI assistants and automated coding agents working on the **DXWrapper** codebase.

---

## 1. Project Overview & Objectives

**DXWrapper** is a high-performance 32-bit DirectDraw replacement (`ddraw.dll`) written in C++17 with an OpenGL 3.3 presentation backend. Its primary goal is to provide seamless windowing, integer/aspect-ratio scaling, DOSBox SVN GLSL shader support, and modern mouse control for classic late-90s Windows games (specifically tested with *Diablo*, *StarCraft: Brood War*, and related titles).

---

## 2. Directory Structure & Key Files

```
dxwrapper/
├── include/              # header definitions
│   ├── config.h          # configuration struct, scale modes, key/button release mappings
│   ├── ddraw.h           # directdraw interface wrappers (IDirectDraw1-7)
│   ├── ddraw_surface.h   # surface interfaces (IDirectDrawSurface1-7)
│   ├── mouse_hook.h      # win32/storm/directinput iat hooks, cursor clipping, coordinates
│   ├── renderer.h        # opengl 3.3 rendering engine, wgl context, title formatting
│   ├── shader.h          # dosbox svn glsl shader compiler & internal fallback shader
│   └── glad/             # opengl glad loader headers
├── src/                  # c++ implementation files
│   ├── config.cpp        # ddraw.ini parser, default file generator
│   ├── ddraw.cpp         # directdraw core api and factories
│   ├── ddraw_surface.cpp # surface locking, flipping, blitting, texture conversions
│   ├── ddraw_palette.cpp # 8-bit paletted surface management
│   ├── ddraw_clipper.cpp # directdraw clipper interface
│   ├── ddraw_gamma.cpp   # gamma control interface
│   ├── dllmain.cpp       # dll entry point, directdraw exported functions
│   ├── mouse_hook.cpp    # win32 api hooks, dinput deltas scaling, coordinates mapping
│   ├── renderer.cpp      # opengl presentation loop, viewport, title bar management
│   └── shader.cpp        # glsl shader compilation using fopen (windows paths)
├── Makefile              # 32-bit mingw makefile
├── ddraw.ini             # main runtime configuration file
├── ddraw.ini.default     # fallback minimalistic configuration template
└── README.md             # user-facing documentation
```

---

## 3. Strict Development Rules & Constraints

When developing or modifying this codebase, AI agents **MUST** follow these rules:

### A. Comment Casing Rule (Mandatory)
- **All code comments in `src/`, `include/`, `Makefile`, and configuration files MUST begin with a lower-case letter.**
- Correct:
  ```cpp
  // initializes opengl context and viewport
  // handles mouse coordinate conversion
  ```
- Incorrect:
  ```cpp
  // Initializes OpenGL context
  // Handles mouse coordinate conversion
  ```
### B. 32-Bit Target Architecture
- Target games are 32-bit x86 Windows binaries (`i686-w64-mingw32`).
- **Never compile with 64-bit GCC** (`x86_64-w64-mingw32`). Always use `/mingw32/bin`.

### C. Windows Backslash Path Separators
- In configuration parsing, file lookups, and shader loading (`fopen()`), standard Windows backslashes (`\`) are used (e.g. `crt\crt-easymode.glsl`).

---

## 4. Build & Compilation Toolchain

### Environment
- **Platform**: MSYS2 with MinGW 32-bit (`mingw-w64-i686-toolchain`).
- **Required Libraries**: `libglfw3.a` and `libwinpthread.a` located in `/mingw32/lib`.

### Standard Build Command
```bash
{msys64path}\usr\bin\bash.exe -lc "export PATH=/mingw32/bin:/usr/bin:$PATH && cd /home/{$USER}/dxwrapper && make clean && make"
```

---

## 5. Architecture & Key Subsystems

### 1. DirectDraw Surface & Presentation Pipeline
- Classic games render 8-bit paletted surfaces (`PALETTE8`) at 640×480.
- `Renderer::present` converts 8-bit index data to 32-bit RGBA using the active palette entries into `conversion_buffer`, uploads via `glTexSubImage2D`, and renders a full-screen quad with letterbox/pillarbox aspect ratio preservation.

### 2. DOSBox SVN GLSL Shader Integration
- Shaders follow single-pass DOSBox SVN specifications with uniforms: `rubyTexture`, `rubyTextureSize`, `rubyInputSize`, `rubyOutputSize`, `Texture`, `TextureSize`, `InputSize`, `OutputSize`.
- `load_dosbox_shader` reads the `.glsl` file via `fopen(win_path.c_str(), "rb")`.
- If a custom shader is missing or fails compilation, `load_dosbox_shader` returns `false` and falls back cleanly to the built-in `"internal"` shader.

### 3. Mouse Hooks & Coordinates Mapping
- Two modes in `ddraw.ini`:
  - `mouse_capture = seamless`: Mouse moves freely across windows and desktop boundaries.
  - `mouse_capture = onclick`: Mouse is locked and clipped to the active viewport when clicked; unlocked via hotkey or mouse button combination.
- `mouse_release`: Supports modifier combinations (`ctrl+alt+shift+m`, `alt+f12`, etc.), mouse buttons (`lmb`, `rmb`, `mmb`), and keys (`f1`–`f24`, `space`, `esc`, `tab`, etc.). Default is `f12`.
- `is_mouse_scaling_enabled()`: Automatically active when target window dimensions exceed standard 640×480 resolution.

### 4. Window Title Bar Management
- Formatted title bar structure:
  - Seamless mode: `<GameTitle>: <w>x<h> - <shader>` (e.g. `Starcraft: 1280x960 - crt-easymode`)
  - OnClick mode: `<GameTitle>: <w>x<h> - <shader> - mouse release = <key>` (e.g. `Diablo: 1280x960 - crt-easymode - mouse release = f12`)
- If the custom shader fails or is absent, the shader portion displays `internal`.
- `Hooked_SetWindowTextA` and `Hooked_SetWindowTextW` in `mouse_hook.cpp` capture game titles while preventing the game from overwriting wrapper metadata.

---

## 6. Checklist for Future Modifications

When making changes, verify the following:
1. **Comment rule check**:
   ```bash
   cd /home/{$USER}/dxwrapper && grep -rn '^[[:space:]]*//[[:space:]]*[A-Z]' src/ include/
   ```
   *(Must return 0 results)*
2. **Clean build check**:
   ```bash
   export PATH=/mingw32/bin:/usr/bin:$PATH && cd /home/{$USER}/dxwrapper && make clean && make
   ```
   *(Must exit with code 0)*