# DXWrapper (DirectDraw GLSL Wrapper)

A lightweight, modern DirectDraw (`ddraw.dll`) wrapper built with OpenGL 3.3 to enhance classic late-90s PC games on modern LCD displays.

Project developed with use of **Gemini 3.7 Flash**.

---


### Motivation
Classic Blizzard masterpieces like **Diablo (1997)** and **StarCraft: Brood War (1998)** were designed for 4:3 cathode-ray tube (CRT) monitors running at native 640×480 resolutions. On modern high-resolution LCD displays, running these gems directly often results in aggressive blurry upscaling, broken color palettes, sluggish window management, and disruptive desktop resolution switching.

**DXWrapper** brings these legendary games seamlessly into the modern era:
- Render crisp, pixel-perfect 640×480 visuals scaled up to modern display resolutions (1080p, 1440p, 4K).
- Recreate authentic CRT phosphors, scanlines, and bloom using **DOSBox SVN** shaders.
- Provide seamless desktop mouse transitions or customizable lock/release hotkeys for distraction-free gameplay.

---

## Key Features

- **DirectDraw 1–7 Compatibility**: Emulates paletted 8-bit surfaces, display mode switching, page flipping, GDI surfaces, and cooperative levels.
- **Modern OpenGL Presentation**: High-performance textured quad rendering with customizable vsync and texture filtering.
- **DOSBox SVN Shader Pipeline**: Built-in support for DOSBox SVN GLSL shaders (`crt-easymode`, `crt-geom`, `crt-lottes`, `fakelottes`, `pixel_perfect`, etc.) with automatic fallback to internal nearest/bilinear scaling.
- **Flexible Mouse Modes**:
  - `seamless`: Freely move the cursor in and out of the game window to multitask.
  - `onclick`: Locks cursor within the game window during play, with fully configurable release combinations (modifiers + keys + mouse buttons).
- **Dynamic Window Title**: Real-time feedback in the title bar showing game title, current window dimensions, active shader, and release hotkey (e.g. `Starcraft: 1280x960 - crt-easymode`).
- **Automatic Configuration**: Generates a default `ddraw.ini` if none is present.

---

## Quick Start / Installation

1. Copy `ddraw.dll` and `ddraw.ini` into your game's root directory (next to `Diablo.exe` or `StarCraft.exe`).
2. Launch the game normally.

---

## Configuration Manual (`ddraw.ini`)

All wrapper settings are controlled via `ddraw.ini` in the game directory.

```ini
# ddraw wrapper configuration file

# target window size (default, <width>x<height>, or omitted for native 640x480)
window_size = 1280x960

# integer scaling multiplier (e.g. 2, 3) - alternative to window_size
# scale_int = 2

# floating point scaling multiplier (e.g. 1.5, 2.5) - alternative to window_size
# scale_float = 1.5

# maintain 4:3 aspect ratio with black bars (letterbox/pillarbox)
aspect_ratio = true

# allow resizing window by dragging borders and corners
resizable = true

# vertical synchronization (true = eliminates tearing, false = uncapped)
vsync = true

# path to dosbox svn glsl post-processing shader
# if omitted or file not found, internal scaler is used
shader = crt\crt-easymode.glsl

# texture filtering mode:
# openglnb = nearest-neighbor filtering (crisp pixels, recommended for crt shaders)
# opengl   = bilinear filtering (smooth interpolation)
filter = openglnb

# mouse capture mode:
# seamless = mouse moves freely in and out of wrapper window to interact with desktop
# onclick  = clicking in window locks mouse to game; release key unlocks cursor
mouse_capture = seamless

# mouse release shortcut (active when mouse_capture = onclick):
# supported keys: f1-f24, a-z, 0-9, space, esc, tab, enter, backspace, insert, delete, home, end, pageup, pagedown, pause
# supported modifiers: ctrl, alt, shift
# supported mouse buttons: lmb, rmb, mmb
# examples: f12, alt+f12, ctrl+alt+shift+m, mmb, ctrl+lmb
# default: f12
mouse_release = f12
```

---

## DOSBox SVN Shaders

A comprehensive collection of compatible shaders is available at https://github.com/dosbox-staging/dosbox-shaders. Place your `.glsl` files in the game directory (or subfolders like `crt\`, `interpolation\`) and configure the `shader` property in `ddraw.ini`:

*Note: For scanline and CRT shaders, setting `filter = openglnb` ensures authentic scanline beam geometry without unwanted pre-blurring.*

---

## Building from Source

### Prerequisites & Required MSYS2 Packages
To build DXWrapper for 32-bit Windows games, install the 32-bit MinGW environment and build tools in MSYS2:

```bash
# install 32-bit mingw toolchain and build tools
pacman -S --needed mingw-w64-i686-toolchain mingw-w64-i686-make mingw-w64-i686-winpthreads
```

**Required Packages & Libraries**:
- `mingw-w64-i686-toolchain` (provides 32-bit GCC compiler `i686-w64-mingw32-g++`, headers, CRT, and linker)
- `mingw-w64-i686-make` (GNU Make)
- `mingw-w64-i686-winpthreads` (provides `libwinpthread.a` in `/mingw32/lib`)
- GLFW3 32-bit static library (`libglfw3.a` in `/mingw32/lib`)

### Build Command
Open the **MSYS2 MINGW32** shell (or add `/mingw32/bin` to `PATH`) and run `make` in the project folder.
The resulting `ddraw.dll` will be generated in the project root.

---

## Acknowledgments & Direct Inspiration

This project draws deep inspiration and architectural paradigms from pioneering open-source projects in the classic gaming and wrapper preservation community:

- **[DOSBox SVN / DOSBox Staging](https://github.com/dosbox-staging/dosbox-staging)**: For the standard single-pass GLSL post-processing shader pipeline, CRT phosphor emulation presets, and integer scaling mathematical models.
- **[cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw)**: For proven DirectDraw 1–7 surface emulation concepts, paletted surface updates, GDI compatibility, and window subclassing methodologies.
- **[DxWrapper](https://github.com/elishacloud/dxwrapper)**: For DirectX API interception patterns, robust Win32 hook dispatching, and DirectInput relative mouse translation techniques.

We extend our gratitude to the authors and maintainers of these projects for advancing the preservation of vintage PC gaming.
