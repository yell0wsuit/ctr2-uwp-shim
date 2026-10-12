#include "generated/coreapp.h"
#include "com/factory.h"
#include "host/host.h"
#include "registry.h"
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::ApplicationModel::Core;
namespace ctr2 {
HRESULT MakeCoreWindow(ABI::Windows::UI::Core::ICoreWindow** value);
class CoreView final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultICoreApplicationView, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Core.CoreApplicationView", BaseTrust);
    EventSource<
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs>
        activated_;

public:
    HRESULT STDMETHODCALLTYPE get_CoreWindow(ABI::Windows::UI::Core::ICoreWindow** value) override {
        return MakeCoreWindow(value);
    }
    HRESULT STDMETHODCALLTYPE get_IsMain(boolean* value) override {
        if (!value) return E_POINTER;
        *value = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsHosted(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Activated(
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return activated_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) override {
        return activated_.Remove(token);
    }
};
class CoreAppFactory final
    : public Factory<DefaultICoreApplication, DefaultICoreApplicationExit, DefaultICoreImmersiveApplication> {
    EventSource<ABI::Windows::Foundation::IEventHandler<ABI::Windows::ApplicationModel::SuspendingEventArgs*>>
        suspending_;
    EventSource<ABI::Windows::Foundation::IEventHandler<IInspectable*>> resuming_, exiting_;

public:
    HRESULT STDMETHODCALLTYPE get_MainView(ICoreApplicationView** value) override {
        if (!value) return E_POINTER;
        static auto view = Make<CoreView>();
        return view ? view.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentView(ICoreApplicationView** value) override { return get_MainView(value); }
    CoreAppFactory() : Factory(L"Windows.ApplicationModel.Core.CoreApplication") {}
    HRESULT STDMETHODCALLTYPE get_Id(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"ctr2", 4, value);
    }
    HRESULT STDMETHODCALLTYPE
    add_Suspending(ABI::Windows::Foundation::IEventHandler<ABI::Windows::ApplicationModel::SuspendingEventArgs*>* h,
                   EventRegistrationToken* t) override {
        return suspending_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Suspending(EventRegistrationToken t) override { return suspending_.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_Resuming(ABI::Windows::Foundation::IEventHandler<IInspectable*>* h,
                                           EventRegistrationToken* t) override {
        return resuming_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Resuming(EventRegistrationToken t) override { return resuming_.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_Exiting(ABI::Windows::Foundation::IEventHandler<IInspectable*>* h,
                                          EventRegistrationToken* t) override {
        return exiting_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Exiting(EventRegistrationToken t) override { return exiting_.Remove(t); }
    HRESULT STDMETHODCALLTYPE Exit() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
};
HRESULT MakeCoreApplicationFactory(IActivationFactory** value) {
    static auto factory = Make<CoreAppFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
