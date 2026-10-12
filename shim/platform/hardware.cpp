#include "registry.h"
#include "com/factory.h"
#include <windows.system.profile.h>
#include <robuffer.h>
#include <array>
using namespace Microsoft::WRL;
using namespace ABI::Windows::System::Profile;
using ABI::Windows::Storage::Streams::IBuffer;
using Windows::Storage::Streams::IBufferByteAccess;
namespace ctr2 {
namespace {
// Synthetic compatibility data, not a physical identifier or a cryptographic
// signature/certificate. Fresh buffers prevent callers from changing the token.
constexpr std::array<byte, 16> tokenId = {'c', 't', 'r', '2', '-', 'w', 'i', 'n', 'e', '-', 's', 'h', 'i', 'm', 0, 1};
constexpr std::array<byte, 16> tokenSignature = {
    'c', 't', 'r', '2', '-', 'o', 'f', 'f', 'l', 'i', 'n', 'e', '-', 's', 'i', 'g',
};
constexpr std::array<byte, 16> tokenCertificate = {
    'c', 't', 'r', '2', '-', 'o', 'f', 'f', 'l', 'i', 'n', 'e', '-', 'c', 'r', 't',
};
class TokenBuffer final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IBuffer, IBufferByteAccess, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.Buffer", BaseTrust);
    std::array<byte, 16> bytes_;
    UINT32 length_ = 16;

public:
    explicit TokenBuffer(const std::array<byte, 16>& bytes) : bytes_(bytes) {}
    HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 16;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = length_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Length(UINT32 value) override {
        if (value > 16) return E_INVALIDARG;
        length_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Buffer(byte** value) noexcept override {
        if (!value) return E_POINTER;
        *value = bytes_.data();
        return S_OK;
    }
};
HRESULT CopyBuffer(const std::array<byte, 16>& bytes, IBuffer** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto buffer = Make<TokenBuffer>(bytes);
    return buffer ? buffer.CopyTo(value) : E_OUTOFMEMORY;
}
class HardwareToken final : public RuntimeClass<RuntimeClassFlags<WinRt>, IHardwareToken, FtmBase> {
    InspectableClass(L"Windows.System.Profile.HardwareToken", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_Id(IBuffer** value) override { return CopyBuffer(tokenId, value); }
    HRESULT STDMETHODCALLTYPE get_Signature(IBuffer** value) override { return CopyBuffer(tokenSignature, value); }
    HRESULT STDMETHODCALLTYPE get_Certificate(IBuffer** value) override { return CopyBuffer(tokenCertificate, value); }
};
class HardwareFactory final : public Factory<IHardwareIdentificationStatics> {
public:
    HardwareFactory() : Factory(L"Windows.System.Profile.HardwareIdentification") {}
    HRESULT STDMETHODCALLTYPE GetPackageSpecificToken(IBuffer* nonce, IHardwareToken** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        // Nonce-bound signatures are intentionally unsupported by this offline
        // compatibility token. The game requests an unbound token (null nonce).
        if (nonce) return E_NOTIMPL;
        auto token = Make<HardwareToken>();
        return token ? token.CopyTo(value) : E_OUTOFMEMORY;
    }
};
}
HRESULT MakeHardwareFactory(IActivationFactory** value) {
    auto factory = Make<HardwareFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
