#include <windows.h>
#include <roapi.h>
#include <windows.system.display.h>
#include <windows.security.exchangeactivesyncprovisioning.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::System::Display;
using namespace ABI::Windows::Security::ExchangeActiveSyncProvisioning;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    auto hr = get(HStringReference(L"Windows.System.Display.DisplayRequest").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL DisplayRequest factory hr=%08lx\n", hr);
        return 2;
    }
    if (factory->ActivateInstance(nullptr) != E_POINTER) return 3;
    ComPtr<IInspectable> first, second;
    if (FAILED(factory->ActivateInstance(&first)) || FAILED(factory->ActivateInstance(&second))) return 4;
    ComPtr<IDisplayRequest> request, other;
    if (FAILED(first.As(&request)) || FAILED(second.As(&other))) return 5;
    if (request->RequestRelease() != E_ILLEGAL_METHOD_CALL || FAILED(request->RequestActive()) ||
        FAILED(request->RequestActive()))
        return 6;
    if (other->RequestRelease() != E_ILLEGAL_METHOD_CALL) return 7;
    if (FAILED(request->RequestRelease()) || FAILED(request->RequestRelease()) ||
        request->RequestRelease() != E_ILLEGAL_METHOD_CALL)
        return 8;
    if (FAILED(other->RequestActive()) || FAILED(other->RequestRelease())) return 9;
    if (FAILED(
            get(HStringReference(L"Windows.Security.ExchangeActiveSyncProvisioning.EasClientDeviceInformation").Get(),
                &factory)))
        return 10;
    GUID original{};
    const GUID expected{0x63747232, 0x7769, 0x6e65, {0x80, 0, 0, 0, 0, 0, 0, 1}};
    for (int repeat = 0; repeat < 2; ++repeat) {
        ComPtr<IInspectable> instance;
        if (FAILED(factory->ActivateInstance(&instance)) || !instance) return 11;
        ComPtr<IEasClientDeviceInformation> info;
        if (FAILED(instance.As(&info))) return 12;
        GUID id{};
        if (info->get_Id(nullptr) != E_POINTER || FAILED(info->get_Id(&id)) || id != expected) return 13;
        if (repeat && id != original) return 14;
        original = id;
        HString value;
        if (info->get_OperatingSystem(nullptr) != E_POINTER ||
            FAILED(info->get_OperatingSystem(value.GetAddressOf())) ||
            wcscmp(value.GetRawBuffer(nullptr), L"Windows") != 0)
            return 15;
        if (info->get_FriendlyName(nullptr) != E_POINTER || FAILED(info->get_FriendlyName(value.GetAddressOf())) ||
            wcscmp(value.GetRawBuffer(nullptr), L"ctr2-wine") != 0)
            return 16;
        if (info->get_SystemManufacturer(nullptr) != E_POINTER ||
            FAILED(info->get_SystemManufacturer(value.GetAddressOf())) ||
            wcscmp(value.GetRawBuffer(nullptr), L"ctr2-wine") != 0)
            return 17;
        if (info->get_SystemProductName(nullptr) != E_POINTER ||
            FAILED(info->get_SystemProductName(value.GetAddressOf())) ||
            wcscmp(value.GetRawBuffer(nullptr), L"Compatibility Device") != 0)
            return 18;
        if (info->get_SystemSku(nullptr) != E_POINTER || FAILED(info->get_SystemSku(value.GetAddressOf())) ||
            wcscmp(value.GetRawBuffer(nullptr), L"offline") != 0)
            return 19;
    }
    puts("devicetest: 0 failure(s)");
    return 0;
}
