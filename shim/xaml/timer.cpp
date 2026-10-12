// Windows.UI.Xaml.DispatcherTimer. The OS class needs a real XAML core on the
// calling thread; activating it from the shim's host fails with
// RPC_E_WRONG_THREAD, which ends the game's intro-movie page.
#include "generated/xaml.h"
#include "com/composable.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl/event.h>
#include <map>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
namespace ctr2 {
namespace {
class DispatcherTimer;
struct RunningTimer {
    DispatcherTimer* timer;
    ComPtr<IUnknown> keepAlive; // A started timer stays alive until stopped, as with the OS dispatcher.
};
// Thread timers (SetTimer without a window) belong to the thread that started
// them and fire from its message loop, which is the dispatcher's UI thread.
std::map<UINT_PTR, RunningTimer>& RunningTimers() {
    static thread_local std::map<UINT_PTR, RunningTimer> timers;
    return timers;
}
class DispatcherTimer final : public Composable<DispatcherTimer, DefaultIDispatcherTimer> {
    EventSource<ABI::Windows::Foundation::IEventHandler<IInspectable*>> tick_;
    ABI::Windows::Foundation::TimeSpan interval_{};
    UINT_PTR timer_ = 0;
    const DWORD thread_ = GetCurrentThreadId();

    static void CALLBACK OnTimer(HWND, UINT, UINT_PTR id, DWORD) {
        auto found = RunningTimers().find(id);
        if (found == RunningTimers().end()) return;
        // A Tick handler may stop the timer and drop the last reference.
        auto keepAlive = found->second.keepAlive;
        auto* timer = found->second.timer;
        ComPtr<IInspectable> sender;
        keepAlive.As(&sender);
        auto hr = timer->tick_.InvokeAll(sender.Get(), nullptr);
        if (FAILED(hr)) Log("FAIL", "DispatcherTimer Tick handler hr=%08lx", hr);
    }
    UINT Milliseconds() const {
        // TimeSpan counts 100 ns units; SetTimer clamps below USER_TIMER_MINIMUM.
        auto ms = interval_.Duration / 10000;
        return ms <= 0 ? 0 : ms > USER_TIMER_MAXIMUM ? USER_TIMER_MAXIMUM : static_cast<UINT>(ms);
    }
    HRESULT Arm() {
        auto id = SetTimer(nullptr, timer_, Milliseconds(), OnTimer);
        if (!id) return HRESULT_FROM_WIN32(GetLastError());
        if (id != timer_) {
            RunningTimer running{this, nullptr};
            auto hr = QueryInterface(IID_PPV_ARGS(&running.keepAlive));
            if (FAILED(hr)) {
                KillTimer(nullptr, id);
                return hr;
            }
            RunningTimers()[id] = std::move(running);
            timer_ = id;
        }
        return S_OK;
    }

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.DispatcherTimer";
    explicit DispatcherTimer(IInspectable* outer) : Composable(outer) {}
    HRESULT STDMETHODCALLTYPE get_Interval(ABI::Windows::Foundation::TimeSpan* value) override {
        if (!value) return E_POINTER;
        *value = interval_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Interval(ABI::Windows::Foundation::TimeSpan value) override {
        if (value.Duration < 0) return E_INVALIDARG;
        interval_ = value;
        // A running timer restarts its period with the new interval.
        return timer_ ? Arm() : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = timer_ != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Tick(ABI::Windows::Foundation::IEventHandler<IInspectable*>* handler,
                                       EventRegistrationToken* token) override {
        return tick_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Tick(EventRegistrationToken token) override { return tick_.Remove(token); }
    HRESULT STDMETHODCALLTYPE Start() override {
        if (GetCurrentThreadId() != thread_) return RPC_E_WRONG_THREAD;
        Log("TRACE", "DispatcherTimer Start interval=%ums", Milliseconds());
        return Arm();
    }
    HRESULT STDMETHODCALLTYPE Stop() override {
        if (GetCurrentThreadId() != thread_) return RPC_E_WRONG_THREAD;
        if (!timer_) return S_OK;
        KillTimer(nullptr, timer_);
        auto id = timer_;
        timer_ = 0;
        // Erase last: it may release the final reference to this timer.
        auto running = std::move(RunningTimers()[id]);
        RunningTimers().erase(id);
        return S_OK;
    }
};
class DispatcherTimerFactory final : public Factory<DefaultIDispatcherTimerFactory> {
public:
    DispatcherTimerFactory() : Factory(L"Windows.UI.Xaml.DispatcherTimer") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner,
                                             IDispatcherTimer** value) override {
        return Compose<DispatcherTimer>(outer, inner, value);
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<IDispatcherTimer> timer;
        auto hr = Compose<DispatcherTimer>(nullptr, nullptr, timer.GetAddressOf());
        return FAILED(hr) ? hr : timer.CopyTo(value);
    }
};
}
HRESULT MakeDispatcherTimerFactory(IActivationFactory** value) {
    auto factory = Make<DispatcherTimerFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
