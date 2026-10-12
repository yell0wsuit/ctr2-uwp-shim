#include "registry.h"
#include "com/factory.h"
#include "generated/streams.h"
#include <robuffer.h>
#include <mutex>
#include <cstring>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Storage::Streams;
using ABI::Windows::Foundation::IClosable;
using Windows::Storage::Streams::IBufferByteAccess;
namespace ctr2 {
namespace {
class BufferReader final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIDataReader, IClosable, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.DataReader", BaseTrust);
    std::mutex mutex_;
    ComPtr<IBuffer> buffer_;
    ComPtr<IBufferByteAccess> access_;
    UINT32 length_ = 0, position_ = 0;
    bool closed_ = false;
    UnicodeEncoding encoding_ = UnicodeEncoding_Utf8;
    ByteOrder byteOrder_ = ByteOrder_LittleEndian;
    InputStreamOptions options_ = InputStreamOptions_None;

public:
    HRESULT Initialize(IBuffer* buffer) {
        if (!buffer) return E_INVALIDARG;
        UINT32 capacity = 0;
        auto hr = buffer->get_Length(&length_);
        if (FAILED(hr)) return hr;
        hr = buffer->get_Capacity(&capacity);
        if (FAILED(hr)) return hr;
        if (length_ > capacity) return E_INVALIDARG;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&access_));
        if (FAILED(hr)) return hr;
        buffer_ = buffer;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_UnconsumedBufferLength(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = length_ - position_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReadByte(BYTE* value) override { return ReadBytes(1, value); }
    HRESULT STDMETHODCALLTYPE ReadBytes(UINT32 count, BYTE* value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (count && !value) return E_POINTER;
        if (count > length_ - position_) return E_BOUNDS;
        if (count) {
            BYTE* bytes = nullptr;
            auto hr = access_->Buffer(&bytes);
            if (FAILED(hr)) return hr;
            if (!bytes) return E_POINTER;
            memcpy(value, bytes + position_, count);
            position_ += count;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_UnicodeEncoding(UnicodeEncoding* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = encoding_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_UnicodeEncoding(UnicodeEncoding value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (value != UnicodeEncoding_Utf8 && value != UnicodeEncoding_Utf16LE && value != UnicodeEncoding_Utf16BE)
            return E_INVALIDARG;
        encoding_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ByteOrder(ByteOrder* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = byteOrder_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ByteOrder(ByteOrder value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (value != ByteOrder_LittleEndian && value != ByteOrder_BigEndian) return E_INVALIDARG;
        byteOrder_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InputStreamOptions(InputStreamOptions* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = options_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_InputStreamOptions(InputStreamOptions value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (static_cast<unsigned>(value) > 3) return E_INVALIDARG;
        options_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DetachBuffer(IBuffer** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = buffer_.Detach();
        access_.Reset();
        length_ = position_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        buffer_.Reset();
        access_.Reset();
        length_ = position_ = 0;
        return S_OK;
    }
};
class ReaderFactory final : public Factory<IDataReaderStatics> {
public:
    ReaderFactory() : Factory(L"Windows.Storage.Streams.DataReader") {}
    HRESULT STDMETHODCALLTYPE FromBuffer(IBuffer* buffer, IDataReader** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto reader = Make<BufferReader>();
        if (!reader) return E_OUTOFMEMORY;
        auto hr = reader->Initialize(buffer);
        if (FAILED(hr)) return hr;
        return reader.CopyTo(value);
    }
};
}
HRESULT MakeDataReaderFactory(IActivationFactory** value) {
    auto factory = Make<ReaderFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
