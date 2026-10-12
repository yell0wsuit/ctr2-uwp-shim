#include "generated/display_request.h"
#include "generated/device_info.h"
#include "registry.h"
#include "com/factory.h"
#include <mutex>
#include <limits>
using namespace Microsoft::WRL;
using namespace ABI::Windows::System::Display;
using namespace ABI::Windows::Security::ExchangeActiveSyncProvisioning;
namespace ctr2 {
namespace {
constexpr wchar_t displayClass[] = L"Windows.System.Display.DisplayRequest";
constexpr wchar_t deviceClass[] = L"Windows.Security.ExchangeActiveSyncProvisioning.EasClientDeviceInformation";
class DisplayRequest final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIDisplayRequest, FtmBase> {
    InspectableClass(L"Windows.System.Display.DisplayRequest", BaseTrust);
    std::mutex mutex_;
    UINT32 requests_ = 0;

public:
    HRESULT STDMETHODCALLTYPE RequestActive() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (requests_ == std::numeric_limits<UINT32>::max()) return E_ILLEGAL_METHOD_CALL;
        ++requests_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestRelease() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!requests_) return E_ILLEGAL_METHOD_CALL;
        --requests_;
        return S_OK;
    }
};
HRESULT Text(const wchar_t* text, HSTRING* value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return WindowsCreateString(text, static_cast<UINT32>(wcslen(text)), value);
}
class DeviceInformation final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIEasClientDeviceInformation, FtmBase> {
    InspectableClass(L"Windows.Security.ExchangeActiveSyncProvisioning.EasClientDeviceInformation", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_Id(GUID* value) override {
        if (!value) return E_POINTER;
        // Fixed synthetic compatibility identity, independent of host/user.
        *value = {0x63747232, 0x7769, 0x6e65, {0x80, 0, 0, 0, 0, 0, 0, 1}};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_OperatingSystem(HSTRING* value) override { return Text(L"Windows", value); }
    HRESULT STDMETHODCALLTYPE get_FriendlyName(HSTRING* value) override { return Text(L"ctr2-wine", value); }
    HRESULT STDMETHODCALLTYPE get_SystemManufacturer(HSTRING* value) override { return Text(L"ctr2-wine", value); }
    HRESULT STDMETHODCALLTYPE get_SystemProductName(HSTRING* value) override {
        return Text(L"Compatibility Device", value);
    }
    HRESULT STDMETHODCALLTYPE get_SystemSku(HSTRING* value) override { return Text(L"offline", value); }
};
class DeviceFactory final : public Factory<> {
    bool display_;

public:
    explicit DeviceFactory(bool display) : Factory(display ? displayClass : deviceClass), display_(display) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (display_) {
            auto request = Make<DisplayRequest>();
            return request ? request.CopyTo(value) : E_OUTOFMEMORY;
        }
        auto device = Make<DeviceInformation>();
        return device ? device.CopyTo(value) : E_OUTOFMEMORY;
    }
};
}
HRESULT MakeDeviceFactory(bool display, IActivationFactory** value) {
    auto factory = Make<DeviceFactory>(display);
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
