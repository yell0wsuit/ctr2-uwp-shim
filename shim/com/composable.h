#pragma once
#include <inspectable.h>
#include <winstring.h>
#include <atomic>
#include <new>
#include "log.h"
namespace ctr2 {
// Type probes whose E_NOINTERFACE is the expected answer: ANGLE asking a
// SwapChainPanel for ICoreWindow before trying ISwapChainPanel, and the Loaded
// walker asking elements for IContentControl/IUserControl to find children.
inline bool ExpectedProbe(REFIID iid) {
    static const IID coreWindow = {0x79b9d5f2, 0x879e, 0x4b89, {0xb7, 0x98, 0x79, 0xe4, 0x75, 0x98, 0x03, 0x0c}};
    static const IID contentControl = {0xa26dd1dc, 0xcd44, 0x435c, {0xbe, 0x94, 0x01, 0xd6, 0x24, 0x1c, 0x23, 0x1c}};
    static const IID userControl = {0xa7a69ec9, 0xea35, 0x4679, {0xbf, 0x29, 0xf4, 0xf0, 0x92, 0x86, 0xd3, 0x14}};
    return iid == coreWindow || iid == contentControl || iid == userControl;
}
// Inner owns the implementation. The outer pointer is borrowed, never retained.
// Returned ordinary interfaces delegate IUnknown to the controlling outer.
template <class Derived, class Primary, class... Interfaces> class Composable : public Primary, public Interfaces... {
    class Inner final : public IInspectable {
        Composable* owner_;
        std::atomic<ULONG> refs_{1};

    public:
        explicit Inner(Composable* owner) : owner_(owner) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
            return owner_->InnerQuery(iid, value);
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
        ULONG STDMETHODCALLTYPE Release() override {
            auto refs = --refs_;
            if (!refs) delete static_cast<Derived*>(owner_);
            return refs;
        }
        HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** ids) override { return owner_->GetIids(count, ids); }
        HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
            return owner_->GetRuntimeClassName(value);
        }
        HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override { return owner_->GetTrustLevel(value); }
    } inner_{this};
    IInspectable* outer_;

protected:
    // Element<Derived> sits between this and Derived, so friend Derived would not suffice.
    // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
    explicit Composable(IInspectable* outer) : outer_(outer) {}

public:
    IInspectable* InnerUnknown() { return &inner_; }
    HRESULT InnerQuery(REFIID iid, void** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable)) {
            *value = &inner_;
            inner_.AddRef();
            return S_OK;
        }
        if (iid == __uuidof(Primary)) *value = static_cast<Primary*>(this);
        (void)((iid == __uuidof(Interfaces) ? (*value = static_cast<Interfaces*>(this), true) : false) || ...);
        if (!*value) {
            Log(ExpectedProbe(iid) ? "TRACE" : "FAIL", "%ls QI missing %08lx-%04x-%04x", Derived::Name, iid.Data1,
                iid.Data2, iid.Data3);
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        return outer_ ? outer_->QueryInterface(iid, value) : InnerQuery(iid, value);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return outer_ ? outer_->AddRef() : inner_.AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return outer_ ? outer_->Release() : inner_.Release(); }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** ids) override {
        if (!count || !ids) return E_POINTER;
        *count = 0;
        *ids = nullptr;
        const IID list[] = {__uuidof(Primary), __uuidof(Interfaces)...};
        auto* memory = static_cast<IID*>(CoTaskMemAlloc(sizeof(list)));
        if (!memory) return E_OUTOFMEMORY;
        memcpy(memory, list, sizeof(list));
        *ids = memory;
        *count = sizeof...(Interfaces) + 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
        if (!value) return E_POINTER;
        const wchar_t* name = Derived::Name;
        return WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), value);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }
};

template <class Object, class Interface> HRESULT Compose(IInspectable* outer, IInspectable** inner, Interface** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (inner) *inner = nullptr;
    if (outer && !inner) return E_INVALIDARG;
    auto object = new (std::nothrow) Object(outer);
    if (!object) return E_OUTOFMEMORY;
    *value = static_cast<Interface*>(object);
    (*value)->AddRef();
    if (inner)
        *inner = object->InnerUnknown();
    else
        object->InnerUnknown()->Release();
    return S_OK;
}
}
