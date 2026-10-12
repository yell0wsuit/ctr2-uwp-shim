#pragma once
#include <activation.h>
#include <wrl.h>
#include <winstring.h>
#include "log.h"
namespace ctr2 {
template <class... Interfaces>
class Factory : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                                    IActivationFactory, Interfaces..., Microsoft::WRL::FtmBase> {
    using Base = Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                              IActivationFactory, Interfaces..., Microsoft::WRL::FtmBase>;
    const wchar_t* name_;

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) noexcept override {
        auto hr = Base::QueryInterface(iid, value);
        if (FAILED(hr)) Log("FAIL", "%ls factory QI missing %08lx-%04x-%04x", name_, iid.Data1, iid.Data2, iid.Data3);
        return hr;
    }
    explicit Factory(const wchar_t* name) : name_(name) {}
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) noexcept override {
        if (!value) return E_POINTER;
        return WindowsCreateString(name_, static_cast<UINT32>(wcslen(name_)), value);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) noexcept override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_ILLEGAL_METHOD_CALL;
    }
};
}
