#pragma once

#include <windows.h>
#include <ddraw.h>
#include <vector>
#include "config.h"
#include "renderer.h"

class DDrawSurfaceImpl;
class DDraw1;
class DDraw2;
class DDraw4;
class DDraw7;

class DDrawImpl {
public:
    DDrawImpl();
    ~DDrawImpl();

    void add_ref() { ++ref_count; }
    ULONG release();

    HRESULT query_interface(REFIID riid, LPVOID* ppvObj);

    HRESULT create_surface(LPDDSURFACEDESC2 lpDDSurfaceDesc, DDrawSurfaceImpl** out_surf);
    HRESULT create_palette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette);
    HRESULT create_clipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper);
    HRESULT set_cooperative_level(HWND hWnd, DWORD dwFlags);
    HRESULT set_display_mode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP);
    HRESULT restore_display_mode();
    HRESULT get_display_mode(LPDDSURFACEDESC2 lpDDSurfaceDesc);
    HRESULT get_caps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHELCaps);
    HRESULT enum_display_modes(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback);
    HRESULT get_device_identifier(LPDDDEVICEIDENTIFIER2 lpdddi, DWORD dwFlags);

    HWND get_target_hwnd() const { return target_hwnd; }
    int get_current_width() const { return display_width; }
    int get_current_height() const { return display_height; }
    int get_current_bpp() const { return display_bpp; }
    const WrapperConfig& get_config() const { return config; }
    DDrawSurfaceImpl* get_primary_surface() const { return primary_surface; }
    void set_primary_surface(DDrawSurfaceImpl* s) { primary_surface = s; }

    DDraw7* get_interface7();
    DDraw4* get_interface4();
    DDraw2* get_interface2();
    DDraw1* get_interface1();

private:
    ULONG ref_count;
    HWND target_hwnd;
    DWORD cooperative_flags;
    int display_width;
    int display_height;
    int display_bpp;
    WrapperConfig config;
    DDrawSurfaceImpl* primary_surface;

    DDraw7* iface7;
    DDraw4* iface4;
    DDraw2* iface2;
    DDraw1* iface1;
};

class DDraw7 : public IDirectDraw7 {
public:
    DDraw7(DDrawImpl* impl) : impl(impl) {}
    ~DDraw7() {}
    DDrawImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(Compact)() override;
    STDMETHOD(CreateClipper)(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) override;
    STDMETHOD(CreatePalette)(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) override;
    STDMETHOD(CreateSurface)(LPDDSURFACEDESC2 lpDDSurfaceDesc, LPDIRECTDRAWSURFACE7* lplpDDSurface, IUnknown* pUnkOuter) override;
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE7 lpDDSurface, LPDIRECTDRAWSURFACE7* lplpDupDDSurface) override;
    STDMETHOD(EnumDisplayModes)(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback) override;
    STDMETHOD(EnumSurfaces)(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSD2, LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback) override;
    STDMETHOD(FlipToGDISurface)() override;
    STDMETHOD(GetCaps)(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHECaps) override;
    STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC2 lpDDSurfaceDesc) override;
    STDMETHOD(GetFourCCCodes)(LPDWORD lpNumCodes, LPDWORD lpCodes) override;
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE7* lplpGDIDDSSurface) override;
    STDMETHOD(GetMonitorFrequency)(LPDWORD lpdwFrequency) override;
    STDMETHOD(GetScanLine)(LPDWORD lpdwScanLine) override;
    STDMETHOD(GetVerticalBlankStatus)(LPBOOL lpbIsInVB) override;
    STDMETHOD(Initialize)(GUID* lpGUID) override;
    STDMETHOD(RestoreDisplayMode)() override;
    STDMETHOD(SetCooperativeLevel)(HWND hWnd, DWORD dwFlags) override;
    STDMETHOD(SetDisplayMode)(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags) override;
    STDMETHOD(WaitForVerticalBlank)(DWORD dwFlags, HANDLE hEvent) override;
    STDMETHOD(GetAvailableVidMem)(LPDDSCAPS2 lpDDSCaps, LPDWORD lpdwTotal, LPDWORD lpdwFree) override;
    STDMETHOD(GetSurfaceFromDC)(HDC hdc, LPDIRECTDRAWSURFACE7* lpDDS) override;
    STDMETHOD(RestoreAllSurfaces)() override;
    STDMETHOD(TestCooperativeLevel)() override;
    STDMETHOD(GetDeviceIdentifier)(LPDDDEVICEIDENTIFIER2 lpdddi, DWORD dwFlags) override;
    STDMETHOD(StartModeTest)(LPSIZE lpModesToTest, DWORD dwNumEntries, DWORD dwFlags) override;
    STDMETHOD(EvaluateMode)(DWORD dwFlags, DWORD* pSecondsUntilTimeout) override;

private:
    DDrawImpl* impl;
};

class DDraw4 : public IDirectDraw4 {
public:
    DDraw4(DDrawImpl* impl) : impl(impl) {}
    ~DDraw4() {}
    DDrawImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(Compact)() override;
    STDMETHOD(CreateClipper)(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) override;
    STDMETHOD(CreatePalette)(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) override;
    STDMETHOD(CreateSurface)(LPDDSURFACEDESC2 lpDDSurfaceDesc, LPDIRECTDRAWSURFACE4* lplpDDSurface, IUnknown* pUnkOuter) override;
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE4 lpDDSurface, LPDIRECTDRAWSURFACE4* lplpDupDDSurface) override;
    STDMETHOD(EnumDisplayModes)(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback) override;
    STDMETHOD(EnumSurfaces)(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSD2, LPVOID lpContext, LPDDENUMSURFACESCALLBACK2 lpEnumSurfacesCallback) override;
    STDMETHOD(FlipToGDISurface)() override;
    STDMETHOD(GetCaps)(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHECaps) override;
    STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC2 lpDDSurfaceDesc) override;
    STDMETHOD(GetFourCCCodes)(LPDWORD lpNumCodes, LPDWORD lpCodes) override;
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE4* lplpGDIDDSSurface) override;
    STDMETHOD(GetMonitorFrequency)(LPDWORD lpdwFrequency) override;
    STDMETHOD(GetScanLine)(LPDWORD lpdwScanLine) override;
    STDMETHOD(GetVerticalBlankStatus)(LPBOOL lpbIsInVB) override;
    STDMETHOD(Initialize)(GUID* lpGUID) override;
    STDMETHOD(RestoreDisplayMode)() override;
    STDMETHOD(SetCooperativeLevel)(HWND hWnd, DWORD dwFlags) override;
    STDMETHOD(SetDisplayMode)(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags) override;
    STDMETHOD(WaitForVerticalBlank)(DWORD dwFlags, HANDLE hEvent) override;
    STDMETHOD(GetAvailableVidMem)(LPDDSCAPS2 lpDDSCaps, LPDWORD lpdwTotal, LPDWORD lpdwFree) override;
    STDMETHOD(GetSurfaceFromDC)(HDC hdc, LPDIRECTDRAWSURFACE4* lpDDS) override;
    STDMETHOD(GetDeviceIdentifier)(LPDDDEVICEIDENTIFIER lpdddi, DWORD dwFlags) override;
    STDMETHOD(RestoreAllSurfaces)() override;
    STDMETHOD(TestCooperativeLevel)() override;

private:
    DDrawImpl* impl;
};

class DDraw2 : public IDirectDraw2 {
public:
    DDraw2(DDrawImpl* impl) : impl(impl) {}
    ~DDraw2() {}
    DDrawImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(Compact)() override;
    STDMETHOD(CreateClipper)(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) override;
    STDMETHOD(CreatePalette)(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) override;
    STDMETHOD(CreateSurface)(LPDDSURFACEDESC lpDDSurfaceDesc, LPDIRECTDRAWSURFACE* lplpDDSurface, IUnknown* pUnkOuter) override;
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE lpDDSurface, LPDIRECTDRAWSURFACE* lplpDupDDSurface) override;
    STDMETHOD(EnumDisplayModes)(DWORD dwFlags, LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK lpEnumModesCallback) override;
    STDMETHOD(EnumSurfaces)(DWORD dwFlags, LPDDSURFACEDESC lpDDSD, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) override;
    STDMETHOD(FlipToGDISurface)() override;
    STDMETHOD(GetCaps)(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHECaps) override;
    STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC lpDDSurfaceDesc) override;
    STDMETHOD(GetFourCCCodes)(LPDWORD lpNumCodes, LPDWORD lpCodes) override;
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE* lplpGDIDDSSurface) override;
    STDMETHOD(GetMonitorFrequency)(LPDWORD lpdwFrequency) override;
    STDMETHOD(GetScanLine)(LPDWORD lpdwScanLine) override;
    STDMETHOD(GetVerticalBlankStatus)(LPBOOL lpbIsInVB) override;
    STDMETHOD(Initialize)(GUID* lpGUID) override;
    STDMETHOD(RestoreDisplayMode)() override;
    STDMETHOD(SetCooperativeLevel)(HWND hWnd, DWORD dwFlags) override;
    STDMETHOD(SetDisplayMode)(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags) override;
    STDMETHOD(WaitForVerticalBlank)(DWORD dwFlags, HANDLE hEvent) override;
    STDMETHOD(GetAvailableVidMem)(LPDDSCAPS lpDDSCaps, LPDWORD lpdwTotal, LPDWORD lpdwFree) override;

private:
    DDrawImpl* impl;
};

class DDraw1 : public IDirectDraw {
public:
    DDraw1(DDrawImpl* impl) : impl(impl) {}
    ~DDraw1() {}
    DDrawImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(Compact)() override;
    STDMETHOD(CreateClipper)(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) override;
    STDMETHOD(CreatePalette)(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) override;
    STDMETHOD(CreateSurface)(LPDDSURFACEDESC lpDDSurfaceDesc, LPDIRECTDRAWSURFACE* lplpDDSurface, IUnknown* pUnkOuter) override;
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE lpDDSurface, LPDIRECTDRAWSURFACE* lplpDupDDSurface) override;
    STDMETHOD(EnumDisplayModes)(DWORD dwFlags, LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK lpEnumModesCallback) override;
    STDMETHOD(EnumSurfaces)(DWORD dwFlags, LPDDSURFACEDESC lpDDSD, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) override;
    STDMETHOD(FlipToGDISurface)() override;
    STDMETHOD(GetCaps)(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHECaps) override;
    STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC lpDDSurfaceDesc) override;
    STDMETHOD(GetFourCCCodes)(LPDWORD lpNumCodes, LPDWORD lpCodes) override;
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE* lplpGDIDDSSurface) override;
    STDMETHOD(GetMonitorFrequency)(LPDWORD lpdwFrequency) override;
    STDMETHOD(GetScanLine)(LPDWORD lpdwScanLine) override;
    STDMETHOD(GetVerticalBlankStatus)(LPBOOL lpbIsInVB) override;
    STDMETHOD(Initialize)(GUID* lpGUID) override;
    STDMETHOD(RestoreDisplayMode)() override;
    STDMETHOD(SetCooperativeLevel)(HWND hWnd, DWORD dwFlags) override;
    STDMETHOD(SetDisplayMode)(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP) override;
    STDMETHOD(WaitForVerticalBlank)(DWORD dwFlags, HANDLE hEvent) override;

private:
    DDrawImpl* impl;
};
