#include "registry.h"
#include "com/factory.h"
#include <windows.ui.core.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Core;
namespace ctr2 {
class Cursor final : public RuntimeClass<RuntimeClassFlags<WinRt>, ICoreCursor, FtmBase> {
    InspectableClass(L"Windows.UI.Core.CoreCursor", BaseTrust);
    CoreCursorType type_;
    UINT32 id_;

public:
    Cursor(CoreCursorType type, UINT32 id) : type_(type), id_(id) {}
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = id_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Type(CoreCursorType* value) override {
        if (!value) return E_POINTER;
        *value = type_;
        return S_OK;
    }
};
class CursorFactory final : public Factory<ICoreCursorFactory> {
public:
    CursorFactory() : Factory(L"Windows.UI.Core.CoreCursor") {}
    HRESULT STDMETHODCALLTYPE CreateCursor(CoreCursorType type, UINT32 id, ICoreCursor** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (type < CoreCursorType_Arrow || type > CoreCursorType_Custom) return E_INVALIDARG;
        auto cursor = Make<Cursor>(type, id);
        return cursor ? cursor.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeCursorFactory(IActivationFactory** value) {
    auto factory = Make<CursorFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
