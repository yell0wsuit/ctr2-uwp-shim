#include "generated/http_filters.h"
#include "generated/http.h"
#include "generated/network.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Web::Http;
using namespace ABI::Windows::Web::Http::Filters;
namespace ctr2 {
class Cache final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIHttpCacheControl, FtmBase> {
    InspectableClass(L"Windows.Web.Http.Filters.HttpCacheControl", BaseTrust);
    HttpCacheReadBehavior read_ = HttpCacheReadBehavior_Default;
    HttpCacheWriteBehavior write_ = HttpCacheWriteBehavior_Default;

public:
    HRESULT STDMETHODCALLTYPE get_ReadBehavior(HttpCacheReadBehavior* value) override {
        if (!value) return E_POINTER;
        *value = read_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ReadBehavior(HttpCacheReadBehavior value) override {
        read_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_WriteBehavior(HttpCacheWriteBehavior* value) override {
        if (!value) return E_POINTER;
        *value = write_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_WriteBehavior(HttpCacheWriteBehavior value) override {
        write_ = value;
        return S_OK;
    }
};
class Filter final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIHttpBaseProtocolFilter, DefaultIHttpFilter,
                                         ABI::Windows::Foundation::IClosable, FtmBase> {
    InspectableClass(L"Windows.Web.Http.Filters.HttpBaseProtocolFilter", BaseTrust);
    ComPtr<Cache> cache_ = Make<Cache>();
    boolean autoRedirect_ = true, ui_ = false, decompress_ = true, proxy_ = false;
    UINT32 connections_ = 6;

public:
#define BOOLEAN_PROPERTY(Name, field)                                                                                  \
    HRESULT STDMETHODCALLTYPE get_##Name(boolean* value) override {                                                    \
        if (!value) return E_POINTER;                                                                                  \
        *value = field;                                                                                                \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE put_##Name(boolean value) override {                                                     \
        field = value;                                                                                                 \
        return S_OK;                                                                                                   \
    }
    BOOLEAN_PROPERTY(AllowAutoRedirect, autoRedirect_)
    BOOLEAN_PROPERTY(AllowUI, ui_)
    BOOLEAN_PROPERTY(AutomaticDecompression, decompress_)
    BOOLEAN_PROPERTY(UseProxy, proxy_)
#undef BOOLEAN_PROPERTY
    HRESULT STDMETHODCALLTYPE get_MaxConnectionsPerServer(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = connections_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_MaxConnectionsPerServer(UINT32 value) override {
        connections_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CacheControl(IHttpCacheControl** value) override {
        if (!value) return E_POINTER;
        return cache_ ? cache_.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SendRequestAsync(
        IHttpRequestMessage*,
        __FIAsyncOperationWithProgress_2_Windows__CWeb__CHttp__CHttpResponseMessage_Windows__CWeb__CHttp__CHttpProgress**
            value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
class Client final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIHttpClient, ABI::Windows::Foundation::IClosable, FtmBase> {
    InspectableClass(L"Windows.Web.Http.HttpClient", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
class HttpFactory final : public Factory<DefaultIHttpClientFactory> {
    bool filter_;

public:
    explicit HttpFactory(bool filter)
        : Factory(filter ? L"Windows.Web.Http.Filters.HttpBaseProtocolFilter" : L"Windows.Web.Http.HttpClient"),
          filter_(filter) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        if (filter_) {
            auto filter = Make<Filter>();
            return filter ? filter.CopyTo(value) : E_OUTOFMEMORY;
        }
        auto client = Make<Client>();
        return client ? client.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Create(IHttpFilter*, IHttpClient** value) override {
        if (!value) return E_POINTER;
        auto client = Make<Client>();
        return client ? client.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class NetworkFactory final : public Factory<ABI::Windows::Networking::Connectivity::DefaultINetworkInformationStatics> {
    EventSource<ABI::Windows::Networking::Connectivity::INetworkStatusChangedEventHandler> changed_;

public:
    NetworkFactory() : Factory(L"Windows.Networking.Connectivity.NetworkInformation") {}
    HRESULT STDMETHODCALLTYPE
    GetInternetConnectionProfile(ABI::Windows::Networking::Connectivity::IConnectionProfile** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    add_NetworkStatusChanged(ABI::Windows::Networking::Connectivity::INetworkStatusChangedEventHandler* handler,
                             EventRegistrationToken* token) override {
        return changed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_NetworkStatusChanged(EventRegistrationToken token) override {
        return changed_.Remove(token);
    }
};
HRESULT MakeHttpFactory(bool filter, IActivationFactory** value) {
    auto factory = Make<HttpFactory>(filter);
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT MakeNetworkFactory(IActivationFactory** value) {
    static auto factory = Make<NetworkFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
