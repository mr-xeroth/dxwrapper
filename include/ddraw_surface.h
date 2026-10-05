#pragma once

#include <windows.h>
#include <ddraw.h>
#include <vector>
#include "ddraw_palette.h"
#include "ddraw_clipper.h"
#include "ddraw_gamma.h"
#include "renderer.h"

class DDrawImpl;
class DDrawSurface1;
class DDrawSurface2;
class DDrawSurface3;
class DDrawSurface4;
class DDrawSurface7;

class DDrawSurfaceImpl {
public:
    DDrawSurfaceImpl(DDrawImpl* parent_dd, const DDSURFACEDESC2* desc);
    ~DDrawSurfaceImpl();

    void add_ref() { ++ref_count; }
    ULONG release();

    HRESULT query_interface(REFIID riid, LPVOID* ppvObj);

    HRESULT lock(LPRECT lpDestRect, DDSURFACEDESC2* lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent);
    HRESULT unlock();
    HRESULT blt(LPRECT lpDestRect, DDrawSurfaceImpl* src, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx);
    HRESULT blt_fast(DWORD dwX, DWORD dwY, DDrawSurfaceImpl* src, LPRECT lpSrcRect, DWORD dwTrans);
    HRESULT flip(DDrawSurfaceImpl* override_target, DWORD dwFlags);
    HRESULT get_attached_surface(DWORD caps, DDrawSurfaceImpl** out_surf);
    HRESULT get_dc(HDC* lphDC);
    HRESULT release_dc(HDC hDC);
    HRESULT set_palette(DDrawPalette* pal);
    HRESULT get_palette(DDrawPalette** pal);
    HRESULT set_clipper(DDrawClipper* clip);
    HRESULT get_clipper(DDrawClipper** clip);
    HRESULT set_color_key(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey);
    HRESULT get_color_key(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey);
    HRESULT get_surface_desc(DDSURFACEDESC2* lpDesc);
    HRESULT get_surface_desc_v1(DDSURFACEDESC* lpDesc);

    bool is_primary() const;
    void present_surface();
    SurfaceFormat get_format() const;
    DDrawImpl* get_parent() const { return parent; }
    void detach_parent() { parent = nullptr; }
    DDrawPalette* get_palette_ptr() const { return palette; }

    DDrawSurface7* get_interface7();
    DDrawSurface4* get_interface4();
    DDrawSurface3* get_interface3();
    DDrawSurface2* get_interface2();
    DDrawSurface1* get_interface1();

private:
    ULONG ref_count;
    DDrawImpl* parent;
    DDSURFACEDESC2 desc;
    std::vector<uint8_t> surface_buffer;
    DDrawPalette* palette;
    DDrawClipper* clipper;
    DDrawGammaControl* gamma_control;
    DDrawSurfaceImpl* attached_backbuffer;

    DDCOLORKEY src_colorkey;
    DDCOLORKEY dest_colorkey;
    bool has_src_colorkey;
    bool has_dest_colorkey;

    HDC mem_dc;
    HBITMAP mem_bitmap;
    HBITMAP old_bitmap;
    void* dc_bits;

    DDrawSurface7* iface7;
    DDrawSurface4* iface4;
    DDrawSurface3* iface3;
    DDrawSurface2* iface2;
    DDrawSurface1* iface1;

    void update_pitch();
};

class DDrawSurface7 : public IDirectDrawSurface7 {
public:
    DDrawSurface7(DDrawSurfaceImpl* impl) : impl(impl) {}
    ~DDrawSurface7() {}
    DDrawSurfaceImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE7 lpDDSurface) override;
    STDMETHOD(AddOverlayDirtyRect)(LPRECT lpRect) override;
    STDMETHOD(Blt)(LPRECT lpDestRect, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) override;
    STDMETHOD(BltBatch)(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) override;
    STDMETHOD(BltFast)(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) override;
    STDMETHOD(DeleteAttachedSurface)(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface) override;
    STDMETHOD(EnumAttachedSurfaces)(LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback) override;
    STDMETHOD(EnumOverlayZOrders)(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpfnCallback) override;
    STDMETHOD(Flip)(LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride, DWORD dwFlags) override;
    STDMETHOD(GetAttachedSurface)(LPDDSCAPS2 lpDDSCaps, LPDIRECTDRAWSURFACE7* lplpDDAttachedSurface) override;
    STDMETHOD(GetBltStatus)(DWORD dwFlags) override;
    STDMETHOD(GetCaps)(LPDDSCAPS2 lpDDSCaps) override;
    STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER* lplpDDClipper) override;
    STDMETHOD(GetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(GetDC)(HDC* lphDC) override;
    STDMETHOD(GetFlipStatus)(DWORD dwFlags) override;
    STDMETHOD(GetOverlayPosition)(LPLONG lplX, LPLONG lplY) override;
    STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE* lplpDDPalette) override;
    STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT lpDDPixelFormat) override;
    STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC2 lpDDSurfaceDesc) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW lpDD, LPDDSURFACEDESC2 lpDDSurfaceDesc) override;
    STDMETHOD(IsLost)() override;
    STDMETHOD(Lock)(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) override;
    STDMETHOD(ReleaseDC)(HDC hDC) override;
    STDMETHOD(Restore)() override;
    STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER lpDDClipper) override;
    STDMETHOD(SetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(SetOverlayPosition)(LONG lX, LONG lY) override;
    STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE lpDDPalette) override;
    STDMETHOD(Unlock)(LPRECT lpRect) override;
    STDMETHOD(UpdateOverlay)(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE7 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) override;
    STDMETHOD(UpdateOverlayDisplay)(DWORD dwFlags) override;
    STDMETHOD(UpdateOverlayZOrder)(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSReference) override;
    STDMETHOD(GetDDInterface)(LPVOID* lplpDD) override;
    STDMETHOD(PageLock)(DWORD dwFlags) override;
    STDMETHOD(PageUnlock)(DWORD dwFlags) override;
    STDMETHOD(SetSurfaceDesc)(LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags) override;
    STDMETHOD(SetPrivateData)(REFGUID guidTag, LPVOID lpData, DWORD cbSize, DWORD dwFlags) override;
    STDMETHOD(GetPrivateData)(REFGUID guidTag, LPVOID lpBuffer, LPDWORD lpcbBufferSize) override;
    STDMETHOD(FreePrivateData)(REFGUID guidTag) override;
    STDMETHOD(GetUniquenessValue)(LPDWORD lpValue) override;
    STDMETHOD(ChangeUniquenessValue)() override;
    STDMETHOD(SetPriority)(DWORD dwPriority) override;
    STDMETHOD(GetPriority)(LPDWORD lpdwPriority) override;
    STDMETHOD(SetLOD)(DWORD dwMaxLOD) override;
    STDMETHOD(GetLOD)(LPDWORD lpdwMaxLOD) override;

private:
    DDrawSurfaceImpl* impl;
};

class DDrawSurface4 : public IDirectDrawSurface4 {
public:
    DDrawSurface4(DDrawSurfaceImpl* impl) : impl(impl) {}
    ~DDrawSurface4() {}
    DDrawSurfaceImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE4 lpDDSurface) override;
    STDMETHOD(AddOverlayDirtyRect)(LPRECT lpRect) override;
    STDMETHOD(Blt)(LPRECT lpDestRect, LPDIRECTDRAWSURFACE4 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) override;
    STDMETHOD(BltBatch)(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) override;
    STDMETHOD(BltFast)(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE4 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) override;
    STDMETHOD(DeleteAttachedSurface)(DWORD dwFlags, LPDIRECTDRAWSURFACE4 lpDDSAttachedSurface) override;
    STDMETHOD(EnumAttachedSurfaces)(LPVOID lpContext, LPDDENUMSURFACESCALLBACK2 lpEnumSurfacesCallback) override;
    STDMETHOD(EnumOverlayZOrders)(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK2 lpfnCallback) override;
    STDMETHOD(Flip)(LPDIRECTDRAWSURFACE4 lpDDSurfaceTargetOverride, DWORD dwFlags) override;
    STDMETHOD(GetAttachedSurface)(LPDDSCAPS2 lpDDSCaps, LPDIRECTDRAWSURFACE4* lplpDDAttachedSurface) override;
    STDMETHOD(GetBltStatus)(DWORD dwFlags) override;
    STDMETHOD(GetCaps)(LPDDSCAPS2 lpDDSCaps) override;
    STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER* lplpDDClipper) override;
    STDMETHOD(GetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(GetDC)(HDC* lphDC) override;
    STDMETHOD(GetFlipStatus)(DWORD dwFlags) override;
    STDMETHOD(GetOverlayPosition)(LPLONG lplX, LPLONG lplY) override;
    STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE* lplpDDPalette) override;
    STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT lpDDPixelFormat) override;
    STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC2 lpDDSurfaceDesc) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW lpDD, LPDDSURFACEDESC2 lpDDSurfaceDesc) override;
    STDMETHOD(IsLost)() override;
    STDMETHOD(Lock)(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) override;
    STDMETHOD(ReleaseDC)(HDC hDC) override;
    STDMETHOD(Restore)() override;
    STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER lpDDClipper) override;
    STDMETHOD(SetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(SetOverlayPosition)(LONG lX, LONG lY) override;
    STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE lpDDPalette) override;
    STDMETHOD(Unlock)(LPRECT lpRect) override;
    STDMETHOD(UpdateOverlay)(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE4 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) override;
    STDMETHOD(UpdateOverlayDisplay)(DWORD dwFlags) override;
    STDMETHOD(UpdateOverlayZOrder)(DWORD dwFlags, LPDIRECTDRAWSURFACE4 lpDDSReference) override;
    STDMETHOD(GetDDInterface)(LPVOID* lplpDD) override;
    STDMETHOD(PageLock)(DWORD dwFlags) override;
    STDMETHOD(PageUnlock)(DWORD dwFlags) override;
    STDMETHOD(SetSurfaceDesc)(LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags) override;
    STDMETHOD(SetPrivateData)(REFGUID guidTag, LPVOID lpData, DWORD cbSize, DWORD dwFlags) override;
    STDMETHOD(GetPrivateData)(REFGUID guidTag, LPVOID lpBuffer, LPDWORD lpcbBufferSize) override;
    STDMETHOD(FreePrivateData)(REFGUID guidTag) override;
    STDMETHOD(GetUniquenessValue)(LPDWORD lpValue) override;
    STDMETHOD(ChangeUniquenessValue)() override;

private:
    DDrawSurfaceImpl* impl;
};

class DDrawSurface3 : public IDirectDrawSurface3 {
public:
    DDrawSurface3(DDrawSurfaceImpl* impl) : impl(impl) {}
    ~DDrawSurface3() {}
    DDrawSurfaceImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE3 lpDDSurface) override;
    STDMETHOD(AddOverlayDirtyRect)(LPRECT lpRect) override;
    STDMETHOD(Blt)(LPRECT lpDestRect, LPDIRECTDRAWSURFACE3 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) override;
    STDMETHOD(BltBatch)(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) override;
    STDMETHOD(BltFast)(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE3 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) override;
    STDMETHOD(DeleteAttachedSurface)(DWORD dwFlags, LPDIRECTDRAWSURFACE3 lpDDSAttachedSurface) override;
    STDMETHOD(EnumAttachedSurfaces)(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) override;
    STDMETHOD(EnumOverlayZOrders)(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback) override;
    STDMETHOD(Flip)(LPDIRECTDRAWSURFACE3 lpDDSurfaceTargetOverride, DWORD dwFlags) override;
    STDMETHOD(GetAttachedSurface)(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE3* lplpDDAttachedSurface) override;
    STDMETHOD(GetBltStatus)(DWORD dwFlags) override;
    STDMETHOD(GetCaps)(LPDDSCAPS lpDDSCaps) override;
    STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER* lplpDDClipper) override;
    STDMETHOD(GetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(GetDC)(HDC* lphDC) override;
    STDMETHOD(GetFlipStatus)(DWORD dwFlags) override;
    STDMETHOD(GetOverlayPosition)(LPLONG lplX, LPLONG lplY) override;
    STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE* lplpDDPalette) override;
    STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT lpDDPixelFormat) override;
    STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC lpDDSurfaceDesc) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc) override;
    STDMETHOD(IsLost)() override;
    STDMETHOD(Lock)(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) override;
    STDMETHOD(ReleaseDC)(HDC hDC) override;
    STDMETHOD(Restore)() override;
    STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER lpDDClipper) override;
    STDMETHOD(SetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(SetOverlayPosition)(LONG lX, LONG lY) override;
    STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE lpDDPalette) override;
    STDMETHOD(Unlock)(LPVOID lpSurfaceData) override;
    STDMETHOD(UpdateOverlay)(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE3 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) override;
    STDMETHOD(UpdateOverlayDisplay)(DWORD dwFlags) override;
    STDMETHOD(UpdateOverlayZOrder)(DWORD dwFlags, LPDIRECTDRAWSURFACE3 lpDDSReference) override;
    STDMETHOD(GetDDInterface)(LPVOID* lplpDD) override;
    STDMETHOD(PageLock)(DWORD dwFlags) override;
    STDMETHOD(PageUnlock)(DWORD dwFlags) override;
    STDMETHOD(SetSurfaceDesc)(LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags) override;

private:
    DDrawSurfaceImpl* impl;
};

class DDrawSurface2 : public IDirectDrawSurface2 {
public:
    DDrawSurface2(DDrawSurfaceImpl* impl) : impl(impl) {}
    ~DDrawSurface2() {}
    DDrawSurfaceImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE2 lpDDSurface) override;
    STDMETHOD(AddOverlayDirtyRect)(LPRECT lpRect) override;
    STDMETHOD(Blt)(LPRECT lpDestRect, LPDIRECTDRAWSURFACE2 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) override;
    STDMETHOD(BltBatch)(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) override;
    STDMETHOD(BltFast)(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE2 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) override;
    STDMETHOD(DeleteAttachedSurface)(DWORD dwFlags, LPDIRECTDRAWSURFACE2 lpDDSAttachedSurface) override;
    STDMETHOD(EnumAttachedSurfaces)(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) override;
    STDMETHOD(EnumOverlayZOrders)(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback) override;
    STDMETHOD(Flip)(LPDIRECTDRAWSURFACE2 lpDDSurfaceTargetOverride, DWORD dwFlags) override;
    STDMETHOD(GetAttachedSurface)(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE2* lplpDDAttachedSurface) override;
    STDMETHOD(GetBltStatus)(DWORD dwFlags) override;
    STDMETHOD(GetCaps)(LPDDSCAPS lpDDSCaps) override;
    STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER* lplpDDClipper) override;
    STDMETHOD(GetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(GetDC)(HDC* lphDC) override;
    STDMETHOD(GetFlipStatus)(DWORD dwFlags) override;
    STDMETHOD(GetOverlayPosition)(LPLONG lplX, LPLONG lplY) override;
    STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE* lplpDDPalette) override;
    STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT lpDDPixelFormat) override;
    STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC lpDDSurfaceDesc) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc) override;
    STDMETHOD(IsLost)() override;
    STDMETHOD(Lock)(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) override;
    STDMETHOD(ReleaseDC)(HDC hDC) override;
    STDMETHOD(Restore)() override;
    STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER lpDDClipper) override;
    STDMETHOD(SetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(SetOverlayPosition)(LONG lX, LONG lY) override;
    STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE lpDDPalette) override;
    STDMETHOD(Unlock)(LPVOID lpSurfaceData) override;
    STDMETHOD(UpdateOverlay)(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE2 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) override;
    STDMETHOD(UpdateOverlayDisplay)(DWORD dwFlags) override;
    STDMETHOD(UpdateOverlayZOrder)(DWORD dwFlags, LPDIRECTDRAWSURFACE2 lpDDSReference) override;
    STDMETHOD(GetDDInterface)(LPVOID* lplpDD) override;
    STDMETHOD(PageLock)(DWORD dwFlags) override;
    STDMETHOD(PageUnlock)(DWORD dwFlags) override;

private:
    DDrawSurfaceImpl* impl;
};

class DDrawSurface1 : public IDirectDrawSurface {
public:
    DDrawSurface1(DDrawSurfaceImpl* impl) : impl(impl) {}
    ~DDrawSurface1() {}
    DDrawSurfaceImpl* get_impl() const { return impl; }

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE lpDDSurface) override;
    STDMETHOD(AddOverlayDirtyRect)(LPRECT lpRect) override;
    STDMETHOD(Blt)(LPRECT lpDestRect, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) override;
    STDMETHOD(BltBatch)(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) override;
    STDMETHOD(BltFast)(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) override;
    STDMETHOD(DeleteAttachedSurface)(DWORD dwFlags, LPDIRECTDRAWSURFACE lpDDSAttachedSurface) override;
    STDMETHOD(EnumAttachedSurfaces)(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) override;
    STDMETHOD(EnumOverlayZOrders)(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback) override;
    STDMETHOD(Flip)(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags) override;
    STDMETHOD(GetAttachedSurface)(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE* lplpDDAttachedSurface) override;
    STDMETHOD(GetBltStatus)(DWORD dwFlags) override;
    STDMETHOD(GetCaps)(LPDDSCAPS lpDDSCaps) override;
    STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER* lplpDDClipper) override;
    STDMETHOD(GetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(GetDC)(HDC* lphDC) override;
    STDMETHOD(GetFlipStatus)(DWORD dwFlags) override;
    STDMETHOD(GetOverlayPosition)(LPLONG lplX, LPLONG lplY) override;
    STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE* lplpDDPalette) override;
    STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT lpDDPixelFormat) override;
    STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC lpDDSurfaceDesc) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc) override;
    STDMETHOD(IsLost)() override;
    STDMETHOD(Lock)(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) override;
    STDMETHOD(ReleaseDC)(HDC hDC) override;
    STDMETHOD(Restore)() override;
    STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER lpDDClipper) override;
    STDMETHOD(SetColorKey)(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;
    STDMETHOD(SetOverlayPosition)(LONG lX, LONG lY) override;
    STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE lpDDPalette) override;
    STDMETHOD(Unlock)(LPVOID lpSurfaceData) override;
    STDMETHOD(UpdateOverlay)(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) override;
    STDMETHOD(UpdateOverlayDisplay)(DWORD dwFlags) override;
    STDMETHOD(UpdateOverlayZOrder)(DWORD dwFlags, LPDIRECTDRAWSURFACE lpDDSReference) override;

private:
    DDrawSurfaceImpl* impl;
};
