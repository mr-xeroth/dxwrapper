#pragma once

#include <windows.h>
#include <ddraw.h>
#include <cstdint>

class DDrawSurfaceImpl;

class DDrawPalette : public IDirectDrawPalette {
public:
    DDrawPalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable);
    ~DDrawPalette();

    // iunknown interface
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // idirectdrawpalette interface
    STDMETHOD(GetCaps)(LPDWORD lpdwCaps) override;
    STDMETHOD(GetEntries)(DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, LPPALETTEENTRY lpEntries) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW lpDD, DWORD dwFlags, LPPALETTEENTRY lpDDColorTable) override;
    STDMETHOD(SetEntries)(DWORD dwFlags, DWORD dwStartingEntry, DWORD dwCount, LPPALETTEENTRY lpEntries) override;

    const uint32_t* get_rgba_entries() const { return rgba_palette; }
    void set_attached_surface(DDrawSurfaceImpl* surf) { attached_surface = surf; }

private:
    ULONG ref_count;
    DWORD caps;
    PALETTEENTRY entries[256];
    uint32_t rgba_palette[256];
    DDrawSurfaceImpl* attached_surface;

    void update_rgba_cache();
};
