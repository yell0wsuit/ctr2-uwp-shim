// These legacy contracts are required by the Windows 8.1 executable.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "generated/display.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Graphics::Display;
namespace ctr2 {
using DisplayHandler = ABI::Windows::Foundation::ITypedEventHandler<DisplayInformation*, IInspectable*>;
class Display final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIDisplayInformation, FtmBase> {
    InspectableClass(L"Windows.Graphics.Display.DisplayInformation", BaseTrust);
    EventSource<DisplayHandler> orientation_, dpi_, stereo_, color_;

public:
    HRESULT STDMETHODCALLTYPE get_CurrentOrientation(DisplayOrientations* v) override {
        if (!v) return E_POINTER;
        *v = DisplayOrientations_Landscape;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NativeOrientation(DisplayOrientations* v) override {
        return get_CurrentOrientation(v);
    }
    HRESULT STDMETHODCALLTYPE get_ResolutionScale(ResolutionScale* v) override {
        if (!v) return E_POINTER;
        *v = ResolutionScale_Scale100Percent;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LogicalDpi(FLOAT* v) override {
        if (!v) return E_POINTER;
        *v = 96;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RawDpiX(FLOAT* v) override { return get_LogicalDpi(v); }
    HRESULT STDMETHODCALLTYPE get_RawDpiY(FLOAT* v) override { return get_LogicalDpi(v); }
    HRESULT STDMETHODCALLTYPE get_StereoEnabled(boolean* v) override {
        if (!v) return E_POINTER;
        *v = false;
        return S_OK;
    }
#define DISPLAY_EVENT(Name, field)                                                                                     \
    HRESULT STDMETHODCALLTYPE add_##Name(DisplayHandler* h, EventRegistrationToken* t) override {                      \
        return field.Add(h, t);                                                                                        \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE remove_##Name(EventRegistrationToken t) override { return field.Remove(t); }
    DISPLAY_EVENT(OrientationChanged, orientation_)
    DISPLAY_EVENT(DpiChanged, dpi_)
    DISPLAY_EVENT(StereoEnabledChanged, stereo_)
    DISPLAY_EVENT(ColorProfileChanged, color_)
#undef DISPLAY_EVENT
};
class DisplayFactory final : public Factory<DefaultIDisplayInformationStatics, DefaultIDisplayPropertiesStatics> {
    ComPtr<Display> display_ = Make<Display>();
    DisplayOrientations rotation_ = DisplayOrientations_Landscape;
    EventSource<DisplayHandler> invalidated_;
    EventSource<IDisplayPropertiesEventHandler> oldOrientation_, oldDpi_, oldStereo_, oldColor_, oldInvalidated_;

public:
    DisplayFactory() : Factory(L"Windows.Graphics.Display.DisplayInformation") {}
    HRESULT STDMETHODCALLTYPE GetForCurrentView(IDisplayInformation** v) override {
        if (!v) return E_POINTER;
        return display_ ? display_.CopyTo(v) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_AutoRotationPreferences(DisplayOrientations* v) override {
        if (!v) return E_POINTER;
        *v = rotation_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AutoRotationPreferences(DisplayOrientations v) override {
        rotation_ = v;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CurrentOrientation(DisplayOrientations* v) override {
        return display_->get_CurrentOrientation(v);
    }
    HRESULT STDMETHODCALLTYPE get_NativeOrientation(DisplayOrientations* v) override {
        return display_->get_NativeOrientation(v);
    }
    HRESULT STDMETHODCALLTYPE get_ResolutionScale(ResolutionScale* v) override {
        return display_->get_ResolutionScale(v);
    }
    HRESULT STDMETHODCALLTYPE get_LogicalDpi(FLOAT* v) override { return display_->get_LogicalDpi(v); }
    HRESULT STDMETHODCALLTYPE get_StereoEnabled(boolean* v) override { return display_->get_StereoEnabled(v); }
    HRESULT STDMETHODCALLTYPE add_DisplayContentsInvalidated(DisplayHandler* h, EventRegistrationToken* t) override {
        return invalidated_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_DisplayContentsInvalidated(EventRegistrationToken t) override {
        invalidated_.Remove(t);
        return oldInvalidated_.Remove(t);
    }
#define OLD_DISPLAY_EVENT(Name, field)                                                                                 \
    HRESULT STDMETHODCALLTYPE add_##Name(IDisplayPropertiesEventHandler* h, EventRegistrationToken* t) override {      \
        return field.Add(h, t);                                                                                        \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE remove_##Name(EventRegistrationToken t) override { return field.Remove(t); }
    OLD_DISPLAY_EVENT(OrientationChanged, oldOrientation_)
    OLD_DISPLAY_EVENT(LogicalDpiChanged, oldDpi_)
    OLD_DISPLAY_EVENT(StereoEnabledChanged, oldStereo_)
    OLD_DISPLAY_EVENT(ColorProfileChanged, oldColor_)
#undef OLD_DISPLAY_EVENT
    HRESULT STDMETHODCALLTYPE add_DisplayContentsInvalidated(IDisplayPropertiesEventHandler* h,
                                                             EventRegistrationToken* t) override {
        return oldInvalidated_.Add(h, t);
    }
};
HRESULT MakeDisplayFactory(IActivationFactory** v) {
    static auto factory = Make<DisplayFactory>();
    return factory ? factory.CopyTo(v) : E_OUTOFMEMORY;
}
}
