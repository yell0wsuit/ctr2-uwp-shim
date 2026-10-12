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
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
namespace ctr2 {
namespace {
using Map = IMap<HSTRING, IInspectable*>;
using Observable = IObservableMap<HSTRING, IInspectable*>;
using Changed = MapChangedEventHandler<HSTRING, IInspectable*>;
using Pair = IKeyValuePair<HSTRING, IInspectable*>;
struct Entry {
    std::wstring key, type, value;
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
    void Close() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
            value = INVALID_HANDLE_VALUE;
        }
    }
};
std::wstring String(HSTRING value) {
    UINT32 n{};
    const auto* p = WindowsGetStringRawBuffer(value, &n);
    return {p, n};
}
// Printable ASCII is written as itself. Anything else is escaped per UTF-16
// code unit, which preserves embedded nulls and surrogate pairs.
std::string Quote(const std::wstring& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    result.reserve(value.size() + 2);
    for (auto c : value) {
        unsigned u = static_cast<unsigned short>(c);
        if (u >= 0x20 && u < 0x7f && u != '"' && u != '\\') {
            result += static_cast<char>(u);
            continue;
        }
        result += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4) result += hex[(u >> shift) & 15];
    }
    result += '"';
    return result;
}
class Reader {
    const std::string& text_;
    size_t pos_{};

public:
    explicit Reader(const std::string& text) : text_(text) {}
    void Space() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\r' || text_[pos_] == '\n' || text_[pos_] == '\t'))
            ++pos_;
    }
    bool Take(char c) {
        Space();
        if (pos_ == text_.size() || text_[pos_] != c) return false;
        ++pos_;
        return true;
    }
    bool End() {
        Space();
        return pos_ == text_.size();
    }
    bool String(std::wstring& result) {
        result.clear();
        if (!Take('"')) return false;
        while (pos_ < text_.size()) {
            unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return true;
            if (c < 32 || c >= 128) return false; // Files written here are ASCII JSON with UTF-16 escapes.
            if (c != '\\') {
                result += static_cast<wchar_t>(c);
                continue;
            }
            if (pos_ == text_.size()) return false;
            switch (text_[pos_++]) {
            case '"':
                result += '"';
                break;
            case '\\':
                result += '\\';
                break;
            case '/':
                result += '/';
                break;
            case 'b':
                result += '\b';
                break;
            case 'f':
                result += '\f';
                break;
            case 'n':
                result += '\n';
                break;
            case 'r':
                result += '\r';
                break;
            case 't':
                result += '\t';
                break;
            case 'u': {
                unsigned n = 0;
                for (int i = 0; i < 4; ++i) {
                    if (pos_ == text_.size()) return false;
                    char h = text_[pos_++];
                    n <<= 4;
                    if (h >= '0' && h <= '9')
                        n |= h - '0';
                    else if (h >= 'a' && h <= 'f')
                        n |= h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F')
                        n |= h - 'A' + 10;
                    else
                        return false;
                }
                result += static_cast<wchar_t>(n);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }
    // One typed record; field order is deliberately fixed by the writer.
    bool Record(Entry& entry) {
        std::wstring field;
        return Take('{') && String(field) && field == L"key" && Take(':') && String(entry.key) && Take(',') &&
               String(field) && field == L"type" && Take(':') && String(entry.type) && Take(',') && String(field) &&
               field == L"value" && Take(':') && String(entry.value) && Take('}');
    }
    bool Entries(std::vector<Entry>& entries) {
        // The document is an array of records.
        if (!Take('[')) return false;
        if (Take(']')) return End();
        do {
            Entry entry;
            if (!Record(entry)) return false;
            entries.push_back(std::move(entry));
        } while (Take(','));
        return Take(']') && End();
    }
};
template <typename T> std::wstring Number(T value) {
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
    return out.str();
}
template <typename T> bool ParseNumber(const std::wstring& value, T& number) {
    std::wistringstream in(value);
    in.imbue(std::locale::classic());
    in >> std::noskipws >> number;
    return !value.empty() && !in.fail() && in.eof();
}
void FreeArrayResult(IPropertyValue* value, void* bytes) {
    // Wine's wintypes PropertyValue currently returns a borrowed buffer from its
    // array getters. Restrict this workaround to that implementation; Windows
    // and third-party IPropertyValue objects follow the CoTaskMemFree contract.
    HMODULE module{};
    auto* vtable = *reinterpret_cast<void***>(value);
    if (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(vtable), &module) &&
        module == GetModuleHandleW(L"wintypes.dll"))
        return;
    CoTaskMemFree(bytes);
}
HRESULT Encode(IInspectable* object, Entry& entry) {
    if (!object) return E_INVALIDARG;
    ComPtr<IPropertyValue> value;
    auto hr = object->QueryInterface(IID_PPV_ARGS(&value));
    if (FAILED(hr)) return hr;
    PropertyType type{};
    hr = value->get_Type(&type);
    if (FAILED(hr)) return hr;
    switch (type) {
    case PropertyType_String: {
        HString s;
        hr = value->GetString(s.GetAddressOf());
        entry.type = L"String";
        if (SUCCEEDED(hr)) entry.value = String(s.Get());
        break;
    }
    case PropertyType_Boolean: {
        boolean b{};
        hr = value->GetBoolean(&b);
        entry.type = L"Boolean";
        entry.value = b ? L"true" : L"false";
        break;
    }
    case PropertyType_Int32: {
        INT32 n{};
        hr = value->GetInt32(&n);
        entry.type = L"Int32";
        entry.value = Number(n);
        break;
    }
    case PropertyType_Int64: {
        INT64 n{};
        hr = value->GetInt64(&n);
        entry.type = L"Int64";
        entry.value = Number(n);
        break;
    }
    case PropertyType_Double: {
        double number{};
        hr = value->GetDouble(&number);
        UINT64 bits{};
        memcpy(&bits, &number, sizeof(bits));
        static constexpr wchar_t hex[] = L"0123456789abcdef";
        entry.type = L"DoubleBits";
        for (int shift = 60; shift >= 0; shift -= 4) entry.value += hex[(bits >> shift) & 15];
        break;
    }
    case PropertyType_UInt8Array: {
        UINT32 n{};
        BYTE* bytes{};
        hr = value->GetUInt8Array(&n, &bytes);
        if (FAILED(hr)) return hr;
        static constexpr wchar_t hex[] = L"0123456789abcdef";
        entry.type = L"UInt8Array";
        try {
            for (UINT32 i = 0; i < n; ++i) {
                entry.value += hex[bytes[i] >> 4];
                entry.value += hex[bytes[i] & 15];
            }
        } catch (...) {
            FreeArrayResult(value.Get(), bytes);
            throw;
        }
        FreeArrayResult(value.Get(), bytes);
        break;
    }
    default:
        return E_NOTIMPL;
    }
    return hr;
}
HRESULT Decode(IPropertyValueStatics* props, const Entry& entry, IInspectable** result) {
    *result = nullptr;
    if (entry.type == L"String") {
        HString text;
        auto hr = text.Set(entry.value.data(), static_cast<UINT32>(entry.value.size()));
        return FAILED(hr) ? hr : props->CreateString(text.Get(), result);
    }
    if (entry.type == L"Boolean") {
        if (entry.value != L"true" && entry.value != L"false") return E_INVALIDARG;
        return props->CreateBoolean(entry.value == L"true", result);
    }
    if (entry.type == L"Int32") {
        INT32 n{};
        return ParseNumber(entry.value, n) ? props->CreateInt32(n, result) : E_INVALIDARG;
    }
    if (entry.type == L"Int64") {
        INT64 n{};
        return ParseNumber(entry.value, n) ? props->CreateInt64(n, result) : E_INVALIDARG;
    }
    if (entry.type == L"DoubleBits") {
        if (entry.value.size() != 16) return E_INVALIDARG;
        UINT64 bits{};
        for (wchar_t c : entry.value) {
            bits <<= 4;
            if (c >= L'0' && c <= L'9')
                bits |= c - L'0';
            else if (c >= L'a' && c <= L'f')
                bits |= c - L'a' + 10;
            else
                return E_INVALIDARG;
        }
        double number{};
        memcpy(&number, &bits, sizeof(number));
        return props->CreateDouble(number, result);
    }
    // Accept finite decimal Double records written by the first schema.
    if (entry.type == L"Double") {
        double n{};
        return ParseNumber(entry.value, n) && std::isfinite(n) ? props->CreateDouble(n, result) : E_INVALIDARG;
    }
    if (entry.type == L"UInt8Array") {
        if (entry.value.size() % 2) return E_INVALIDARG;
        std::vector<BYTE> bytes;
        for (size_t i = 0; i < entry.value.size(); i += 2) {
            unsigned n = 0;
            for (int j = 0; j < 2; ++j) {
                wchar_t c = entry.value[i + j];
                n <<= 4;
                if (c >= L'0' && c <= L'9')
                    n |= c - L'0';
                else if (c >= L'a' && c <= L'f')
                    n |= c - L'a' + 10;
                else
                    return E_INVALIDARG;
            }
            bytes.push_back(static_cast<BYTE>(n));
        }
        return props->CreateUInt8Array(static_cast<UINT32>(bytes.size()), bytes.data(), result);
    }
    return E_NOTIMPL;
}
HRESULT ReadFileText(const std::wstring& path, std::string& text) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) {
        auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? S_FALSE : HRESULT_FROM_WIN32(error);
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size)) return HRESULT_FROM_WIN32(GetLastError());
    if (size.QuadPart < 0 || size.QuadPart > LONGLONG{16} * 1024 * 1024)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    text.resize(static_cast<size_t>(size.QuadPart));
    DWORD read{};
    if (!ReadFile(file.value, text.data(), static_cast<DWORD>(text.size()), &read, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    return read == text.size() ? S_OK : HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
}
HRESULT WriteAtomic(const std::wstring& path, const std::string& text) {
    static std::atomic<unsigned long> sequence{};
    std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                             std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(++sequence) + L".tmp";
    Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    DWORD written{};
    HRESULT hr = S_OK;
    if (!WriteFile(file.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
        written != text.size())
        hr = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
    if (SUCCEEDED(hr) && !FlushFileBuffers(file.value)) hr = HRESULT_FROM_WIN32(GetLastError());
    file.Close();
    if (SUCCEEDED(hr) &&
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        hr = HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr)) DeleteFileW(temporary.c_str());
    return hr;
}
std::string RecordText(const Entry& entry) {
    return "{\"key\":" + Quote(entry.key) + ",\"type\":" + Quote(entry.type) + ",\"value\":" + Quote(entry.value) + "}";
}
// Journal record types beside the value types: removing one key, and clearing.
constexpr wchar_t RemoveRecord[] = L"Remove";
constexpr wchar_t ClearRecord[] = L"Clear";
HRESULT Seek(HANDLE file, size_t offset) {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}
}
// settings.json holds a snapshot; settings.json.journal holds one record per
// change since then. Writing the whole snapshot for every change made a game
// save (one Insert per pref, about 1,200 of them) stall its render thread for
// seconds, so changes append to the journal, which is replayed on load and
// folded into the snapshot (compacted) on load and once it grows large.
// A save also stores every pref again, mostly unchanged; storing an equal
// value adds no record.
class SettingsPersistence final {
    friend class PersistentPropertySet;
    static constexpr size_t CompactBytes = 1024 * 1024;
    std::wstring path_, journal_;
    // Opened on the first change and kept open: reopening per record cost
    // most of each Insert. Sharing is read-only, so no other writer interleaves.
    Handle journalFile_{INVALID_HANDLE_VALUE};
    size_t journalBytes_ = 0; // Length of the journal's complete, valid records.
    ComPtr<Map> map_;
    // The persisted form of every value, by key: what reloading would give.
    // Updated with each record, so compaction writes it without re-encoding.
    std::map<std::wstring, Entry> entries_;
    std::mutex mutex_;

public:
    explicit SettingsPersistence(std::wstring path) : path_(std::move(path)), journal_(path_ + L".journal") {}
    HRESULT Initialize(IPropertySet* values) {
        auto hr = LoadSnapshot(values);
        if (FAILED(hr)) return hr;
        std::string text;
        hr = ReadFileText(journal_, text);
        if (FAILED(hr)) return hr;
        if (hr == S_FALSE || text.empty()) return S_OK;
        hr = Replay(text);
        if (FAILED(hr)) return hr;
        // Fold the journal into the snapshot. If that fails the journal stays,
        // and replaying it again on the next load gives the same result.
        hr = Compact();
        if (FAILED(hr)) Log("FAIL", "settings compaction failed hr=%08lx", hr);
        return S_OK;
    }
    // Appends without flushing: the record reaches the OS file cache before the
    // change is visible, so it survives the process crashing or being killed.
    HRESULT Append(const Entry& entry) {
        if (journalFile_.value == INVALID_HANDLE_VALUE) {
            journalFile_.value = CreateFileW(journal_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                             FILE_ATTRIBUTE_NORMAL, nullptr);
            if (journalFile_.value == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
            // Drop anything after the valid records (a torn or malformed tail
            // left by a failed compaction) so new records stay readable.
            auto hr = Seek(journalFile_.value, journalBytes_);
            if (SUCCEEDED(hr) && !SetEndOfFile(journalFile_.value)) hr = HRESULT_FROM_WIN32(GetLastError());
            if (FAILED(hr)) {
                journalFile_.Close();
                return hr;
            }
        }
        auto text = RecordText(entry) + "\n";
        DWORD written{};
        if (!WriteFile(journalFile_.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
            written != text.size()) {
            auto hr = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
            // Cut a partial record off so later records remain readable.
            if (FAILED(Seek(journalFile_.value, journalBytes_)) || !SetEndOfFile(journalFile_.value))
                journalFile_.Close();
            return hr;
        }
        journalBytes_ += text.size();
        return S_OK;
    }
    bool Unchanged(const Entry& entry) const {
        auto stored = entries_.find(entry.key);
        return stored != entries_.end() && stored->second.type == entry.type && stored->second.value == entry.value;
    }
    // Call after the change is applied to map_; the journal already holds it.
    void CompactIfLarge() {
        if (journalBytes_ < CompactBytes) return;
        auto hr = Compact();
        if (FAILED(hr)) Log("FAIL", "settings compaction failed hr=%08lx", hr);
    }

private:
    HRESULT Replay(const std::string& text) {
        ComPtr<IPropertyValueStatics> props;
        auto hr =
            RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(), IID_PPV_ARGS(&props));
        if (FAILED(hr)) return hr;
        // Each record ends with a newline. A crash can leave the last record
        // without one; it was never applied, so it is dropped.
        size_t start = 0, applied = 0;
        for (auto end = text.find('\n'); end != std::string::npos; start = end + 1, end = text.find('\n', start)) {
            std::string line = text.substr(start, end - start);
            Entry entry;
            Reader reader(line);
            if (!reader.Record(entry) || !reader.End()) {
                Log("FAIL", "settings journal record %zu is malformed; ignoring the rest", applied);
                journalBytes_ = start;
                return S_OK;
            }
            HString key;
            hr = key.Set(entry.key.data(), static_cast<UINT32>(entry.key.size()));
            if (FAILED(hr)) return hr;
            if (entry.type == RemoveRecord) {
                boolean found{};
                if (SUCCEEDED(map_->HasKey(key.Get(), &found)) && found) hr = map_->Remove(key.Get());
                entries_.erase(entry.key);
            } else if (entry.type == ClearRecord) {
                hr = map_->Clear();
                entries_.clear();
            } else {
                ComPtr<IInspectable> value;
                hr = Decode(props.Get(), entry, &value);
                boolean replaced{};
                if (SUCCEEDED(hr)) hr = map_->Insert(key.Get(), value.Get(), &replaced);
                if (SUCCEEDED(hr)) entries_.insert_or_assign(entry.key, entry);
            }
            if (FAILED(hr)) return hr;
            ++applied;
        }
        if (start < text.size()) Log("TRACE", "settings journal: dropped an incomplete last record");
        journalBytes_ = start;
        return S_OK;
    }
    HRESULT Compact() {
        const auto started = GetTickCount64();
        std::string text;
        auto hr = Serialize(text);
        if (SUCCEEDED(hr)) hr = WriteAtomic(path_, text);
        if (FAILED(hr)) return hr;
        journalFile_.Close();
        if (!DeleteFileW(journal_.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            return HRESULT_FROM_WIN32(GetLastError());
        Log("TRACE", "settings compacted %zu journal bytes in %llu ms", journalBytes_, GetTickCount64() - started);
        journalBytes_ = 0;
        return S_OK;
    }
    HRESULT LoadSnapshot(IPropertySet* values) {
        auto hr = values->QueryInterface(IID_PPV_ARGS(&map_));
        if (FAILED(hr)) return hr;
        std::string text;
        hr = ReadFileText(path_, text);
        if (FAILED(hr)) return hr;
        if (hr == S_OK) {
            std::vector<Entry> entries;
            if (!Reader(text).Entries(entries)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            ComPtr<IPropertyValueStatics> props;
            hr = RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(),
                                        IID_PPV_ARGS(&props));
            if (FAILED(hr)) return hr;
            // Decode the complete file first so malformed records never produce a partially loaded map.
            std::vector<ComPtr<IInspectable>> decoded;
            for (const auto& entry : entries) {
                ComPtr<IInspectable> value;
                hr = Decode(props.Get(), entry, &value);
                if (FAILED(hr)) return hr;
                decoded.push_back(std::move(value));
            }
            for (size_t i = 0; i < entries.size(); ++i) {
                HString key;
                hr = key.Set(entries[i].key.data(), static_cast<UINT32>(entries[i].key.size()));
                if (FAILED(hr)) return hr;
                boolean replaced{};
                hr = map_->Insert(key.Get(), decoded[i].Get(), &replaced);
                if (FAILED(hr)) return hr;
                entries_.insert_or_assign(entries[i].key, entries[i]);
            }
        }
        return S_OK;
    }
    HRESULT Serialize(std::string& text) const {
        text = "[";
        bool first = true;
        for (const auto& stored : entries_) {
            if (!first) text += ',';
            first = false;
            text += "\n" + RecordText(stored.second);
        }
        text += "\n]\n";
        return text.size() > 16 * 1024 * 1024 ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) : S_OK;
    }
};
namespace {
template <typename Fn> HRESULT Guard(Fn&& fn) noexcept {
    try {
        return fn();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}
class MapChangedArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, IMapChangedEventArgs<HSTRING>, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.MapChangedEventArgs", BaseTrust);
    CollectionChange change_;
    HString key_;

public:
    explicit MapChangedArgs(CollectionChange change) : change_(change) {}
    HRESULT Initialize(HSTRING key) { return key_.Set(key); }
    HRESULT STDMETHODCALLTYPE get_CollectionChange(CollectionChange* value) override {
        if (!value) return E_POINTER;
        *value = change_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Key(HSTRING* value) override {
        if (!value) return E_POINTER;
        return key_.CopyTo(value);
    }
};
}
// All required PropertySet interfaces share one COM identity. The native backing map
// supplies SDK-compatible views/iterators; writes stay here so Wine's unimplemented
// native MapChanged event is never needed for persistence.
class PersistentPropertySet final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IPropertySet, Map, Observable, IIterable<Pair*>, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.PropertySet", BaseTrust);
    std::shared_ptr<SettingsPersistence> state_;
    EventSource<Changed> changed_;
    HRESULT Notify(CollectionChange change, HSTRING key) {
        auto args = Make<MapChangedArgs>(change);
        if (!args) return E_OUTOFMEMORY;
        auto hr = args->Initialize(key);
        if (FAILED(hr)) return hr;
        return changed_.InvokeAll(this, args.Get());
    }

public:
    explicit PersistentPropertySet(std::shared_ptr<SettingsPersistence> state) : state_(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(state_->mutex_);
        return state_->map_->Lookup(key, value);
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(state_->mutex_);
        return state_->map_->get_Size(value);
    }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING key, boolean* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(state_->mutex_);
        return state_->map_->HasKey(key, value);
    }
    HRESULT STDMETHODCALLTYPE GetView(IMapView<HSTRING, IInspectable*>** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(state_->mutex_);
        return state_->map_->GetView(value);
    }
    HRESULT STDMETHODCALLTYPE First(IIterator<Pair*>** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(state_->mutex_);
        ComPtr<IIterable<Pair*>> iterable;
        auto hr = state_->map_.As(&iterable);
        return FAILED(hr) ? hr : iterable->First(value);
    }
    HRESULT STDMETHODCALLTYPE Insert(HSTRING key, IInspectable* value, boolean* replaced) override {
        if (!replaced) return E_POINTER;
        *replaced = false;
        return Guard([&]() -> HRESULT {
            Entry entry;
            auto hr = Encode(value, entry);
            if (FAILED(hr)) return hr;
            entry.key = String(key);
            {
                std::lock_guard<std::mutex> lock(state_->mutex_);
                // An equal value is already persisted. It still replaces the
                // stored object and notifies, as the native map would.
                const bool unchanged = state_->Unchanged(entry);
                if (!unchanged) {
                    // Journal first: a change that cannot be recorded is not applied.
                    hr = state_->Append(entry);
                    if (FAILED(hr)) {
                        Log("FAIL", "settings save failed hr=%08lx", hr);
                        return hr;
                    }
                    state_->entries_.insert_or_assign(entry.key, entry);
                }
                hr = state_->map_->Insert(key, value, replaced);
                if (FAILED(hr)) return hr;
                if (!unchanged) state_->CompactIfLarge();
            }
            return Notify(*replaced ? CollectionChange_ItemChanged : CollectionChange_ItemInserted, key);
        });
    }
    HRESULT STDMETHODCALLTYPE Remove(HSTRING key) override {
        return Guard([&]() -> HRESULT {
            {
                std::lock_guard<std::mutex> lock(state_->mutex_);
                boolean found{};
                auto hr = state_->map_->HasKey(key, &found);
                if (FAILED(hr)) return hr;
                if (!found) return state_->map_->Remove(key); // The backing map's error for a missing key.
                auto name = String(key);
                hr = state_->Append({name, RemoveRecord, {}});
                if (FAILED(hr)) {
                    Log("FAIL", "settings save failed hr=%08lx", hr);
                    return hr;
                }
                state_->entries_.erase(name);
                hr = state_->map_->Remove(key);
                if (FAILED(hr)) return hr;
                state_->CompactIfLarge();
            }
            return Notify(CollectionChange_ItemRemoved, key);
        });
    }
    HRESULT STDMETHODCALLTYPE Clear() override {
        return Guard([&]() -> HRESULT {
            {
                std::lock_guard<std::mutex> lock(state_->mutex_);
                auto hr = state_->Append({{}, ClearRecord, {}});
                if (FAILED(hr)) {
                    Log("FAIL", "settings save failed hr=%08lx", hr);
                    return hr;
                }
                state_->entries_.clear();
                hr = state_->map_->Clear();
                if (FAILED(hr)) return hr;
                state_->CompactIfLarge();
            }
            return Notify(CollectionChange_Reset, nullptr);
        });
    }
    HRESULT STDMETHODCALLTYPE add_MapChanged(Changed* handler, EventRegistrationToken* token) override {
        if (!handler || !token) return E_POINTER;
        return changed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_MapChanged(EventRegistrationToken token) override {
        return changed_.Remove(token);
    }
};
HRESULT CreatePersistentSettings(const std::wstring& path, ComPtr<IPropertySet>& values,
                                 std::shared_ptr<SettingsPersistence>& persistence) {
    return Guard([&]() -> HRESULT {
        ComPtr<IInspectable> object;
        auto hr = RoActivateInstance(HStringReference(L"Windows.Foundation.Collections.PropertySet").Get(), &object);
        if (FAILED(hr)) return hr;
        ComPtr<IPropertySet> set;
        hr = object.As(&set);
        if (FAILED(hr)) return hr;
        auto state = std::make_shared<SettingsPersistence>(path);
        hr = state->Initialize(set.Get());
        if (FAILED(hr)) {
            Log("FAIL", "settings load failed hr=%08lx", hr);
            return hr;
        }
        auto wrapper = Make<PersistentPropertySet>(state);
        if (!wrapper) return E_OUTOFMEMORY;
        hr = wrapper.As(&values);
        if (FAILED(hr)) return hr;
        persistence = std::move(state);
        return S_OK;
    });
}
}
