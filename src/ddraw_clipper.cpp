#include "../include/ddraw_clipper.h"
#include <cstring>

// constructs clipper object with initial reference count
DDrawClipper::DDrawClipper()
    : ref_count(1), target_hwnd(nullptr) {}

// cleans up clipper resources
DDrawClipper::~DDrawClipper() {}

// queries supported interfaces on the clipper com object
STDMETHODIMP DDrawClipper::QueryInterface(REFIID riid, LPVOID* ppvObj) {
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirectDrawClipper) {
        *ppvObj = this;
        AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

// increments reference count
STDMETHODIMP_(ULONG) DDrawClipper::AddRef() {
    return ++ref_count;
}

// decrements reference count and frees object when zero
STDMETHODIMP_(ULONG) DDrawClipper::Release() {
    ULONG val = --ref_count;
    if (val == 0) {
        delete this;
    }
    return val;
}

// retrieves clip list bounding rects or required buffer size
STDMETHODIMP DDrawClipper::GetClipList(LPRECT lpRect, LPRGNDATA lpClipList, LPDWORD lpdwSize) {
    if (!lpdwSize) return DDERR_INVALIDPARAMS;
    if (clip_region_data.empty()) {
        *lpdwSize = sizeof(RGNDATAHEADER);
        return DD_OK;
    }
    if (!lpClipList) {
        *lpdwSize = (DWORD)clip_region_data.size();
        return DD_OK;
    }
    if (*lpdwSize < clip_region_data.size()) return DDERR_REGIONTOOSMALL;
    std::memcpy(lpClipList, clip_region_data.data(), clip_region_data.size());
    return DD_OK;
}

// retrieves window handle associated with the clipper
STDMETHODIMP DDrawClipper::GetHWnd(HWND* lphWnd) {
    if (!lphWnd) return DDERR_INVALIDPARAMS;
    *lphWnd = target_hwnd;
    return DD_OK;
}

// initializes clipper object
STDMETHODIMP DDrawClipper::Initialize(LPDIRECTDRAW lpDD, DWORD dwFlags) {
    return DD_OK;
}

// checks if clip list has changed
STDMETHODIMP DDrawClipper::IsClipListChanged(BOOL* lpbChanged) {
    if (lpbChanged) *lpbChanged = FALSE;
    return DD_OK;
}

// sets clip region list for surface blits
STDMETHODIMP DDrawClipper::SetClipList(LPRGNDATA lpClipList, DWORD dwFlags) {
    if (!lpClipList) {
        clip_region_data.clear();
        return DD_OK;
    }
    DWORD sz = sizeof(RGNDATAHEADER) + lpClipList->rdh.nCount * sizeof(RECT);
    clip_region_data.resize(sz);
    std::memcpy(clip_region_data.data(), lpClipList, sz);
    return DD_OK;
}

// attaches window handle to clipper
STDMETHODIMP DDrawClipper::SetHWnd(DWORD dwFlags, HWND hWnd) {
    target_hwnd = hWnd;
    return DD_OK;
}
