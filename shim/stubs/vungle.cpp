#include "vungle.h"
#include "com/error_operation.h"
#include "com/factory.h"
#include "registry.h"
#include <map>
#include <mutex>
using namespace Microsoft::WRL;
using namespace ctr2::vungle;
namespace ctr2 {
class Ad final : public RuntimeClass<RuntimeClassFlags<WinRt>, IVungleAd, FtmBase> {
    InspectableClass(L"VungleSDK.VungleAd", BaseTrust);
    std::mutex mutex_;
    INT64 next_ = 0;
    std::map<INT64, ComPtr<IUnknown>> events_;
    HRESULT Add(IUnknown* handler, EventRegistrationToken* token) {
        if (!handler || !token) return E_INVALIDARG;
        std::lock_guard<std::mutex> lock(mutex_);
        token->value = ++next_;
        events_[token->value] = handler;
        return S_OK;
    }
    HRESULT Remove(EventRegistrationToken token) {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.erase(token.value);
        return S_OK;
    }

public:
#define AD_EVENT(Name)                                                                                                 \
    HRESULT STDMETHODCALLTYPE add_##Name(IUnknown* handler, EventRegistrationToken* token) override {                  \
        return Add(handler, token);                                                                                    \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE remove_##Name(EventRegistrationToken token) override { return Remove(token); }
    AD_EVENT(Diagnostic)
    AD_EVENT(OnAdEnd)
    AD_EVENT(OnAdPlayableChanged)
    AD_EVENT(OnAdStart)
    AD_EVENT(OnVideoView)
#undef AD_EVENT
    HRESULT STDMETHODCALLTYPE get_AdPlayable(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PlayAdAsync(IInspectable*,
                                          ABI::Windows::Foundation::IAsyncOperation<HSTRING>** value) override {
        if (!value) return E_POINTER;
        auto operation = Make<ErrorOperation<HSTRING>>();
        return operation ? operation.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class AdFactory final : public Factory<IAdFactoryStatic> {
public:
    AdFactory() : Factory(L"VungleSDK.AdFactory") {}
    HRESULT STDMETHODCALLTYPE GetInstance(HSTRING, IVungleAd** value) override {
        if (!value) return E_POINTER;
        static auto ad = Make<Ad>();
        return ad ? ad.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetInstanceWithConfig(HSTRING id, IInspectable*, IVungleAd** value) override {
        return GetInstance(id, value);
    }
};
HRESULT MakeAdFactory(IActivationFactory** value) {
    auto factory = Make<AdFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
