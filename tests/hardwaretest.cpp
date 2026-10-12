#include <windows.h>
#include <roapi.h>
#include <windows.system.profile.h>
#include <robuffer.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <array>
#include <cstdio>
#include <cstring>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::System::Profile;
using ABI::Windows::Storage::Streams::IBuffer;
using Windows::Storage::Streams::IBufferByteAccess;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    HRESULT hr = get(HStringReference(L"Windows.System.Profile.HardwareIdentification").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL HardwareIdentification factory hr=%08lx\n", hr);
        return 2;
    }
    ComPtr<IHardwareIdentificationStatics> statics;
    if (FAILED(factory.As(&statics))) return 3;
    if (statics->GetPackageSpecificToken(nullptr, nullptr) != E_POINTER) return 4;
    std::array<std::array<byte, 16>, 3> initial{};
    for (int repeat = 0; repeat < 2; ++repeat) {
        ComPtr<IHardwareToken> token;
        if (FAILED(statics->GetPackageSpecificToken(nullptr, &token)) || !token) return 5;
        if (token->get_Id(nullptr) != E_POINTER || token->get_Signature(nullptr) != E_POINTER ||
            token->get_Certificate(nullptr) != E_POINTER)
            return 6;
        ComPtr<IBuffer> buffers[3];
        if (FAILED(token->get_Id(&buffers[0])) || FAILED(token->get_Signature(&buffers[1])) ||
            FAILED(token->get_Certificate(&buffers[2])))
            return 7;
        for (int i = 0; i < 3; ++i) {
            if (!buffers[i]) return 8;
            UINT32 size = 0, capacity = 0;
            if (FAILED(buffers[i]->get_Length(&size)) || size != 16 || FAILED(buffers[i]->get_Capacity(&capacity)) ||
                capacity != 16)
                return 9;
            ComPtr<IBufferByteAccess> access;
            if (FAILED(buffers[i].As(&access))) return 10;
            if (buffers[i]->get_Length(nullptr) != E_POINTER || buffers[i]->get_Capacity(nullptr) != E_POINTER ||
                access->Buffer(nullptr) != E_POINTER)
                return 15;
            byte* bytes = nullptr;
            if (FAILED(access->Buffer(&bytes)) || !bytes) return 11;
            if (repeat == 0)
                memcpy(initial[i].data(), bytes, 16);
            else if (memcmp(initial[i].data(), bytes, 16) != 0)
                return 12;
            if (buffers[i]->put_Length(17) != E_INVALIDARG || FAILED(buffers[i]->put_Length(8)) ||
                FAILED(buffers[i]->get_Length(&size)) || size != 8)
                return 13;
            memset(bytes, 0, 16); // A consumer's writable buffer must not mutate later tokens.
        }
    }
    const byte expectedId[16] = {'c', 't', 'r', '2', '-', 'w', 'i', 'n', 'e', '-', 's', 'h', 'i', 'm', 0, 1};
    if (memcmp(initial[0].data(), expectedId, 16) != 0) return 14;
    puts("hardwaretest: 0 failure(s)");
    return 0;
}
