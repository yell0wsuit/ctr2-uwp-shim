#include "generated/storage.h"
#include "registry.h"
#include "settings.h"
#include "com/factory.h"
#include <roapi.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage;
namespace ctr2 {
namespace {
// Where saves live. CTR2SHIM_DATA wins (scripts and tests); otherwise
// SaveLocation in ctr2shim.ini beside the game executable:
//   game (default)  <game folder>\SaveData
//   documents       Documents\CutTheRope2UWP_SaveData
//   appdata         %LOCALAPPDATA%\ctr2, the original location
//   anything else   a folder; relative paths start at the game folder
std::filesystem::path GameDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    while ((length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))) == path.size())
        path.resize(path.size() * 2);
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}
std::wstring SaveLocationSetting() {
    wchar_t value[1024]{};
    const auto length = GetEnvironmentVariableW(L"CTR2SHIM_DATA", value, 1024);
    if (length > 0 && length < 1024) return value;
    GetPrivateProfileStringW(L"storage", L"SaveLocation", L"", value, 1024,
                             (GameDirectory() / L"ctr2shim.ini").c_str());
    std::wstring setting(value);
    setting.erase(0, setting.find_first_not_of(L" \t\""));
    setting.erase(setting.find_last_not_of(L" \t\"") + 1);
    return setting;
}
std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR path{};
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path))) return {};
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}
std::filesystem::path AppDataRoot() {
    // The environment variable first, so tests can point it elsewhere.
    wchar_t value[MAX_PATH]{};
    auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", value, MAX_PATH);
    auto base = length && length < MAX_PATH ? std::filesystem::path(value) : KnownFolder(FOLDERID_LocalAppData);
    return base.empty() ? base : base / L"ctr2";
}
std::filesystem::path DocumentsRoot() {
    auto base = KnownFolder(FOLDERID_Documents);
    return base.empty() ? base : base / L"CutTheRope2UWP_SaveData";
}
std::filesystem::path LocationPath(const std::wstring& setting) {
    if (setting.empty() || _wcsicmp(setting.c_str(), L"game") == 0) return GameDirectory() / L"SaveData";
    if (_wcsicmp(setting.c_str(), L"documents") == 0) return DocumentsRoot();
    if (_wcsicmp(setting.c_str(), L"appdata") == 0) return AppDataRoot();
    std::wstring expanded(32768, L'\0');
    auto length = ExpandEnvironmentStringsW(setting.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
    expanded.resize(length ? length - 1 : 0);
    std::filesystem::path path(expanded.empty() ? setting : expanded);
    return path.is_absolute() ? path : GameDirectory() / path;
}
// Creates the folders and proves the root is writable (a game folder under
// Program Files is not).
bool PrepareRoot(const std::filesystem::path& root) {
    if (root.empty()) return false;
    std::error_code error;
    for (const auto* name : {L"Local", L"Roaming", L"Temp"}) {
        std::filesystem::create_directories(root / name, error);
        if (error) return false;
    }
    HANDLE probe = CreateFileW((root / L"Local" / L".ctr2shim-write-test").c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (probe == INVALID_HANDLE_VALUE) return false;
    CloseHandle(probe);
    return true;
}
bool HasSettings(const std::filesystem::path& root) {
    std::error_code error;
    return std::filesystem::exists(root / L"Local" / L"settings.json", error) ||
           std::filesystem::exists(root / L"Local" / L"settings.json.journal", error);
}
// A new location starts with a copy of the saves from %LOCALAPPDATA%\ctr2,
// so switching keeps progress. The original files stay where they are.
void CopyLegacySaves(const std::filesystem::path& root) {
    auto legacy = AppDataRoot();
    std::error_code error;
    if (legacy.empty() || std::filesystem::equivalent(root, legacy, error) || HasSettings(root) || !HasSettings(legacy))
        return;
    for (const auto* name : {L"Local", L"Roaming"}) {
        std::filesystem::copy(legacy / name, root / name,
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing,
                              error);
        if (error) {
            Log("FAIL", "copying saves from %ls failed: %s", legacy.c_str(), error.message().c_str());
            return;
        }
    }
    Log("INFO", "copied saves from %ls to %ls", legacy.c_str(), root.c_str());
}
HRESULT ResolveDataRoot(std::filesystem::path& root) {
    const auto setting = SaveLocationSetting();
    std::vector<std::filesystem::path> candidates{LocationPath(setting), DocumentsRoot(), AppDataRoot()};
    for (auto candidate : candidates) {
        // The game joins file names onto these paths; keep them in one style.
        std::error_code error;
        if (!candidate.empty()) candidate = std::filesystem::absolute(candidate, error).lexically_normal();
        if (!PrepareRoot(candidate)) {
            Log("FAIL", "save location %ls is not writable", candidate.c_str());
            continue;
        }
        CopyLegacySaves(candidate);
        Log("INFO", "saves: %ls", candidate.c_str());
        root = std::move(candidate);
        return S_OK;
    }
    return E_ACCESSDENIED;
}
}
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
        std::filesystem::path root;
        HRESULT hr{};
        try {
            hr = ResolveDataRoot(root);
            if (FAILED(hr)) return hr;
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
