#include "../include/ddraw_palette.h"
#include "../include/ddraw_surface.h"
#include "../include/ddraw_interface.h"
#include <cstring>

// constructs directdraw palette and generates initial 32-bit rgba cache
DDrawPalette::DDrawPalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable)
    : ref_count(1), caps(dwFlags), attached_surface(nullptr) {
    std::memset(entries, 0, sizeof(entries));
    if (lpColorTable) {
        int count = (dwFlags & DDPCAPS_8BIT) ? 256 : ((dwFlags & DDPCAPS_4BIT) ? 16 : 256);
        std::memcpy(entries, lpColorTable, count * sizeof(PALETTEENTRY));
    }
    update_rgba_cache();
}

// virtual destructor for directdraw palette
DDrawPalette::~DDrawPalette() {}

// precomputes 256 dword rgba color values for fast 8-bit to 32-bit texture conversion
void DDrawPalette::update_rgba_cache() {
    for (int i = 0; i < 256; ++i) {
        uint32_t r = entries[i].peRed;
        uint32_t g = entries[i].peGreen;
        uint32_t b = entries[i].peBlue;
        rgba_palette[i] = (0xFF000000) | (b << 16) | (g << 8) | r;
    }
}

// retrieves com interface pointer for palette
STDMETHODIMP DDrawPalette::QueryInterface(REFIID riid, LPVOID* ppvObj) {
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirectDrawPalette) {
        *ppvObj = this;
        AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

// increments reference count
STDMETHODIMP_(ULONG) DDrawPalette::AddRef() {
    return ++ref_count;
}

// decrements reference count and deletes instance when zero
STDMETHODIMP_(ULONG) DDrawPalette::Release() {
    if (ref_count > 0) {
        --ref_count;
    }
    return ref_count;
}

// returns palette capabilities flags
STDMETHODIMP DDrawPalette::GetCaps(LPDWORD lpdwCaps) {
    if (!lpdwCaps) return DDERR_INVALIDPARAMS;
    *lpdwCaps = caps;
    return DD_OK;
}

// copies palette entries to caller buffer
STDMETHODIMP DDrawPalette::GetEntries(DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, LPPALETTEENTRY lpEntries) {
    if (!lpEntries || dwBase + dwNumEntries > 256) return DDERR_INVALIDPARAMS;
    std::memcpy(lpEntries, &entries[dwBase], dwNumEntries * sizeof(PALETTEENTRY));
    return DD_OK;
}

// initializes palette object (stub for directdraw 1.0 compatibility)
STDMETHODIMP DDrawPalette::Initialize(LPDIRECTDRAW lpDD, DWORD dwFlags, LPPALETTEENTRY lpDDColorTable) {
    return DD_OK;
}

// updates palette color entries, rebuilds rgba lookup cache, and triggers primary surface redraw
STDMETHODIMP DDrawPalette::SetEntries(DWORD dwFlags, DWORD dwStartingEntry, DWORD dwCount, LPPALETTEENTRY lpEntries) {
    if (!lpEntries || dwStartingEntry + dwCount > 256) return DDERR_INVALIDPARAMS;
    std::memcpy(&entries[dwStartingEntry], lpEntries, dwCount * sizeof(PALETTEENTRY));
    update_rgba_cache();
    if (attached_surface && attached_surface->is_primary()) {
        attached_surface->present_surface();
    }
    return DD_OK;
}
