#include "generated/storage.h"
#include "registry.h"
#include "settings.h"
#include "com/factory.h"
#include <roapi.h>
#include <shlobj.h>
#include <filesystem>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage;
namespace ctr2 {
class Folder final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIStorageFolder, DefaultIStorageItem, FtmBase> {
    InspectableClass(L"Windows.Storage.StorageFolder", BaseTrust);
    std::wstring path_;

public:
    explicit Folder(std::wstring path) : path_(std::move(path)) {}
    HRESULT STDMETHODCALLTYPE get_Path(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(path_.data(), static_cast<UINT32>(path_.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        auto name = std::filesystem::path(path_).filename().wstring();
        return WindowsCreateString(name.data(), static_cast<UINT32>(name.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_Attributes(ABI::Windows::Storage::FileAttributes* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::Storage::FileAttributes_Directory;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DateCreated(ABI::Windows::Foundation::DateTime* value) override {
        if (!value) return E_POINTER;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data))
            return HRESULT_FROM_WIN32(GetLastError());
        value->UniversalTime =
            (static_cast<INT64>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsOfType(StorageItemTypes type, boolean* value) override {
        if (!value) return E_POINTER;
        *value = (type & StorageItemTypes_Folder) != 0;
        return S_OK;
    }
};
class Container final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIApplicationDataContainer, FtmBase> {
    InspectableClass(L"Windows.Storage.ApplicationDataContainer", BaseTrust);
    ApplicationDataLocality locality_;
    ComPtr<ABI::Windows::Foundation::Collections::IPropertySet> values_;
    std::shared_ptr<SettingsPersistence> persistence_;

public:
    explicit Container(ApplicationDataLocality locality) : locality_(locality) {}
    HRESULT Initialize(const std::wstring& path) { return CreatePersistentSettings(path, values_, persistence_); }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Locality(ApplicationDataLocality* value) override {
        if (!value) return E_POINTER;
        *value = locality_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Values(ABI::Windows::Foundation::Collections::IPropertySet** value) override {
        if (!value) return E_POINTER;
        return values_.CopyTo(value);
    }
};
class AppData final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIApplicationData, FtmBase> {
    InspectableClass(L"Windows.Storage.ApplicationData", BaseTrust);
    ComPtr<Folder> local_, roaming_, temporary_;
    ComPtr<Container> localSettings_, roamingSettings_;
    EventSource<__FITypedEventHandler_2_Windows__CStorage__CApplicationData_IInspectable> changed_;

public:
    HRESULT Initialize() {
        PWSTR path{};
        auto hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path);
        if (FAILED(hr)) return hr;
        std::filesystem::path root(path);
        CoTaskMemFree(path);
        root /= L"ctr2";
        try {
            for (auto name : {L"Local", L"Roaming", L"Temp"}) std::filesystem::create_directories(root / name);
            local_ = Make<Folder>((root / L"Local").wstring());
            roaming_ = Make<Folder>((root / L"Roaming").wstring());
            temporary_ = Make<Folder>((root / L"Temp").wstring());
        } catch (...) {
            return E_FAIL;
        }
        localSettings_ = Make<Container>(ApplicationDataLocality_Local);
        roamingSettings_ = Make<Container>(ApplicationDataLocality_Roaming);
        if (!local_ || !roaming_ || !temporary_ || !localSettings_ || !roamingSettings_) return E_OUTOFMEMORY;
        hr = localSettings_->Initialize((root / L"Local" / L"settings.json").wstring());
        return FAILED(hr) ? hr : roamingSettings_->Initialize((root / L"Roaming" / L"settings.json").wstring());
    }
    HRESULT STDMETHODCALLTYPE get_Version(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LocalFolder(IStorageFolder** value) override {
        if (!value) return E_POINTER;
        Log("TRACE", "ApplicationData.LocalFolder");
        return local_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_RoamingFolder(IStorageFolder** value) override {
        if (!value) return E_POINTER;
        return roaming_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_TemporaryFolder(IStorageFolder** value) override {
        if (!value) return E_POINTER;
        return temporary_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_LocalSettings(IApplicationDataContainer** value) override {
        if (!value) return E_POINTER;
        Log("TRACE", "ApplicationData.LocalSettings");
        return localSettings_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_RoamingSettings(IApplicationDataContainer** value) override {
        if (!value) return E_POINTER;
        return roamingSettings_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_RoamingStorageQuota(UINT64* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    add_DataChanged(__FITypedEventHandler_2_Windows__CStorage__CApplicationData_IInspectable* handler,
                    EventRegistrationToken* token) override {
        return changed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DataChanged(EventRegistrationToken token) override {
        return changed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE SignalDataChanged() override { return changed_.InvokeAll(this, nullptr); }
};
class StorageFactory final : public Factory<IApplicationDataStatics> {
    ComPtr<AppData> data_;
    HRESULT initialized_ = E_UNEXPECTED;

public:
    StorageFactory() : Factory(L"Windows.Storage.ApplicationData"), data_(Make<AppData>()) {
        initialized_ = data_ ? data_->Initialize() : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_Current(IApplicationData** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return FAILED(initialized_) ? initialized_ : data_.CopyTo(value);
    }
};
HRESULT MakeStorageFactory(IActivationFactory** value) {
    static auto factory = Make<StorageFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
