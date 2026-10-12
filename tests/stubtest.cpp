#include <windows.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include "stubs/vungle.h"
#include <windows.web.http.filters.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"VungleSDK.AdFactory").Get(), &factory))) return 2;
    ComPtr<ctr2::vungle::IAdFactoryStatic> statics;
    if (FAILED(factory.As(&statics))) return 3;
    ComPtr<ctr2::vungle::IVungleAd> ad;
    if (FAILED(statics->GetInstance(HStringReference(L"test").Get(), &ad)) || !ad) return 4;
    boolean available = true;
    if (FAILED(ad->get_AdPlayable(&available)) || available) return 5;
    ComPtr<ABI::Windows::Foundation::IAsyncOperation<HSTRING>> operation;
    if (FAILED(ad->PlayAdAsync(nullptr, &operation)) || !operation) return 6;
    HString result;
    if (SUCCEEDED(operation->GetResults(result.GetAddressOf()))) return 7;
    ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
    operation.As(&info);
    ABI::Windows::Foundation::AsyncStatus status;
    if (!info || FAILED(info->get_Status(&status)) || status != ABI::Windows::Foundation::AsyncStatus::Error) return 8;
    if (FAILED(get(HStringReference(L"Windows.Web.Http.Filters.HttpBaseProtocolFilter").Get(), &factory))) return 9;
    ComPtr<IInspectable> filter;
    if (FAILED(factory->ActivateInstance(&filter))) return 10;
    ComPtr<ABI::Windows::Web::Http::Filters::IHttpBaseProtocolFilter> base;
    if (FAILED(filter.As(&base))) return 11;
    ComPtr<ABI::Windows::Web::Http::Filters::IHttpCacheControl> cache;
    if (FAILED(base->get_CacheControl(&cache)) || !cache) return 12;
    puts("stubtest: ads unavailable; playback fails");
    return 0;
}
