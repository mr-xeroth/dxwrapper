# Devlog: Building a Modern OpenGL 3.3+ DirectDraw Wrapper

## 1. Introduction & Overview

Legacy DirectX 5.0–7.0 titles (such as *Diablo*, *StarCraft*, *Fallout*, etc.) rely on Microsoft DirectDraw to manage display modes, surface locking, and video memory blitting. On modern versions of Windows, DirectDraw is implemented as an unaccelerated software compatibility layer that suffers from color palette corruption, poor performance, broken window scaling, and lack of support for modern high-DPI displays or post-processing shaders.

This project implements a complete from-scratch DirectDraw (`ddraw.dll`) drop-in replacement built on top of **OpenGL 3.3+ Core/Compatibility profile** and **GLFW**, featuring DOSBox SVN CRT shader support, aspect ratio preservation with letterboxing/pillarboxing, real-time window resizing, DirectInput mouse scaling, and standalone static linking for 32-bit x86 Windows applications.

---

## 2. DirectDraw Wrapper Architecture & Interface Delegation

DirectDraw uses COM (Component Object Model) interface versioning spanning DirectX 1.0 to DirectX 7.0:
- `IDirectDraw`, `IDirectDraw2`, `IDirectDraw4`, `IDirectDraw7`
- `IDirectDrawSurface`, `IDirectDrawSurface2`, `IDirectDrawSurface3`, `IDirectDrawSurface4`, `IDirectDrawSurface7`
- `IDirectDrawPalette`
- `IDirectDrawClipper`
- `IDirectDrawGammaControl`

### Interface Delegation vs. Multiple Inheritance
DirectDraw interfaces evolve by adding methods and changing method parameter structs across versions (e.g., `DDSURFACEDESC` in DirectDraw 1/2/3 vs. `DDSURFACEDESC2` in DirectDraw 4/7). Attempting to inherit all interface versions into a single concrete class causes vtable collisions and conflicting virtual function signatures.

To solve this, the wrapper uses an **Implementation–Interface Delegation pattern**:
1. `DDrawImpl` & `DDrawSurfaceImpl` manage the core resources: surface pixel buffers, dimensions, format detection, active palettes, clippers, and presentation triggers.
2. Lightweight COM wrapper classes (`DDrawSurface1`, `DDrawSurface2`, `DDrawSurface3`, `DDrawSurface4`, `DDrawSurface7`) implement their respective COM vtables and thunk calls into the shared implementation.

```
+-------------------------------------------------------------+
|                     diablo.exe (Game)                       |
+-------------------------------------------------------------+
                               |
               Calls DirectDraw COM Interfaces
                               v
+-------------------------------------------------------------+
|    DDrawSurface1 / DDrawSurface2 / ... / DDrawSurface7      |
+-------------------------------------------------------------+
                               |
                   Delegates & Translates Structs
                               v
+-------------------------------------------------------------+
|                      DDrawSurfaceImpl                       |
|   - 4-Byte Aligned Pixel Buffer (System Memory)             |
|   - Lock / Unlock / Blt / BltFast / ColorKey / Palette      |
|   - GetDC / ReleaseDC (Win32 GDI Emulation)                 |
+-------------------------------------------------------------+
                               |
                 Present (on Flip / Primary Unlock)
                               v
+-------------------------------------------------------------+
|                  Renderer (GLFW + OpenGL 3.3)               |
|   - Pixel Conversion (8-bit Paletted -> RGBA32)             |
|   - glTexSubImage2D Texture Streaming                       |
|   - DOSBox SVN CRT GLSL Shader Pipeline                     |
|   - Aspect-Ratio Maintained Quad & Black Bars               |
+-------------------------------------------------------------+
```

---

## 3. Surface Memory & Framebuffer Streaming into OpenGL

### Memory Management & Pitch Alignment
DirectDraw games expect pixel memory to be byte-addressable with a pitch (`lPitch`) aligned to 4-byte boundaries (DWORD aligned).

$$\text{Pitch} = \left\lfloor \frac{\text{Width} \times \text{BytesPerPixel} + 3}{4} \right\rfloor \times 4$$

When a game calls `Lock()`, the wrapper provides a direct pointer to the internal system memory buffer. The game writes pixels with zero API overhead.

### Zero-Stall CPU-to-GPU Texture Upload
DirectDraw surface presentation happens on two main events:
1. `Flip()` on flipping chains (primary surface + attached backbuffer).
2. `Unlock()` or `Blt()` when modifying an unbuffered primary surface directly.

When presentation is triggered:
1. **Format Unpacking & Palette Conversion**:
   - For **8-bit Paletted** surfaces (standard in *Diablo*): An internal cache of 256 DWORD RGBA values is maintained from `IDirectDrawPalette::SetEntries`. Each byte index in the surface buffer is expanded to 32-bit RGBA in a single contiguous pass:
     $$\text{RGBA} = \text{PaletteTable}[\text{SurfaceIndex}] \,|\, \text{0xFF000000}$$
   - For **16-bit RGB565 / RGB555**: Unpacks bitfields into 8-bit components via bit-shifts and multiplication LUTs.
   - For **24-bit / 32-bit RGB**: Directly transfers or pads alpha channels.
2. **Texture Update via `glTexSubImage2D`**:
   The packed RGBA buffer is transferred to the allocated 2D OpenGL texture (`GL_RGBA8` internal format).
3. **Viewport & Aspect Ratio Calculation**:
   The wrapper queries the current GLFW window framebuffer size (`fb_w`, `fb_h`).
   - If `aspect_ratio = true`: Calculates the maximum centered rectangle preserving the game's original aspect ratio ($\frac{w_{\text{game}}}{h_{\text{game}}}$), letterboxing (horizontal black bars) or pillarboxing (vertical black bars) as necessary.
   - If `aspect_ratio = false`: Stretches across the full window area.
4. **Shader Execution**:
   A full-screen quad is rendered through the active DOSBox SVN CRT shader.

---

## 4. DOSBox SVN CRT Shader Pipeline

DOSBox SVN shaders use a unified single-file GLSL format containing both vertex and fragment stages separated by preprocessor guards:
- `#if defined(VERTEX)`
- `#elif defined(FRAGMENT)`

### Dynamic Shader Stage Compilation
The wrapper inspects the shader source, injects `#version 130` / `#version 330 core` headers, defines `VERTEX 1` for the vertex stage and `FRAGMENT 1` for the fragment stage, and optionally injects `#define OPENGLNB 1` when nearest-neighbor filtering is configured (`filter = openglnb`).

Uniforms supplied dynamically to the shader on every frame:
- `uniform vec2 rubyTextureSize`: Dimensions of the backing texture.
- `uniform vec2 rubyInputSize`: In-game native resolution (e.g. `640x480`).
- `uniform vec2 rubyOutputSize`: Target viewport rendering resolution.
- `uniform sampler2D rubyTexture`: Sampler unit bound to the game surface texture.

---

## 5. DirectInput & Mouse Coordinate Scaling

When scaling a $640 \times 480$ game window to $1280 \times 960$ (or arbitrary resizable window sizes), raw mouse coordinates from the OS no longer align with game elements.

The wrapper handles two distinct mouse input mechanisms:
1. **DirectInput Relative Deltas (`DIMOUSESTATE` / `DIDEVICEOBJECTDATA`)**:
   DirectInput queries relative mouse displacement ($\Delta x, \Delta y$). The wrapper scales motion deltas by the ratio between the native resolution and the active viewport resolution:
   $$\Delta x_{\text{scaled}} = \Delta x \times \frac{w_{\text{game}}}{w_{\text{viewport}}}$$
   $$\Delta y_{\text{scaled}} = \Delta y \times \frac{h_{\text{game}}}{h_{\text{viewport}}}$$
2. **Win32 Absolute Cursor Coordinates (`GetCursorPos` / `ScreenToClient`)**:
   Window client coordinates $(x_{\text{client}}, y_{\text{client}})$ are mapped relative to the centered viewport origin $(x_{\text{vp}}, y_{\text{vp}})$ and clamped to $[0, w_{\text{game}}-1]$ and $[0, h_{\text{game}}-1]$.

---

## 6. Compilation, Linking & Standalone Deployment

### 32-bit (x86) Architecture Requirement
Classic DirectX 5.0–7.0 games are 32-bit PE binaries (`pei-i386`). Under Windows WOW64:
- A 32-bit process can only load 32-bit DLLs.
- If a 64-bit `ddraw.dll` is placed in the game directory, the OS dynamic loader silently skips it and falls back to `C:\Windows\SysWOW64\ddraw.dll`, causing the game to drop into legacy 640x480 fullscreen.
- Therefore, the wrapper is compiled with the **MinGW32 (i686-w64-mingw32)** toolchain.

### Eliminating Runtime DLL Dependencies (Static Linking)
Default GCC builds link standard C++ runtimes and POSIX thread libraries dynamically (`libstdc++-6.dll`, `libgcc_s_dw2-1.dll`, `libwinpthread-1.dll`). On machines without MSYS2 in their system path, the game crashes immediately on startup with *"unable to find ddraw.dll"*.

To ensure `ddraw.dll` runs self-contained on any clean Windows installation:
1. Linked static archive objects directly: `lib/libglfw3.a` and `lib/libwinpthread.a`.
2. Passed static linker flags:
   ```makefile
   LDFLAGS = -shared -static -static-libgcc -static-libstdc++ -Llib -L/mingw32/lib -Wl,--enable-stdcall-fixup
   LIBS    = src/ddraw.def lib/libglfw3.a lib/libwinpthread.a -lopengl32 -lgdi32 -ldxguid -ldinput8 -lole32 -luser32 -lkernel32
   ```
3. Verified binary import table with `objdump -p ddraw.dll`. The resulting binary imports only core Windows libraries:
   - `KERNEL32.dll`
   - `USER32.dll`
   - `GDI32.dll`
   - `msvcrt.dll`
   - `SHELL32.dll`

---

## 7. Deep-Dive: Debugger Research & Critical Implementation Pitfalls

During integration and stress-testing with *Diablo (1997)* and its core library `Storm.dll`, several critical low-level binary compatibility hurdles were encountered. The following analysis documents the findings uncovered with GDB and the essential rules for writing stable DirectDraw wrappers.

---

### Pitfall 1: COM Vtable Layout & Virtual Destructors in MinGW GCC

#### The Bug & Crash Symptom
When navigating between menus (e.g. Title Screen $\to$ Main Menu $\to$ Single Player $\to$ Hero Selection), the game crashed with a `SIGSEGV` at `0x00000000`:
```text
Thread 1 received signal SIGSEGV, Segmentation fault.
0x00000000 in ?? ()
#0  0x00000000 in ?? ()
#1  0x1500a1bc in storm!SDrawUpdatePalette () from Storm.dll
```

#### Disassembly & Vtable Dissection in GDB
Inspecting `Storm.dll!SDrawUpdatePalette` around `0x1500a1a0`:
```asm
0x1500a1ac: mov    0x15031164, %ecx     ; %ecx = global IDirectDrawPalette pointer
0x1500a1b2: push   %eax                 ; lpEntries
0x1500a1b3: push   %ebx                 ; dwCount
0x1500a1b4: push   $0x0                 ; dwStartingEntry
0x1500a1b6: mov    (%ecx), %eax         ; %eax = vtable pointer
0x1500a1b8: push   %ecx                 ; this pointer (stdcall)
0x1500a1b9: call   *0x18(%eax)          ; Call method at vtable slot 6 (0x18 / 4 = 6)
```

The COM specification for `IDirectDrawPalette` defines:
- Slot 0 (`0x00`): `QueryInterface`
- Slot 1 (`0x04`): `AddRef`
- Slot 2 (`0x08`): `Release`
- Slot 3 (`0x0C`): `GetCaps`
- Slot 4 (`0x10`): `GetEntries`
- Slot 5 (`0x14`): `Initialize`
- Slot 6 (`0x18`): `SetEntries`

#### The Root Cause
In MSVC (which compiled *Diablo* and DirectX SDKs), pure COM interfaces have **no virtual destructor** in their vtable. Object deletion is strictly mediated by `IUnknown::Release()`.

In GCC / MinGW, declaring a virtual destructor on a COM class:
```cpp
// INCORRECT in MinGW COM classes:
class DDrawPalette : public IDirectDrawPalette {
public:
    virtual ~DDrawPalette(); // GCC inserts destructor into vtable[0]!
};
```
causes GCC to insert the virtual destructor at slot 0 (or prepending it before the interface methods). This shifts every method by $+4$ bytes:
- `vtable[0]` became `~DDrawPalette()`
- `vtable[1]` became `QueryInterface()`
- `vtable[6]` (`0x18`) became `Initialize()` instead of `SetEntries()`

When `Storm.dll` invoked `call *0x18(%eax)`, it was calling an uninitialized function pointer or the wrong method signature, causing an immediate jump to `0x00000000`.

#### The Fix
All COM wrapper classes (`IDirectDrawX`, `IDirectDrawSurfaceX`, `IDirectDrawPalette`, `IDirectDrawClipper`, `IDirectDrawGammaControl`) must declare **non-virtual destructors**:
```cpp
// CORRECT:
class DDrawPalette : public IDirectDrawPalette {
public:
    ~DDrawPalette(); // Non-virtual; COM vtable stays identical to MSVC ABI
};
```

---

### Pitfall 2: Invasive Inline Hooking & Storm Memory Corruption

#### The Bug & Out-of-Memory Dialog
When transitioning between menus, Diablo displayed a fatal error dialog:
> *"DIABLO: diablo.exe - Системная ошибка: Процесс был завершен, так как не удалось выделить дополнительную память."*
> *(The process was terminated because additional memory could not be allocated.)*

#### Root Cause Analysis
Blizzard's `Storm.dll` manages internal heap memory blocks via `SMemAlloc` / `SMemFree` and blit descriptor structures (`STrans` objects for sprite transparencies).

Invasive trampolines placed on `STransBlt` and `GetRegionData` modified structure fields (`obj[0] = 0`, `obj[4] = 0`) and returned truncated region headers. `obj[0]` was the internal allocated memory block pointer. Zeroing it caused:
1. Memory leaks and pointer loss inside Storm.
2. Corruption of Storm's internal block headers (`cmpw $0x6f6d, -0x2(%ebx)` verification tags).
3. Heap exhaustion resulting in `SMemAlloc` aborting the process.

#### The Fix
Remove all ad-hoc engine patching and region spoofing. A DirectDraw wrapper must maintain strict boundary isolation:
- Intercept only clean Win32 / DirectDraw / DirectInput interfaces.
- Preserve all application buffers and heap headers without modification.

---

### Pitfall 3: Autonomous Debugging & Crash Testing

To rapidly verify state transitions through the game's menu cycle (*Intro $\to$ Title $\to$ Single Player $\to$ Character Select $\to$ In-game Tristram*):
- An autonomous test thread was constructed using Win32 `keybd_event(VK_RETURN, 0x1C, ...)` pulsing enter keys at 2-second intervals.
- GDB batch mode (`gdb -batch -ex 'run' -ex 'bt' diablo.exe`) allowed automated headless verification of game loop execution and stack trace capture upon uncaught exceptions.

### Pitfall 4: Windows 10/11 GDI Region Virtualization & Storm `SDlgBltToWindow` Crash

#### The Crash Symptom
When navigating into the Hero Selection menu (or dialog windows with saved characters), the game crashed with a `SIGSEGV` at `0x150057b3`:
```text
Thread 1 received signal SIGSEGV, Segmentation fault.
0x150057b3 in storm!SDlgBltToWindow () from Storm.dll
#0  0x150057b3 in storm!SDlgBltToWindow () from Storm.dll
```

#### Debugger Dissection & GDI Behavior
Inspecting `Storm.dll!SDlgBltToWindow` around `0x15005740`:
```asm
0x1500574e: call   *0x15036424       ; GDI32.dll!GetRegionData(hrgn, 0, NULL)
0x15005754: mov    %eax, %ebx        ; %ebx = size needed
0x15005761: call   SMemAlloc         ; Allocates %ebx bytes
0x15005771: call   *0x15036424       ; GetRegionData(hrgn, size, buffer)
0x1500577b: add    $0x20, %eax       ; %eax += sizeof(RGNDATAHEADER) -> ptr to RECTs
0x1500577e: mov    %eax, 0x18(%esp)  ; Save rect pointer
0x15005782: mov    -0x18(%eax), %ecx ; %ecx = rgnData->rdh.nCount
0x15005789: mov    0x10(%esp), %eax  ; %eax = nCount
0x150057b3: mov    (%ecx), %eax      ; Dereferences first RECT -> CRASH!
```

On Windows 10/11:
1. When `hrgn` is an uninitialized or virtualized region handle (`0xd70404d7`), GDI `GetRegionData(hrgn, 0, NULL)` returns `0` (failure).
2. Storm passes `size = 0` to `SMemAlloc(0)`, which returns a buffer.
3. The second `GetRegionData(hrgn, 0, buffer)` also returns `0`, leaving the buffer uninitialized.
4. Storm reads `rgnData->rdh.nCount` from uninitialized heap memory (`buffer + 8`), misinterprets it as non-zero, and attempts to read `(buffer + 0x20)->left`, triggering a `SIGSEGV` segmentation fault past the 0-byte allocation.
5. If a hook attempts to synthesize a fake $640 \times 480$ region when `GetRegionData` fails, `SDlgBltToWindow` adds the control window's offset $(x_{\text{ctrl}}, y_{\text{ctrl}})$ to $(0, 0, 640, 480)$, causing the static image to be shifted by $x_{\text{ctrl}}$ (e.g. 3/4 across the screen).

#### The Fix
`Hooked_GetRegionData` zeroes out `lpRgnData->rdh` (`nCount = 0`) whenever `hrgn` is NULL or `GetRegionData` fails. This ensures Storm reads `nCount == 0` and safely bypasses region blitting without memory corruption or coordinate shifting.

---
### Pitfall 5: Inventory Item Grab Cursor Teleportation (`SetCursorPos` & Window Message Desynchronization)

#### The Bug & Symptom
When opening the player inventory (`'i'` key) and clicking on any item or equipment slot, the item icon was picked up, but the cursor suddenly jumped across the screen to an offset position.

#### Root Cause Analysis
In *Diablo (1997)*, the inventory item-picking logic (`CheckInvItem` / `GrabItem` at `0x41f6fe` and `0x41fb8c`) computes the exact center of the item slot in $640 \times 480$ game coordinates and calls the Win32 API:
```c
SetCursorPos(game_x, game_y);
```
When running the game in a scaled viewport (e.g. $1280 \times 960$ with letterboxing):
1. Diablo called unhooked `SetCursorPos(game_x, game_y)` with virtual $640 \times 480$ coordinates.
2. The OS placed the physical cursor at client position $(game\_x, game\_y)$ on the physical monitor.
3. On the next frame, the OS returned $(game\_x, game\_y)$ through `WM_MOUSEMOVE` and `GetCursorPos`.
4. The wrapper scaled $(game\_x, game\_y)$ down a second time:
   $$\text{scaled\_x} = (game\_x - x_{\text{viewport}}) \times \frac{640}{w_{\text{viewport}}}$$
   causing the cursor to jump exponentially toward the top-left corner on every item click.

#### The Fix
1. **`SetCursorPos` IAT Hook**:
   Intercepts `SetCursorPos(game_x, game_y)`, projects native game coordinates through the active OpenGL viewport $(x_{\text{vp}}, y_{\text{vp}}, w_{\text{vp}}, h_{\text{vp}})$, converts them to physical screen coordinates via `ClientToScreen`, and sets the physical OS cursor.
2. **`GetCursorPos` IAT Hook**:
   Intercepts `GetCursorPos(&pt)` and converts physical monitor coordinates back into virtual $640 \times 480$ game space via `ScreenToClient` and inverse viewport projection.
3. **Window Subclassing (`WrapperWndProc`)**:
   Subclasses the game window to translate `lParam` in mouse window messages (`WM_MOUSEMOVE`, `WM_LBUTTONDOWN/UP`, `WM_RBUTTONDOWN/UP`, `WM_MBUTTONDOWN/UP`) to $640 \times 480$ game coordinates.

---

### Pitfall 6: Smacker Video Stream Teardown & Out-of-Memory Race Condition

#### The Bug & Symptom
When launching Diablo, the title screen displayed for a split second and terminated with a Windows message box:
> *"DIABLO: diablo.exe - Системная ошибка: Процесс был завершен, так как не удалось выделить дополнительную память."*
> *(The process was terminated because additional memory could not be allocated.)*

#### Root Cause Analysis
This error represents `ERROR_NOT_ENOUGH_MEMORY` surfaced through Storm's `SErrDisplayError` handler. When the background test thread (`AutoEnterThread`) dispatched `VK_RETURN` 2 seconds after startup, Diablo was in the middle of allocating Smacker video decompression streams (`Smackw32.dll` / `Storm.dll!SVidPlayBegin`).

Prematurely canceling video streaming before file headers and memory buffers finished initializing caused resource teardown race conditions in Storm's memory pool, causing the subsequent UI allocation for the main menu to fail.

#### The Fix
- Disabled artificial input injection in production builds.
### Pitfall 7: Storm ROP3 Blitter JIT Execution, Stack ABI & Bounded Software Fallbacks

#### The Crash Symptom
When pressing 'Enter' on the hero selection screen to enter the game world (Tristram), or when rendering hero selection dialog scrollbars, the game crashed with a segmentation fault (`SIGSEGV` / `STATUS_ACCESS_VIOLATION`) inside `Storm.dll!SDlgBltToWindow` / `Storm.dll!SBltROP3Tiled`.

#### Root Cause Analysis
1. **Dynamic x86 JIT Generation**: `Diabloui.dll` renders dialog borders, scrollbar tracks, and hero selection elements using `Storm.dll!SBltROP3Tiled` (Ordinal 5), which invokes `Storm.dll!SBltROP3` (Ordinal 3). Storm's default implementation generates dynamic x86 machine code at runtime into a 4096-byte heap buffer (`0x15031058`).
2. **DEP & Heap Buffer Overruns**: Modern Windows 10/11 enforces Data Execution Prevention (DEP). Additionally, when UI dialogs render with scaled window coordinates, the unconstrained JIT code attempted to write past the end of the 307,200-byte ($640 \times 480$) backbuffer surface.
3. **Stack Corruption via ABI Argument Mismatch**: `Storm.dll!SBltROP3Tiled` is a `stdcall` function with **10 arguments** (`ret 0x28` / 40 bytes), whereas typical ROP3 blitters take 8 arguments. An 8-argument hook popped only 32 bytes, leaving `esp` misaligned by 8 bytes upon returning to `SDlgBltToWindow` and crashing subsequent stack dereferences.
4. **Obsolete Win32 Memory Checks**: Invoking `IsBadReadPtr` or `IsBadWritePtr` inside per-pixel loops triggers hardware page exceptions under Windows WOW64 that disrupt exception handling and debuggers.

#### The Fix
1. **`SetProcessDEPPolicy(0)`**: Disabled strict DEP enforcement at process attachment in `DllMain`.
2. **Selective Blitter Hooking & Safe 10-Argument Software Tiler**:
   - `SBltROP3` handles full ROP3 raster operations, alpha masks, and title screen / main menu background fire animations. Leaving `SBltROP3` to execute under `SetProcessDEPPolicy(0)` preserves 100% of title and menu transparency effects.
   - `SBltROP3Tiled` is intercepted via a 5-byte JMP trampoline redirecting to `Safe_SBltROP3Tiled` with the correct 10-argument prototype and $[0, 639] \times [0, 479]$ coordinate clamping:
   ```cpp
   BOOL WINAPI Safe_SBltROP3Tiled(void* lpDest, RECT* lpDestRect, int destPitch,
                                 void* lpSrc, RECT* lpSrcRect, int srcPitch,
                                 int maskX, int maskY, void* lpMask, DWORD dwRop);
   ```
3. **Strict Clamping & VirtualQuery**:
   All tiled blit coordinates are strictly clamped to $[0, 639]$ and $[0, 479]$, eliminating out-of-bounds writes while maintaining accurate dialog and listbox backgrounds.

### Pitfall 8: Win32 Dialog Coordinate Hooks & Child Control Displacement

#### The Bug & Symptom
In the title screen and main menu dialogs, static background artwork and child controls appeared visually displaced/shifted to the right by $\approx 3/4$ of the screen width, while animated effects rendered in their normal positions.

#### Root Cause Analysis
1. `Diabloui.dll` calculates the layout of child dialog controls (e.g. background image controls, listboxes, buttons) by invoking `GetClientRect`, `GetWindowRect`, `ClientToScreen`, and `ScreenToClient` on both the main window and child dialog controls.
2. Hooking `ScreenToClient` and `ClientToScreen` injected viewport scaling factors and black-bar letterbox offsets (`current_viewport.x`) into internal Win32 coordinate math.
3. When `Diabloui` converted child control coordinates to screen space and back to dialog space, one call went through the hooked viewport transformation and the other through native Win32, shifting child control rectangles by the viewport offset ($\approx 3/4$ of the screen).
4. Similarly, returning $640 \times 480$ from `GetClientRect`/`GetWindowRect` on child popup/dialog handles caused small child controls to receive full-screen dimensions.

#### The Fix
1. Removed viewport scaling from `ScreenToClient` and `ClientToScreen`, allowing Win32 to compute exact child-to-parent dialog coordinates without modification.
2. Restricted `GetClientRect` and `GetWindowRect` dimension spoofing ($640 \times 480$) strictly to the primary application window (`hWnd == r.get_target_hwnd()`), preserving true control bounding boxes for all child dialog windows.

### Pitfall 9: `ClipCursor` & `GetClientRect` Collision Limiting Cursor to Half-Display

#### The Bug & Symptom
In games such as *StarCraft: Brood War*, the in-game mouse cursor was able to move during active gameplay, but in menus, briefing screens, or high-resolution windowed modes, the cursor was trapped within the top half of the display vertically ($y \le 480$ in a $1280 \times 960$ window).

#### Root Cause Analysis
1. **`GetClientRect` Spoofing Collision with `Renderer::update_viewport()`**:
   When `GetClientRect` was hooked in the module IAT to return $640 \times 480$ for the main game window, `Renderer::update_viewport()` invoked the hooked `GetClientRect(target_hwnd, &rc)`. As a result, `Renderer` believed the physical client area was only $640 \times 480$ instead of the true $1280 \times 960$.
2. **Cascading Viewport & Clipping Compression**:
   - `current_viewport.height` was set to 480 (instead of 960).
   - `Renderer::game_to_screen(640, 480, ...)` calculated $sy2 = \text{origin.y} + 480$.
   - `Hooked_ClipCursor` clipped the physical OS cursor to $[0, 480]$ vertically, which is exactly $50\%$ (half) of the $960\text{px}$ window height.
3. **Startup Initialization Timing**:
   `Renderer::init()` did not invoke `update_viewport()` upon completing context creation, leaving viewport metrics uninitialized if `ClipCursor` or mouse queries occurred before the first frame presentation.

#### The Fix
1. **Clean Win32 Geometry Passthrough**:
   Removed artificial dimension spoofing from `Hooked_GetClientRect`, `Hooked_GetWindowRect`, and `Hooked_GetSystemMetrics`, allowing Win32 window management to query true display metrics and client dimensions.
2. **Immediate Viewport Initialization**:
   Added an explicit call to `update_viewport()` at the end of `Renderer::init()`.
3. **Robust Viewport-Projected `ClipCursor`**:
   Projects incoming game bounding boxes through `Renderer::game_to_screen()`, cleanly mapping $640 \times 480$ virtual game space to the full physical OpenGL viewport $[0, 1280] \times [0, 960]$. When passed `NULL`, cursor clipping is released.

---

### Pitfall 10: High-Frequency Dirty-Rect Blitting & VSync Throttling

#### The Bug & Symptom
In *StarCraft*, gameplay frame rates dropped severely to 2–5 FPS despite low CPU/GPU utilization, resulting in sluggish animations and unresponsive UI.

#### Root Cause Analysis
1. Unlike games that render entire frames to a backbuffer and present once via `IDirectDrawSurface::Flip`, games with software dirty-rect engines (such as StarCraft) perform dozens of small, sub-rectangle `Blt()` / `BltFast()` or `Unlock()` calls directly to the primary surface for each animated sprite, UI element, and tile update per frame.
2. If every primary surface blit or unlock triggers an immediate `Renderer::present()` with `glfwSwapBuffers()` under VSync (`vsync = true`), each dirty rect blit blocks for a full 16.6ms monitor refresh interval.
3. Executing 30–60 dirty-rect blits per frame resulted in multi-second frame times and severe frame rate drops.

#### The Fix
1. Implemented high-resolution timer debouncing in `Renderer::present()` using `QueryPerformanceCounter`.
2. When `vsync` is enabled:
   - Intermediate dirty-rect surface updates upload the modified sub-regions to the OpenGL texture via `glTexSubImage2D` and issue a non-blocking `glFlush()`.
   - Full buffer swaps via `glfwSwapBuffers()` are debounced with an 8.0ms threshold ($\approx 120\text{ FPS}$ ceiling), while explicit `Flip()` calls immediately swap the buffer.
3. This decouples high-frequency software dirty-rect updates from the monitor refresh rate, restoring silky smooth 60 FPS gameplay in StarCraft while maintaining tear-free CRT shader presentation.

---

### Pitfall 11: Menu Mouse Subtraction & Virtual Screen Coordinate Mapping

#### The Bug & Symptom
In *StarCraft*, in-game mouse movement functioned properly during RTS matches, but in the main menu, campaign selection, and briefing dialogs immediately following game launch, the cursor remained restricted to the top-left quadrant of the screen.

#### Root Cause Analysis
1. **StarCraft's Menu Mouse Logic**:
   Disassembly of StarCraft's UI hit-testing routine (`0x44d8e0`) revealed that the menu computes client-relative cursor positions by subtracting the window bounding rect from the raw `GetCursorPos` output:
   $$\text{client\_x} = \text{cursor\_pt.x} - \text{win\_rect.left}$$
   $$\text{client\_y} = \text{cursor\_pt.y} - \text{win\_rect.top}$$
2. **The Coordinate Offset Double-Subtraction**:
   - `Hooked_GetCursorPos` was returning raw $640 \times 480$ game coordinates $[0..640, 0..480]$.
   - When the centered wrapper window was positioned at $(320, 60)$ on a $1920 \times 1080$ display, `win_rect.left` was $320$.
   - StarCraft computed $\text{client\_x} = \text{game\_x} - 320$.
   - For $\text{game\_x} < 320$, coordinates became negative (ignored). For $\text{game\_x} = 640$ (right edge of window), StarCraft computed $640 - 320 = 320$ (exactly **half** the $640\text{px}$ canvas).
3. **`user32.dll` Indirect Jump Stub Bypass**:
   In modern Windows, `user32.dll!ClipCursor` and `SetCursorPos` are indirect JMP forwarders (`FF 25 [addr]`). Module-level IAT hooking failed to intercept direct calls, allowing unscaled $[0, 0, 640, 480]$ desktop clipping rectangles to reach the Windows kernel.

#### The Fix
1. **Origin-Relative `GetCursorPos` and `SetCursorPos` Mapping**:
   - `Hooked_GetCursorPos` returns $\text{origin.x} + \text{game\_x}$ and $\text{origin.y} + \text{game\_y}$. When StarCraft subtracts $\text{origin.x}$ and $\text{origin.y}$, it recovers the exact virtual $640 \times 480$ coordinate space across the entire window.
   - `Hooked_SetCursorPos` subtracts the window origin before projecting virtual coordinates to physical screen space.
2. **Direct Inline API Trampolines**:
   Implemented `InstallApiHook` to place 5-byte JMP trampolines directly on `user32.dll!ClipCursor`, `SetCursorPos`, and `GetCursorPos`, guaranteeing 100% call interception across all modules and threads.

---

### Pitfall 12: Startup `ClipCursor` Race Condition & Viewport Lifecycle Synchronization

#### The Bug & Symptom
In *StarCraft*, the in-game mouse cursor remained restricted to the top-left quadrant of the desktop in the main menu immediately after launching the game executable, but moved completely freely once an active RTS match session was started via hotkeys.

#### Root Cause Analysis
1. **Pre-`SetDisplayMode` Initialization Sequence**:
   During game launch, StarCraft creates its window and immediately calls `0x4215e0` $\to$ `ClipCursor(&rect)` with unscaled $[0, 0, 640, 480]$ coordinates *before* DirectDraw's `SetDisplayMode()` is invoked.
2. **The Initialization State Gap**:
   - At that initial millisecond, `Renderer::instance().is_initialized()` was `false`.
   - As a fallback, `Hooked_ClipCursor` passed the raw $[0, 0, 640, 480]$ rect directly to the real Win32 `ClipCursor`, locking the physical cursor into the top-left corner of the Windows desktop.
   - When DirectDraw subsequently initialized and centered the $1280 \times 960$ window, StarCraft did not issue another `ClipCursor` call while in the menu.
   - Consequently, the mouse cursor remained constrained to the desktop top-left corner until the user started an actual RTS game match, which triggered a fresh `ClipCursor` call that was correctly scaled by the now-initialized renderer.

#### The Fix
1. **Pre-Initialization `ClipCursor` Guard**:
   In `Hooked_ClipCursor`, any `ClipCursor` calls received before the renderer has completed initialization are safely swallowed (`return TRUE`) without forwarding unscaled coordinates to Windows.
2. **Explicit Post-Init Viewport Synchronization**:
   Added `MouseHook::clip_to_viewport()` directly at the end of `Renderer::init()`, guaranteeing that as soon as the window is centered and the OpenGL viewport established, the cursor clip is immediately projected to the true viewport bounds $[x_{\text{vp}}, y_{\text{vp}}] \to [x_{\text{vp}} + w_{\text{vp}}, y_{\text{vp}} + h_{\text{vp}}]$.
3. **Window Focus Lifecycle Management**:
   Subclassed window messages in `WrapperWndProc`:
   - `WM_ACTIVATE` / `WM_SETFOCUS`: Re-applies `MouseHook::clip_to_viewport()` when the game window receives focus.
   - `WM_KILLFOCUS` / `WA_INACTIVE`: Calls `MouseHook::release_clip()` (`ClipCursor(nullptr)`) so the user can freely move the mouse across monitor displays when switching applications.

---

### Pitfall 13: Diablo Inventory `SetCursorPos` Teleportation & Unscaled Game Coordinates

#### The Bug & Symptom
In *Diablo*, when clicking on an item in the character inventory, the mouse cursor abruptly teleported to the far left edge of the display instead of staying centered on the clicked inventory slot.

#### Root Cause Analysis
1. **Direct Game Coordinate Dispatch**:
   When the player clicks an item in Diablo's inventory grid (located on the right half of the $640 \times 480$ canvas, e.g. $X \in [350..600], Y \in [200..400]$), Diablo calls `SetCursorPos(item_x, item_y)` with pure, unadorned virtual game coordinates ($[0, 640] \times [0, 480]$).
2. **Double-Offset Subtraction Failure**:
   - `Hooked_SetCursorPos` previously assumed that the game was passing screen coordinates ($X = \text{origin.x} + \text{game\_x}$) and subtracted $\text{origin.x} = 320$.
   - When Diablo passed $X = 350$, the hook calculated $\text{game\_x} = 350 - 320 = 30$, placing the cursor at the far left edge of the screen!
   - When Diablo passed $X < 320$, coordinates clamped to $0$, snapping the physical cursor hard to the left border.

#### The Fix
1. **Direct Game-to-Screen Projection in `SetCursorPos`**:
   `Hooked_SetCursorPos(X, Y)` directly accepts virtual $640 \times 480$ game coordinates and projects them into the centered OpenGL viewport via `Renderer::game_to_screen(X, Y, sx, sy)`.
2. **Unified Virtual Space with `GetWindowRect` Spoofing**:
   - `Hooked_GetCursorPos` returns native game coordinates $[0..640, 0..480]$.
   - `Hooked_GetWindowRect` returns $[0, 0, 640, 480]$ for the main application window `target_hwnd` while passing through true geometry for child dialogs (`diabloui.dll`).
   - Both Diablo (which expects pure $(game\_x, game\_y)$) and StarCraft (which subtracts `win_rect.origin = (0, 0)`) now operate seamlessly without coordinate teleportation or clipping anomalies.

### Pitfall 14: Windows Alert Ding Suppression & Configurable Mouse Capture Modes (`mouse_capture`)

#### The Problem & Symptoms
1. **Windows "Ding" Alert Sound in Game Menus**:
   When launching *Diablo (1997)* and clicking on menu buttons or dialog areas using the Left Mouse Button (LMB), Windows played an alert "ding" sound on every click.
   - *Root Cause*: `diabloui.dll` displays menu interfaces using modal and semi-modal child dialogs. When clicking the parent window or areas outside active dialog controls, Windows' default dialog window procedure invokes `MessageBeep(MB_OK)` / `MessageBeep(0xFFFFFFFF)`.
2. **Inflexible Cursor Confinement in Windowed Mode**:
   During active gameplay, the mouse cursor was strictly trapped inside the wrapper window with no OS cursor visible, preventing users from moving outside to interact with desktop features without terminating or Alt-Tabbing out of the game.

#### The Architecture & Solution: `mouse_capture`
Implemented a configurable `mouse_capture` setting in `ddraw.ini` with two dedicated operational modes:

1. **`seamless` Mode (Default)**:
   - **Zero Cursor Trap & Startup Clip Release**: Early in game initialization (such as StarCraft's pre-DirectDraw window creation), legacy games issue unscaled `ClipCursor` calls with $(0, 0, 640, 480)$ coordinates. In `seamless` mode, `Hooked_ClipCursor`, `MouseHook::init()`, and `Renderer::init()` explicitly issue `ClipCursor(NULL)` (`release_clip()`), preventing the mouse pointer from being trapped in an invisible top-left desktop rectangle at startup.
   - **In-Game Tracking**: While inside the wrapper window, coordinates are dynamically projected via `Renderer::screen_to_game()` and passed to the game's message pump and DirectInput hooks.
   - **Seamless Desktop Interaction**: Moving outside the window seamlessly transitions to the standard Windows desktop cursor, allowing instant interaction with other applications and monitors.

2. **`onclick` Mode**:
   - **Click to Lock**: Clicking (`WM_LBUTTONDOWN`, `WM_RBUTTONDOWN`, `WM_MBUTTONDOWN`) inside the wrapper window binds and clips the mouse to the game's scaled viewport.
   - **F12 Hotkey to Unlock**: Pressing `VK_F12` immediately:
     - Releases cursor clipping via `MouseHook::release_clip()`.
     - Marks mouse state as unlocked (`g_mouse_locked = false`).
     - Restores the physical Windows arrow cursor (`WM_SETCURSOR` $\to$ `IDC_ARROW`).
     - Freezes in-game cursor coordinates in `WM_MOUSEMOVE`, `Hooked_GetCursorPos`, `Hooked_SetCursorPos`, and zeroes DirectInput deltas, preventing camera drift or menu jitter while using desktop features.

3. **`MessageBeep` IAT & Inline Hooking**:
   Hooked `user32.dll!MessageBeep` across all loaded modules (`Hooked_MessageBeep`) to return `TRUE` without emitting system sound alerts, completely eliminating the Windows ding sound during menu interactions.

---

### Pitfall 15: Title Bar, Close Button & Non-Client Cursor Visibility

#### The Problem & Symptoms
When moving the mouse over the window title bar, window caption, close button, minimize/maximize buttons, or window resizing borders, the Windows cursor remained invisible until a mouse click was made inside the title bar.

#### Root Cause Analysis
1. Legacy DirectX 5.0–7.0 fullscreen games (`WS_POPUP`) implement custom `WM_SETCURSOR` message handlers in their primary `WndProc` that unconditionally invoke `SetCursor(NULL)` and return `TRUE`.
2. Furthermore, games invoke `ShowCursor(FALSE)` upon startup to suppress the OS cursor in full-screen mode. In Win32, `ShowCursor` decrements an internal thread-specific display counter. Whenever this counter is negative ($< 0$), Windows suppresses cursor rendering across all windows belonging to that thread.
3. When the wrapper transforms `target_hwnd` into a decorated window with standard Win32 title bar, caption, close button, and sizing borders (`WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME`), Windows sends `WM_SETCURSOR` and `WM_NCMOUSEMOVE` with non-client hit-test codes (`LOWORD(lParam) != HTCLIENT`).
4. Because the thread display counter was negative and `WM_SETCURSOR` was forwarded to the game's `WndProc`, the cursor remained invisible over the title bar until mouse clicks or focus switches.

#### The Fix
In `WrapperWndProc`, whenever `LOWORD(lParam) != HTCLIENT`, `WM_SETCURSOR` and `WM_NCMOUSEMOVE` immediately set `SetCursor(LoadCursor(NULL, IDC_ARROW))` and dispatch directly to `DefWindowProc(hwnd, uMsg, wParam, lParam)`:
- Restores the standard `IDC_ARROW` cursor immediately upon hovering over the title bar, caption, close/minimize/maximize buttons, and system menu.
- Restores standard two-headed resize arrows (`IDC_SIZEWE`, `IDC_SIZENS`, `IDC_SIZENWSE`, `IDC_SIZENESW`) when hovering over window sizing borders.
- Leaves in-game cursor rendering inside the client area (`HTCLIENT`) intact.

---

### Pitfall 16: Invisible Secondary Dialog Windows Blocking Desktop Clicks & Hiding OS Cursor

#### The Problem & Symptoms
In *Diablo (1997)* and *StarCraft*, secondary invisible dialog windows (e.g., Storm's `"SDlgDialog"` windows positioned at $(0, 0, 640, 480)$ on the screen) intercepted mouse clicks when the user moved the cursor outside the main wrapper window. Hovering the mouse over the top-left quadrant of the desktop caused the OS mouse pointer to become hidden, and desktop icons or open applications beneath $(0, 0, 640, 480)$ were not clickable.

#### Attempted Approaches & Pitfalls
1. **Trampolining `CreateWindowExA` / `SetWindowPos` or Reparenting**:
   - Intercepting `CreateWindowExA` to alter window styles or reparent dialogs to `target_hwnd` resulted in black opaque rectangles rendered over the OpenGL framebuffer and caused the game engine message pump to freeze during startup.
2. **Hooking `ShowCursor`**:
   - Trampolining `ShowCursor` disrupted Win32 internal modal loops and dialog state tracking.

#### The Robust Solution: Transparent Hit-Testing via Subclassing
Instead of modifying native window creation or reparenting:
1. Safely subclass all secondary windows belonging to the process via `EnumWindows`:
   - `SecondaryWndProc` handles `WM_NCHITTEST` by returning `HTTRANSPARENT` for any window that is not the primary rendering window (`hwnd != r.get_target_hwnd()`).
   - Returning `HTTRANSPARENT` instructs the Windows Window Manager to completely ignore the window for hit-testing and pass all mouse motion and click events straight through to whatever window is behind it (the desktop, browser, or taskbar).
2. When Windows dispatches `WM_SETCURSOR` to secondary windows, `SecondaryWndProc` sets `SetCursor(LoadCursor(NULL, IDC_ARROW))` and passes to `DefWindowProc`, ensuring the OS arrow cursor remains visible when moving across screen boundaries.
3. This completely eliminates the invisible $(0, 0, 640, 480)$ click-blocking zone without interfering with game engine internals or introducing startup freezes.

---

## 8. Conclusion

By combining interface delegation, strict MSVC-compatible COM vtable layouts, selective software blitter trampolines, zero-overhead memory locking, dynamic viewport coordinate projection for mouse and cursor clipping, high-frequency dirty-rect presentation debouncing, an OpenGL 3.3 texture streaming pipeline, configurable mouse capture (`seamless` vs `onclick`), non-client cursor restoration, and fully static 32-bit compilation, the wrapper provides seamless compatibility for classic DirectX titles with high-fidelity CRT shaders, aspect-ratio correction, and resizable windowing.



