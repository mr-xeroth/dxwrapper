#include "../include/ddraw_interface.h"
#include "../include/ddraw_surface.h"
#include "../include/ddraw_palette.h"
#include "../include/ddraw_clipper.h"
#include "../include/mouse_hook.h"
#include <cstring>

// translates directdraw 4/7 surface descriptor to directdraw 1/2/3 descriptor
static void desc2_to_desc1(const DDSURFACEDESC2* d2, DDSURFACEDESC* d1) {
    if (!d2 || !d1) return;
    std::memset(d1, 0, sizeof(DDSURFACEDESC));
    d1->dwSize = sizeof(DDSURFACEDESC);
    d1->dwFlags = d2->dwFlags & 0x007FFFFF;
    d1->dwHeight = d2->dwHeight;
    d1->dwWidth = d2->dwWidth;
    d1->lPitch = d2->lPitch;
    d1->dwBackBufferCount = d2->dwBackBufferCount;
    d1->dwZBufferBitDepth = d2->dwMipMapCount;
    d1->dwAlphaBitDepth = d2->dwAlphaBitDepth;
    d1->dwReserved = d2->dwReserved;
    d1->lpSurface = d2->lpSurface;
    d1->ddckCKDestOverlay = d2->ddckCKDestOverlay;
    d1->ddckCKDestBlt = d2->ddckCKDestBlt;
    d1->ddckCKSrcOverlay = d2->ddckCKSrcOverlay;
    d1->ddckCKSrcBlt = d2->ddckCKSrcBlt;
    d1->ddpfPixelFormat = d2->ddpfPixelFormat;
    d1->ddsCaps.dwCaps = d2->ddsCaps.dwCaps;
}

// translates directdraw 1/2/3 surface descriptor to directdraw 4/7 descriptor
static void desc1_to_desc2(const DDSURFACEDESC* d1, DDSURFACEDESC2* d2) {
    if (!d1 || !d2) return;
    std::memset(d2, 0, sizeof(DDSURFACEDESC2));
    d2->dwSize = sizeof(DDSURFACEDESC2);
    d2->dwFlags = d1->dwFlags;
    d2->dwHeight = d1->dwHeight;
    d2->dwWidth = d1->dwWidth;
    d2->lPitch = d1->lPitch;
    d2->dwBackBufferCount = d1->dwBackBufferCount;
    d2->dwMipMapCount = d1->dwZBufferBitDepth;
    d2->dwAlphaBitDepth = d1->dwAlphaBitDepth;
    d2->dwReserved = d1->dwReserved;
    d2->lpSurface = d1->lpSurface;
    d2->ddckCKDestOverlay = d1->ddckCKDestOverlay;
    d2->ddckCKDestBlt = d1->ddckCKDestBlt;
    d2->ddckCKSrcOverlay = d1->ddckCKSrcOverlay;
    d2->ddckCKSrcBlt = d1->ddckCKSrcBlt;
    d2->ddpfPixelFormat = d1->ddpfPixelFormat;
    d2->ddsCaps.dwCaps = d1->ddsCaps.dwCaps;
}

// constructs directdraw core implementation instance and loads ini configuration
DDrawImpl::DDrawImpl()
    : ref_count(1), target_hwnd(nullptr), cooperative_flags(0),
      display_width(640), display_height(480), display_bpp(8),
      primary_surface(nullptr),
      iface7(nullptr), iface4(nullptr), iface2(nullptr), iface1(nullptr) {
    config = ConfigManager::load();
    MouseHook::init();
}

// cleans up primary surface attachments and com interface wrapper instances
DDrawImpl::~DDrawImpl() {
    if (primary_surface) {
        primary_surface->detach_parent();
        primary_surface = nullptr;
    }
    delete iface7;
    delete iface4;
    delete iface2;
    delete iface1;
}

// decrements implementation reference count
ULONG DDrawImpl::release() {
    if (ref_count > 0) {
        --ref_count;
    }
    return ref_count;
}

// lazily creates and returns idirectdraw7 com wrapper
DDraw7* DDrawImpl::get_interface7() {
    if (!iface7) iface7 = new DDraw7(this);
    return iface7;
}

// lazily creates and returns idirectdraw4 com wrapper
DDraw4* DDrawImpl::get_interface4() {
    if (!iface4) iface4 = new DDraw4(this);
    return iface4;
}

// lazily creates and returns idirectdraw2 com wrapper
DDraw2* DDrawImpl::get_interface2() {
    if (!iface2) iface2 = new DDraw2(this);
    return iface2;
}

// lazily creates and returns idirectdraw com wrapper
DDraw1* DDrawImpl::get_interface1() {
    if (!iface1) iface1 = new DDraw1(this);
    return iface1;
}

// queries directdraw com interface by iid
HRESULT DDrawImpl::query_interface(REFIID riid, LPVOID* ppvObj) {
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirectDraw7) {
        *ppvObj = get_interface7();
        add_ref();
        return S_OK;
    }
    if (riid == IID_IDirectDraw4) {
        *ppvObj = get_interface4();
        add_ref();
        return S_OK;
    }
    if (riid == IID_IDirectDraw2) {
        *ppvObj = get_interface2();
        add_ref();
        return S_OK;
    }
    if (riid == IID_IDirectDraw) {
        *ppvObj = get_interface1();
        add_ref();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

// allocates new surface implementation and registers primary surface if requested
HRESULT DDrawImpl::create_surface(LPDDSURFACEDESC2 lpDDSurfaceDesc, DDrawSurfaceImpl** out_surf) {
    if (!out_surf || !lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* surf = new DDrawSurfaceImpl(this, lpDDSurfaceDesc);
    if (surf->is_primary()) {
        primary_surface = surf;
    }
    *out_surf = surf;
    return DD_OK;
}

// creates new directdraw palette object
HRESULT DDrawImpl::create_palette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette) {
    if (!lplpDDPalette) return DDERR_INVALIDPARAMS;
    *lplpDDPalette = new DDrawPalette(dwFlags, lpColorTable);
    return DD_OK;
}

// creates new directdraw clipper object
HRESULT DDrawImpl::create_clipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper) {
    if (!lplpDDClipper) return DDERR_INVALIDPARAMS;
    *lplpDDClipper = new DDrawClipper();
    return DD_OK;
}

// sets cooperative level flags and associates target window handle
HRESULT DDrawImpl::set_cooperative_level(HWND hWnd, DWORD dwFlags) {
    target_hwnd = hWnd;
    cooperative_flags = dwFlags;
    return DD_OK;
}

// configures virtual display resolution and initializes opengl presentation window
HRESULT DDrawImpl::set_display_mode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP) {
    display_width = dwWidth > 0 ? dwWidth : 640;
    display_height = dwHeight > 0 ? dwHeight : 480;
    display_bpp = dwBPP > 0 ? dwBPP : 32;

    Renderer::instance().init(target_hwnd, display_width, display_height, config);
    return DD_OK;
}

// restores original desktop display mode
HRESULT DDrawImpl::restore_display_mode() {
    return DD_OK;
}

// populates surface descriptor with active display mode dimensions and pixel format
HRESULT DDrawImpl::get_display_mode(LPDDSURFACEDESC2 lpDDSurfaceDesc) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DWORD sz = lpDDSurfaceDesc->dwSize;
    std::memset(lpDDSurfaceDesc, 0, sz);
    lpDDSurfaceDesc->dwSize = sz;
    lpDDSurfaceDesc->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    lpDDSurfaceDesc->dwWidth = display_width;
    lpDDSurfaceDesc->dwHeight = display_height;
    lpDDSurfaceDesc->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = DDPF_RGB;
    lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = display_bpp;
    return DD_OK;
}

// returns simulated hardware and emulation driver capabilities
HRESULT DDrawImpl::get_caps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHELCaps) {
    auto fill_caps = [](LPDDCAPS c) {
        if (!c) return;
        DWORD sz = c->dwSize ? c->dwSize : sizeof(DDCAPS);
        std::memset(c, 0, sz);
        c->dwSize = sz;
        c->dwCaps = DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_COLORKEY | DDCAPS_PALETTE | DDCAPS_READSCANLINE;
        c->dwCKeyCaps = DDCKEYCAPS_SRCBLT | DDCKEYCAPS_DESTBLT;
        c->dwFXCaps = DDFXCAPS_BLTMIRRORLEFTRIGHT | DDFXCAPS_BLTMIRRORUPDOWN;
        c->dwVidMemTotal = 128 * 1024 * 1024;
        c->dwVidMemFree = 128 * 1024 * 1024;
    };
    fill_caps(lpDDDriverCaps);
    fill_caps(lpDDHELCaps);
    return DD_OK;
}

// enumerates supported display resolutions and color depths for the application
HRESULT DDrawImpl::enum_display_modes(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback) {
    if (!lpEnumModesCallback) return DDERR_INVALIDPARAMS;

    const struct { int w; int h; } res_list[] = {
        { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 },
        { 640, 400 }, { 640, 480 }, { 800, 600 }, { 1024, 768 },
        { 1280, 720 }, { 1280, 960 }, { 1280, 1024 }, { 1600, 1200 }, { 1920, 1080 }
    };
    const int bpp_list[] = { 8, 16, 24, 32 };

    for (const auto& res : res_list) {
        for (int bpp : bpp_list) {
            DDSURFACEDESC2 d;
            std::memset(&d, 0, sizeof(d));
            d.dwSize = sizeof(d);
            d.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
            d.dwWidth = res.w;
            d.dwHeight = res.h;
            d.dwRefreshRate = 60;
            d.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
            d.ddpfPixelFormat.dwFlags = DDPF_RGB;
            d.ddpfPixelFormat.dwRGBBitCount = bpp;
            if (bpp == 8) {
                d.ddpfPixelFormat.dwFlags |= DDPF_PALETTEINDEXED8;
            }

            if (lpEnumModesCallback(&d, lpContext) == DDENUMRET_CANCEL) {
                return DD_OK;
            }
        }
    }
    return DD_OK;
}

// fills device identifier struct with wrapper branding strings
HRESULT DDrawImpl::get_device_identifier(LPDDDEVICEIDENTIFIER2 lpdddi, DWORD dwFlags) {
    if (!lpdddi) return DDERR_INVALIDPARAMS;
    DWORD sz = sizeof(DDDEVICEIDENTIFIER2);
    std::memset(lpdddi, 0, sz);
    strncpy(lpdddi->szDriver, "DirectDraw OpenGL Wrapper", sizeof(lpdddi->szDriver) - 1);
    strncpy(lpdddi->szDescription, "OpenGL 3.3+ DirectDraw Display", sizeof(lpdddi->szDescription) - 1);
    return DD_OK;
}

// directdraw 7 interface thunks
STDMETHODIMP DDraw7::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDraw7::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDraw7::Release() { return impl->release(); }
STDMETHODIMP DDraw7::Compact() { return DD_OK; }
STDMETHODIMP DDraw7::CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) { return impl->create_clipper(dwFlags, lplpDDClipper); }
STDMETHODIMP DDraw7::CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) { return impl->create_palette(dwFlags, lpColorTable, lplpDDPalette); }
STDMETHODIMP DDraw7::CreateSurface(LPDDSURFACEDESC2 lpDDSurfaceDesc, LPDIRECTDRAWSURFACE7* lplpDDSurface, IUnknown* pUnkOuter) {
    if (!lplpDDSurface || !lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* surf = nullptr;
    HRESULT hr = impl->create_surface(lpDDSurfaceDesc, &surf);
    if (SUCCEEDED(hr) && surf) {
        *lplpDDSurface = surf->get_interface7();
        surf->get_surface_desc(lpDDSurfaceDesc);
    }
    return hr;
}
STDMETHODIMP DDraw7::DuplicateSurface(LPDIRECTDRAWSURFACE7 lpDDSurface, LPDIRECTDRAWSURFACE7* lplpDupDDSurface) { return DDERR_CANTDUPLICATE; }
STDMETHODIMP DDraw7::EnumDisplayModes(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback) {
    return impl->enum_display_modes(dwFlags, lpDDSurfaceDesc, lpContext, lpEnumModesCallback);
}
STDMETHODIMP DDraw7::EnumSurfaces(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSD2, LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDraw7::FlipToGDISurface() { return DD_OK; }
STDMETHODIMP DDraw7::GetCaps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHECaps) { return impl->get_caps(lpDDDriverCaps, lpDDHECaps); }
STDMETHODIMP DDraw7::GetDisplayMode(LPDDSURFACEDESC2 lpDDSurfaceDesc) { return impl->get_display_mode(lpDDSurfaceDesc); }
STDMETHODIMP DDraw7::GetFourCCCodes(LPDWORD lpNumCodes, LPDWORD lpCodes) { if (lpNumCodes) *lpNumCodes = 0; return DD_OK; }
STDMETHODIMP DDraw7::GetGDISurface(LPDIRECTDRAWSURFACE7* lplpGDIDDSSurface) {
    if (!lplpGDIDDSSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* p = impl->get_primary_surface();
    *lplpGDIDDSSurface = p ? p->get_interface7() : nullptr;
    if (p) p->add_ref();
    return DD_OK;
}
STDMETHODIMP DDraw7::GetMonitorFrequency(LPDWORD lpdwFrequency) { if (lpdwFrequency) *lpdwFrequency = 60; return DD_OK; }
STDMETHODIMP DDraw7::GetScanLine(LPDWORD lpdwScanLine) { if (lpdwScanLine) *lpdwScanLine = 0; return DD_OK; }
STDMETHODIMP DDraw7::GetVerticalBlankStatus(LPBOOL lpbIsInVB) { if (lpbIsInVB) *lpbIsInVB = FALSE; return DD_OK; }
STDMETHODIMP DDraw7::Initialize(GUID* lpGUID) { return DD_OK; }
STDMETHODIMP DDraw7::RestoreDisplayMode() { return impl->restore_display_mode(); }
STDMETHODIMP DDraw7::SetCooperativeLevel(HWND hWnd, DWORD dwFlags) { return impl->set_cooperative_level(hWnd, dwFlags); }
STDMETHODIMP DDraw7::SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags) {
    return impl->set_display_mode(dwWidth, dwHeight, dwBPP);
}
STDMETHODIMP DDraw7::WaitForVerticalBlank(DWORD dwFlags, HANDLE hEvent) { Sleep(16); return DD_OK; }
STDMETHODIMP DDraw7::GetAvailableVidMem(LPDDSCAPS2 lpDDSCaps, LPDWORD lpdwTotal, LPDWORD lpdwFree) {
    if (lpdwTotal) *lpdwTotal = 128 * 1024 * 1024;
    if (lpdwFree) *lpdwFree = 128 * 1024 * 1024;
    return DD_OK;
}
STDMETHODIMP DDraw7::GetSurfaceFromDC(HDC hdc, LPDIRECTDRAWSURFACE7* lpDDS) {
    if (!lpDDS) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* p = impl->get_primary_surface();
    *lpDDS = p ? p->get_interface7() : nullptr;
    if (p) p->add_ref();
    return DD_OK;
}
STDMETHODIMP DDraw7::RestoreAllSurfaces() { return DD_OK; }
STDMETHODIMP DDraw7::TestCooperativeLevel() { return DD_OK; }
STDMETHODIMP DDraw7::GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 lpdddi, DWORD dwFlags) { return impl->get_device_identifier(lpdddi, dwFlags); }
STDMETHODIMP DDraw7::StartModeTest(LPSIZE lpModesToTest, DWORD dwNumEntries, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDraw7::EvaluateMode(DWORD dwFlags, DWORD* pSecondsUntilTimeout) { if (pSecondsUntilTimeout) *pSecondsUntilTimeout = 0; return DD_OK; }

// directdraw 4 interface thunks
STDMETHODIMP DDraw4::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDraw4::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDraw4::Release() { return impl->release(); }
STDMETHODIMP DDraw4::Compact() { return DD_OK; }
STDMETHODIMP DDraw4::CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) { return impl->create_clipper(dwFlags, lplpDDClipper); }
STDMETHODIMP DDraw4::CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) { return impl->create_palette(dwFlags, lpColorTable, lplpDDPalette); }
STDMETHODIMP DDraw4::CreateSurface(LPDDSURFACEDESC2 lpDDSurfaceDesc, LPDIRECTDRAWSURFACE4* lplpDDSurface, IUnknown* pUnkOuter) {
    if (!lplpDDSurface || !lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* surf = nullptr;
    HRESULT hr = impl->create_surface(lpDDSurfaceDesc, &surf);
    if (SUCCEEDED(hr) && surf) {
        *lplpDDSurface = surf->get_interface4();
        surf->get_surface_desc(lpDDSurfaceDesc);
    }
    return hr;
}
STDMETHODIMP DDraw4::DuplicateSurface(LPDIRECTDRAWSURFACE4 lpDDSurface, LPDIRECTDRAWSURFACE4* lplpDupDDSurface) { return DDERR_CANTDUPLICATE; }
STDMETHODIMP DDraw4::EnumDisplayModes(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback) {
    return impl->enum_display_modes(dwFlags, lpDDSurfaceDesc, lpContext, lpEnumModesCallback);
}
STDMETHODIMP DDraw4::EnumSurfaces(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSD2, LPVOID lpContext, LPDDENUMSURFACESCALLBACK2 lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDraw4::FlipToGDISurface() { return DD_OK; }
STDMETHODIMP DDraw4::GetCaps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHECaps) { return impl->get_caps(lpDDDriverCaps, lpDDHECaps); }
STDMETHODIMP DDraw4::GetDisplayMode(LPDDSURFACEDESC2 lpDDSurfaceDesc) { return impl->get_display_mode(lpDDSurfaceDesc); }
STDMETHODIMP DDraw4::GetFourCCCodes(LPDWORD lpNumCodes, LPDWORD lpCodes) { if (lpNumCodes) *lpNumCodes = 0; return DD_OK; }
STDMETHODIMP DDraw4::GetGDISurface(LPDIRECTDRAWSURFACE4* lplpGDIDDSSurface) {
    if (!lplpGDIDDSSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* p = impl->get_primary_surface();
    *lplpGDIDDSSurface = p ? p->get_interface4() : nullptr;
    if (p) p->add_ref();
    return DD_OK;
}
STDMETHODIMP DDraw4::GetMonitorFrequency(LPDWORD lpdwFrequency) { if (lpdwFrequency) *lpdwFrequency = 60; return DD_OK; }
STDMETHODIMP DDraw4::GetScanLine(LPDWORD lpdwScanLine) { if (lpdwScanLine) *lpdwScanLine = 0; return DD_OK; }
STDMETHODIMP DDraw4::GetVerticalBlankStatus(LPBOOL lpbIsInVB) { if (lpbIsInVB) *lpbIsInVB = FALSE; return DD_OK; }
STDMETHODIMP DDraw4::Initialize(GUID* lpGUID) { return DD_OK; }
STDMETHODIMP DDraw4::RestoreDisplayMode() { return impl->restore_display_mode(); }
STDMETHODIMP DDraw4::SetCooperativeLevel(HWND hWnd, DWORD dwFlags) { return impl->set_cooperative_level(hWnd, dwFlags); }
STDMETHODIMP DDraw4::SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags) {
    return impl->set_display_mode(dwWidth, dwHeight, dwBPP);
}
STDMETHODIMP DDraw4::WaitForVerticalBlank(DWORD dwFlags, HANDLE hEvent) { Sleep(16); return DD_OK; }
STDMETHODIMP DDraw4::GetAvailableVidMem(LPDDSCAPS2 lpDDSCaps, LPDWORD lpdwTotal, LPDWORD lpdwFree) {
    if (lpdwTotal) *lpdwTotal = 128 * 1024 * 1024;
    if (lpdwFree) *lpdwFree = 128 * 1024 * 1024;
    return DD_OK;
}
STDMETHODIMP DDraw4::GetSurfaceFromDC(HDC hdc, LPDIRECTDRAWSURFACE4* lpDDS) {
    if (!lpDDS) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* p = impl->get_primary_surface();
    *lpDDS = p ? p->get_interface4() : nullptr;
    if (p) p->add_ref();
    return DD_OK;
}
STDMETHODIMP DDraw4::GetDeviceIdentifier(LPDDDEVICEIDENTIFIER lpdddi, DWORD dwFlags) {
    if (!lpdddi) return DDERR_INVALIDPARAMS;
    DWORD sz = sizeof(DDDEVICEIDENTIFIER);
    std::memset(lpdddi, 0, sz);
    strncpy(lpdddi->szDriver, "DirectDraw OpenGL Wrapper", sizeof(lpdddi->szDriver) - 1);
    strncpy(lpdddi->szDescription, "OpenGL 3.3+ DirectDraw Display", sizeof(lpdddi->szDescription) - 1);
    return DD_OK;
}
STDMETHODIMP DDraw4::RestoreAllSurfaces() { return DD_OK; }
STDMETHODIMP DDraw4::TestCooperativeLevel() { return DD_OK; }

// directdraw 2 interface thunks
STDMETHODIMP DDraw2::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDraw2::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDraw2::Release() { return impl->release(); }
STDMETHODIMP DDraw2::Compact() { return DD_OK; }
STDMETHODIMP DDraw2::CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) { return impl->create_clipper(dwFlags, lplpDDClipper); }
STDMETHODIMP DDraw2::CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) { return impl->create_palette(dwFlags, lpColorTable, lplpDDPalette); }
STDMETHODIMP DDraw2::CreateSurface(LPDDSURFACEDESC lpDDSurfaceDesc, LPDIRECTDRAWSURFACE* lplpDDSurface, IUnknown* pUnkOuter) {
    if (!lplpDDSurface || !lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d2;
    desc1_to_desc2(lpDDSurfaceDesc, &d2);
    DDrawSurfaceImpl* surf = nullptr;
    HRESULT hr = impl->create_surface(&d2, &surf);
    if (SUCCEEDED(hr) && surf) {
        *lplpDDSurface = surf->get_interface1();
        surf->get_surface_desc_v1(lpDDSurfaceDesc);
    }
    return hr;
}
STDMETHODIMP DDraw2::DuplicateSurface(LPDIRECTDRAWSURFACE lpDDSurface, LPDIRECTDRAWSURFACE* lplpDupDDSurface) { return DDERR_CANTDUPLICATE; }
STDMETHODIMP DDraw2::EnumDisplayModes(DWORD dwFlags, LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK lpEnumModesCallback) {
    struct CallbackThunk {
        LPDDENUMMODESCALLBACK cb;
        LPVOID ctx;
        static HRESULT WINAPI thunk(LPDDSURFACEDESC2 d2, LPVOID ctx_ptr) {
            CallbackThunk* self = (CallbackThunk*)ctx_ptr;
            DDSURFACEDESC d1;
            desc2_to_desc1(d2, &d1);
            return self->cb(&d1, self->ctx);
        }
    };
    CallbackThunk th = { lpEnumModesCallback, lpContext };
    return impl->enum_display_modes(dwFlags, nullptr, &th, (LPDDENUMMODESCALLBACK2)CallbackThunk::thunk);
}
STDMETHODIMP DDraw2::EnumSurfaces(DWORD dwFlags, LPDDSURFACEDESC lpDDSD, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDraw2::FlipToGDISurface() { return DD_OK; }
STDMETHODIMP DDraw2::GetCaps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHECaps) { return impl->get_caps(lpDDDriverCaps, lpDDHECaps); }
STDMETHODIMP DDraw2::GetDisplayMode(LPDDSURFACEDESC lpDDSurfaceDesc) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d2;
    std::memset(&d2, 0, sizeof(d2));
    d2.dwSize = sizeof(d2);
    HRESULT hr = impl->get_display_mode(&d2);
    if (SUCCEEDED(hr)) desc2_to_desc1(&d2, lpDDSurfaceDesc);
    return hr;
}
STDMETHODIMP DDraw2::GetFourCCCodes(LPDWORD lpNumCodes, LPDWORD lpCodes) { if (lpNumCodes) *lpNumCodes = 0; return DD_OK; }
STDMETHODIMP DDraw2::GetGDISurface(LPDIRECTDRAWSURFACE* lplpGDIDDSSurface) {
    if (!lplpGDIDDSSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* p = impl->get_primary_surface();
    *lplpGDIDDSSurface = p ? p->get_interface1() : nullptr;
    if (p) p->add_ref();
    return DD_OK;
}
STDMETHODIMP DDraw2::GetMonitorFrequency(LPDWORD lpdwFrequency) { if (lpdwFrequency) *lpdwFrequency = 60; return DD_OK; }
STDMETHODIMP DDraw2::GetScanLine(LPDWORD lpdwScanLine) { if (lpdwScanLine) *lpdwScanLine = 0; return DD_OK; }
STDMETHODIMP DDraw2::GetVerticalBlankStatus(LPBOOL lpbIsInVB) { if (lpbIsInVB) *lpbIsInVB = FALSE; return DD_OK; }
STDMETHODIMP DDraw2::Initialize(GUID* lpGUID) { return DD_OK; }
STDMETHODIMP DDraw2::RestoreDisplayMode() { return impl->restore_display_mode(); }
STDMETHODIMP DDraw2::SetCooperativeLevel(HWND hWnd, DWORD dwFlags) { return impl->set_cooperative_level(hWnd, dwFlags); }
STDMETHODIMP DDraw2::SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags) {
    return impl->set_display_mode(dwWidth, dwHeight, dwBPP);
}
STDMETHODIMP DDraw2::WaitForVerticalBlank(DWORD dwFlags, HANDLE hEvent) { Sleep(16); return DD_OK; }
STDMETHODIMP DDraw2::GetAvailableVidMem(LPDDSCAPS lpDDSCaps, LPDWORD lpdwTotal, LPDWORD lpdwFree) {
    if (lpdwTotal) *lpdwTotal = 128 * 1024 * 1024;
    if (lpdwFree) *lpdwFree = 128 * 1024 * 1024;
    return DD_OK;
}

// directdraw 1 interface thunks
STDMETHODIMP DDraw1::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDraw1::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDraw1::Release() { return impl->release(); }
STDMETHODIMP DDraw1::Compact() { return DD_OK; }
STDMETHODIMP DDraw1::CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) { return impl->create_clipper(dwFlags, lplpDDClipper); }
STDMETHODIMP DDraw1::CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) { return impl->create_palette(dwFlags, lpColorTable, lplpDDPalette); }
STDMETHODIMP DDraw1::CreateSurface(LPDDSURFACEDESC lpDDSurfaceDesc, LPDIRECTDRAWSURFACE* lplpDDSurface, IUnknown* pUnkOuter) {
    if (!lplpDDSurface || !lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d2;
    desc1_to_desc2(lpDDSurfaceDesc, &d2);
    DDrawSurfaceImpl* surf = nullptr;
    HRESULT hr = impl->create_surface(&d2, &surf);
    if (SUCCEEDED(hr) && surf) {
        *lplpDDSurface = surf->get_interface1();
        surf->get_surface_desc_v1(lpDDSurfaceDesc);
    }
    return hr;
}
STDMETHODIMP DDraw1::DuplicateSurface(LPDIRECTDRAWSURFACE lpDDSurface, LPDIRECTDRAWSURFACE* lplpDupDDSurface) { return DDERR_CANTDUPLICATE; }
STDMETHODIMP DDraw1::EnumDisplayModes(DWORD dwFlags, LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK lpEnumModesCallback) {
    struct CallbackThunk {
        LPDDENUMMODESCALLBACK cb;
        LPVOID ctx;
        static HRESULT WINAPI thunk(LPDDSURFACEDESC2 d2, LPVOID ctx_ptr) {
            CallbackThunk* self = (CallbackThunk*)ctx_ptr;
            DDSURFACEDESC d1;
            desc2_to_desc1(d2, &d1);
            return self->cb(&d1, self->ctx);
        }
    };
    CallbackThunk th = { lpEnumModesCallback, lpContext };
    return impl->enum_display_modes(dwFlags, nullptr, &th, (LPDDENUMMODESCALLBACK2)CallbackThunk::thunk);
}
STDMETHODIMP DDraw1::EnumSurfaces(DWORD dwFlags, LPDDSURFACEDESC lpDDSD, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDraw1::FlipToGDISurface() { return DD_OK; }
STDMETHODIMP DDraw1::GetCaps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHECaps) { return impl->get_caps(lpDDDriverCaps, lpDDHECaps); }
STDMETHODIMP DDraw1::GetDisplayMode(LPDDSURFACEDESC lpDDSurfaceDesc) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d2;
    std::memset(&d2, 0, sizeof(d2));
    d2.dwSize = sizeof(d2);
    HRESULT hr = impl->get_display_mode(&d2);
    if (SUCCEEDED(hr)) desc2_to_desc1(&d2, lpDDSurfaceDesc);
    return hr;
}
STDMETHODIMP DDraw1::GetFourCCCodes(LPDWORD lpNumCodes, LPDWORD lpCodes) { if (lpNumCodes) *lpNumCodes = 0; return DD_OK; }
STDMETHODIMP DDraw1::GetGDISurface(LPDIRECTDRAWSURFACE* lplpGDIDDSSurface) {
    if (!lplpGDIDDSSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* p = impl->get_primary_surface();
    *lplpGDIDDSSurface = p ? p->get_interface1() : nullptr;
    if (p) p->add_ref();
    return DD_OK;
}
STDMETHODIMP DDraw1::GetMonitorFrequency(LPDWORD lpdwFrequency) { if (lpdwFrequency) *lpdwFrequency = 60; return DD_OK; }
STDMETHODIMP DDraw1::GetScanLine(LPDWORD lpdwScanLine) { if (lpdwScanLine) *lpdwScanLine = 0; return DD_OK; }
STDMETHODIMP DDraw1::GetVerticalBlankStatus(LPBOOL lpbIsInVB) { if (lpbIsInVB) *lpbIsInVB = FALSE; return DD_OK; }
STDMETHODIMP DDraw1::Initialize(GUID* lpGUID) { return DD_OK; }
STDMETHODIMP DDraw1::RestoreDisplayMode() { return impl->restore_display_mode(); }
STDMETHODIMP DDraw1::SetCooperativeLevel(HWND hWnd, DWORD dwFlags) { return impl->set_cooperative_level(hWnd, dwFlags); }
STDMETHODIMP DDraw1::SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP) {
    return impl->set_display_mode(dwWidth, dwHeight, dwBPP);
}
STDMETHODIMP DDraw1::WaitForVerticalBlank(DWORD dwFlags, HANDLE hEvent) { Sleep(16); return DD_OK; }
