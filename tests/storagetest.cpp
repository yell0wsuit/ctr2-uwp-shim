#include <windows.h>
#include <roapi.h>
#include <windows.storage.h>
#include <windows.foundation.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <filesystem>
#include <string>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage;
void FreeArrayResult(ABI::Windows::Foundation::IPropertyValue* value, void* bytes) {
    // Wine wintypes returns borrowed storage; releasing it corrupts the native object.
    HMODULE module{};
    if (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(*reinterpret_cast<void***>(value)), &module) &&
        module == GetModuleHandleW(L"wintypes.dll"))
        return;
    CoTaskMemFree(bytes);
}
std::string ReadAll(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string text(GetFileSize(file, nullptr), 0);
    DWORD read{};
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) read = 0;
    CloseHandle(file);
    text.resize(read);
    return text;
}
bool WriteText(const std::filesystem::path& path, const char* text, DWORD disposition = OPEN_ALWAYS) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    HANDLE file = CreateFileW(path.c_str(), disposition == OPEN_ALWAYS ? FILE_APPEND_DATA : GENERIC_WRITE,
                              FILE_SHARE_READ, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written{}, size = static_cast<DWORD>(strlen(text));
    const bool ok = WriteFile(file, text, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return ok;
}
std::filesystem::path ExeDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}
std::filesystem::path EnvironmentPath(const wchar_t* name) {
    wchar_t value[MAX_PATH]{};
    auto length = GetEnvironmentVariableW(name, value, MAX_PATH);
    return length && length < MAX_PATH ? std::filesystem::absolute(value).lexically_normal() : std::filesystem::path();
}
int main(int argc, char** argv) {
    RoInitialize(RO_INIT_MULTITHREADED);
    const char* mode = argc > 1 ? argv[1] : "";
    const bool torn = !strcmp(mode, "torn");
    // Location tests run without CTR2SHIM_DATA and with LOCALAPPDATA pointing
    // into the build tree; every other mode keeps its saves in CTR2SHIM_DATA.
    const bool location = !strcmp(mode, "location"), locationIni = !strcmp(mode, "location_ini");
    std::filesystem::path root = location      ? ExeDirectory() / L"SaveData"
                                 : locationIni ? ExeDirectory() / L"ini-saves"
                                               : EnvironmentPath(L"CTR2SHIM_DATA");
    if (root.empty()) return 70;
    const auto ini = ExeDirectory() / L"ctr2shim.ini";
    if (location || locationIni) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::remove(ini, error);
        // Saves from the original location are copied into a new one.
        auto legacy = EnvironmentPath(L"LOCALAPPDATA") / L"ctr2" / L"Local" / L"settings.json";
        if (!WriteText(legacy, "[\n{\"key\":\"__ctr2shim_test_moved\",\"type\":\"String\",\"value\":\"yes\"}\n]\n",
                       CREATE_ALWAYS))
            return 71;
        if (locationIni && !WriteText(ini, "[storage]\r\nSaveLocation = ini-saves\r\n", CREATE_ALWAYS)) return 72;
    }
    // A crash can cut the last journal record short. Loading keeps the
    // complete records before it and drops the partial one.
    if (torn && !WriteText(root / L"Local" / L"settings.json.journal",
                           "{\"key\":\"__ctr2shim_test_torn\",\"type\":\"String\",\"value\":\"ok\"}\n"
                           "{\"key\":\"__ctr2shim_te"))
        return 61;
    // The store keeps its journal open after its first change, so the atomic
    // test seeds its key through the journal and locks it before any change.
    if (!strcmp(mode, "atomic")) {
        for (const auto* store : {L"Local", L"Roaming"})
            if (!WriteText(root / store / L"settings.json.journal",
                           "{\"key\":\"__ctr2shim_test_atomic\",\"type\":\"Boolean\",\"value\":\"true\"}\n"))
                return 62;
    }
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"Windows.Storage.ApplicationData").Get(), &factory))) return 2;
    ComPtr<IApplicationDataStatics> statics;
    if (FAILED(factory.As(&statics))) return 3;
    ComPtr<IApplicationData> data;
    if (FAILED(statics->get_Current(&data))) return 4;
    ComPtr<IStorageFolder> folder;
    if (FAILED(data->get_LocalFolder(&folder))) return 5;
    ComPtr<IStorageItem> item;
    if (FAILED(folder.As(&item))) return 6;
    HString path;
    if (FAILED(item->get_Path(path.GetAddressOf()))) return 7;
    std::wstring text(path.GetRawBuffer(nullptr));
    if (text != (root / L"Local").wstring()) {
        printf("LocalFolder %ls, expected %ls\n", text.c_str(), (root / L"Local").c_str());
        return 8;
    }
    if (location || locationIni) {
        ComPtr<IApplicationDataContainer> container;
        ComPtr<ABI::Windows::Foundation::Collections::IPropertySet> set;
        ComPtr<ABI::Windows::Foundation::Collections::IMap<HSTRING, IInspectable*>> map;
        ComPtr<IInspectable> value;
        ComPtr<ABI::Windows::Foundation::IPropertyValue> property;
        HString moved;
        if (FAILED(data->get_LocalSettings(&container)) || FAILED(container->get_Values(&set)) ||
            FAILED(set.As(&map)) || FAILED(map->Lookup(HStringReference(L"__ctr2shim_test_moved").Get(), &value)) ||
            FAILED(value.As(&property)) || FAILED(property->GetString(moved.GetAddressOf())) ||
            wcscmp(moved.GetRawBuffer(nullptr), L"yes") != 0)
            return 73;
        std::error_code error;
        if (!std::filesystem::exists(EnvironmentPath(L"LOCALAPPDATA") / L"ctr2" / L"Local" / L"settings.json", error))
            return 74; // Copied, not moved.
        std::filesystem::remove(ini, error);
        return 0;
    }
    if (!(GetFileAttributesW(text.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) return 9;
    std::wstring file = text + L"\\shim-test.tmp";
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return 10;
    DWORD written{};
    const char bytes[] = "roundtrip";
    if (!WriteFile(handle, bytes, sizeof(bytes), &written, nullptr)) return 11;
    CloseHandle(handle);
    handle = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return 12;
    char restored[sizeof(bytes)]{};
    DWORD read{};
    ReadFile(handle, restored, sizeof(restored), &read, nullptr);
    CloseHandle(handle);
    DeleteFileW(file.c_str());
    if (read != sizeof(bytes) || memcmp(restored, bytes, sizeof(bytes)) != 0) return 13;
    ComPtr<ABI::Windows::Foundation::IPropertyValueStatics> props;
    if (FAILED(
            RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(), IID_PPV_ARGS(&props))))
        return 14;
    for (bool roaming : {false, true}) {
        ComPtr<IApplicationDataContainer> container;
        if (FAILED(roaming ? data->get_RoamingSettings(&container) : data->get_LocalSettings(&container))) return 15;
        ComPtr<ABI::Windows::Foundation::Collections::IPropertySet> set;
        container->get_Values(&set);
        ComPtr<ABI::Windows::Foundation::Collections::IMap<HSTRING, IInspectable*>> map;
        if (FAILED(set.As(&map))) return 16;
        const wchar_t payload[] = {roaming ? L'R' : L'V', 0, L'ü', L'中', 0xd83d, 0xde00};
        HString expected;
        expected.Set(payload, 6);
        const wchar_t* keys[] = {
            L"__ctr2shim_test_string", L"__ctr2shim_test_bool",   L"__ctr2shim_test_i32",
            L"__ctr2shim_test_i64",    L"__ctr2shim_test_double", L"__ctr2shim_test_bytes",
        };
        ComPtr<ABI::Windows::Foundation::Collections::IObservableMap<HSTRING, IInspectable*>> observable;
        if (FAILED(set.As(&observable))) return 29;
        ComPtr<IUnknown> setIdentity, mapIdentity, observableIdentity;
        set.As(&setIdentity);
        map.As(&mapIdentity);
        observable.As(&observableIdentity);
        if (setIdentity.Get() != mapIdentity.Get() || setIdentity.Get() != observableIdentity.Get()) return 30;
        unsigned events{};
        auto handler = Callback<ABI::Windows::Foundation::Collections::MapChangedEventHandler<HSTRING, IInspectable*>>(
            [&](ABI::Windows::Foundation::Collections::IObservableMap<HSTRING, IInspectable*>* sender,
                ABI::Windows::Foundation::Collections::IMapChangedEventArgs<HSTRING>* args) -> HRESULT {
                if (sender != observable.Get() || !args) return E_FAIL;
                HString eventKey;
                if (FAILED(args->get_Key(eventKey.GetAddressOf()))) return E_FAIL;
                UINT32 size{};
                if (FAILED(map->get_Size(&size))) return E_FAIL; // Event callbacks may reenter the map.
                ++events;
                return S_OK;
            });
        EventRegistrationToken token{};
        if (FAILED(observable->add_MapChanged(handler.Get(), &token))) return 31;
        const std::wstring settingsFile =
            text.substr(0, text.size() - 5) + (roaming ? L"Roaming" : L"Local") + L"\\settings.json";
        if (torn) {
            if (!roaming) {
                ComPtr<IInspectable> value;
                ComPtr<ABI::Windows::Foundation::IPropertyValue> property;
                HString restoredText;
                if (FAILED(map->Lookup(HStringReference(L"__ctr2shim_test_torn").Get(), &value)) ||
                    FAILED(value.As(&property)) || FAILED(property->GetString(restoredText.GetAddressOf())) ||
                    wcscmp(restoredText.GetRawBuffer(nullptr), L"ok") != 0)
                    return 63;
                if (!ReadAll(settingsFile + L".journal").empty()) return 64; // Replayed, then compacted.
                if (FAILED(map->Remove(HStringReference(L"__ctr2shim_test_torn").Get()))) return 65;
            }
            if (FAILED(observable->remove_MapChanged(token))) return 66;
            continue;
        }
        if (argc > 1 && !strcmp(argv[1], "burst")) {
            // Saving progress writes each of the game's prefs from its render
            // thread. A change appends to the journal; it must not rewrite
            // settings.json, whose size grows with every stored key.
            const auto snapshot = ReadAll(settingsFile);
            const auto start = GetTickCount64();
            constexpr int count = 300;
            for (int i = 0; i < count; ++i) {
                std::wstring key = L"__ctr2shim_test_burst_" + std::to_wstring(i);
                ComPtr<IInspectable> value;
                props->CreateInt64(i, &value);
                boolean replaced{};
                if (FAILED(map->Insert(HStringReference(key.c_str()).Get(), value.Get(), &replaced))) return 50;
            }
            printf("burst: %d inserts in %llu ms\n", count, GetTickCount64() - start);
            if (ReadAll(settingsFile) != snapshot) return 51;
            const std::wstring journalFile = settingsFile + L".journal";
            const auto journal = ReadAll(journalFile);
            if (journal.empty()) return 52;
            // Printable ASCII is stored as itself rather than as \u escapes.
            if (journal.find("\"__ctr2shim_test_burst_0\"") == std::string::npos) return 56;
            // A save stores every pref again, most of them unchanged. An equal
            // value still replaces and notifies, but adds nothing to the journal.
            const auto eventCount = events;
            for (int i = 0; i < count; ++i) {
                std::wstring key = L"__ctr2shim_test_burst_" + std::to_wstring(i);
                ComPtr<IInspectable> value;
                props->CreateInt64(i, &value);
                boolean replaced{};
                if (FAILED(map->Insert(HStringReference(key.c_str()).Get(), value.Get(), &replaced)) || !replaced)
                    return 57;
            }
            if (ReadAll(journalFile) != journal || events != eventCount + count) return 58;
            // The type is part of the value: Int32 0 differs from Int64 0, both ways.
            HStringReference first(L"__ctr2shim_test_burst_0");
            ComPtr<IInspectable> narrow, wide;
            props->CreateInt32(0, &narrow);
            props->CreateInt64(0, &wide);
            boolean replaced{};
            if (FAILED(map->Insert(first.Get(), narrow.Get(), &replaced))) return 59;
            const auto changed = ReadAll(journalFile);
            if (changed.size() <= journal.size()) return 59;
            if (FAILED(map->Insert(first.Get(), wide.Get(), &replaced)) ||
                ReadAll(journalFile).size() <= changed.size())
                return 60;
            for (int i = 0; i < count; ++i) {
                std::wstring key = L"__ctr2shim_test_burst_" + std::to_wstring(i);
                ComPtr<IInspectable> value;
                ComPtr<ABI::Windows::Foundation::IPropertyValue> property;
                INT64 number{};
                if (FAILED(map->Lookup(HStringReference(key.c_str()).Get(), &value)) || FAILED(value.As(&property)) ||
                    FAILED(property->GetInt64(&number)) || number != i)
                    return 53;
                if (FAILED(map->Remove(HStringReference(key.c_str()).Get()))) return 54;
            }
            if (FAILED(observable->remove_MapChanged(token))) return 55;
            continue;
        }
        if (argc > 1 && !strcmp(argv[1], "atomic")) {
            HStringReference key(L"__ctr2shim_test_atomic"), newKey(L"__ctr2shim_test_atomic_new");
            ComPtr<IInspectable> original, replacement;
            props->CreateBoolean(true, &original);
            props->CreateBoolean(false, &replacement);
            boolean replaced{}, seeded{};
            // Loaded from the seeded journal, which loading then compacted away.
            if (FAILED(map->HasKey(key.Get(), &seeded)) || !seeded) return 35;
            // A change that cannot be journaled fails and leaves memory unchanged.
            const std::wstring journal = settingsFile + L".journal";
            HANDLE locked = CreateFileW(journal.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
            if (locked == INVALID_HANDLE_VALUE) return 36;
            struct CloseLocked {
                HANDLE h;
                ~CloseLocked() { CloseHandle(h); }
            } close{locked};
            DWORD size = GetFileSize(locked, nullptr), lockedRead{};
            std::string before(size, 0), after(size, 0);
            if (!ReadFile(locked, before.data(), size, &lockedRead, nullptr) || lockedRead != size) return 37;
            const auto eventCount = events;
            if (SUCCEEDED(map->Insert(key.Get(), replacement.Get(), &replaced))) return 38;
            ComPtr<IInspectable> current;
            ComPtr<ABI::Windows::Foundation::IPropertyValue> property;
            boolean actual{};
            if (FAILED(map->Lookup(key.Get(), &current)) || FAILED(current.As(&property)) ||
                FAILED(property->GetBoolean(&actual)) || !actual) {
                puts("failed Insert changed memory");
                return 39;
            }
            if (SUCCEEDED(map->Insert(newKey.Get(), replacement.Get(), &replaced))) return 40;
            boolean found{};
            if (FAILED(map->HasKey(newKey.Get(), &found)) || found) return 41;
            if (SUCCEEDED(map->Remove(key.Get())) || FAILED(map->HasKey(key.Get(), &found)) || !found) return 42;
            UINT32 countBefore{}, countAfter{};
            map->get_Size(&countBefore);
            if (SUCCEEDED(map->Clear()) || FAILED(map->get_Size(&countAfter)) || countAfter != countBefore) return 43;
            SetFilePointer(locked, 0, nullptr, FILE_BEGIN);
            if (GetFileSize(locked, nullptr) != size || !ReadFile(locked, after.data(), size, &lockedRead, nullptr) ||
                lockedRead != size || after != before || events != eventCount)
                return 44;
            CloseHandle(close.h);
            close.h = INVALID_HANDLE_VALUE;
            if (FAILED(map->Remove(key.Get()))) return 45;
            if (FAILED(observable->remove_MapChanged(token))) return 46;
            continue;
        }
        for (int i = 0; i < 6; ++i) {
            HString key;
            key.Set(keys[i]);
            if (i == 0) {
                const wchar_t special[] = L"__ctr2shim_test_string\0中";
                key.Set(special, static_cast<UINT32>(sizeof(special) / sizeof(wchar_t) - 1));
            }
            if (argc > 1 && !strcmp(argv[1], "write")) {
                ComPtr<IInspectable> value;
                HRESULT hr = E_FAIL;
                BYTE arrayBytes[] = {0, 128, 255};
                switch (i) {
                case 0:
                    hr = props->CreateString(expected.Get(), &value);
                    break;
                case 1:
                    hr = props->CreateBoolean(true, &value);
                    break;
                case 2:
                    hr = props->CreateInt32(-123456, &value);
                    break;
                case 3:
                    hr = props->CreateInt64(-9007199254740993LL, &value);
                    break;
                case 4:
                    hr = props->CreateDouble(1.2345678901234567, &value);
                    break;
                case 5:
                    hr = props->CreateUInt8Array(3, arrayBytes, &value);
                    break;
                default:
                    break;
                }
                boolean replaced{};
                if (FAILED(hr) || FAILED(map->Insert(key.Get(), value.Get(), &replaced))) return 17;
                if (i == 0 && (FAILED(map->Insert(key.Get(), value.Get(), &replaced)) || !replaced)) return 34;
            } else if (argc > 1 && !strcmp(argv[1], "read")) {
                // Loading replayed the write test's journal and compacted it
                // into settings.json, which must hold every replayed key.
                std::string name = "\"";
                for (const wchar_t* c = keys[i]; *c; ++c) name += static_cast<char>(*c);
                if (i > 0 && ReadAll(settingsFile).find(name + "\"") == std::string::npos) return 75;
                ComPtr<IInspectable> value;
                auto hr = map->Lookup(key.Get(), &value);
                if (FAILED(hr)) {
                    printf("missing persisted %ls (%s) hr=%08lx\n", keys[i], roaming ? "roaming" : "local", hr);
                    return 18;
                }
                ComPtr<ABI::Windows::Foundation::IPropertyValue> prop;
                if (FAILED(value.As(&prop))) return 19;
                switch (i) {
                case 0: {
                    HString actual;
                    if (FAILED(prop->GetString(actual.GetAddressOf()))) return 20;
                    UINT32 n{};
                    const auto* p = actual.GetRawBuffer(&n);
                    if (n != 6 || memcmp(p, payload, sizeof(payload)) != 0) return 21;
                    break;
                }
                case 1: {
                    boolean actual{};
                    if (FAILED(prop->GetBoolean(&actual)) || !actual) return 22;
                    break;
                }
                case 2: {
                    INT32 actual{};
                    if (FAILED(prop->GetInt32(&actual)) || actual != -123456) return 23;
                    break;
                }
                case 3: {
                    INT64 actual{};
                    if (FAILED(prop->GetInt64(&actual)) || actual != -9007199254740993LL) return 24;
                    break;
                }
                case 4: {
                    double actual{};
                    if (FAILED(prop->GetDouble(&actual)) || actual != 1.2345678901234567) return 25;
                    break;
                }
                case 5: {
                    UINT32 count{};
                    BYTE* actual{};
                    if (FAILED(prop->GetUInt8Array(&count, &actual))) return 26;
                    bool okay = count == 3 && actual[0] == 0 && actual[1] == 128 && actual[2] == 255;
                    FreeArrayResult(prop.Get(), actual);
                    if (!okay) return 27;
                    break;
                }
                default:
                    break;
                }
                if (FAILED(map->Remove(key.Get()))) return 28;
            }
        }
        const UINT64 doubleBits[] = {
            0x7ff0000000000000ULL,
            0xfff0000000000000ULL,
            0x7ff8000000000042ULL,
            0x8000000000000000ULL,
        };
        for (int i = 0; i < 4; ++i) {
            std::wstring keyText = L"__ctr2shim_test_doublebits" + std::to_wstring(i);
            HString key;
            key.Set(keyText.c_str());
            if (argc > 1 && !strcmp(argv[1], "specialwrite")) {
                double number{};
                memcpy(&number, &doubleBits[i], sizeof(number));
                ComPtr<IInspectable> value;
                boolean replaced{};
                if (FAILED(props->CreateDouble(number, &value)) ||
                    FAILED(map->Insert(key.Get(), value.Get(), &replaced))) {
                    puts("special Double rejected");
                    return 47;
                }
            } else if (argc > 1 && !strcmp(argv[1], "specialread")) {
                ComPtr<IInspectable> value;
                ComPtr<ABI::Windows::Foundation::IPropertyValue> property;
                double number{};
                UINT64 actual{};
                if (FAILED(map->Lookup(key.Get(), &value)) || FAILED(value.As(&property)) ||
                    FAILED(property->GetDouble(&number)))
                    return 48;
                memcpy(&actual, &number, sizeof(actual));
                if (actual != doubleBits[i]) return 49;
                if (FAILED(map->Remove(key.Get()))) return 50;
            }
        }
        if (argc > 1 && events != (!strcmp(argv[1], "write") ? 7u : !strcmp(argv[1], "read") ? 6u : 4u)) return 32;
        if (FAILED(observable->remove_MapChanged(token))) return 33;
    }
    puts("storagetest: path/file and settings checks passed");
    return 0;
}
