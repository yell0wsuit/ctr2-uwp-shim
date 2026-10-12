#include <windows.h>
#include <roapi.h>
#include <windows.storage.h>
#include <windows.foundation.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <string>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage;
void FreeArrayResult(ABI::Windows::Foundation::IPropertyValue* value,void* bytes) {
    // Wine wintypes returns borrowed storage; releasing it corrupts the native object.
    HMODULE module{};
    if(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version") &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(*reinterpret_cast<void***>(value)),&module) && module==GetModuleHandleW(L"wintypes.dll")) return;
    CoTaskMemFree(bytes);
}
int main(int argc, char** argv) {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto dll=LoadLibraryW(L"ctr2shim.dll"); if(!dll) return 1;
    auto get=reinterpret_cast<HRESULT(WINAPI*)(HSTRING,IActivationFactory**)>(GetProcAddress(dll,"ShimGetFactory"));
    ComPtr<IActivationFactory> factory; if(FAILED(get(HStringReference(L"Windows.Storage.ApplicationData").Get(),&factory))) return 2;
    ComPtr<IApplicationDataStatics> statics; if(FAILED(factory.As(&statics))) return 3;
    ComPtr<IApplicationData> data; if(FAILED(statics->get_Current(&data))) return 4;
    ComPtr<IStorageFolder> folder; if(FAILED(data->get_LocalFolder(&folder))) return 5;
    ComPtr<IStorageItem> item; if(FAILED(folder.As(&item))) return 6;
    HString path; if(FAILED(item->get_Path(path.GetAddressOf()))) return 7;
    std::wstring text(path.GetRawBuffer(nullptr)); if(text.size()<11 || text.substr(text.size()-11)!=L"\\ctr2\\Local") return 8;
    if(!(GetFileAttributesW(text.c_str())&FILE_ATTRIBUTE_DIRECTORY)) return 9;
    std::wstring file=text+L"\\shim-test.tmp";
    HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); if(handle==INVALID_HANDLE_VALUE) return 10;
    DWORD written{}; const char bytes[]="roundtrip"; if(!WriteFile(handle,bytes,sizeof(bytes),&written,nullptr)) return 11; CloseHandle(handle);
    handle=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr); if(handle==INVALID_HANDLE_VALUE) return 12;
    char restored[sizeof(bytes)]{}; DWORD read{}; ReadFile(handle,restored,sizeof(restored),&read,nullptr); CloseHandle(handle); DeleteFileW(file.c_str());
    if(read!=sizeof(bytes) || memcmp(restored,bytes,sizeof(bytes))) return 13;
    ComPtr<ABI::Windows::Foundation::IPropertyValueStatics> props;
    if(FAILED(RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(),IID_PPV_ARGS(&props)))) return 14;
    for(bool roaming : {false,true}) {
        ComPtr<IApplicationDataContainer> container;
        if(FAILED(roaming ? data->get_RoamingSettings(&container) : data->get_LocalSettings(&container))) return 15;
        ComPtr<ABI::Windows::Foundation::Collections::IPropertySet> set; container->get_Values(&set);
        ComPtr<ABI::Windows::Foundation::Collections::IMap<HSTRING,IInspectable*>> map;
        if(FAILED(set.As(&map))) return 16;
        const wchar_t payload[]={roaming?L'R':L'V',0,L'ü',L'中',0xd83d,0xde00};
        HString expected; expected.Set(payload,6);
        const wchar_t* keys[]={L"__ctr2shim_test_string",L"__ctr2shim_test_bool",L"__ctr2shim_test_i32",L"__ctr2shim_test_i64",L"__ctr2shim_test_double",L"__ctr2shim_test_bytes"};
        ComPtr<ABI::Windows::Foundation::Collections::IObservableMap<HSTRING,IInspectable*>> observable;
        if(FAILED(set.As(&observable))) return 29;
        ComPtr<IUnknown> setIdentity,mapIdentity,observableIdentity;
        set.As(&setIdentity); map.As(&mapIdentity); observable.As(&observableIdentity);
        if(setIdentity.Get()!=mapIdentity.Get() || setIdentity.Get()!=observableIdentity.Get()) return 30;
        unsigned events{};
        auto handler=Callback<ABI::Windows::Foundation::Collections::MapChangedEventHandler<HSTRING,IInspectable*>>(
            [&](ABI::Windows::Foundation::Collections::IObservableMap<HSTRING,IInspectable*>* sender,
                ABI::Windows::Foundation::Collections::IMapChangedEventArgs<HSTRING>* args) -> HRESULT {
                if(sender!=observable.Get() || !args) return E_FAIL;
                HString eventKey; if(FAILED(args->get_Key(eventKey.GetAddressOf()))) return E_FAIL;
                UINT32 size{}; if(FAILED(map->get_Size(&size))) return E_FAIL; // Event callbacks may reenter the map.
                ++events; return S_OK;
            });
        EventRegistrationToken token{};
        if(FAILED(observable->add_MapChanged(handler.Get(),&token))) return 31;
        if(argc>1 && !strcmp(argv[1],"atomic")) {
            HStringReference key(L"__ctr2shim_test_atomic"),newKey(L"__ctr2shim_test_atomic_new");
            ComPtr<IInspectable> original,replacement;
            props->CreateBoolean(true,&original); props->CreateBoolean(false,&replacement);
            boolean replaced{}; if(FAILED(map->Insert(key.Get(),original.Get(),&replaced))) return 35;
            std::wstring settings=text.substr(0,text.size()-5)+(roaming?L"Roaming":L"Local")+L"\\settings.json";
            HANDLE locked=CreateFileW(settings.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(locked==INVALID_HANDLE_VALUE) return 36;
            struct CloseLocked { HANDLE h; ~CloseLocked() { CloseHandle(h); } } close{locked};
            DWORD size=GetFileSize(locked,nullptr),read{}; std::string before(size,0),after(size,0);
            if(!ReadFile(locked,before.data(),size,&read,nullptr) || read!=size) return 37;
            const auto eventCount=events;
            if(SUCCEEDED(map->Insert(key.Get(),replacement.Get(),&replaced))) return 38;
            ComPtr<IInspectable> current; ComPtr<ABI::Windows::Foundation::IPropertyValue> property;
            boolean actual{}; if(FAILED(map->Lookup(key.Get(),&current)) || FAILED(current.As(&property)) || FAILED(property->GetBoolean(&actual)) || !actual) { puts("failed Insert changed memory"); return 39; }
            if(SUCCEEDED(map->Insert(newKey.Get(),replacement.Get(),&replaced))) return 40;
            boolean found{}; if(FAILED(map->HasKey(newKey.Get(),&found)) || found) return 41;
            if(SUCCEEDED(map->Remove(key.Get())) || FAILED(map->HasKey(key.Get(),&found)) || !found) return 42;
            UINT32 countBefore{},countAfter{}; map->get_Size(&countBefore);
            if(SUCCEEDED(map->Clear()) || FAILED(map->get_Size(&countAfter)) || countAfter!=countBefore) return 43;
            SetFilePointer(locked,0,nullptr,FILE_BEGIN);
            if(GetFileSize(locked,nullptr)!=size || !ReadFile(locked,after.data(),size,&read,nullptr) || read!=size || after!=before || events!=eventCount) return 44;
            CloseHandle(close.h); close.h=INVALID_HANDLE_VALUE;
            if(FAILED(map->Remove(key.Get()))) return 45;
            if(FAILED(observable->remove_MapChanged(token))) return 46;
            continue;
        }
        for(int i=0;i<6;++i) {
            HString key; key.Set(keys[i]);
            if(i==0) { const wchar_t special[]=L"__ctr2shim_test_string\0中"; key.Set(special,static_cast<UINT32>(sizeof(special)/sizeof(wchar_t)-1)); }
            if(argc>1 && !strcmp(argv[1],"write")) {
                ComPtr<IInspectable> value; HRESULT hr=E_FAIL; BYTE bytes[]={0,128,255};
                switch(i) {
                case 0: hr=props->CreateString(expected.Get(),&value); break;
                case 1: hr=props->CreateBoolean(true,&value); break;
                case 2: hr=props->CreateInt32(-123456,&value); break;
                case 3: hr=props->CreateInt64(-9007199254740993LL,&value); break;
                case 4: hr=props->CreateDouble(1.2345678901234567,&value); break;
                case 5: hr=props->CreateUInt8Array(3,bytes,&value); break;
                }
                boolean replaced{}; if(FAILED(hr) || FAILED(map->Insert(key.Get(),value.Get(),&replaced))) return 17;
                if(i==0 && (FAILED(map->Insert(key.Get(),value.Get(),&replaced)) || !replaced)) return 34;
            } else if(argc>1 && !strcmp(argv[1],"read")) {
                ComPtr<IInspectable> value; auto hr=map->Lookup(key.Get(),&value);
                if(FAILED(hr)) { printf("missing persisted %ls (%s) hr=%08lx\n",keys[i],roaming?"roaming":"local",hr); return 18; }
                ComPtr<ABI::Windows::Foundation::IPropertyValue> prop;
                if(FAILED(value.As(&prop))) return 19;
                switch(i) {
                case 0: { HString actual; if(FAILED(prop->GetString(actual.GetAddressOf()))) return 20; UINT32 n{}; auto p=actual.GetRawBuffer(&n); if(n!=6 || memcmp(p,payload,sizeof(payload))) return 21; break; }
                case 1: { boolean actual{}; if(FAILED(prop->GetBoolean(&actual)) || !actual) return 22; break; }
                case 2: { INT32 actual{}; if(FAILED(prop->GetInt32(&actual)) || actual!=-123456) return 23; break; }
                case 3: { INT64 actual{}; if(FAILED(prop->GetInt64(&actual)) || actual!=-9007199254740993LL) return 24; break; }
                case 4: { double actual{}; if(FAILED(prop->GetDouble(&actual)) || actual!=1.2345678901234567) return 25; break; }
                case 5: { UINT32 count{}; BYTE* actual{}; if(FAILED(prop->GetUInt8Array(&count,&actual))) return 26; bool okay=count==3 && actual[0]==0 && actual[1]==128 && actual[2]==255; FreeArrayResult(prop.Get(),actual); if(!okay) return 27; break; }
                }
                if(FAILED(map->Remove(key.Get()))) return 28;
            }
        }
        const UINT64 doubleBits[]={0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000042ULL,0x8000000000000000ULL};
        for(int i=0;i<4;++i) {
            std::wstring keyText=L"__ctr2shim_test_doublebits"+std::to_wstring(i); HString key; key.Set(keyText.c_str());
            if(argc>1 && !strcmp(argv[1],"specialwrite")) {
                double number{}; memcpy(&number,&doubleBits[i],sizeof(number)); ComPtr<IInspectable> value; boolean replaced{};
                if(FAILED(props->CreateDouble(number,&value)) || FAILED(map->Insert(key.Get(),value.Get(),&replaced))) { puts("special Double rejected"); return 47; }
            } else if(argc>1 && !strcmp(argv[1],"specialread")) {
                ComPtr<IInspectable> value; ComPtr<ABI::Windows::Foundation::IPropertyValue> property; double number{}; UINT64 actual{};
                if(FAILED(map->Lookup(key.Get(),&value)) || FAILED(value.As(&property)) || FAILED(property->GetDouble(&number))) return 48;
                memcpy(&actual,&number,sizeof(actual)); if(actual!=doubleBits[i]) return 49;
                if(FAILED(map->Remove(key.Get()))) return 50;
            }
        }
        if(argc>1 && events!=(!strcmp(argv[1],"write")?7u:!strcmp(argv[1],"read")?6u:4u)) return 32;
        if(FAILED(observable->remove_MapChanged(token))) return 33;
    }
    puts("storagetest: path/file and settings checks passed"); return 0;
}
