#include "../include/ddraw_gamma.h"
#include <cstring>

// constructs gamma control object and initializes linear identity ramp
DDrawGammaControl::DDrawGammaControl()
    : ref_count(1) {
    for (int i = 0; i < 256; ++i) {
        WORD val = (WORD)((i * 65535) / 255);
        gamma_ramp.red[i] = val;
        gamma_ramp.green[i] = val;
        gamma_ramp.blue[i] = val;
    }
}

// cleans up gamma control resources
DDrawGammaControl::~DDrawGammaControl() {}

// queries supported interfaces on the gamma control com object
STDMETHODIMP DDrawGammaControl::QueryInterface(REFIID riid, LPVOID* ppvObj) {
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirectDrawGammaControl) {
        *ppvObj = this;
        AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

// increments reference count
STDMETHODIMP_(ULONG) DDrawGammaControl::AddRef() {
    return ++ref_count;
}

// decrements reference count and frees object when zero
STDMETHODIMP_(ULONG) DDrawGammaControl::Release() {
    ULONG val = --ref_count;
    if (val == 0) {
        delete this;
    }
    return val;
}

// retrieves current gamma ramp data
STDMETHODIMP DDrawGammaControl::GetGammaRamp(DWORD dwFlags, LPDDGAMMARAMP lpRampData) {
    if (!lpRampData) return DDERR_INVALIDPARAMS;
    std::memcpy(lpRampData, &gamma_ramp, sizeof(DDGAMMARAMP));
    return DD_OK;
}

// sets current gamma ramp data
STDMETHODIMP DDrawGammaControl::SetGammaRamp(DWORD dwFlags, LPDDGAMMARAMP lpRampData) {
    if (!lpRampData) return DDERR_INVALIDPARAMS;
    std::memcpy(&gamma_ramp, lpRampData, sizeof(DDGAMMARAMP));
    return DD_OK;
}
