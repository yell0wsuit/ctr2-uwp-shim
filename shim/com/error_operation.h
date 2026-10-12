#pragma once
#include "async_id.h"
#include <windows.foundation.h>
#include <wrl.h>
#include <atomic>
#include <mutex>
namespace ctr2 {
template <class T, class TResult = T>
class ErrorOperation final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          ABI::Windows::Foundation::IAsyncOperation<T>,
                                          ABI::Windows::Foundation::IAsyncInfo, Microsoft::WRL::FtmBase> {
    using RuntimeClassT = Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                                       ABI::Windows::Foundation::IAsyncOperation<T>,
                                                       ABI::Windows::Foundation::IAsyncInfo, Microsoft::WRL::FtmBase>;
    InspectableClass(L"Windows.Foundation.IAsyncOperation`1", BaseTrust);
    using Handler = ABI::Windows::Foundation::IAsyncOperationCompletedHandler<T>;
    Microsoft::WRL::ComPtr<Handler> completed_;
    std::mutex mutex_;
    bool assigned_ = false, closed_ = false;
    HRESULT error_;
    const UINT32 id_ = NextAsyncId();

public:
    explicit ErrorOperation(HRESULT error = E_ACCESSDENIED) : error_(FAILED(error) ? error : E_FAIL) {}
    HRESULT STDMETHODCALLTYPE put_Completed(Handler* handler) override {
        Microsoft::WRL::ComPtr<Handler> callback(handler);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (assigned_) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
            assigned_ = true;
            completed_ = callback;
        }
        if (callback) callback->Invoke(this, ABI::Windows::Foundation::AsyncStatus::Error);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(Handler** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : completed_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetResults(TResult* value) override {
        if (!value) return E_POINTER;
        *value = {};
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : error_;
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = id_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(ABI::Windows::Foundation::AsyncStatus* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = ABI::Windows::Foundation::AsyncStatus::Error;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = error_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        Microsoft::WRL::ComPtr<Handler> releasing;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            releasing.Attach(completed_.Detach());
        }
        // Delegate destruction can reenter this operation; release after unlock.
        return S_OK;
    }
};
}
