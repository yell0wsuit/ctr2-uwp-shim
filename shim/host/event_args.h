#pragma once
#include <windows.ui.core.h>
#include <wrl.h>
namespace ctr2 {
using namespace ABI::Windows::UI::Core;
class ActivationArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          IWindowActivatedEventArgs, ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.WindowActivatedEventArgs", BaseTrust);
    CoreWindowActivationState state_;
    boolean handled_ = false;

public:
    explicit ActivationArgs(CoreWindowActivationState state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE get_WindowActivationState(CoreWindowActivationState* value) override {
        if (!value) return E_POINTER;
        *value = state_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
class WindowSizeArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          IWindowSizeChangedEventArgs, ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.WindowSizeChangedEventArgs", BaseTrust);
    ABI::Windows::Foundation::Size size_;
    boolean handled_ = false;

public:
    explicit WindowSizeArgs(ABI::Windows::Foundation::Size size) : size_(size) {}
    HRESULT STDMETHODCALLTYPE get_Size(ABI::Windows::Foundation::Size* value) override {
        if (!value) return E_POINTER;
        *value = size_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
class VisibilityArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          IVisibilityChangedEventArgs, ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.VisibilityChangedEventArgs", BaseTrust);
    boolean visible_, handled_ = false;

public:
    explicit VisibilityArgs(boolean visible) : visible_(visible) {}
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* value) override {
        if (!value) return E_POINTER;
        *value = visible_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
}
