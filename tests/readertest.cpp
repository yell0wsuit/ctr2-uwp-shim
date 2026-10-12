#include <windows.h>
#include <roapi.h>
#include <windows.system.profile.h>
#include <windows.storage.streams.h>
#include <robuffer.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <array>
#include <cstdio>
#include <cstring>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage::Streams;
using namespace ABI::Windows::System::Profile;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    HRESULT hr = get(HStringReference(L"Windows.Storage.Streams.DataReader").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL DataReader factory hr=%08lx\n", hr);
        return 2;
    }
    ComPtr<IDataReaderStatics> statics;
    if (FAILED(factory.As(&statics))) return 3;
    if (FAILED(get(HStringReference(L"Windows.System.Profile.HardwareIdentification").Get(), &factory))) return 4;
    ComPtr<IHardwareIdentificationStatics> hardware;
    if (FAILED(factory.As(&hardware))) return 5;
    ComPtr<IHardwareToken> token;
    if (FAILED(hardware->GetPackageSpecificToken(nullptr, &token))) return 6;
    ComPtr<IBuffer> buffer;
    if (FAILED(token->get_Id(&buffer))) return 7;
    ComPtr<IDataReader> reader;
    if (statics->FromBuffer(buffer.Get(), nullptr) != E_POINTER ||
        statics->FromBuffer(nullptr, &reader) != E_INVALIDARG || reader)
        return 8;
    if (FAILED(statics->FromBuffer(buffer.Get(), &reader)) || !reader) return 9;
    buffer.Reset();
    token.Reset(); // Reader retains the actual source buffer.
    UINT32 size = 0;
    if (FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 16) return 10;
    if (reader->get_UnconsumedBufferLength(nullptr) != E_POINTER || reader->ReadByte(nullptr) != E_POINTER ||
        reader->ReadBytes(1, nullptr) != E_POINTER)
        return 11;
    std::array<byte, 17> bytes{};
    bytes.fill(0xab);
    if (reader->ReadBytes(17, bytes.data()) != E_BOUNDS || bytes[0] != 0xab ||
        FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 16)
        return 12;
    byte first = 0;
    if (FAILED(reader->ReadByte(&first)) || first != 'c') return 13;
    if (FAILED(reader->ReadBytes(15, bytes.data()))) return 14;
    const byte expected[15] = {'t', 'r', '2', '-', 'w', 'i', 'n', 'e', '-', 's', 'h', 'i', 'm', 0, 1};
    if (memcmp(bytes.data(), expected, 15) != 0 || FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 0)
        return 15;
    if (reader->ReadByte(&first) != E_BOUNDS || FAILED(reader->ReadBytes(0, nullptr))) return 16;
    ByteOrder order{};
    UnicodeEncoding encoding{};
    InputStreamOptions options{};
    if (FAILED(reader->get_ByteOrder(&order)) || order != ByteOrder_LittleEndian ||
        FAILED(reader->get_UnicodeEncoding(&encoding)) || encoding != UnicodeEncoding_Utf8 ||
        FAILED(reader->get_InputStreamOptions(&options)) || options != InputStreamOptions_None)
        return 17;
    if (FAILED(reader->put_ByteOrder(ByteOrder_BigEndian)) || FAILED(reader->get_ByteOrder(&order)) ||
        order != ByteOrder_BigEndian || reader->put_ByteOrder(static_cast<ByteOrder>(99)) != E_INVALIDARG)
        return 18;
    if (FAILED(reader->put_UnicodeEncoding(UnicodeEncoding_Utf16BE)) ||
        FAILED(reader->get_UnicodeEncoding(&encoding)) || encoding != UnicodeEncoding_Utf16BE ||
        reader->put_UnicodeEncoding(static_cast<UnicodeEncoding>(99)) != E_INVALIDARG)
        return 19;
    if (FAILED(reader->put_InputStreamOptions(InputStreamOptions_ReadAhead)) ||
        FAILED(reader->get_InputStreamOptions(&options)) || options != InputStreamOptions_ReadAhead ||
        reader->put_InputStreamOptions(static_cast<InputStreamOptions>(99)) != E_INVALIDARG)
        return 20;
    if (reader->DetachBuffer(nullptr) != E_POINTER || FAILED(reader->DetachBuffer(&buffer)) || !buffer) return 21;
    if (FAILED(buffer->get_Length(&size)) || size != 16 || FAILED(reader->get_UnconsumedBufferLength(&size)) ||
        size != 0 || reader->ReadByte(&first) != E_BOUNDS)
        return 22;
    ComPtr<ABI::Windows::Foundation::IClosable> close;
    if (FAILED(reader.As(&close)) || FAILED(close->Close()) || FAILED(close->Close())) return 23;
    if (reader->get_UnconsumedBufferLength(&size) != RO_E_CLOSED || reader->ReadBytes(0, nullptr) != RO_E_CLOSED ||
        reader->DetachBuffer(&buffer) != RO_E_CLOSED || buffer)
        return 24;
    puts("readertest: 0 failure(s)");
    return 0;
}
