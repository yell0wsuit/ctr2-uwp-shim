#pragma once
#include "async_id.h"
#include "log.h"
#include <windows.foundation.h>
#include <wrl.h>
#include <mutex>
#include <atomic>
namespace ctr2 {
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
class AsyncAction final : public RuntimeClass<RuntimeClassFlags<WinRt>, IAsyncAction, IAsyncInfo, FtmBase> {
    InspectableClass(L"Windows.Foundation.IAsyncAction", BaseTrust);
    const UINT32 id_ = NextAsyncId();
    std::mutex mutex_;
    AsyncStatus status_ = AsyncStatus::Started;
    HRESULT error_ = S_OK;
    ComPtr<IAsyncActionCompletedHandler> completed_;
    bool assigned_ = false, closed_ = false;

public:
    void Finish(HRESULT error) {
        ComPtr<IAsyncActionCompletedHandler> callback;
        AsyncStatus status;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (status_ != AsyncStatus::Started) return;
            error_ = error;
            status_ = FAILED(error) ? AsyncStatus::Error : AsyncStatus::Completed;
            status = status_;
            callback = completed_;
        }
        if (callback) callback->Invoke(this, status);
    }
    HRESULT STDMETHODCALLTYPE put_Completed(IAsyncActionCompletedHandler* handler) override {
        AsyncStatus status;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (assigned_) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
            assigned_ = true;
            completed_ = handler;
            status = status_;
        }
        if (handler && status != AsyncStatus::Started) handler->Invoke(this, status);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(IAsyncActionCompletedHandler** value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> guard(mutex_);
        *value = nullptr;
        return closed_ ? RO_E_CLOSED : completed_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetResults() override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (status_ == AsyncStatus::Started) return E_ILLEGAL_METHOD_CALL;
        return status_ == AsyncStatus::Canceled ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : error_;
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> guard(mutex_);
        *value = id_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> guard(mutex_);
        *value = status_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> guard(mutex_);
        *value = error_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override {
        Log("TRACE", "AsyncAction %u Cancel", id_);
        // Cancellation is cooperative. WorkItemHandler observes this status.
        ComPtr<IAsyncActionCompletedHandler> callback;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (status_ != AsyncStatus::Started) return S_OK;
            status_ = AsyncStatus::Canceled;
            callback = completed_;
        }
        if (callback) callback->Invoke(this, AsyncStatus::Canceled);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (status_ == AsyncStatus::Started) return E_ILLEGAL_METHOD_CALL;
        closed_ = true;
        completed_.Reset();
        return S_OK;
    }
};

}
