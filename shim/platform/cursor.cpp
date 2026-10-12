#include "cursor.h"
#include "registry.h"
#include "log.h"
#include "com/factory.h"
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
        // Windows 8.1's cursor types; Pin and Person came with Windows 10.
        if (type < CoreCursorType_Arrow || type > CoreCursorType_Wait) return E_INVALIDARG;
        auto cursor = Make<Cursor>(type, id);
        return cursor ? cursor.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HCURSOR LoadCoreCursor(ICoreCursor* cursor) noexcept {
    const HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW);
    CoreCursorType type = CoreCursorType_Arrow;
    UINT32 id = 0;
    if (!cursor || FAILED(cursor->get_Type(&type)) || FAILED(cursor->get_Id(&id))) return arrow;
    if (type == CoreCursorType_Custom) {
        // Store apps compile custom cursors into the executable; Game.exe has 101 and 102.
        HCURSOR custom = LoadCursorW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id));
        if (!custom) Log("FAIL", "custom cursor %u is not in the executable; showing the arrow", id);
        return custom ? custom : arrow;
    }
    static const wchar_t* const system[] = {
        IDC_ARROW,    IDC_CROSS,  nullptr,      IDC_HAND,   IDC_HELP, IDC_IBEAM,   IDC_SIZEALL,
        IDC_SIZENESW, IDC_SIZENS, IDC_SIZENWSE, IDC_SIZEWE, IDC_NO,   IDC_UPARROW, IDC_WAIT,
    };
    const auto index = static_cast<size_t>(type);
    HCURSOR result = index < ARRAYSIZE(system) && system[index] ? LoadCursorW(nullptr, system[index]) : nullptr;
    return result ? result : arrow;
}
HRESULT MakeCursorFactory(IActivationFactory** value) {
    auto factory = Make<CursorFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
