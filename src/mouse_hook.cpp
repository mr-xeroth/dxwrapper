#include "../include/mouse_hook.h"
#include "../include/renderer.h"
#include <tlhelp32.h>
#include <cstring>
#include <algorithm>

typedef BOOL (WINAPI *PFN_SETCURSORPOS)(int X, int Y);
static PFN_SETCURSORPOS g_real_SetCursorPos = nullptr;

typedef BOOL (WINAPI *PFN_GETCURSORPOS)(LPPOINT lpPoint);
static PFN_GETCURSORPOS g_real_GetCursorPos = nullptr;

typedef BOOL (WINAPI *PFN_SCREENTOCLIENT)(HWND hWnd, LPPOINT lpPoint);
static PFN_SCREENTOCLIENT g_real_ScreenToClient = nullptr;

typedef BOOL (WINAPI *PFN_CLIENTTOSCREEN)(HWND hWnd, LPPOINT lpPoint);
static PFN_CLIENTTOSCREEN g_real_ClientToScreen = nullptr;

typedef BOOL (WINAPI *PFN_GETCLIENTRECT)(HWND hWnd, LPRECT lpRect);
static PFN_GETCLIENTRECT g_real_GetClientRect = nullptr;

typedef BOOL (WINAPI *PFN_GETWINDOWRECT)(HWND hWnd, LPRECT lpRect);
static PFN_GETWINDOWRECT g_real_GetWindowRect = nullptr;

typedef int (WINAPI *PFN_GETSYSTEMMETRICS)(int nIndex);
static PFN_GETSYSTEMMETRICS g_real_GetSystemMetrics = nullptr;

typedef DWORD (WINAPI *PFN_GETREGIONDATA)(HRGN hrgn, DWORD nCount, LPRGNDATA lpRgnData);
static PFN_GETREGIONDATA g_real_GetRegionData = nullptr;

typedef HMODULE (WINAPI *PFN_LOADLIBRARYA)(LPCSTR lpLibFileName);
static PFN_LOADLIBRARYA g_real_LoadLibraryA = nullptr;

typedef HMODULE (WINAPI *PFN_LOADLIBRARYW)(LPCWSTR lpLibFileName);
static PFN_LOADLIBRARYW g_real_LoadLibraryW = nullptr;

typedef HMODULE (WINAPI *PFN_LOADLIBRARYEXA)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
static PFN_LOADLIBRARYEXA g_real_LoadLibraryExA = nullptr;

typedef HMODULE (WINAPI *PFN_LOADLIBRARYEXW)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
static PFN_LOADLIBRARYEXW g_real_LoadLibraryExW = nullptr;

typedef BOOL (WINAPI *PFN_SDRAWUPDATEPALETTE)(DWORD dwStartingEntry, DWORD dwCount, LPPALETTEENTRY lpEntries, DWORD dwFlags);
static PFN_SDRAWUPDATEPALETTE g_real_SDrawUpdatePalette = nullptr;

typedef BOOL (WINAPI *PFN_MESSAGEBEEP)(UINT uType);
static PFN_MESSAGEBEEP g_real_MessageBeep = nullptr;

typedef BOOL (WINAPI *PFN_SETWINDOWTEXTA)(HWND hWnd, LPCSTR lpString);
static PFN_SETWINDOWTEXTA g_real_SetWindowTextA = nullptr;

typedef BOOL (WINAPI *PFN_SETWINDOWTEXTW)(HWND hWnd, LPCWSTR lpString);
static PFN_SETWINDOWTEXTW g_real_SetWindowTextW = nullptr;

// prevents games from overriding formatted wrapper title bar on main window while capturing game title
static BOOL WINAPI Hooked_SetWindowTextA(HWND hWnd, LPCSTR lpString) {
    Renderer& r = Renderer::instance();
    if (r.is_initialized() && (hWnd == r.get_target_hwnd() || hWnd == r.get_hwnd())) {
        if (lpString && lpString[0] != '\0') {
            r.set_game_title(lpString);
        }
        std::string title = r.get_formatted_window_title();
        if (!title.empty() && g_real_SetWindowTextA) {
            return g_real_SetWindowTextA(hWnd, title.c_str());
        }
    }
    if (g_real_SetWindowTextA) {
        return g_real_SetWindowTextA(hWnd, lpString);
    }
    return SetWindowTextA(hWnd, lpString);
}

// prevents games from overriding formatted wrapper title bar on main window while capturing game title (unicode)
static BOOL WINAPI Hooked_SetWindowTextW(HWND hWnd, LPCWSTR lpString) {
    Renderer& r = Renderer::instance();
    if (r.is_initialized() && (hWnd == r.get_target_hwnd() || hWnd == r.get_hwnd())) {
        if (lpString && lpString[0] != L'\0') {
            char buf[256] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, lpString, -1, buf, sizeof(buf), NULL, NULL);
            if (buf[0] != '\0') {
                r.set_game_title(buf);
            }
        }
        std::string title = r.get_formatted_window_title();
        if (!title.empty() && g_real_SetWindowTextA) {
            return g_real_SetWindowTextA(hWnd, title.c_str());
        }
    }
    if (g_real_SetWindowTextW) {
        return g_real_SetWindowTextW(hWnd, lpString);
    }
    return SetWindowTextW(hWnd, lpString);
}

// suppresses default windows bell sounds when clicking menu dialogs
static BOOL WINAPI Hooked_MessageBeep(UINT uType) {
    return TRUE;
}

static bool g_mouse_locked = false;
static long g_last_game_x = 320;
static long g_last_game_y = 240;

// window procedure for subclassed secondary dialog and tool windows
// returning httransparent allows mouse clicks to fall through to the main viewport while preserving cursor display
static LRESULT CALLBACK SecondaryWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_NCHITTEST) {
        Renderer& r = Renderer::instance();
        if (r.is_initialized() && hwnd != r.get_target_hwnd()) {
            return HTTRANSPARENT;
        }
    }
    if (uMsg == WM_SETCURSOR) {
        Renderer& r = Renderer::instance();
        if (r.is_initialized() && hwnd != r.get_target_hwnd()) {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }
    WNDPROC orig = (WNDPROC)GetPropA(hwnd, "DxWrapper_OrigProc");
    if (orig) {
        return CallWindowProc(orig, hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// enumerates top-level windows in the current process to subclass non-main windows
static BOOL CALLBACK EnumProcessSubclassProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) {
        return TRUE;
    }
    Renderer& r = Renderer::instance();
    if (hwnd && hwnd != r.get_target_hwnd() && !GetPropA(hwnd, "DxWrapper_Subclassed")) {
        WNDPROC orig = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)SecondaryWndProc);
        if (orig) {
            SetPropA(hwnd, "DxWrapper_OrigProc", (HANDLE)orig);
            SetPropA(hwnd, "DxWrapper_Subclassed", (HANDLE)1);
        }
    }
    return TRUE;
}

// iterates and subclasses secondary process windows to forward input
void MouseHook::subclass_secondary_windows() {
    EnumWindows(EnumProcessSubclassProc, 0);
}

// locks mouse cursor within the rendered viewport
void MouseHook::lock_mouse() {
    g_mouse_locked = true;
    clip_to_viewport();
}

// unlocks mouse cursor and removes clipping boundaries
void MouseHook::unlock_mouse() {
    g_mouse_locked = false;
    release_clip();
}

// returns whether the mouse is currently locked
bool MouseHook::is_locked() {
    return g_mouse_locked;
}

// converts game coordinates to screen coordinates and sets system cursor position
static BOOL WINAPI Hooked_SetCursorPos(int X, int Y) {
    g_last_game_x = X;
    g_last_game_y = Y;

    Renderer& r = Renderer::instance();
    if (r.is_initialized()) {
        if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && !g_mouse_locked) {
            return TRUE;
        }
        long sx = X, sy = Y;
        r.game_to_screen(X, Y, sx, sy);
        if (g_real_SetCursorPos) {
            return g_real_SetCursorPos(sx, sy);
        }
        return SetCursorPos(sx, sy);
    }
    if (g_real_SetCursorPos) {
        return g_real_SetCursorPos(X, Y);
    }
    return SetCursorPos(X, Y);
}

// intercepts system cursor position and maps it back to game space coordinates
static BOOL WINAPI Hooked_GetCursorPos(LPPOINT lpPoint) {
    if (!lpPoint) return FALSE;

    BOOL ret = FALSE;
    if (g_real_GetCursorPos) {
        ret = g_real_GetCursorPos(lpPoint);
    } else {
        ret = GetCursorPos(lpPoint);
    }
    if (!ret) return FALSE;

    Renderer& r = Renderer::instance();
    if (r.is_initialized()) {
        if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && !g_mouse_locked) {
            lpPoint->x = g_last_game_x;
            lpPoint->y = g_last_game_y;
            return TRUE;
        }

        r.screen_to_game(lpPoint->x, lpPoint->y, lpPoint->x, lpPoint->y);
        g_last_game_x = lpPoint->x;
        g_last_game_y = lpPoint->y;
    }
    return TRUE;
}

typedef BOOL (WINAPI *PFN_CLIPCURSOR)(const RECT* lpRect);
static PFN_CLIPCURSOR g_real_ClipCursor = nullptr;

// applies cursor clipping rectangle mapped to the active rendered viewport
void MouseHook::clip_to_viewport() {
    Renderer& r = Renderer::instance();
    if (!r.is_initialized()) {
        release_clip();
        return;
    }

    if (r.get_config().mouse_capture == MouseCaptureMode::SEAMLESS) {
        release_clip();
        return;
    }

    if (g_mouse_locked && r.get_target_hwnd() && IsWindow(r.get_target_hwnd())) {
        long sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
        r.game_to_screen(0, 0, sx1, sy1);
        r.game_to_screen(r.get_game_width(), r.get_game_height(), sx2, sy2);

        RECT scaled_clip;
        scaled_clip.left = std::min(sx1, sx2);
        scaled_clip.top = std::min(sy1, sy2);
        scaled_clip.right = std::max(sx1, sx2);
        scaled_clip.bottom = std::max(sy1, sy2);

        if (g_real_ClipCursor) g_real_ClipCursor(&scaled_clip);
        else ClipCursor(&scaled_clip);
    } else {
        release_clip();
    }
}

// clears active cursor clipping rect
void MouseHook::release_clip() {
    if (g_real_ClipCursor) g_real_ClipCursor(nullptr);
    else ClipCursor(nullptr);
}

// intercepts game clip cursor requests and adapts them to the scaled viewport
static BOOL WINAPI Hooked_ClipCursor(const RECT* lpRect) {
    if (!lpRect) {
        MouseHook::release_clip();
        return TRUE;
    }

    Renderer& r = Renderer::instance();
    if (r.is_initialized()) {
        if (r.get_config().mouse_capture == MouseCaptureMode::SEAMLESS) {
            MouseHook::release_clip();
            return TRUE;
        }
        if (r.get_target_hwnd() && IsWindow(r.get_target_hwnd())) {
            MouseHook::clip_to_viewport();
            return TRUE;
        }
    } else {
        MouseHook::release_clip();
        return TRUE;
    }

    return TRUE;
}

// converts screen coordinates to client coordinates via original api
static BOOL WINAPI Hooked_ScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    if (g_real_ScreenToClient) {
        return g_real_ScreenToClient(hWnd, lpPoint);
    }
    return ScreenToClient(hWnd, lpPoint);
}

// converts client coordinates to screen coordinates via original api
static BOOL WINAPI Hooked_ClientToScreen(HWND hWnd, LPPOINT lpPoint) {
    if (g_real_ClientToScreen) {
        return g_real_ClientToScreen(hWnd, lpPoint);
    }
    return ClientToScreen(hWnd, lpPoint);
}

// retrieves client area rectangle via original api
static BOOL WINAPI Hooked_GetClientRect(HWND hWnd, LPRECT lpRect) {
    if (g_real_GetClientRect) {
        return g_real_GetClientRect(hWnd, lpRect);
    }
    return GetClientRect(hWnd, lpRect);
}

// returns virtualized game dimensions for the target window or passes through
static BOOL WINAPI Hooked_GetWindowRect(HWND hWnd, LPRECT lpRect) {
    Renderer& r = Renderer::instance();
    if (r.is_initialized() && lpRect && hWnd == r.get_target_hwnd()) {
        lpRect->left = 0;
        lpRect->top = 0;
        lpRect->right = r.get_game_width();
        lpRect->bottom = r.get_game_height();
        return TRUE;
    }
    if (g_real_GetWindowRect) {
        return g_real_GetWindowRect(hWnd, lpRect);
    }
    return GetWindowRect(hWnd, lpRect);
}

// retrieves system metrics via original api
static int WINAPI Hooked_GetSystemMetrics(int nIndex) {
    if (g_real_GetSystemMetrics) {
        return g_real_GetSystemMetrics(nIndex);
    }
    return GetSystemMetrics(nIndex);
}

// retrieves region data or constructs a fallback rectangle region if empty
static DWORD WINAPI Hooked_GetRegionData(HRGN hrgn, DWORD nCount, LPRGNDATA lpRgnData) {
    DWORD ret = 0;
    if (hrgn && g_real_GetRegionData) {
        ret = g_real_GetRegionData(hrgn, nCount, lpRgnData);
    } else if (hrgn) {
        ret = GetRegionData(hrgn, nCount, lpRgnData);
    }

    if (ret > 0) {
        return ret;
    }

    RECT box = { 0, 0, 0, 0 };
    int rgnType = hrgn ? GetRgnBox(hrgn, &box) : 0;
    bool hasRect = (rgnType != 0 && rgnType != NULLREGION && box.right > box.left && box.bottom > box.top);

    DWORD needed = sizeof(RGNDATAHEADER) + (hasRect ? sizeof(RECT) : 0);
    if (!lpRgnData) {
        return needed;
    }

    RGNDATA* rgn = (RGNDATA*)lpRgnData;
    std::memset(rgn, 0, sizeof(RGNDATAHEADER));
    rgn->rdh.dwSize = sizeof(RGNDATAHEADER);
    rgn->rdh.iType = RDH_RECTANGLES;

    if (hasRect) {
        rgn->rdh.nCount = 1;
        rgn->rdh.nRgnSize = sizeof(RECT);
        rgn->rdh.rcBound = box;
        std::memcpy(rgn->Buffer, &box, sizeof(RECT));
    } else {
        rgn->rdh.nCount = 0;
        rgn->rdh.nRgnSize = 0;
    }

    return needed;
}

// checks whether a memory pointer is committed, readable, and valid
static inline bool IsValidMemory(const void* ptr, size_t size) {
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD)) {
            return true;
        }
    }
    return false;
}

typedef BOOL (WINAPI *PFN_SBLTROP3)(void* lpDest, void* lpSrc, int width, int height,
                                     int destPitch, int srcPitch, void* lpMask, DWORD dwRop);
static PFN_SBLTROP3 g_real_SBltROP3 = nullptr;

// robust fallback and bounds-checked wrapper for storm.dll SBltROP3Tiled
static BOOL WINAPI Safe_SBltROP3Tiled(void* lpDest, RECT* lpDestRect, int destPitch,
                                      void* lpSrc, RECT* lpSrcRect, int srcPitch,
                                      int maskX, int maskY, void* lpMask, DWORD dwRop) {
    if (!lpDest || !lpSrc || destPitch <= 0 || srcPitch <= 0) return FALSE;
    if (!IsValidMemory(lpSrc, 4) || !IsValidMemory(lpDest, 4)) return FALSE;

    RECT dst = { 0, 0, 639, 479 };
    if (lpDestRect) dst = *lpDestRect;
    RECT src = { 0, 0, srcPitch - 1, 479 };
    if (lpSrcRect) src = *lpSrcRect;

    if (dst.left < 0) dst.left = 0;
    if (dst.top < 0) dst.top = 0;
    if (dst.right >= 640) dst.right = 639;
    if (dst.bottom >= 480) dst.bottom = 479;
    if (dst.left > dst.right || dst.top > dst.bottom) return TRUE;

    if (src.left < 0) src.left = 0;
    if (src.top < 0) src.top = 0;
    if (src.right >= srcPitch) src.right = srcPitch - 1;
    if (src.bottom >= 480) src.bottom = 479;
    if (src.left > src.right || src.top > src.bottom) return TRUE;

    int srcW = src.right - src.left + 1;
    int srcH = src.bottom - src.top + 1;
    if (srcW <= 0) srcW = srcPitch;
    if (srcH <= 0) srcH = 1;

    int cur_dy = dst.top;
    int chunk_sy = src.top + (((maskY % srcH) + srcH) % srcH);

    while (cur_dy <= dst.bottom) {
        int chunkH = src.bottom - chunk_sy + 1;
        int remH = dst.bottom - cur_dy + 1;
        if (chunkH > remH) chunkH = remH;

        int cur_dx = dst.left;
        int chunk_sx = src.left + (((maskX % srcW) + srcW) % srcW);

        while (cur_dx <= dst.right) {
            int chunkW = src.right - chunk_sx + 1;
            int remW = dst.right - cur_dx + 1;
            if (chunkW > remW) chunkW = remW;

            uint8_t* pDest = (uint8_t*)lpDest + (cur_dy * destPitch) + cur_dx;
            const uint8_t* pSrc = (const uint8_t*)lpSrc + (chunk_sy * srcPitch) + chunk_sx;

            if (g_real_SBltROP3) {
                g_real_SBltROP3(pDest, (void*)pSrc, chunkW, chunkH, destPitch, srcPitch, lpMask, dwRop);
            } else {
                for (int y = 0; y < chunkH; ++y) {
                    memcpy(pDest + (y * destPitch), pSrc + (y * srcPitch), chunkW);
                }
            }

            cur_dx += chunkW;
            chunk_sx = src.left;
        }

        cur_dy += chunkH;
        chunk_sy = src.top;
    }

    return TRUE;
}

// guards against invalid vtable pointers when storm updates palette
static BOOL WINAPI Hooked_SDrawUpdatePalette(DWORD dwStartingEntry, DWORD dwCount, LPPALETTEENTRY lpEntries, DWORD dwFlags) {
    if (!lpEntries || dwStartingEntry + dwCount > 256) return FALSE;
    if (g_real_SDrawUpdatePalette) {
        void** ppPal = (void**)0x15031164;
        if (ppPal && IsValidMemory(ppPal, sizeof(void*)) && *ppPal) {
            void* pal = *ppPal;
            if (!IsValidMemory(pal, sizeof(void*))) return FALSE;
            DWORD vptr = *(DWORD*)pal;
            if (vptr == 0x5f5f5f5f || vptr == 0xfeeefeee || vptr == 0xdddddddd || !IsValidMemory((void*)vptr, 0x20)) {
                return FALSE;
            }
            return g_real_SDrawUpdatePalette(dwStartingEntry, dwCount, lpEntries, dwFlags);
        }
    }
    return TRUE;
}

static void HookModuleIAT(HMODULE hMod);

// intercepts ansi module loading to hook imported api tables
static HMODULE WINAPI Hooked_LoadLibraryA(LPCSTR lpLibFileName) {
    HMODULE hMod = g_real_LoadLibraryA ? g_real_LoadLibraryA(lpLibFileName) : LoadLibraryA(lpLibFileName);
    if (hMod) {
        HookModuleIAT(hMod);
    }
    return hMod;
}

// intercepts wide module loading to hook imported api tables
static HMODULE WINAPI Hooked_LoadLibraryW(LPCWSTR lpLibFileName) {
    HMODULE hMod = g_real_LoadLibraryW ? g_real_LoadLibraryW(lpLibFileName) : LoadLibraryW(lpLibFileName);
    if (hMod) {
        HookModuleIAT(hMod);
    }
    return hMod;
}

// intercepts extended ansi module loading to hook imported api tables
static HMODULE WINAPI Hooked_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    HMODULE hMod = g_real_LoadLibraryExA ? g_real_LoadLibraryExA(lpLibFileName, hFile, dwFlags) : LoadLibraryExA(lpLibFileName, hFile, dwFlags);
    if (hMod && !(dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE))) {
        HookModuleIAT(hMod);
    }
    return hMod;
}

// intercepts extended wide module loading to hook imported api tables
static HMODULE WINAPI Hooked_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    HMODULE hMod = g_real_LoadLibraryExW ? g_real_LoadLibraryExW(lpLibFileName, hFile, dwFlags) : LoadLibraryExW(lpLibFileName, hFile, dwFlags);
    if (hMod && !(dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE))) {
        HookModuleIAT(hMod);
    }
    return hMod;
}

// patches import address table (iat) entries in a loaded module
static void SafeHookIAT(HMODULE hMod, const char* targetDllName, void* origFuncPtr, void* hookFuncPtr) {
    if (!hMod || !origFuncPtr || !hookFuncPtr) return;

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hMod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    DWORD importSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
    if (!importRva || !importSize) return;

    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hMod + importRva);

    for (; importDesc->Name && importDesc->FirstThunk; ++importDesc) {
        if (importDesc->Name >= nt->OptionalHeader.SizeOfImage) continue;
        const char* modName = (const char*)((BYTE*)hMod + importDesc->Name);
        if (_stricmp(modName, targetDllName) != 0) continue;

        if (importDesc->FirstThunk >= nt->OptionalHeader.SizeOfImage) continue;
        PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((BYTE*)hMod + importDesc->FirstThunk);
        while (thunk->u1.Function) {
            if ((void*)thunk->u1.Function == origFuncPtr) {
                DWORD oldProt;
                if (VirtualProtect(&thunk->u1.Function, sizeof(DWORD_PTR), PAGE_READWRITE, &oldProt)) {
                    thunk->u1.Function = (DWORD_PTR)hookFuncPtr;
                    VirtualProtect(&thunk->u1.Function, sizeof(DWORD_PTR), oldProt, &oldProt);
                }
            }
            thunk++;
        }
    }
}

// installs an inline jump hook or trampoline at a target function entry
static void* InstallApiHook(void* targetFunc, void* hookFunc) {
    if (!targetFunc || !hookFunc) return nullptr;

    BYTE* p = (BYTE*)targetFunc;
    void* realFunc = nullptr;

    if (p[0] == 0xFF && p[1] == 0x25) {
        DWORD* targetPtr = *(DWORD**)(p + 2);
        if (targetPtr && IsValidMemory(targetPtr, sizeof(void*))) {
            realFunc = (void*)(*targetPtr);
        }
    } else {
        BYTE* tramp = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (tramp) {
            memcpy(tramp, p, 5);
            tramp[5] = 0xE9;
            *(DWORD*)(tramp + 6) = ((DWORD)p + 5) - ((DWORD)tramp + 5 + 5);
            FlushInstructionCache(GetCurrentProcess(), tramp, 16);
            realFunc = tramp;
        }
    }

    DWORD oldProt;
    if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
        p[0] = 0xE9;
        *(DWORD*)(p + 1) = (DWORD)hookFunc - ((DWORD)p + 5);
        VirtualProtect(p, 5, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
    }

    return realFunc;
}

// applies all win32 and storm api hooks to the import table of a module
static void HookModuleIAT(HMODULE hMod) {
    if (!hMod) return;

    if (g_real_SetCursorPos) SafeHookIAT(hMod, "user32.dll", (void*)g_real_SetCursorPos, (void*)Hooked_SetCursorPos);
    if (g_real_GetCursorPos) SafeHookIAT(hMod, "user32.dll", (void*)g_real_GetCursorPos, (void*)Hooked_GetCursorPos);
    if (g_real_ClipCursor) SafeHookIAT(hMod, "user32.dll", (void*)g_real_ClipCursor, (void*)Hooked_ClipCursor);
    if (g_real_ScreenToClient) SafeHookIAT(hMod, "user32.dll", (void*)g_real_ScreenToClient, (void*)Hooked_ScreenToClient);
    if (g_real_ClientToScreen) SafeHookIAT(hMod, "user32.dll", (void*)g_real_ClientToScreen, (void*)Hooked_ClientToScreen);
    if (g_real_GetClientRect) SafeHookIAT(hMod, "user32.dll", (void*)g_real_GetClientRect, (void*)Hooked_GetClientRect);
    if (g_real_GetWindowRect) SafeHookIAT(hMod, "user32.dll", (void*)g_real_GetWindowRect, (void*)Hooked_GetWindowRect);
    if (g_real_GetSystemMetrics) SafeHookIAT(hMod, "user32.dll", (void*)g_real_GetSystemMetrics, (void*)Hooked_GetSystemMetrics);
    if (g_real_GetRegionData) SafeHookIAT(hMod, "gdi32.dll", (void*)g_real_GetRegionData, (void*)Hooked_GetRegionData);
    if (g_real_MessageBeep) SafeHookIAT(hMod, "user32.dll", (void*)g_real_MessageBeep, (void*)Hooked_MessageBeep);
    if (g_real_SetWindowTextA) SafeHookIAT(hMod, "user32.dll", (void*)g_real_SetWindowTextA, (void*)Hooked_SetWindowTextA);
    if (g_real_SetWindowTextW) SafeHookIAT(hMod, "user32.dll", (void*)g_real_SetWindowTextW, (void*)Hooked_SetWindowTextW);

    if (g_real_LoadLibraryA) SafeHookIAT(hMod, "kernel32.dll", (void*)g_real_LoadLibraryA, (void*)Hooked_LoadLibraryA);
    if (g_real_LoadLibraryW) SafeHookIAT(hMod, "kernel32.dll", (void*)g_real_LoadLibraryW, (void*)Hooked_LoadLibraryW);
    if (g_real_LoadLibraryExA) SafeHookIAT(hMod, "kernel32.dll", (void*)g_real_LoadLibraryExA, (void*)Hooked_LoadLibraryExA);
    if (g_real_LoadLibraryExW) SafeHookIAT(hMod, "kernel32.dll", (void*)g_real_LoadLibraryExW, (void*)Hooked_LoadLibraryExW);

    HMODULE hStorm = GetModuleHandleA("storm.dll");
    if (hStorm) {
        void* pTiled = (void*)GetProcAddress(hStorm, "SBltROP3Tiled");
        if (pTiled) SafeHookIAT(hMod, "storm.dll", pTiled, (void*)Safe_SBltROP3Tiled);
    }
    if (g_real_SDrawUpdatePalette) SafeHookIAT(hMod, "storm.dll", (void*)g_real_SDrawUpdatePalette, (void*)Hooked_SDrawUpdatePalette);
}

// initializes api hooks and hooks all loaded process modules
void MouseHook::init() {
    static bool s_initialized = false;
    if (s_initialized) return;
    s_initialized = true;

    HMODULE hUser = GetModuleHandleA("user32.dll");
    HMODULE hGdi = GetModuleHandleA("gdi32.dll");
    HMODULE hKernel = GetModuleHandleA("kernel32.dll");
    HMODULE hStorm = GetModuleHandleA("storm.dll");

    if (hUser) {
        void* pClip = (void*)GetProcAddress(hUser, "ClipCursor");
        void* pSet = (void*)GetProcAddress(hUser, "SetCursorPos");
        void* pGet = (void*)GetProcAddress(hUser, "GetCursorPos");
        void* pBeep = (void*)GetProcAddress(hUser, "MessageBeep");
        void* pSetTxtA = (void*)GetProcAddress(hUser, "SetWindowTextA");
        void* pSetTxtW = (void*)GetProcAddress(hUser, "SetWindowTextW");

        if (pClip) g_real_ClipCursor = (PFN_CLIPCURSOR)InstallApiHook(pClip, (void*)Hooked_ClipCursor);
        if (pSet) g_real_SetCursorPos = (PFN_SETCURSORPOS)InstallApiHook(pSet, (void*)Hooked_SetCursorPos);
        if (pGet) g_real_GetCursorPos = (PFN_GETCURSORPOS)InstallApiHook(pGet, (void*)Hooked_GetCursorPos);
        if (pBeep) g_real_MessageBeep = (PFN_MESSAGEBEEP)InstallApiHook(pBeep, (void*)Hooked_MessageBeep);
        if (pSetTxtA) g_real_SetWindowTextA = (PFN_SETWINDOWTEXTA)InstallApiHook(pSetTxtA, (void*)Hooked_SetWindowTextA);
        if (pSetTxtW) g_real_SetWindowTextW = (PFN_SETWINDOWTEXTW)InstallApiHook(pSetTxtW, (void*)Hooked_SetWindowTextW);

        if (!g_real_ScreenToClient) g_real_ScreenToClient = (PFN_SCREENTOCLIENT)GetProcAddress(hUser, "ScreenToClient");
        if (!g_real_ClientToScreen) g_real_ClientToScreen = (PFN_CLIENTTOSCREEN)GetProcAddress(hUser, "ClientToScreen");
        if (!g_real_GetClientRect) g_real_GetClientRect = (PFN_GETCLIENTRECT)GetProcAddress(hUser, "GetClientRect");
        if (!g_real_GetWindowRect) g_real_GetWindowRect = (PFN_GETWINDOWRECT)GetProcAddress(hUser, "GetWindowRect");
        if (!g_real_GetSystemMetrics) g_real_GetSystemMetrics = (PFN_GETSYSTEMMETRICS)GetProcAddress(hUser, "GetSystemMetrics");
    }

    if (!g_real_GetRegionData && hGdi) g_real_GetRegionData = (PFN_GETREGIONDATA)GetProcAddress(hGdi, "GetRegionData");

    if (!g_real_LoadLibraryA && hKernel) g_real_LoadLibraryA = (PFN_LOADLIBRARYA)GetProcAddress(hKernel, "LoadLibraryA");
    if (!g_real_LoadLibraryW && hKernel) g_real_LoadLibraryW = (PFN_LOADLIBRARYW)GetProcAddress(hKernel, "LoadLibraryW");
    if (!g_real_LoadLibraryExA && hKernel) g_real_LoadLibraryExA = (PFN_LOADLIBRARYEXA)GetProcAddress(hKernel, "LoadLibraryExA");
    if (!g_real_LoadLibraryExW && hKernel) g_real_LoadLibraryExW = (PFN_LOADLIBRARYEXW)GetProcAddress(hKernel, "LoadLibraryExW");

    if (hStorm) {
        if (!g_real_SBltROP3) g_real_SBltROP3 = (PFN_SBLTROP3)GetProcAddress(hStorm, "SBltROP3");
        void* pTiled = (void*)GetProcAddress(hStorm, "SBltROP3Tiled");
        if (pTiled) InstallApiHook(pTiled, (void*)Safe_SBltROP3Tiled);
        if (!g_real_SDrawUpdatePalette) g_real_SDrawUpdatePalette = (PFN_SDRAWUPDATEPALETTE)GetProcAddress(hStorm, "SDrawUpdatePalette");
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me;
        me.dwSize = sizeof(MODULEENTRY32);
        if (Module32First(hSnap, &me)) {
            do {
                HookModuleIAT(me.hModule);
            } while (Module32Next(hSnap, &me));
        }
        CloseHandle(hSnap);
    } else {
        HookModuleIAT(GetModuleHandleA(NULL));
        HookModuleIAT(GetModuleHandleA("Storm.dll"));
        HookModuleIAT(GetModuleHandleA("diabloui.dll"));
    }

    release_clip();
}

// scales directinput mouse deltas to match rendering aspect ratio
void MouseHook::process_dimousestate(DIMOUSESTATE* state) {
    if (!state) return;
    Renderer& r = Renderer::instance();
    if (!r.is_initialized()) return;

    if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && !g_mouse_locked) {
        state->lX = 0;
        state->lY = 0;
        state->lZ = 0;
        return;
    }

    r.scale_mouse_delta(state->lX, state->lY);
}

// scales directinput mouse2 deltas to match rendering aspect ratio
void MouseHook::process_dimousestate2(DIMOUSESTATE2* state) {
    if (!state) return;
    Renderer& r = Renderer::instance();
    if (!r.is_initialized()) return;

    if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && !g_mouse_locked) {
        state->lX = 0;
        state->lY = 0;
        state->lZ = 0;
        return;
    }

    r.scale_mouse_delta(state->lX, state->lY);
}

// scales buffered directinput axis deltas to match rendering aspect ratio
void MouseHook::process_dideviceobjectdata(DIDEVICEOBJECTDATA* data, DWORD count) {
    if (!data || count == 0) return;
    Renderer& r = Renderer::instance();
    if (!r.is_initialized()) return;

    if (r.get_config().mouse_capture == MouseCaptureMode::ONCLICK && !g_mouse_locked) {
        for (DWORD i = 0; i < count; ++i) {
            if (data[i].dwOfs == DIMOFS_X || data[i].dwOfs == DIMOFS_Y || data[i].dwOfs == DIMOFS_Z) {
                data[i].dwData = 0;
            }
        }
        return;
    }

    for (DWORD i = 0; i < count; ++i) {
        if (data[i].dwOfs == DIMOFS_X) {
            long dx = (long)data[i].dwData;
            long dummy = 0;
            r.scale_mouse_delta(dx, dummy);
            data[i].dwData = (DWORD)dx;
        } else if (data[i].dwOfs == DIMOFS_Y) {
            long dy = (long)data[i].dwData;
            long dummy = 0;
            r.scale_mouse_delta(dummy, dy);
            data[i].dwData = (DWORD)dy;
        }
    }
}

// maps win32 client coordinates into game resolution space
void MouseHook::transform_client_pos(POINT* pt) {
    if (!pt) return;
    Renderer& r = Renderer::instance();
    if (!r.is_initialized()) return;

    long gx = 0, gy = 0;
    r.client_to_game(pt->x, pt->y, gx, gy);
    pt->x = gx;
    pt->y = gy;
}

// maps win32 screen coordinates into game resolution space
void MouseHook::transform_screen_pos(POINT* pt) {
    if (!pt) return;
    Renderer& r = Renderer::instance();
    if (!r.is_initialized()) return;

    long gx = 0, gy = 0;
    r.screen_to_game(pt->x, pt->y, gx, gy);
    pt->x = gx;
    pt->y = gy;
}
