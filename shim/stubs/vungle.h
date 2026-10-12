#pragma once
#include <windows.foundation.h>
#include <windows.applicationmodel.store.h>
// ABI projected from the supplied VungleSDK.winmd's IAdFactoryStatic and
// IVungleAdClass. Methods retain metadata order, including event accessors.
namespace ctr2::vungle {
struct __declspec(uuid("075ae55a-570e-55dc-6d2c-3afec4080034")) IVungleAd : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_Diagnostic(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_OnAdEnd(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_OnAdPlayableChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_OnAdStart(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_OnVideoView(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AdPlayable(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE PlayAdAsync(IInspectable*,
                                                  ABI::Windows::Foundation::IAsyncOperation<HSTRING>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Diagnostic(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_OnAdEnd(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_OnAdPlayableChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_OnAdStart(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_OnVideoView(EventRegistrationToken) = 0;
};
struct __declspec(uuid("881fa4e3-15d1-54f2-6fcd-5eeebf79d308")) IAdFactoryStatic : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetInstance(HSTRING, IVungleAd**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInstanceWithConfig(HSTRING, IInspectable*, IVungleAd**) = 0;
};
}
