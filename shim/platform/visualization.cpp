#include "registry.h"
#include "com/factory.h"
#include <windows.ui.input.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Input;
namespace ctr2 {
class Visualization final : public RuntimeClass<RuntimeClassFlags<WinRt>, IPointerVisualizationSettings> {
    InspectableClass(L"Windows.UI.Input.PointerVisualizationSettings", BaseTrust);
    boolean contact_ = true, barrel_ = true;

public:
    HRESULT STDMETHODCALLTYPE put_IsContactFeedbackEnabled(boolean value) override {
        contact_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsContactFeedbackEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = contact_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsBarrelButtonFeedbackEnabled(boolean value) override {
        barrel_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsBarrelButtonFeedbackEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = barrel_;
        return S_OK;
    }
};
class VisualizationFactory final : public Factory<IPointerVisualizationSettingsStatics> {
    ComPtr<Visualization> settings_ = Make<Visualization>();

public:
    VisualizationFactory() : Factory(L"Windows.UI.Input.PointerVisualizationSettings") {}
    HRESULT STDMETHODCALLTYPE GetForCurrentView(IPointerVisualizationSettings** value) override {
        if (!value) return E_POINTER;
        return settings_ ? settings_.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeVisualizationFactory(IActivationFactory** value) {
    static auto factory = Make<VisualizationFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
