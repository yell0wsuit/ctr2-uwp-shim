#include "registry.h"
#include "log.h"
#include "com/async_action.h"
#include "platform/timer_deadline.h"
#include <windows.system.threading.h>
#include <roapi.h>
#include <algorithm>
#include <wrl.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <limits>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>

using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::System::Threading;
namespace ctr2 {

class Timer;
struct TimerState;
using TimerKey = std::pair<TimerClock::time_point, ULONGLONG>;
// Ordered priority queue permits allocation-free cancellation/rearming through
// node extraction, while keeping equal-deadline timers in insertion order.
using TimerQueue = std::map<TimerKey, std::shared_ptr<TimerState>>;
struct TimerState {
    ComPtr<Timer> timer;
    ComPtr<ITimerElapsedHandler> callback;
    ComPtr<ITimerDestroyedHandler> destroyed;
    std::int64_t intervalTicks = 0;
    TimerKey key;
    bool periodic = false, queued = false, canceled = false, finished = false;
};
class TimerScheduler {
    std::mutex mutex_;
    std::condition_variable changed_;
    TimerQueue queue_;
    ULONGLONG sequence_ = 0;
    void Run();

public:
    TimerScheduler() {
        std::thread([this] { Run(); }).detach();
    }
    static TimerScheduler& Instance() {
        // Process-lifetime service: no DLL teardown joins or callbacks under
        // loader lock. ctr2shim is kept loaded by the compiler proxy.
        static auto* scheduler = new TimerScheduler();
        return *scheduler;
    }
    void Schedule(const std::shared_ptr<TimerState>& state) {
        std::lock_guard<std::mutex> lock(mutex_);
        state->key = {TimerDeadline(TimerClock::now(), state->intervalTicks), sequence_++};
        queue_.emplace(state->key, state);
        state->queued = true;
        changed_.notify_one();
    }
    void Cancel(const std::shared_ptr<TimerState>& state) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state->finished || state->canceled) return;
        state->canceled = true;
        if (state->queued) {
            auto node = queue_.extract(state->key);
            state->key = {TimerClock::now(), sequence_++};
            node.key() = state->key;
            queue_.insert(std::move(node));
        }
        changed_.notify_one();
    }
};
class Timer final : public RuntimeClass<RuntimeClassFlags<WinRt>, IThreadPoolTimer, FtmBase> {
    InspectableClass(L"Windows.System.Threading.ThreadPoolTimer", BaseTrust);
    TimeSpan interval_{};
    bool periodic_ = false;
    std::weak_ptr<TimerState> state_;

public:
    HRESULT Start(ITimerElapsedHandler* handler, TimeSpan time, bool periodic, ITimerDestroyedHandler* destroyed) {
        interval_ = time;
        periodic_ = periodic;
        try {
            auto state = std::make_shared<TimerState>();
            state->timer = this;
            state->callback = handler;
            state->destroyed = destroyed;
            state->periodic = periodic;
            state->intervalTicks = time.Duration;
            state_ = state;
            TimerScheduler::Instance().Schedule(state);
        } catch (...) {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Period(TimeSpan* value) override {
        if (!value) return E_POINTER;
        *value = periodic_ ? interval_ : TimeSpan{};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Delay(TimeSpan* value) override {
        if (!value) return E_POINTER;
        *value = periodic_ ? TimeSpan{} : interval_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override {
        if (auto state = state_.lock()) TimerScheduler::Instance().Cancel(state);
        return S_OK;
    }
};
void TimerScheduler::Run() {
    HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized)) Log("FAIL", "Timer scheduler MTA initialization hr=%08lx", initialized);
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        if (queue_.empty()) {
            changed_.wait(lock);
            continue;
        }
        auto due = queue_.begin()->first.first;
        if (due > TimerClock::now()) {
            changed_.wait_until(lock, due);
            continue;
        }
        auto node = queue_.extract(queue_.begin());
        auto state = node.mapped();
        state->queued = false;
        bool invoke = !state->canceled;
        lock.unlock();
        if (invoke) {
            HRESULT hr = E_FAIL;
            static std::atomic<unsigned> fired{0};
            const unsigned count = ++fired;
            if (count <= 5 || count % 500 == 0) Log("TRACE", "ThreadPoolTimer fire %u", count);
            try {
                hr = state->callback->Invoke(state->timer.Get());
            } catch (...) {
                hr = E_UNEXPECTED;
            }
            if (FAILED(hr)) Log("FAIL", "TimerElapsedHandler hr=%08lx", hr);
        }
        lock.lock();
        if (state->periodic && !state->canceled) {
            state->key = {TimerDeadline(TimerClock::now(), state->intervalTicks), sequence_++};
            node.key() = state->key;
            state->queued = true;
            queue_.insert(std::move(node));
        } else {
            state->finished = true;
            lock.unlock();
            if (state->destroyed) {
                HRESULT hr = E_FAIL;
                try {
                    hr = state->destroyed->Invoke(state->timer.Get());
                } catch (...) {
                    hr = E_UNEXPECTED;
                }
                if (FAILED(hr)) Log("FAIL", "TimerDestroyedHandler hr=%08lx", hr);
            }
            // Release handler and timer references outside the scheduler lock;
            // their destructors can legally reenter timer creation/cancellation.
            node = {};
            state.reset();
            lock.lock();
        }
    }
}

class TimerFactory final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IActivationFactory, IThreadPoolTimerStatics, FtmBase> {
public:
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* out) noexcept override {
        if (!out) return E_POINTER;
        const wchar_t* name = L"Windows.System.Threading.ThreadPoolTimer";
        return WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), out);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* out) noexcept override {
        if (!out) return E_POINTER;
        *out = BaseTrust;
        return S_OK;
    }

private:
    HRESULT Create(ITimerElapsedHandler* handler, TimeSpan time, bool periodic, ITimerDestroyedHandler* destroyed,
                   IThreadPoolTimer** out) {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!handler || time.Duration < 0 || (periodic && time.Duration == 0)) return E_INVALIDARG;
        auto timer = Make<Timer>();
        if (!timer) return E_OUTOFMEMORY;
        Log("TRACE", "ThreadPoolTimer create periodic=%d interval=%lldus", periodic, time.Duration / 10);
        auto hr = timer->Start(handler, time, periodic, destroyed);
        return FAILED(hr) ? hr : timer.CopyTo(out);
    }

public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return E_ILLEGAL_METHOD_CALL;
    }
    HRESULT STDMETHODCALLTYPE CreateTimer(ITimerElapsedHandler* h, TimeSpan t, IThreadPoolTimer** out) override {
        return Create(h, t, false, nullptr, out);
    }
    HRESULT STDMETHODCALLTYPE CreatePeriodicTimer(ITimerElapsedHandler* h, TimeSpan t,
                                                  IThreadPoolTimer** out) override {
        return Create(h, t, true, nullptr, out);
    }
    HRESULT STDMETHODCALLTYPE CreateTimerWithCompletion(ITimerElapsedHandler* h, TimeSpan t, ITimerDestroyedHandler* d,
                                                        IThreadPoolTimer** out) override {
        return Create(h, t, false, d, out);
    }
    HRESULT STDMETHODCALLTYPE CreatePeriodicTimerWithCompletion(ITimerElapsedHandler* h, TimeSpan t,
                                                                ITimerDestroyedHandler* d,
                                                                IThreadPoolTimer** out) override {
        return Create(h, t, true, d, out);
    }
};
class PoolFactory final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IActivationFactory, IThreadPoolStatics, FtmBase> {
public:
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* out) noexcept override {
        if (!out) return E_POINTER;
        const wchar_t* name = L"Windows.System.Threading.ThreadPool";
        return WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), out);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* out) noexcept override {
        if (!out) return E_POINTER;
        *out = BaseTrust;
        return S_OK;
    }

private:
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return E_ILLEGAL_METHOD_CALL;
    }
    HRESULT STDMETHODCALLTYPE RunAsync(IWorkItemHandler* handler, IAsyncAction** out) override {
        return RunWithPriorityAndOptionsAsync(handler, WorkItemPriority_Normal, WorkItemOptions_None, out);
    }
    HRESULT STDMETHODCALLTYPE RunWithPriorityAsync(IWorkItemHandler* handler, WorkItemPriority priority,
                                                   IAsyncAction** out) override {
        return RunWithPriorityAndOptionsAsync(handler, priority, WorkItemOptions_None, out);
    }
    HRESULT STDMETHODCALLTYPE RunWithPriorityAndOptionsAsync(IWorkItemHandler* handler, WorkItemPriority priority,
                                                             WorkItemOptions options, IAsyncAction** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!handler || priority < WorkItemPriority_Low || priority > WorkItemPriority_High ||
            (options != WorkItemOptions_None && options != WorkItemOptions_TimeSliced))
            return E_INVALIDARG;
        auto action = Make<AsyncAction>();
        if (!action) return E_OUTOFMEMORY;
        ComPtr<IWorkItemHandler> callback(handler);
        try {
            std::thread([action, callback, priority, options] {
                Log("TRACE", "WorkItem start priority=%d options=%d", priority, options);
                const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
                SetThreadPriority(GetCurrentThread(), priority == WorkItemPriority_High  ? THREAD_PRIORITY_ABOVE_NORMAL
                                                      : priority == WorkItemPriority_Low ? THREAD_PRIORITY_BELOW_NORMAL
                                                                                         : THREAD_PRIORITY_NORMAL);
                HRESULT hr = E_FAIL;
                try {
                    hr = callback->Invoke(action.Get());
                } catch (...) {
                    hr = E_UNEXPECTED;
                }
                Log(FAILED(hr) ? "FAIL" : "TRACE", "WorkItemHandler hr=%08lx", hr);
                action->Finish(hr);
                if (SUCCEEDED(initialized)) RoUninitialize();
            }).detach();
        } catch (...) {
            return E_OUTOFMEMORY;
        }
        return action.CopyTo(out);
    }
};
HRESULT MakeThreadPoolFactory(bool timers, IActivationFactory** factory) {
    if (timers) {
        auto value = Make<TimerFactory>();
        return value ? value.CopyTo(factory) : E_OUTOFMEMORY;
    }
    auto value = Make<PoolFactory>();
    return value ? value.CopyTo(factory) : E_OUTOFMEMORY;
}
}
