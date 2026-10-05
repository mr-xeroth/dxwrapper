#pragma once

#include <windows.h>
#include <ddraw.h>
#include <vector>
#include <cstdint>

class DDrawClipper : public IDirectDrawClipper {
public:
    DDrawClipper();
    ~DDrawClipper();

    // iunknown interface
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // idirectdrawclipper interface
    STDMETHOD(GetClipList)(LPRECT lpRect, LPRGNDATA lpClipList, LPDWORD lpdwSize) override;
    STDMETHOD(GetHWnd)(HWND* lphWnd) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW lpDD, DWORD dwFlags) override;
    STDMETHOD(IsClipListChanged)(BOOL* lpbChanged) override;
    STDMETHOD(SetClipList)(LPRGNDATA lpClipList, DWORD dwFlags) override;
    STDMETHOD(SetHWnd)(DWORD dwFlags, HWND hWnd) override;

private:
    ULONG ref_count;
    HWND target_hwnd;
    std::vector<uint8_t> clip_region_data;
};
