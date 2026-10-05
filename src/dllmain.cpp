#include <windows.h>
#include <ddraw.h>
#include "../include/ddraw_interface.h"
#include "../include/mouse_hook.h"

HMODULE g_hModule = nullptr;

/*
// helper to synthesize keyboard input with hardware scan code
static void emit_key_vk(BYTE vk, BYTE scan) {
    keybd_event(vk, scan, 0, 0);
    Sleep(80);
    keybd_event(vk, scan, KEYEVENTF_KEYUP, 0);
}

static void emit_key_char(char c) {
    SHORT vk_res = VkKeyScanA(c);
    BYTE vk = LOBYTE(vk_res);
    BYTE scan = (BYTE)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    keybd_event(vk, scan, 0, 0);
    Sleep(80);
    keybd_event(vk, scan, KEYEVENTF_KEYUP, 0);
}

// automated key emitter sequence for 'Diablo.exe'
static DWORD WINAPI TestInputThread_Diablo(LPVOID) {
    // initial delay for game window and directdraw initialization
    Sleep(2500);

    // 1s delay, 'enter' key
    Sleep(1000);
    emit_key_vk(VK_RETURN, 0x1C);

    // 1s delay, 'enter' key
    Sleep(1000);
    emit_key_vk(VK_RETURN, 0x1C);

    // 1s delay, 'enter' key
    Sleep(1000);
    emit_key_vk(VK_RETURN, 0x1C);

    return 0;
}

// automated key emitter sequence for 'StarCraft.exe'
static DWORD WINAPI TestInputThread_StarCraft(LPVOID) {
    // initial delay for game window and directdraw initialization
    Sleep(3000);

    // 2s delay, 's' key
    Sleep(2000);
    emit_key_char('s');

    // 1s delay, 'o' key
    Sleep(1000);
    emit_key_char('o');

    // 1s delay, 'u' key
    Sleep(1000);
    emit_key_char('u');

    // 1s delay, 'o' key
    Sleep(1000);
    emit_key_char('o');

    return 0;
}
*/

// dynamic link library main entry point
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: {
            g_hModule = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);

            // disable data execution prevention (dep) for compatibility with legacy jits/blt thunks
            typedef BOOL (WINAPI *PFN_SETPROCESSDEPPOLICY)(DWORD dwFlags);
            HMODULE hK32 = GetModuleHandleA("kernel32.dll");
            if (hK32) {
                PFN_SETPROCESSDEPPOLICY pSetDEP = (PFN_SETPROCESSDEPPOLICY)GetProcAddress(hK32, "SetProcessDEPPolicy");
                if (pSetDEP) {
                    pSetDEP(0); // 0 = PROCESS_DEP_DISABLE
                }
            }

            // initialize mouse coordinate hooks and transparent secondary window handlers
            MouseHook::init();

            // test key emitter thread (uncomment for automated test runs)
            // diablo test input: CreateThread(nullptr, 0, TestInputThread_Diablo, nullptr, 0, nullptr);
            // starcraft test input: CreateThread(nullptr, 0, TestInputThread_StarCraft, nullptr, 0, nullptr);
            break;
        }
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

extern "C" {

// exports DirectDrawCreate entry point (DirectX 1.0 - 6.0 interface)
HRESULT WINAPI DirectDrawCreate(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter) {
    MouseHook::init();
    if (!lplpDD) return DDERR_INVALIDPARAMS;
    DDrawImpl* dd = new DDrawImpl();
    *lplpDD = (LPDIRECTDRAW)dd->get_interface1();
    return DD_OK;
}

// exports DirectDrawCreateEx entry point (DirectX 7.0 interface)
HRESULT WINAPI DirectDrawCreateEx(GUID* lpGUID, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter) {
    MouseHook::init();
    if (!lplpDD) return DDERR_INVALIDPARAMS;
    DDrawImpl* dd = new DDrawImpl();
    HRESULT hr = dd->query_interface(iid, lplpDD);
    dd->release();
    return hr;
}

// enumerates display drivers (ansi)
HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA lpCallback, LPVOID lpContext) {
    if (lpCallback) {
        lpCallback(nullptr, (LPSTR)"DirectDraw OpenGL Wrapper", (LPSTR)"display", lpContext);
    }
    return DD_OK;
}

// enumerates display drivers (unicode)
HRESULT WINAPI DirectDrawEnumerateW(LPDDENUMCALLBACKW lpCallback, LPVOID lpContext) {
    if (lpCallback) {
        lpCallback(nullptr, (LPWSTR)L"DirectDraw OpenGL Wrapper", (LPWSTR)L"display", lpContext);
    }
    return DD_OK;
}

// extended driver enumeration (ansi)
HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA lpCallback, LPVOID lpContext, DWORD dwFlags) {
    if (lpCallback) {
        lpCallback(nullptr, (LPSTR)"DirectDraw OpenGL Wrapper", (LPSTR)"display", lpContext, nullptr);
    }
    return DD_OK;
}

// extended driver enumeration (unicode)
HRESULT WINAPI DirectDrawEnumerateExW(LPDDENUMCALLBACKEXW lpCallback, LPVOID lpContext, DWORD dwFlags) {
    if (lpCallback) {
        lpCallback(nullptr, (LPWSTR)L"DirectDraw OpenGL Wrapper", (LPWSTR)L"display", lpContext, nullptr);
    }
    return DD_OK;
}

// checks if dll can be unloaded by com runtime
HRESULT WINAPI DllCanUnloadNow() {
    return S_OK;
}

// retrieves class factory object for com registration
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

// registers com server
HRESULT WINAPI DllRegisterServer() {
    return S_OK;
}

// unregisters com server
HRESULT WINAPI DllUnregisterServer() {
    return S_OK;
}

// acquires directdraw thread lock (stub for win9x directdraw synchronization)
void WINAPI AcquireDDThreadLock() {}

// releases directdraw thread lock (stub for win9x directdraw synchronization)
void WINAPI ReleaseDDThreadLock() {}

}
