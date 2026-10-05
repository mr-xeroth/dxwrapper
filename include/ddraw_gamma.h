#pragma once

#include <windows.h>
#include <ddraw.h>

class DDrawGammaControl : public IDirectDrawGammaControl {
public:
    DDrawGammaControl();
    ~DDrawGammaControl();

    // iunknown interface
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // idirectdrawgammacontrol interface
    STDMETHOD(GetGammaRamp)(DWORD dwFlags, LPDDGAMMARAMP lpRampData) override;
    STDMETHOD(SetGammaRamp)(DWORD dwFlags, LPDDGAMMARAMP lpRampData) override;

private:
    ULONG ref_count;
    DDGAMMARAMP gamma_ramp;
};
