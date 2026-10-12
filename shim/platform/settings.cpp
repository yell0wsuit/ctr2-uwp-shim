#include "settings.h"
#include "log.h"
#include <roapi.h>
#include <wrl/wrappers/corewrappers.h>
#include <wrl/event.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <sstream>
#include <vector>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
namespace ctr2 {
namespace {
using Map = IMap<HSTRING,IInspectable*>;
using Observable = IObservableMap<HSTRING,IInspectable*>;
using Changed = MapChangedEventHandler<HSTRING,IInspectable*>;
using Pair = IKeyValuePair<HSTRING,IInspectable*>;
struct Entry { std::wstring key, type, value; };
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if(value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
    void Close() { if(value!=INVALID_HANDLE_VALUE) { CloseHandle(value); value=INVALID_HANDLE_VALUE; } }
};
std::wstring String(HSTRING value) { UINT32 n{}; auto p=WindowsGetStringRawBuffer(value,&n); return {p,n}; }
// Escape individual UTF-16 code units. This preserves embedded nulls and surrogate pairs.
std::string Quote(const std::wstring& value) {
    static constexpr char hex[]="0123456789abcdef";
    std::string result="\"";
    for(auto c : value) { unsigned u=static_cast<unsigned short>(c); result+="\\u"; for(int shift=12;shift>=0;shift-=4) result+=hex[(u>>shift)&15]; }
    result+='"'; return result;
}
class Reader {
    const std::string& text_; size_t pos_{};
public:
    explicit Reader(const std::string& text) : text_(text) {}
    void Space() { while(pos_<text_.size() && (text_[pos_]==' ' || text_[pos_]=='\r' || text_[pos_]=='\n' || text_[pos_]=='\t')) ++pos_; }
    bool Take(char c) { Space(); if(pos_==text_.size() || text_[pos_]!=c) return false; ++pos_; return true; }
    bool End() { Space(); return pos_==text_.size(); }
    bool String(std::wstring& result) {
        result.clear(); if(!Take('"')) return false;
        while(pos_<text_.size()) {
            unsigned char c=static_cast<unsigned char>(text_[pos_++]); if(c=='"') return true;
            if(c<32 || c>=128) return false; // Files written here are ASCII JSON with UTF-16 escapes.
            if(c!='\\') { result+=static_cast<wchar_t>(c); continue; }
            if(pos_==text_.size()) return false;
            switch(text_[pos_++]) {
            case '"': result+='"'; break; case '\\': result+='\\'; break; case '/': result+='/'; break;
            case 'b': result+='\b'; break; case 'f': result+='\f'; break; case 'n': result+='\n'; break; case 'r': result+='\r'; break; case 't': result+='\t'; break;
            case 'u': {
                unsigned n=0; for(int i=0;i<4;++i) { if(pos_==text_.size()) return false; char h=text_[pos_++]; n<<=4; if(h>='0'&&h<='9') n|=h-'0'; else if(h>='a'&&h<='f') n|=h-'a'+10; else if(h>='A'&&h<='F') n|=h-'A'+10; else return false; }
                result+=static_cast<wchar_t>(n); break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Entries(std::vector<Entry>& entries) {
        // The document is an array of typed records; field order is deliberately fixed by the writer.
        if(!Take('[')) return false;
        if(Take(']')) return End();
        do {
            Entry entry; std::wstring field;
            if(!Take('{') || !String(field) || field!=L"key" || !Take(':') || !String(entry.key) || !Take(',') ||
                !String(field) || field!=L"type" || !Take(':') || !String(entry.type) || !Take(',') ||
                !String(field) || field!=L"value" || !Take(':') || !String(entry.value) || !Take('}')) return false;
            entries.push_back(std::move(entry));
        } while(Take(','));
        return Take(']') && End();
    }
};
template<typename T> std::wstring Number(T value) { std::wostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(std::numeric_limits<T>::max_digits10)<<value; return out.str(); }
template<typename T> bool ParseNumber(const std::wstring& value,T& number) {
    std::wistringstream in(value); in.imbue(std::locale::classic()); in>>std::noskipws>>number; return !value.empty() && !in.fail() && in.eof();
}
void FreeArrayResult(IPropertyValue* value,void* bytes) {
    // Wine's wintypes PropertyValue currently returns a borrowed buffer from its
    // array getters. Restrict this workaround to that implementation; Windows
    // and third-party IPropertyValue objects follow the CoTaskMemFree contract.
    HMODULE module{};
    auto vtable=*reinterpret_cast<void***>(value);
    if(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version") &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(vtable),&module) && module==GetModuleHandleW(L"wintypes.dll")) return;
    CoTaskMemFree(bytes);
}
HRESULT Encode(IInspectable* object, Entry& entry) {
    if(!object) return E_INVALIDARG;
    ComPtr<IPropertyValue> value; auto hr=object->QueryInterface(IID_PPV_ARGS(&value)); if(FAILED(hr)) return hr;
    PropertyType type{}; hr=value->get_Type(&type); if(FAILED(hr)) return hr;
    switch(type) {
    case PropertyType_String: { HString s; hr=value->GetString(s.GetAddressOf()); entry.type=L"String"; if(SUCCEEDED(hr)) entry.value=String(s.Get()); break; }
    case PropertyType_Boolean: { boolean b{}; hr=value->GetBoolean(&b); entry.type=L"Boolean"; entry.value=b?L"true":L"false"; break; }
    case PropertyType_Int32: { INT32 n{}; hr=value->GetInt32(&n); entry.type=L"Int32"; entry.value=Number(n); break; }
    case PropertyType_Int64: { INT64 n{}; hr=value->GetInt64(&n); entry.type=L"Int64"; entry.value=Number(n); break; }
    case PropertyType_Double: {
        double number{}; hr=value->GetDouble(&number); UINT64 bits{}; memcpy(&bits,&number,sizeof(bits));
        static constexpr wchar_t hex[]=L"0123456789abcdef"; entry.type=L"DoubleBits";
        for(int shift=60;shift>=0;shift-=4) entry.value+=hex[(bits>>shift)&15];
        break;
    }
    case PropertyType_UInt8Array: {
        UINT32 n{}; BYTE* bytes{}; hr=value->GetUInt8Array(&n,&bytes); if(FAILED(hr)) return hr;
        static constexpr wchar_t hex[]=L"0123456789abcdef"; entry.type=L"UInt8Array";
        try { for(UINT32 i=0;i<n;++i) { entry.value+=hex[bytes[i]>>4]; entry.value+=hex[bytes[i]&15]; } } catch(...) { FreeArrayResult(value.Get(),bytes); throw; }
        FreeArrayResult(value.Get(),bytes); break;
    }
    default: return E_NOTIMPL;
    }
    return hr;
}
HRESULT Decode(IPropertyValueStatics* props,const Entry& entry,IInspectable** result) {
    *result=nullptr;
    if(entry.type==L"String") { HString text; auto hr=text.Set(entry.value.data(),static_cast<UINT32>(entry.value.size())); return FAILED(hr)?hr:props->CreateString(text.Get(),result); }
    if(entry.type==L"Boolean") { if(entry.value!=L"true" && entry.value!=L"false") return E_INVALIDARG; return props->CreateBoolean(entry.value==L"true",result); }
    if(entry.type==L"Int32") { INT32 n{}; return ParseNumber(entry.value,n)?props->CreateInt32(n,result):E_INVALIDARG; }
    if(entry.type==L"Int64") { INT64 n{}; return ParseNumber(entry.value,n)?props->CreateInt64(n,result):E_INVALIDARG; }
    if(entry.type==L"DoubleBits") {
        if(entry.value.size()!=16) return E_INVALIDARG;
        UINT64 bits{}; for(wchar_t c:entry.value) { bits<<=4; if(c>=L'0'&&c<=L'9') bits|=c-L'0'; else if(c>=L'a'&&c<=L'f') bits|=c-L'a'+10; else return E_INVALIDARG; }
        double number{}; memcpy(&number,&bits,sizeof(number)); return props->CreateDouble(number,result);
    }
    // Accept finite decimal Double records written by the first schema.
    if(entry.type==L"Double") { double n{}; return ParseNumber(entry.value,n)&&std::isfinite(n)?props->CreateDouble(n,result):E_INVALIDARG; }
    if(entry.type==L"UInt8Array") {
        if(entry.value.size()%2) return E_INVALIDARG;
        std::vector<BYTE> bytes; for(size_t i=0;i<entry.value.size();i+=2) {
            unsigned n=0; for(int j=0;j<2;++j) { wchar_t c=entry.value[i+j]; n<<=4; if(c>=L'0'&&c<=L'9') n|=c-L'0'; else if(c>=L'a'&&c<=L'f') n|=c-L'a'+10; else return E_INVALIDARG; }
            bytes.push_back(static_cast<BYTE>(n));
        }
        return props->CreateUInt8Array(static_cast<UINT32>(bytes.size()),bytes.data(),result);
    }
    return E_NOTIMPL;
}
HRESULT ReadFileText(const std::wstring& path,std::string& text) {
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE) { auto error=GetLastError(); return error==ERROR_FILE_NOT_FOUND?S_FALSE:HRESULT_FROM_WIN32(error); }
    LARGE_INTEGER size{}; if(!GetFileSizeEx(file.value,&size)) return HRESULT_FROM_WIN32(GetLastError());
    if(size.QuadPart<0 || size.QuadPart>16*1024*1024) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    text.resize(static_cast<size_t>(size.QuadPart)); DWORD read{};
    if(!ReadFile(file.value,text.data(),static_cast<DWORD>(text.size()),&read,nullptr)) return HRESULT_FROM_WIN32(GetLastError());
    return read==text.size()?S_OK:HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
}
HRESULT WriteAtomic(const std::wstring& path,const std::string& text) {
    static std::atomic<unsigned long> sequence{};
    std::wstring temporary=path+L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L"."+std::to_wstring(++sequence)+L".tmp";
    Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    DWORD written{}; HRESULT hr=S_OK;
    if(!WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) || written!=text.size()) hr=HRESULT_FROM_WIN32(GetLastError()?GetLastError():ERROR_WRITE_FAULT);
    if(SUCCEEDED(hr) && !FlushFileBuffers(file.value)) hr=HRESULT_FROM_WIN32(GetLastError());
    file.Close();
    if(SUCCEEDED(hr) && !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) hr=HRESULT_FROM_WIN32(GetLastError());
    if(FAILED(hr)) DeleteFileW(temporary.c_str());
    return hr;
}
}
class SettingsPersistence final {
    friend class PersistentPropertySet;
    std::wstring path_;
    ComPtr<Map> map_;
    std::mutex mutex_;
public:
    explicit SettingsPersistence(std::wstring path) : path_(std::move(path)) {}
    HRESULT Initialize(IPropertySet* values) {
        auto hr=values->QueryInterface(IID_PPV_ARGS(&map_)); if(FAILED(hr)) return hr;
        std::string text; hr=ReadFileText(path_,text); if(FAILED(hr)) return hr;
        if(hr==S_OK) {
            std::vector<Entry> entries; if(!Reader(text).Entries(entries)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            ComPtr<IPropertyValueStatics> props; hr=RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(),IID_PPV_ARGS(&props)); if(FAILED(hr)) return hr;
            // Decode the complete file first so malformed records never produce a partially loaded map.
            std::vector<ComPtr<IInspectable>> decoded;
            for(const auto& entry: entries) { ComPtr<IInspectable> value; hr=Decode(props.Get(),entry,&value); if(FAILED(hr)) return hr; decoded.push_back(std::move(value)); }
            for(size_t i=0;i<entries.size();++i) { HString key; hr=key.Set(entries[i].key.data(),static_cast<UINT32>(entries[i].key.size())); if(FAILED(hr)) return hr; boolean replaced{}; hr=map_->Insert(key.Get(),decoded[i].Get(),&replaced); if(FAILED(hr)) return hr; }
        }
        return S_OK;
    }
    HRESULT Candidate(ComPtr<Map>& result,bool copy=true) {
        ComPtr<IInspectable> object; auto hr=RoActivateInstance(HStringReference(L"Windows.Foundation.Collections.PropertySet").Get(),&object); if(FAILED(hr)) return hr;
        hr=object.As(&result); if(FAILED(hr) || !copy) return hr;
        ComPtr<IIterable<Pair*>> iterable; hr=map_.As(&iterable); if(FAILED(hr)) return hr;
        ComPtr<IIterator<Pair*>> iterator; hr=iterable->First(&iterator); if(FAILED(hr)) return hr;
        boolean has{}; hr=iterator->get_HasCurrent(&has);
        while(SUCCEEDED(hr) && has) {
            ComPtr<Pair> pair; hr=iterator->get_Current(&pair); if(FAILED(hr)) return hr;
            HString key; hr=pair->get_Key(key.GetAddressOf()); if(FAILED(hr)) return hr;
            ComPtr<IInspectable> value; hr=pair->get_Value(&value); if(FAILED(hr)) return hr;
            boolean replaced{}; hr=result->Insert(key.Get(),value.Get(),&replaced); if(FAILED(hr)) return hr;
            hr=iterator->MoveNext(&has);
        }
        return hr;
    }
    HRESULT Save(Map* candidate) {
        ComPtr<IIterable<Pair*>> iterable; auto hr=candidate->QueryInterface(IID_PPV_ARGS(&iterable)); if(FAILED(hr)) return hr;
        ComPtr<IIterator<Pair*>> iterator; hr=iterable->First(&iterator); if(FAILED(hr)) return hr;
        std::string text="["; boolean has{}; hr=iterator->get_HasCurrent(&has); bool first=true;
        while(SUCCEEDED(hr) && has) {
            ComPtr<Pair> pair; hr=iterator->get_Current(&pair); if(FAILED(hr)) return hr;
            HString key; hr=pair->get_Key(key.GetAddressOf()); if(FAILED(hr)) return hr;
            ComPtr<IInspectable> value; hr=pair->get_Value(&value); if(FAILED(hr)) return hr;
            Entry entry; entry.key=String(key.Get()); hr=Encode(value.Get(),entry); if(FAILED(hr)) return hr;
            if(!first) text+=','; first=false;
            text+="\n{\"key\":"+Quote(entry.key)+",\"type\":"+Quote(entry.type)+",\"value\":"+Quote(entry.value)+"}";
            hr=iterator->MoveNext(&has);
        }
        if(FAILED(hr)) return hr;
        text+="\n]\n"; if(text.size()>16*1024*1024) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE); return WriteAtomic(path_,text);
    }
};
namespace {
template<typename Fn> HRESULT Guard(Fn&& fn) noexcept {
    try { return fn(); } catch(const std::bad_alloc&) { return E_OUTOFMEMORY; } catch(...) { return E_FAIL; }
}
class MapChangedArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>,IMapChangedEventArgs<HSTRING>,FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.MapChangedEventArgs",BaseTrust);
    CollectionChange change_;
    HString key_;
public:
    explicit MapChangedArgs(CollectionChange change) : change_(change) {}
    HRESULT Initialize(HSTRING key) { return key_.Set(key); }
    HRESULT STDMETHODCALLTYPE get_CollectionChange(CollectionChange* value) override { if(!value) return E_POINTER; *value=change_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Key(HSTRING* value) override { if(!value) return E_POINTER; return key_.CopyTo(value); }
};
}
// All required PropertySet interfaces share one COM identity. The native backing map
// supplies SDK-compatible views/iterators; writes stay here so Wine's unimplemented
// native MapChanged event is never needed for persistence.
class PersistentPropertySet final : public RuntimeClass<RuntimeClassFlags<WinRt>,IPropertySet,Map,Observable,IIterable<Pair*>,FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.PropertySet",BaseTrust);
    std::shared_ptr<SettingsPersistence> state_;
    EventSource<Changed> changed_;
    HRESULT Notify(CollectionChange change,HSTRING key) {
        auto args=Make<MapChangedArgs>(change); if(!args) return E_OUTOFMEMORY;
        auto hr=args->Initialize(key); if(FAILED(hr)) return hr;
        return changed_.InvokeAll(this,args.Get());
    }
public:
    explicit PersistentPropertySet(std::shared_ptr<SettingsPersistence> state) : state_(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING key,IInspectable** value) override {
        if(!value) return E_POINTER; *value=nullptr;
        std::lock_guard<std::mutex> lock(state_->mutex_); return state_->map_->Lookup(key,value);
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override { if(!value) return E_POINTER; std::lock_guard<std::mutex> lock(state_->mutex_); return state_->map_->get_Size(value); }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING key,boolean* value) override { if(!value) return E_POINTER; std::lock_guard<std::mutex> lock(state_->mutex_); return state_->map_->HasKey(key,value); }
    HRESULT STDMETHODCALLTYPE GetView(IMapView<HSTRING,IInspectable*>** value) override { if(!value) return E_POINTER; *value=nullptr; std::lock_guard<std::mutex> lock(state_->mutex_); return state_->map_->GetView(value); }
    HRESULT STDMETHODCALLTYPE First(IIterator<Pair*>** value) override {
        if(!value) return E_POINTER; *value=nullptr; std::lock_guard<std::mutex> lock(state_->mutex_);
        ComPtr<IIterable<Pair*>> iterable; auto hr=state_->map_.As(&iterable); return FAILED(hr)?hr:iterable->First(value);
    }
    HRESULT STDMETHODCALLTYPE Insert(HSTRING key,IInspectable* value,boolean* replaced) override {
        if(!replaced) return E_POINTER; *replaced=false;
        return Guard([&]() -> HRESULT {
            Entry entry; auto hr=Encode(value,entry); if(FAILED(hr)) return hr;
            {
                std::lock_guard<std::mutex> lock(state_->mutex_);
                ComPtr<Map> candidate; hr=state_->Candidate(candidate); if(FAILED(hr)) return hr;
                boolean candidateReplaced{}; hr=candidate->Insert(key,value,&candidateReplaced); if(FAILED(hr)) return hr;
                hr=state_->Save(candidate.Get()); if(FAILED(hr)) { Log("FAIL","settings save failed hr=%08lx",hr); return hr; }
                state_->map_=std::move(candidate); *replaced=candidateReplaced;
            }
            return Notify(*replaced?CollectionChange_ItemChanged:CollectionChange_ItemInserted,key);
        });
    }
    HRESULT STDMETHODCALLTYPE Remove(HSTRING key) override {
        return Guard([&]() -> HRESULT {
            {
                std::lock_guard<std::mutex> lock(state_->mutex_);
                ComPtr<Map> candidate; auto hr=state_->Candidate(candidate); if(FAILED(hr)) return hr;
                hr=candidate->Remove(key); if(FAILED(hr)) return hr;
                hr=state_->Save(candidate.Get()); if(FAILED(hr)) { Log("FAIL","settings save failed hr=%08lx",hr); return hr; }
                state_->map_=std::move(candidate);
            }
            return Notify(CollectionChange_ItemRemoved,key);
        });
    }
    HRESULT STDMETHODCALLTYPE Clear() override {
        return Guard([&]() -> HRESULT {
            {
                std::lock_guard<std::mutex> lock(state_->mutex_);
                ComPtr<Map> candidate; auto hr=state_->Candidate(candidate,false); if(FAILED(hr)) return hr;
                hr=state_->Save(candidate.Get()); if(FAILED(hr)) { Log("FAIL","settings save failed hr=%08lx",hr); return hr; }
                state_->map_=std::move(candidate);
            }
            return Notify(CollectionChange_Reset,nullptr);
        });
    }
    HRESULT STDMETHODCALLTYPE add_MapChanged(Changed* handler,EventRegistrationToken* token) override { if(!handler || !token) return E_POINTER; return changed_.Add(handler,token); }
    HRESULT STDMETHODCALLTYPE remove_MapChanged(EventRegistrationToken token) override { return changed_.Remove(token); }
};
HRESULT CreatePersistentSettings(const std::wstring& path,ComPtr<IPropertySet>& values,std::shared_ptr<SettingsPersistence>& persistence) {
    return Guard([&]() -> HRESULT {
        ComPtr<IInspectable> object; auto hr=RoActivateInstance(HStringReference(L"Windows.Foundation.Collections.PropertySet").Get(),&object); if(FAILED(hr)) return hr;
        ComPtr<IPropertySet> set; hr=object.As(&set); if(FAILED(hr)) return hr;
        auto state=std::make_shared<SettingsPersistence>(path); hr=state->Initialize(set.Get()); if(FAILED(hr)) { Log("FAIL","settings load failed hr=%08lx",hr); return hr; }
        auto wrapper=Make<PersistentPropertySet>(state); if(!wrapper) return E_OUTOFMEMORY;
        hr=wrapper.As(&values); if(FAILED(hr)) return hr;
        persistence=std::move(state); return S_OK;
    });
}
}
