#include "../include/ddraw_surface.h"
#include "../include/ddraw_interface.h"
#include <cstring>
#include <algorithm>

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

// constructs surface implementation, allocates memory buffer, and initializes backbuffer if complex flipping chain
DDrawSurfaceImpl::DDrawSurfaceImpl(DDrawImpl* parent_dd, const DDSURFACEDESC2* lpDesc)
    : ref_count(1), parent(parent_dd), palette(nullptr), clipper(nullptr),
      gamma_control(new DDrawGammaControl()), attached_backbuffer(nullptr),
      has_src_colorkey(false), has_dest_colorkey(false),
      mem_dc(nullptr), mem_bitmap(nullptr), old_bitmap(nullptr), dc_bits(nullptr),
      iface7(nullptr), iface4(nullptr), iface3(nullptr), iface2(nullptr), iface1(nullptr) {
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (lpDesc) {
        std::memcpy(&desc, lpDesc, std::min((size_t)lpDesc->dwSize, sizeof(desc)));
    }

    if (!(desc.dwFlags & DDSD_WIDTH) || desc.dwWidth == 0) {
        desc.dwWidth = parent ? parent->get_current_width() : 640;
        desc.dwFlags |= DDSD_WIDTH;
    }
    if (!(desc.dwFlags & DDSD_HEIGHT) || desc.dwHeight == 0) {
        desc.dwHeight = parent ? parent->get_current_height() : 480;
        desc.dwFlags |= DDSD_HEIGHT;
    }
    if (!(desc.dwFlags & DDSD_PIXELFORMAT)) {
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
        int bpp = parent ? parent->get_current_bpp() : 32;
        desc.ddpfPixelFormat.dwRGBBitCount = bpp;
        if (bpp == 8) {
            desc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8;
        } else if (bpp == 16) {
            desc.ddpfPixelFormat.dwRBitMask = 0xF800;
            desc.ddpfPixelFormat.dwGBitMask = 0x07E0;
            desc.ddpfPixelFormat.dwBBitMask = 0x001F;
        } else if (bpp == 24 || bpp == 32) {
            desc.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
            desc.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
            desc.ddpfPixelFormat.dwBBitMask = 0x000000FF;
        }
        desc.dwFlags |= DDSD_PIXELFORMAT;
    }

    update_pitch();
    surface_buffer.resize(desc.lPitch * desc.dwHeight, 0);

    if ((desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) && 
        ((desc.ddsCaps.dwCaps & DDSCAPS_FLIP) || (desc.ddsCaps.dwCaps & DDSCAPS_COMPLEX) || ((desc.dwFlags & DDSD_BACKBUFFERCOUNT) && desc.dwBackBufferCount > 0))) {
        DDSURFACEDESC2 back_desc = desc;
        back_desc.ddsCaps.dwCaps &= ~DDSCAPS_PRIMARYSURFACE;
        back_desc.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
        attached_backbuffer = new DDrawSurfaceImpl(parent, &back_desc);
    }
}

// frees attached resources, gdi memory dcs, and com interface wrappers
DDrawSurfaceImpl::~DDrawSurfaceImpl() {
    if (parent && parent->get_primary_surface() == this) {
        parent->set_primary_surface(nullptr);
    }
    if (attached_backbuffer) {
        attached_backbuffer->release();
        attached_backbuffer = nullptr;
    }
    if (palette) {
        palette->set_attached_surface(nullptr);
        palette->Release();
        palette = nullptr;
    }
    if (clipper) {
        clipper->Release();
        clipper = nullptr;
    }
    if (gamma_control) {
        gamma_control->Release();
        gamma_control = nullptr;
    }
    if (mem_dc) {
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(mem_bitmap);
        DeleteDC(mem_dc);
    }
    delete iface7;
    delete iface4;
    delete iface3;
    delete iface2;
    delete iface1;
}

// decrements surface reference count and deletes non-primary surface if zero
ULONG DDrawSurfaceImpl::release() {
    if (ref_count > 0) {
        --ref_count;
    }
    if (ref_count == 0 && !is_primary()) {
        delete this;
    }
    return ref_count;
}

// computes 4-byte aligned pitch for surface row strides
void DDrawSurfaceImpl::update_pitch() {
    int bpp = desc.ddpfPixelFormat.dwRGBBitCount;
    if (bpp <= 0) bpp = 32;
    int bytes_per_pixel = (bpp + 7) / 8;
    desc.lPitch = ((desc.dwWidth * bytes_per_pixel + 3) / 4) * 4;
    desc.dwFlags |= DDSD_PITCH;
}

// checks if surface is marked as the primary display surface
bool DDrawSurfaceImpl::is_primary() const {
    return (desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
}

// determines internal pixel format enum based on format flags and bit depth
SurfaceFormat DDrawSurfaceImpl::get_format() const {
    if (desc.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8 || desc.ddpfPixelFormat.dwRGBBitCount == 8) {
        return SurfaceFormat::PALETTE8;
    }
    if (desc.ddpfPixelFormat.dwRGBBitCount == 16) {
        if (desc.ddpfPixelFormat.dwGBitMask == 0x07E0) return SurfaceFormat::RGB565;
        return SurfaceFormat::RGB555;
    }
    if (desc.ddpfPixelFormat.dwRGBBitCount == 24) {
        return SurfaceFormat::RGB24;
    }
    return SurfaceFormat::RGB32;
}

// dispatches surface pixel buffer and palette to opengl renderer for presentation
void DDrawSurfaceImpl::present_surface() {
    Renderer& r = Renderer::instance();
    if (!r.is_initialized()) {
        r.init(parent ? parent->get_target_hwnd() : nullptr, desc.dwWidth, desc.dwHeight, parent ? parent->get_config() : ConfigManager::load());
    }
    const uint32_t* pal = palette ? palette->get_rgba_entries() : nullptr;
    if (!pal && parent && parent->get_primary_surface() && parent->get_primary_surface()->get_palette_ptr()) {
        pal = parent->get_primary_surface()->get_palette_ptr()->get_rgba_entries();
    }
    r.present(surface_buffer.data(), (int)desc.lPitch, get_format(), pal);
}

// lazily creates and returns idirectdrawsurface7 com wrapper
DDrawSurface7* DDrawSurfaceImpl::get_interface7() {
    if (!iface7) iface7 = new DDrawSurface7(this);
    return iface7;
}

// lazily creates and returns idirectdrawsurface4 com wrapper
DDrawSurface4* DDrawSurfaceImpl::get_interface4() {
    if (!iface4) iface4 = new DDrawSurface4(this);
    return iface4;
}

// lazily creates and returns idirectdrawsurface3 com wrapper
DDrawSurface3* DDrawSurfaceImpl::get_interface3() {
    if (!iface3) iface3 = new DDrawSurface3(this);
    return iface3;
}

// lazily creates and returns idirectdrawsurface2 com wrapper
DDrawSurface2* DDrawSurfaceImpl::get_interface2() {
    if (!iface2) iface2 = new DDrawSurface2(this);
    return iface2;
}

// lazily creates and returns idirectdrawsurface com wrapper
DDrawSurface1* DDrawSurfaceImpl::get_interface1() {
    if (!iface1) iface1 = new DDrawSurface1(this);
    return iface1;
}

// queries surface com interface by iid
HRESULT DDrawSurfaceImpl::query_interface(REFIID riid, LPVOID* ppvObj) {
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirectDrawSurface7) {
        *ppvObj = get_interface7();
        add_ref();
        return S_OK;
    }
    if (riid == IID_IDirectDrawSurface4) {
        *ppvObj = get_interface4();
        add_ref();
        return S_OK;
    }
    if (riid == IID_IDirectDrawSurface3) {
        *ppvObj = get_interface3();
        add_ref();
        return S_OK;
    }
    if (riid == IID_IDirectDrawSurface2) {
        *ppvObj = get_interface2();
        add_ref();
        return S_OK;
    }
    if (riid == IID_IDirectDrawSurface) {
        *ppvObj = get_interface1();
        add_ref();
        return S_OK;
    }
    if (riid == IID_IDirectDrawGammaControl) {
        *ppvObj = (IDirectDrawGammaControl*)gamma_control;
        gamma_control->AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

// locks surface memory buffer and returns direct pointer to pixel buffer
HRESULT DDrawSurfaceImpl::lock(LPRECT lpDestRect, DDSURFACEDESC2* lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DWORD sz = lpDDSurfaceDesc->dwSize ? lpDDSurfaceDesc->dwSize : sizeof(DDSURFACEDESC2);
    std::memcpy(lpDDSurfaceDesc, &desc, std::min((size_t)sz, sizeof(desc)));
    lpDDSurfaceDesc->dwSize = sz;
    lpDDSurfaceDesc->lpSurface = surface_buffer.data();
    lpDDSurfaceDesc->lPitch = desc.lPitch;
    lpDDSurfaceDesc->dwFlags |= DDSD_LPSURFACE | DDSD_PITCH;

    if (lpDestRect) {
        int bytes = (desc.ddpfPixelFormat.dwRGBBitCount + 7) / 8;
        uint8_t* ptr = (uint8_t*)surface_buffer.data() + (lpDestRect->top * desc.lPitch) + (lpDestRect->left * bytes);
        lpDDSurfaceDesc->lpSurface = ptr;
    }
    return DD_OK;
}

// unlocks surface and triggers opengl presentation if modifying primary surface
HRESULT DDrawSurfaceImpl::unlock() {
    if (is_primary()) {
        present_surface();
    }
    return DD_OK;
}

// performs 2d blt operations including color fills, transparency keys, and rect clipping
HRESULT DDrawSurfaceImpl::blt(LPRECT lpDestRect, DDrawSurfaceImpl* src, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {
    RECT dst_rc = { 0, 0, (LONG)desc.dwWidth, (LONG)desc.dwHeight };
    if (lpDestRect) dst_rc = *lpDestRect;

    if (dwFlags & DDBLT_COLORFILL) {
        DWORD fill_color = lpDDBltFx ? lpDDBltFx->dwFillColor : 0;
        int bpp = desc.ddpfPixelFormat.dwRGBBitCount;
        int bytes = (bpp + 7) / 8;

        LONG start_y = std::max((LONG)0, dst_rc.top);
        LONG end_y = std::min((LONG)desc.dwHeight, dst_rc.bottom);
        LONG start_x = std::max((LONG)0, dst_rc.left);
        LONG end_x = std::min((LONG)desc.dwWidth, dst_rc.right);

        for (LONG y = start_y; y < end_y; ++y) {
            uint8_t* row = surface_buffer.data() + (y * desc.lPitch);
            for (LONG x = start_x; x < end_x; ++x) {
                if (bytes == 1) {
                    row[x] = (uint8_t)fill_color;
                } else if (bytes == 2) {
                    ((uint16_t*)row)[x] = (uint16_t)fill_color;
                } else if (bytes == 4) {
                    ((uint32_t*)row)[x] = (uint32_t)fill_color;
                }
            }
        }
        if (is_primary()) {
            present_surface();
        }
        return DD_OK;
    }

    if (src) {
        RECT src_rc = { 0, 0, (LONG)src->desc.dwWidth, (LONG)src->desc.dwHeight };
        if (lpSrcRect) src_rc = *lpSrcRect;

        LONG copy_w = std::min(dst_rc.right - dst_rc.left, src_rc.right - src_rc.left);
        LONG copy_h = std::min(dst_rc.bottom - dst_rc.top, src_rc.bottom - src_rc.top);
        int bytes = (desc.ddpfPixelFormat.dwRGBBitCount + 7) / 8;

        bool use_key = (dwFlags & DDBLT_KEYSRC) && src->has_src_colorkey;
        DWORD key_val = src->src_colorkey.dwColorSpaceLowValue;

        for (LONG y = 0; y < copy_h; ++y) {
            LONG sy = src_rc.top + y;
            LONG dy = dst_rc.top + y;
            if (sy < 0 || sy >= (LONG)src->desc.dwHeight || dy < 0 || dy >= (LONG)desc.dwHeight) continue;

            const uint8_t* src_row = src->surface_buffer.data() + (sy * src->desc.lPitch);
            uint8_t* dst_row = surface_buffer.data() + (dy * desc.lPitch);

            for (LONG x = 0; x < copy_w; ++x) {
                LONG sx = src_rc.left + x;
                LONG dx = dst_rc.left + x;
                if (sx < 0 || sx >= (LONG)src->desc.dwWidth || dx < 0 || dx >= (LONG)desc.dwWidth) continue;

                if (!use_key) {
                    if (bytes == 1) {
                        dst_row[dx] = src_row[sx];
                    } else if (bytes == 2) {
                        ((uint16_t*)dst_row)[dx] = ((const uint16_t*)src_row)[sx];
                    } else if (bytes == 4) {
                        ((uint32_t*)dst_row)[dx] = ((const uint32_t*)src_row)[sx];
                    }
                } else {
                    if (bytes == 1) {
                        uint8_t val = src_row[sx];
                        if (val != (uint8_t)key_val) dst_row[dx] = val;
                    } else if (bytes == 2) {
                        uint16_t val = ((const uint16_t*)src_row)[sx];
                        if (val != (uint16_t)key_val) ((uint16_t*)dst_row)[dx] = val;
                    } else if (bytes == 4) {
                        uint32_t val = ((const uint32_t*)src_row)[sx];
                        if (val != (uint32_t)key_val) ((uint32_t*)dst_row)[dx] = val;
                    }
                }
            }
        }
    }
    if (is_primary()) {
        present_surface();
    }
    return DD_OK;
}

// fast blitter thunk that delegates to general blt implementation
HRESULT DDrawSurfaceImpl::blt_fast(DWORD dwX, DWORD dwY, DDrawSurfaceImpl* src, LPRECT lpSrcRect, DWORD dwTrans) {
    if (!src) return DDERR_INVALIDPARAMS;
    RECT src_rc = { 0, 0, (LONG)src->desc.dwWidth, (LONG)src->desc.dwHeight };
    if (lpSrcRect) src_rc = *lpSrcRect;

    RECT dst_rc = { (LONG)dwX, (LONG)dwY, (LONG)dwX + (src_rc.right - src_rc.left), (LONG)dwY + (src_rc.bottom - src_rc.top) };
    DWORD flags = (dwTrans & DDBLTFAST_SRCCOLORKEY) ? DDBLT_KEYSRC : 0;
    return blt(&dst_rc, src, &src_rc, flags, nullptr);
}

// swaps primary and attached backbuffer pixel memory and presents frame
HRESULT DDrawSurfaceImpl::flip(DDrawSurfaceImpl* override_target, DWORD dwFlags) {
    if (override_target) {
        std::swap(surface_buffer, override_target->surface_buffer);
    } else if (attached_backbuffer) {
        std::swap(surface_buffer, attached_backbuffer->surface_buffer);
    }
    present_surface();
    return DD_OK;
}

// retrieves attached backbuffer or creates one if flipping primary surface
HRESULT DDrawSurfaceImpl::get_attached_surface(DWORD caps, DDrawSurfaceImpl** out_surf) {
    if (!out_surf) return DDERR_INVALIDPARAMS;
    if (attached_backbuffer) {
        *out_surf = attached_backbuffer;
        attached_backbuffer->add_ref();
        return DD_OK;
    }
    if (is_primary()) {
        DDSURFACEDESC2 back_desc = desc;
        back_desc.ddsCaps.dwCaps &= ~DDSCAPS_PRIMARYSURFACE;
        back_desc.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
        attached_backbuffer = new DDrawSurfaceImpl(parent, &back_desc);
        *out_surf = attached_backbuffer;
        attached_backbuffer->add_ref();
        return DD_OK;
    }
    *out_surf = nullptr;
    return DDERR_NOTFOUND;
}

// creates compatible gdi memory dc and syncs surface pixel buffer to dib section
HRESULT DDrawSurfaceImpl::get_dc(HDC* lphDC) {
    if (!lphDC) return DDERR_INVALIDPARAMS;
    if (!mem_dc) {
        HDC screen_dc = ::GetDC(nullptr);
        mem_dc = CreateCompatibleDC(screen_dc);

        struct {
            BITMAPINFOHEADER bmiHeader;
            RGBQUAD bmiColors[256];
        } bmi;
        std::memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = desc.dwWidth;
        bmi.bmiHeader.biHeight = -(LONG)desc.dwHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = desc.ddpfPixelFormat.dwRGBBitCount ? desc.ddpfPixelFormat.dwRGBBitCount : 8;
        bmi.bmiHeader.biCompression = BI_RGB;

        if (bmi.bmiHeader.biBitCount == 8 && palette) {
            PALETTEENTRY pe[256];
            palette->GetEntries(0, 0, 256, pe);
            for (int i = 0; i < 256; ++i) {
                bmi.bmiColors[i].rgbRed = pe[i].peRed;
                bmi.bmiColors[i].rgbGreen = pe[i].peGreen;
                bmi.bmiColors[i].rgbBlue = pe[i].peBlue;
                bmi.bmiColors[i].rgbReserved = 0;
            }
        }

        mem_bitmap = CreateDIBSection(mem_dc, (const BITMAPINFO*)&bmi, DIB_RGB_COLORS, &dc_bits, nullptr, 0);
        old_bitmap = (HBITMAP)SelectObject(mem_dc, mem_bitmap);
        ReleaseDC(nullptr, screen_dc);
    }
    if (dc_bits && !surface_buffer.empty()) {
        std::memcpy(dc_bits, surface_buffer.data(), surface_buffer.size());
    }
    *lphDC = mem_dc;
    return DD_OK;
}

// syncs dib section pixels back to surface buffer and presents if primary surface
HRESULT DDrawSurfaceImpl::release_dc(HDC hDC) {
    if (dc_bits && !surface_buffer.empty()) {
        std::memcpy(surface_buffer.data(), dc_bits, surface_buffer.size());
        if (is_primary() && !attached_backbuffer) present_surface();
    }
    return DD_OK;
}

// associates palette with surface and increments reference count
HRESULT DDrawSurfaceImpl::set_palette(DDrawPalette* pal) {
    if (palette) {
        palette->set_attached_surface(nullptr);
        palette->Release();
    }
    palette = pal;
    if (palette) {
        palette->AddRef();
        palette->set_attached_surface(this);
    }
    return DD_OK;
}

// retrieves associated palette pointer and increments reference count
HRESULT DDrawSurfaceImpl::get_palette(DDrawPalette** pal) {
    if (!pal) return DDERR_INVALIDPARAMS;
    *pal = palette;
    if (palette) palette->AddRef();
    return DD_OK;
}

// associates clipper with surface
HRESULT DDrawSurfaceImpl::set_clipper(DDrawClipper* clip) {
    if (clipper) clipper->Release();
    clipper = clip;
    if (clipper) clipper->AddRef();
    return DD_OK;
}

// retrieves associated clipper
HRESULT DDrawSurfaceImpl::get_clipper(DDrawClipper** clip) {
    if (!clip) return DDERR_INVALIDPARAMS;
    *clip = clipper;
    if (clipper) clipper->AddRef();
    return DD_OK;
}

// sets source or destination color key value for transparent blitting
HRESULT DDrawSurfaceImpl::set_color_key(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) {
    if (dwFlags & DDCKEY_SRCBLT) {
        if (lpDDColorKey) {
            src_colorkey = *lpDDColorKey;
            has_src_colorkey = true;
        } else {
            has_src_colorkey = false;
        }
    }
    if (dwFlags & DDCKEY_DESTBLT) {
        if (lpDDColorKey) {
            dest_colorkey = *lpDDColorKey;
            has_dest_colorkey = true;
        } else {
            has_dest_colorkey = false;
        }
    }
    return DD_OK;
}

// retrieves source or destination color key value
HRESULT DDrawSurfaceImpl::get_color_key(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) {
    if (!lpDDColorKey) return DDERR_INVALIDPARAMS;
    if (dwFlags & DDCKEY_SRCBLT) {
        *lpDDColorKey = src_colorkey;
        return DD_OK;
    }
    if (dwFlags & DDCKEY_DESTBLT) {
        *lpDDColorKey = dest_colorkey;
        return DD_OK;
    }
    return DDERR_INVALIDPARAMS;
}

// copies current surface descriptor to directdraw 4/7 caller struct
HRESULT DDrawSurfaceImpl::get_surface_desc(DDSURFACEDESC2* lpDesc) {
    if (!lpDesc) return DDERR_INVALIDPARAMS;
    DWORD sz = lpDesc->dwSize ? lpDesc->dwSize : sizeof(DDSURFACEDESC2);
    std::memcpy(lpDesc, &desc, std::min((size_t)sz, sizeof(desc)));
    lpDesc->dwSize = sz;
    return DD_OK;
}

// copies current surface descriptor to directdraw 1/2/3 caller struct
HRESULT DDrawSurfaceImpl::get_surface_desc_v1(DDSURFACEDESC* lpDesc) {
    if (!lpDesc) return DDERR_INVALIDPARAMS;
    desc2_to_desc1(&desc, lpDesc);
    return DD_OK;
}

// directdraw surface 7 interface thunks
STDMETHODIMP DDrawSurface7::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDrawSurface7::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDrawSurface7::Release() { return impl->release(); }
STDMETHODIMP DDrawSurface7::AddAttachedSurface(LPDIRECTDRAWSURFACE7 lpDDSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface7::AddOverlayDirtyRect(LPRECT lpRect) { return DD_OK; }
STDMETHODIMP DDrawSurface7::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface7*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt(lpDestRect, src_impl, lpSrcRect, dwFlags, lpDDBltFx);
}
STDMETHODIMP DDrawSurface7::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface7::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface7*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt_fast(dwX, dwY, src_impl, lpSrcRect, dwTrans);
}
STDMETHODIMP DDrawSurface7::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface7::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface7::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpfnCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface7::Flip(LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride, DWORD dwFlags) {
    DDrawSurfaceImpl* target_impl = lpDDSurfaceTargetOverride ? ((DDrawSurface7*)lpDDSurfaceTargetOverride)->get_impl() : nullptr;
    return impl->flip(target_impl, dwFlags);
}
STDMETHODIMP DDrawSurface7::GetAttachedSurface(LPDDSCAPS2 lpDDSCaps, LPDIRECTDRAWSURFACE7* lplpDDAttachedSurface) {
    if (!lplpDDAttachedSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* att = nullptr;
    HRESULT hr = impl->get_attached_surface(lpDDSCaps ? lpDDSCaps->dwCaps : 0, &att);
    if (SUCCEEDED(hr) && att) {
        *lplpDDAttachedSurface = att->get_interface7();
    }
    return hr;
}
STDMETHODIMP DDrawSurface7::GetBltStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface7::GetCaps(LPDDSCAPS2 lpDDSCaps) {
    if (!lpDDSCaps) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    *lpDDSCaps = d.ddsCaps;
    return DD_OK;
}
STDMETHODIMP DDrawSurface7::GetClipper(LPDIRECTDRAWCLIPPER* lplpDDClipper) {
    return impl->get_clipper((DDrawClipper**)lplpDDClipper);
}
STDMETHODIMP DDrawSurface7::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->get_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface7::GetDC(HDC* lphDC) { return impl->get_dc(lphDC); }
STDMETHODIMP DDrawSurface7::GetFlipStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface7::GetOverlayPosition(LPLONG lplX, LPLONG lplY) { return DD_OK; }
STDMETHODIMP DDrawSurface7::GetPalette(LPDIRECTDRAWPALETTE* lplpDDPalette) {
    return impl->get_palette((DDrawPalette**)lplpDDPalette);
}
STDMETHODIMP DDrawSurface7::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat) {
    if (!lpDDPixelFormat) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    *lpDDPixelFormat = d.ddpfPixelFormat;
    return DD_OK;
}
STDMETHODIMP DDrawSurface7::GetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc) { return impl->get_surface_desc(lpDDSurfaceDesc); }
STDMETHODIMP DDrawSurface7::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC2 lpDDSurfaceDesc) { return DD_OK; }
STDMETHODIMP DDrawSurface7::IsLost() { return DD_OK; }
STDMETHODIMP DDrawSurface7::Lock(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) {
    return impl->lock(lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
}
STDMETHODIMP DDrawSurface7::ReleaseDC(HDC hDC) { return impl->release_dc(hDC); }
STDMETHODIMP DDrawSurface7::Restore() { return DD_OK; }
STDMETHODIMP DDrawSurface7::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) { return impl->set_clipper((DDrawClipper*)lpDDClipper); }
STDMETHODIMP DDrawSurface7::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->set_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface7::SetOverlayPosition(LONG lX, LONG lY) { return DD_OK; }
STDMETHODIMP DDrawSurface7::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) { return impl->set_palette((DDrawPalette*)lpDDPalette); }
STDMETHODIMP DDrawSurface7::Unlock(LPRECT lpRect) { return impl->unlock(); }
STDMETHODIMP DDrawSurface7::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE7 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) { return DD_OK; }
STDMETHODIMP DDrawSurface7::UpdateOverlayDisplay(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface7::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSReference) { return DD_OK; }
STDMETHODIMP DDrawSurface7::GetDDInterface(LPVOID* lplpDD) { return DD_OK; }
STDMETHODIMP DDrawSurface7::PageLock(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface7::PageUnlock(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface7::SetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface7::SetPrivateData(REFGUID guidTag, LPVOID lpData, DWORD cbSize, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface7::GetPrivateData(REFGUID guidTag, LPVOID lpBuffer, LPDWORD lpcbBufferSize) { return DD_OK; }
STDMETHODIMP DDrawSurface7::FreePrivateData(REFGUID guidTag) { return DD_OK; }
STDMETHODIMP DDrawSurface7::GetUniquenessValue(LPDWORD lpValue) { if (lpValue) *lpValue = 0; return DD_OK; }
STDMETHODIMP DDrawSurface7::ChangeUniquenessValue() { return DD_OK; }
STDMETHODIMP DDrawSurface7::SetPriority(DWORD dwPriority) { return DD_OK; }
STDMETHODIMP DDrawSurface7::GetPriority(LPDWORD lpdwPriority) { if (lpdwPriority) *lpdwPriority = 0; return DD_OK; }
STDMETHODIMP DDrawSurface7::SetLOD(DWORD dwMaxLOD) { return DD_OK; }
STDMETHODIMP DDrawSurface7::GetLOD(LPDWORD lpdwMaxLOD) { if (lpdwMaxLOD) *lpdwMaxLOD = 0; return DD_OK; }

// directdraw surface 4 interface thunks
STDMETHODIMP DDrawSurface4::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDrawSurface4::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDrawSurface4::Release() { return impl->release(); }
STDMETHODIMP DDrawSurface4::AddAttachedSurface(LPDIRECTDRAWSURFACE4 lpDDSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface4::AddOverlayDirtyRect(LPRECT lpRect) { return DD_OK; }
STDMETHODIMP DDrawSurface4::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE4 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface4*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt(lpDestRect, src_impl, lpSrcRect, dwFlags, lpDDBltFx);
}
STDMETHODIMP DDrawSurface4::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface4::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE4 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface4*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt_fast(dwX, dwY, src_impl, lpSrcRect, dwTrans);
}
STDMETHODIMP DDrawSurface4::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE4 lpDDSAttachedSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface4::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK2 lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface4::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK2 lpfnCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface4::Flip(LPDIRECTDRAWSURFACE4 lpDDSurfaceTargetOverride, DWORD dwFlags) {
    DDrawSurfaceImpl* target_impl = lpDDSurfaceTargetOverride ? ((DDrawSurface4*)lpDDSurfaceTargetOverride)->get_impl() : nullptr;
    return impl->flip(target_impl, dwFlags);
}
STDMETHODIMP DDrawSurface4::GetAttachedSurface(LPDDSCAPS2 lpDDSCaps, LPDIRECTDRAWSURFACE4* lplpDDAttachedSurface) {
    if (!lplpDDAttachedSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* att = nullptr;
    HRESULT hr = impl->get_attached_surface(lpDDSCaps ? lpDDSCaps->dwCaps : 0, &att);
    if (SUCCEEDED(hr) && att) {
        *lplpDDAttachedSurface = att->get_interface4();
    }
    return hr;
}
STDMETHODIMP DDrawSurface4::GetBltStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface4::GetCaps(LPDDSCAPS2 lpDDSCaps) {
    if (!lpDDSCaps) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    *lpDDSCaps = d.ddsCaps;
    return DD_OK;
}
STDMETHODIMP DDrawSurface4::GetClipper(LPDIRECTDRAWCLIPPER* lplpDDClipper) { return impl->get_clipper((DDrawClipper**)lplpDDClipper); }
STDMETHODIMP DDrawSurface4::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->get_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface4::GetDC(HDC* lphDC) { return impl->get_dc(lphDC); }
STDMETHODIMP DDrawSurface4::GetFlipStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface4::GetOverlayPosition(LPLONG lplX, LPLONG lplY) { return DD_OK; }
STDMETHODIMP DDrawSurface4::GetPalette(LPDIRECTDRAWPALETTE* lplpDDPalette) { return impl->get_palette((DDrawPalette**)lplpDDPalette); }
STDMETHODIMP DDrawSurface4::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat) {
    if (!lpDDPixelFormat) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    *lpDDPixelFormat = d.ddpfPixelFormat;
    return DD_OK;
}
STDMETHODIMP DDrawSurface4::GetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc) { return impl->get_surface_desc(lpDDSurfaceDesc); }
STDMETHODIMP DDrawSurface4::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC2 lpDDSurfaceDesc) { return DD_OK; }
STDMETHODIMP DDrawSurface4::IsLost() { return DD_OK; }
STDMETHODIMP DDrawSurface4::Lock(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) {
    return impl->lock(lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
}
STDMETHODIMP DDrawSurface4::ReleaseDC(HDC hDC) { return impl->release_dc(hDC); }
STDMETHODIMP DDrawSurface4::Restore() { return DD_OK; }
STDMETHODIMP DDrawSurface4::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) { return impl->set_clipper((DDrawClipper*)lpDDClipper); }
STDMETHODIMP DDrawSurface4::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->set_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface4::SetOverlayPosition(LONG lX, LONG lY) { return DD_OK; }
STDMETHODIMP DDrawSurface4::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) { return impl->set_palette((DDrawPalette*)lpDDPalette); }
STDMETHODIMP DDrawSurface4::Unlock(LPRECT lpRect) { return impl->unlock(); }
STDMETHODIMP DDrawSurface4::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE4 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) { return DD_OK; }
STDMETHODIMP DDrawSurface4::UpdateOverlayDisplay(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface4::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE4 lpDDSReference) { return DD_OK; }
STDMETHODIMP DDrawSurface4::GetDDInterface(LPVOID* lplpDD) { return DD_OK; }
STDMETHODIMP DDrawSurface4::PageLock(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface4::PageUnlock(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface4::SetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface4::SetPrivateData(REFGUID guidTag, LPVOID lpData, DWORD cbSize, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface4::GetPrivateData(REFGUID guidTag, LPVOID lpBuffer, LPDWORD lpcbBufferSize) { return DD_OK; }
STDMETHODIMP DDrawSurface4::FreePrivateData(REFGUID guidTag) { return DD_OK; }
STDMETHODIMP DDrawSurface4::GetUniquenessValue(LPDWORD lpValue) { if (lpValue) *lpValue = 0; return DD_OK; }
STDMETHODIMP DDrawSurface4::ChangeUniquenessValue() { return DD_OK; }

// directdraw surface 3 interface thunks
STDMETHODIMP DDrawSurface3::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDrawSurface3::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDrawSurface3::Release() { return impl->release(); }
STDMETHODIMP DDrawSurface3::AddAttachedSurface(LPDIRECTDRAWSURFACE3 lpDDSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface3::AddOverlayDirtyRect(LPRECT lpRect) { return DD_OK; }
STDMETHODIMP DDrawSurface3::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE3 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface3*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt(lpDestRect, src_impl, lpSrcRect, dwFlags, lpDDBltFx);
}
STDMETHODIMP DDrawSurface3::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface3::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE3 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface3*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt_fast(dwX, dwY, src_impl, lpSrcRect, dwTrans);
}
STDMETHODIMP DDrawSurface3::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE3 lpDDSAttachedSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface3::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface3::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface3::Flip(LPDIRECTDRAWSURFACE3 lpDDSurfaceTargetOverride, DWORD dwFlags) {
    DDrawSurfaceImpl* target_impl = lpDDSurfaceTargetOverride ? ((DDrawSurface3*)lpDDSurfaceTargetOverride)->get_impl() : nullptr;
    return impl->flip(target_impl, dwFlags);
}
STDMETHODIMP DDrawSurface3::GetAttachedSurface(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE3* lplpDDAttachedSurface) {
    if (!lplpDDAttachedSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* att = nullptr;
    HRESULT hr = impl->get_attached_surface(lpDDSCaps ? lpDDSCaps->dwCaps : 0, &att);
    if (SUCCEEDED(hr) && att) {
        *lplpDDAttachedSurface = att->get_interface3();
    }
    return hr;
}
STDMETHODIMP DDrawSurface3::GetBltStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface3::GetCaps(LPDDSCAPS lpDDSCaps) {
    if (!lpDDSCaps) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    lpDDSCaps->dwCaps = d.ddsCaps.dwCaps;
    return DD_OK;
}
STDMETHODIMP DDrawSurface3::GetClipper(LPDIRECTDRAWCLIPPER* lplpDDClipper) { return impl->get_clipper((DDrawClipper**)lplpDDClipper); }
STDMETHODIMP DDrawSurface3::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->get_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface3::GetDC(HDC* lphDC) { return impl->get_dc(lphDC); }
STDMETHODIMP DDrawSurface3::GetFlipStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface3::GetOverlayPosition(LPLONG lplX, LPLONG lplY) { return DD_OK; }
STDMETHODIMP DDrawSurface3::GetPalette(LPDIRECTDRAWPALETTE* lplpDDPalette) { return impl->get_palette((DDrawPalette**)lplpDDPalette); }
STDMETHODIMP DDrawSurface3::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat) {
    if (!lpDDPixelFormat) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    *lpDDPixelFormat = d.ddpfPixelFormat;
    return DD_OK;
}
STDMETHODIMP DDrawSurface3::GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc) { return impl->get_surface_desc_v1(lpDDSurfaceDesc); }
STDMETHODIMP DDrawSurface3::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc) { return DD_OK; }
STDMETHODIMP DDrawSurface3::IsLost() { return DD_OK; }
STDMETHODIMP DDrawSurface3::Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d2;
    std::memset(&d2, 0, sizeof(d2));
    d2.dwSize = sizeof(d2);
    HRESULT hr = impl->lock(lpDestRect, &d2, dwFlags, hEvent);
    if (SUCCEEDED(hr)) {
        desc2_to_desc1(&d2, lpDDSurfaceDesc);
    }
    return hr;
}
STDMETHODIMP DDrawSurface3::ReleaseDC(HDC hDC) { return impl->release_dc(hDC); }
STDMETHODIMP DDrawSurface3::Restore() { return DD_OK; }
STDMETHODIMP DDrawSurface3::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) { return impl->set_clipper((DDrawClipper*)lpDDClipper); }
STDMETHODIMP DDrawSurface3::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->set_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface3::SetOverlayPosition(LONG lX, LONG lY) { return DD_OK; }
STDMETHODIMP DDrawSurface3::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) { return impl->set_palette((DDrawPalette*)lpDDPalette); }
STDMETHODIMP DDrawSurface3::Unlock(LPVOID lpSurfaceData) { return impl->unlock(); }
STDMETHODIMP DDrawSurface3::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE3 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) { return DD_OK; }
STDMETHODIMP DDrawSurface3::UpdateOverlayDisplay(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface3::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE3 lpDDSReference) { return DD_OK; }
STDMETHODIMP DDrawSurface3::GetDDInterface(LPVOID* lplpDD) { return DD_OK; }
STDMETHODIMP DDrawSurface3::PageLock(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface3::PageUnlock(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface3::SetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags) { return DD_OK; }

// directdraw surface 2 interface thunks
STDMETHODIMP DDrawSurface2::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDrawSurface2::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDrawSurface2::Release() { return impl->release(); }
STDMETHODIMP DDrawSurface2::AddAttachedSurface(LPDIRECTDRAWSURFACE2 lpDDSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface2::AddOverlayDirtyRect(LPRECT lpRect) { return DD_OK; }
STDMETHODIMP DDrawSurface2::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE2 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface2*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt(lpDestRect, src_impl, lpSrcRect, dwFlags, lpDDBltFx);
}
STDMETHODIMP DDrawSurface2::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface2::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE2 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface2*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt_fast(dwX, dwY, src_impl, lpSrcRect, dwTrans);
}
STDMETHODIMP DDrawSurface2::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE2 lpDDSAttachedSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface2::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface2::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface2::Flip(LPDIRECTDRAWSURFACE2 lpDDSurfaceTargetOverride, DWORD dwFlags) {
    DDrawSurfaceImpl* target_impl = lpDDSurfaceTargetOverride ? ((DDrawSurface2*)lpDDSurfaceTargetOverride)->get_impl() : nullptr;
    return impl->flip(target_impl, dwFlags);
}
STDMETHODIMP DDrawSurface2::GetAttachedSurface(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE2* lplpDDAttachedSurface) {
    if (!lplpDDAttachedSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* att = nullptr;
    HRESULT hr = impl->get_attached_surface(lpDDSCaps ? lpDDSCaps->dwCaps : 0, &att);
    if (SUCCEEDED(hr) && att) {
        *lplpDDAttachedSurface = att->get_interface2();
    }
    return hr;
}
STDMETHODIMP DDrawSurface2::GetBltStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface2::GetCaps(LPDDSCAPS lpDDSCaps) {
    if (!lpDDSCaps) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    lpDDSCaps->dwCaps = d.ddsCaps.dwCaps;
    return DD_OK;
}
STDMETHODIMP DDrawSurface2::GetClipper(LPDIRECTDRAWCLIPPER* lplpDDClipper) { return impl->get_clipper((DDrawClipper**)lplpDDClipper); }
STDMETHODIMP DDrawSurface2::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->get_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface2::GetDC(HDC* lphDC) { return impl->get_dc(lphDC); }
STDMETHODIMP DDrawSurface2::GetFlipStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface2::GetOverlayPosition(LPLONG lplX, LPLONG lplY) { return DD_OK; }
STDMETHODIMP DDrawSurface2::GetPalette(LPDIRECTDRAWPALETTE* lplpDDPalette) { return impl->get_palette((DDrawPalette**)lplpDDPalette); }
STDMETHODIMP DDrawSurface2::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat) {
    if (!lpDDPixelFormat) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    *lpDDPixelFormat = d.ddpfPixelFormat;
    return DD_OK;
}
STDMETHODIMP DDrawSurface2::GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc) { return impl->get_surface_desc_v1(lpDDSurfaceDesc); }
STDMETHODIMP DDrawSurface2::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc) { return DD_OK; }
STDMETHODIMP DDrawSurface2::IsLost() { return DD_OK; }
STDMETHODIMP DDrawSurface2::Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d2;
    std::memset(&d2, 0, sizeof(d2));
    d2.dwSize = sizeof(d2);
    HRESULT hr = impl->lock(lpDestRect, &d2, dwFlags, hEvent);
    if (SUCCEEDED(hr)) {
        desc2_to_desc1(&d2, lpDDSurfaceDesc);
    }
    return hr;
}
STDMETHODIMP DDrawSurface2::ReleaseDC(HDC hDC) { return impl->release_dc(hDC); }
STDMETHODIMP DDrawSurface2::Restore() { return DD_OK; }
STDMETHODIMP DDrawSurface2::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) { return impl->set_clipper((DDrawClipper*)lpDDClipper); }
STDMETHODIMP DDrawSurface2::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->set_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface2::SetOverlayPosition(LONG lX, LONG lY) { return DD_OK; }
STDMETHODIMP DDrawSurface2::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) { return impl->set_palette((DDrawPalette*)lpDDPalette); }
STDMETHODIMP DDrawSurface2::Unlock(LPVOID lpSurfaceData) { return impl->unlock(); }
STDMETHODIMP DDrawSurface2::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE2 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) { return DD_OK; }
STDMETHODIMP DDrawSurface2::UpdateOverlayDisplay(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface2::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE2 lpDDSReference) { return DD_OK; }
STDMETHODIMP DDrawSurface2::GetDDInterface(LPVOID* lplpDD) { return DD_OK; }
STDMETHODIMP DDrawSurface2::PageLock(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface2::PageUnlock(DWORD dwFlags) { return DD_OK; }

// directdraw surface 1 interface thunks
STDMETHODIMP DDrawSurface1::QueryInterface(REFIID riid, LPVOID* ppvObj) { return impl->query_interface(riid, ppvObj); }
STDMETHODIMP_(ULONG) DDrawSurface1::AddRef() { impl->add_ref(); return 1; }
STDMETHODIMP_(ULONG) DDrawSurface1::Release() { return impl->release(); }
STDMETHODIMP DDrawSurface1::AddAttachedSurface(LPDIRECTDRAWSURFACE lpDDSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface1::AddOverlayDirtyRect(LPRECT lpRect) { return DD_OK; }
STDMETHODIMP DDrawSurface1::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface1*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt(lpDestRect, src_impl, lpSrcRect, dwFlags, lpDDBltFx);
}
STDMETHODIMP DDrawSurface1::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface1::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) {
    DDrawSurfaceImpl* src_impl = lpDDSrcSurface ? ((DDrawSurface1*)lpDDSrcSurface)->get_impl() : nullptr;
    return impl->blt_fast(dwX, dwY, src_impl, lpSrcRect, dwTrans);
}
STDMETHODIMP DDrawSurface1::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE lpDDSAttachedSurface) { return DD_OK; }
STDMETHODIMP DDrawSurface1::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface1::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback) { return DD_OK; }
STDMETHODIMP DDrawSurface1::Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags) {
    DDrawSurfaceImpl* target_impl = lpDDSurfaceTargetOverride ? ((DDrawSurface1*)lpDDSurfaceTargetOverride)->get_impl() : nullptr;
    return impl->flip(target_impl, dwFlags);
}
STDMETHODIMP DDrawSurface1::GetAttachedSurface(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE* lplpDDAttachedSurface) {
    if (!lplpDDAttachedSurface) return DDERR_INVALIDPARAMS;
    DDrawSurfaceImpl* att = nullptr;
    HRESULT hr = impl->get_attached_surface(lpDDSCaps ? lpDDSCaps->dwCaps : 0, &att);
    if (SUCCEEDED(hr) && att) {
        *lplpDDAttachedSurface = att->get_interface1();
    }
    return hr;
}
STDMETHODIMP DDrawSurface1::GetBltStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface1::GetCaps(LPDDSCAPS lpDDSCaps) {
    if (!lpDDSCaps) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    lpDDSCaps->dwCaps = d.ddsCaps.dwCaps;
    return DD_OK;
}
STDMETHODIMP DDrawSurface1::GetClipper(LPDIRECTDRAWCLIPPER* lplpDDClipper) { return impl->get_clipper((DDrawClipper**)lplpDDClipper); }
STDMETHODIMP DDrawSurface1::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->get_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface1::GetDC(HDC* lphDC) { return impl->get_dc(lphDC); }
STDMETHODIMP DDrawSurface1::GetFlipStatus(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface1::GetOverlayPosition(LPLONG lplX, LPLONG lplY) { return DD_OK; }
STDMETHODIMP DDrawSurface1::GetPalette(LPDIRECTDRAWPALETTE* lplpDDPalette) { return impl->get_palette((DDrawPalette**)lplpDDPalette); }
STDMETHODIMP DDrawSurface1::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat) {
    if (!lpDDPixelFormat) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d;
    d.dwSize = sizeof(d);
    impl->get_surface_desc(&d);
    *lpDDPixelFormat = d.ddpfPixelFormat;
    return DD_OK;
}
STDMETHODIMP DDrawSurface1::GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc) { return impl->get_surface_desc_v1(lpDDSurfaceDesc); }
STDMETHODIMP DDrawSurface1::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc) { return DD_OK; }
STDMETHODIMP DDrawSurface1::IsLost() { return DD_OK; }
STDMETHODIMP DDrawSurface1::Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 d2;
    std::memset(&d2, 0, sizeof(d2));
    d2.dwSize = sizeof(d2);
    HRESULT hr = impl->lock(lpDestRect, &d2, dwFlags, hEvent);
    if (SUCCEEDED(hr)) {
        desc2_to_desc1(&d2, lpDDSurfaceDesc);
    }
    return hr;
}
STDMETHODIMP DDrawSurface1::ReleaseDC(HDC hDC) { return impl->release_dc(hDC); }
STDMETHODIMP DDrawSurface1::Restore() { return DD_OK; }
STDMETHODIMP DDrawSurface1::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) { return impl->set_clipper((DDrawClipper*)lpDDClipper); }
STDMETHODIMP DDrawSurface1::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) { return impl->set_color_key(dwFlags, lpDDColorKey); }
STDMETHODIMP DDrawSurface1::SetOverlayPosition(LONG lX, LONG lY) { return DD_OK; }
STDMETHODIMP DDrawSurface1::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) { return impl->set_palette((DDrawPalette*)lpDDPalette); }
STDMETHODIMP DDrawSurface1::Unlock(LPVOID lpSurfaceData) { return impl->unlock(); }
STDMETHODIMP DDrawSurface1::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) { return DD_OK; }
STDMETHODIMP DDrawSurface1::UpdateOverlayDisplay(DWORD dwFlags) { return DD_OK; }
STDMETHODIMP DDrawSurface1::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE lpDDSReference) { return DD_OK; }
