#include "generated/xaml.h"
#include "com/factory.h"
#include "com/composable.h"
#include "host/host.h"
#include "registry.h"
#include <windows.applicationmodel.activation.h>
#include <windows.ui.xaml.markup.h>
#include <wrl/event.h>
#include <thread>
#include <atomic>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::ApplicationModel::Activation;
namespace ctr2 {
static ComPtr<IApplication> current;
HRESULT CurrentApplication(IApplication** value) {
    if (!value) return E_POINTER;
    return current.CopyTo(value);
}
class LaunchArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, ILaunchActivatedEventArgs, IActivatedEventArgs> {
    InspectableClass(L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_Arguments(HSTRING* v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TileId(HSTRING* v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Kind(ActivationKind* v) override {
        if (!v) return E_POINTER;
        *v = ActivationKind_Launch;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PreviousExecutionState(ApplicationExecutionState* v) override {
        if (!v) return E_POINTER;
        *v = ApplicationExecutionState_NotRunning;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SplashScreen(ISplashScreen** v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        return S_OK;
    }
};
class InitParams final : public RuntimeClass<RuntimeClassFlags<WinRt>, IApplicationInitializationCallbackParams> {
    InspectableClass(L"Windows.UI.Xaml.ApplicationInitializationCallbackParams", BaseTrust);
};
class Application final : public Composable<Application, DefaultIApplication, DefaultIApplicationOverrides> {
    EventSource<ISuspendingEventHandler> suspending_;
    EventSource<ABI::Windows::Foundation::IEventHandler<IInspectable*>> resuming_;
    EventSource<IUnhandledExceptionEventHandler> unhandled_;
    ComPtr<IResourceDictionary> resources_;
    ApplicationTheme theme_ = ApplicationTheme_Light;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Application";
    explicit Application(IInspectable* outer) : Composable(outer) {}
    HRESULT STDMETHODCALLTYPE get_Resources(IResourceDictionary** value) override {
        if (!value) return E_POINTER;
        return resources_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Resources(IResourceDictionary* value) override {
        resources_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RequestedTheme(ApplicationTheme* value) override {
        if (!value) return E_POINTER;
        *value = theme_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RequestedTheme(ApplicationTheme value) override {
        theme_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_UnhandledException(IUnhandledExceptionEventHandler* handler,
                                                     EventRegistrationToken* token) override {
        return unhandled_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_UnhandledException(EventRegistrationToken token) override {
        return unhandled_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Suspending(ISuspendingEventHandler* handler, EventRegistrationToken* token) override {
        return suspending_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Suspending(EventRegistrationToken token) override {
        return suspending_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Resuming(ABI::Windows::Foundation::IEventHandler<IInspectable*>* handler,
                                           EventRegistrationToken* token) override {
        return resuming_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Resuming(EventRegistrationToken token) override { return resuming_.Remove(token); }
    HRESULT STDMETHODCALLTYPE Exit() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnLaunched(ILaunchActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnActivated(IActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnWindowCreated(IWindowCreatedEventArgs*) override { return S_OK; }
};
class ApplicationFactory final : public Factory<DefaultIApplicationFactory, DefaultIApplicationStatics> {
public:
    ApplicationFactory() : Factory(L"Windows.UI.Xaml.Application") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IApplication** value) override {
        auto hr = Compose<Application>(outer, inner, value);
        if (SUCCEEDED(hr) && HostWindow()) current = *value;
        Log("TRACE", "ApplicationFactory::CreateInstance hr=%08lx", hr);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Current(IApplication** value) override {
        if (!value) return E_POINTER;
        return current.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE LoadComponent(IInspectable* component,
                                            ABI::Windows::Foundation::IUriRuntimeClass* uri) override {
        if (!uri) return E_INVALIDARG;
        HSTRING text{};
        uri->get_AbsoluteUri(&text);
        Log("TRACE", "Application::LoadComponent %ls", WindowsGetStringRawBuffer(text, nullptr));
        const bool movie = wcscmp(WindowsGetStringRawBuffer(text, nullptr), L"ms-appx:///MovieMgrPage.xaml") == 0;
        WindowsDeleteString(text);
        if (movie && component) {
            // The compiled XAML's connection 1 installs MediaOpened/Ended/Failed
            // and PointerPressed handlers. FindName alone only binds the field.
            ComPtr<ABI::Windows::UI::Xaml::Markup::IComponentConnector> connector;
            auto hr = component->QueryInterface(IID_PPV_ARGS(&connector));
            if (FAILED(hr)) return hr;
            ComPtr<IFrameworkElement> element;
            hr = component->QueryInterface(IID_PPV_ARGS(&element));
            if (FAILED(hr)) return hr;
            ComPtr<IInspectable> media;
            hr = element->FindName(Wrappers::HStringReference(L"mediaElement").Get(), &media);
            if (FAILED(hr)) return hr;
            return connector->Connect(1, media.Get());
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE LoadComponentWithResourceLocation(
        IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri,
        ABI::Windows::UI::Xaml::Controls::Primitives::ComponentResourceLocation) override {
        return LoadComponent(component, uri);
    }
    HRESULT STDMETHODCALLTYPE Start(IApplicationInitializationCallback* callback) override {
        if (!callback) return E_INVALIDARG;
        static std::atomic_flag running = ATOMIC_FLAG_INIT;
        if (running.test_and_set()) return E_ILLEGAL_METHOD_CALL;
        Log("TRACE", "ApplicationStatics::Start");
        HRESULT hr = E_FAIL;
        ComPtr<IApplicationInitializationCallback> retained(callback);
        try {
            std::thread view([&] {
                auto initialized = RoInitialize(RO_INIT_SINGLETHREADED);
                if (FAILED(initialized)) {
                    hr = initialized;
                    return;
                }
                try {
                    hr = RunHost([&]() -> HRESULT {
                        auto params = Make<InitParams>();
                        if (!params) return E_OUTOFMEMORY;
                        auto result = retained->Invoke(params.Get());
                        if (FAILED(result)) return result;
                        if (!current) return E_UNEXPECTED;
                        ComPtr<IApplicationOverrides> overrides;
                        result = current.As(&overrides);
                        if (FAILED(result)) return result;
                        auto args = Make<LaunchArgs>();
                        if (!args) return E_OUTOFMEMORY;
                        result = overrides->OnLaunched(args.Get());
                        Log("TRACE", "OnLaunched hr=%08lx", result);
                        return result;
                    });
                } catch (const std::bad_alloc&) {
                    hr = E_OUTOFMEMORY;
                } catch (...) {
                    hr = E_FAIL;
                }
                current.Reset();
                RoUninitialize();
            });
            view.join();
        } catch (const std::bad_alloc&) {
            hr = E_OUTOFMEMORY;
        } catch (...) {
            hr = E_FAIL;
        }
        running.clear();
        return hr;
    }
};
HRESULT MakeApplicationFactory(IActivationFactory** value) {
    auto factory = Make<ApplicationFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
