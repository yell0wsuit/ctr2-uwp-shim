#include <windows.h>
#include <roapi.h>
#include <windows.system.threading.h>
#include <windows.ui.core.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <atomic>
#include <limits>
#include "../shim/platform/timer_deadline.h"

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::System::Threading;
using namespace ABI::Windows::Foundation;
static int failures = 0;
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, #expr);                                                        \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
using GetFactory = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);

int main() {
    const auto fixedNow = ctr2::TimerClock::time_point(std::chrono::seconds(1));
    constexpr std::int64_t sixtyDays = 60LL * 24 * 60 * 60 * 10000000;
    CHECK(ctr2::TimerDeadline(fixedNow, sixtyDays) - fixedNow == std::chrono::hours(60 * 24));
    CHECK(ctr2::TimerDeadline(fixedNow, (std::numeric_limits<std::int64_t>::max)()) ==
          ctr2::TimerClock::time_point::max());
    CHECK(ctr2::TimerDeadline(fixedNow, 0) == fixedNow);
    CHECK(ctr2::TimerDeadline(fixedNow, 1) - fixedNow == std::chrono::nanoseconds(100));
    auto nearMax = ctr2::TimerClock::time_point::max() - std::chrono::nanoseconds(50);
    CHECK(ctr2::TimerDeadline(nearMax, 1) == ctr2::TimerClock::time_point::max());
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    CHECK(dll != nullptr);
    if (!dll) return 1;
    auto get = reinterpret_cast<GetFactory>(GetProcAddress(dll, "ShimGetFactory"));
    CHECK(get != nullptr);
    if (!get) return 1;
    ComPtr<IActivationFactory> factory;
    CHECK(get(HStringReference(L"Unknown.Class").Get(), &factory) == CLASS_E_CLASSNOTAVAILABLE);
    CHECK(factory == nullptr);
    CHECK(get(nullptr, nullptr) == E_POINTER);
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.System.Threading.ThreadPoolTimer").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IThreadPoolTimerStatics> timers;
    CHECK(SUCCEEDED(factory.As(&timers)));
    ComPtr<IUnknown> identity1, identity2;
    factory.As(&identity1);
    timers.As(&identity2);
    CHECK(identity1.Get() == identity2.Get());
    HString runtime;
    CHECK(SUCCEEDED(factory->GetRuntimeClassName(runtime.GetAddressOf())));
    CHECK(wcscmp(runtime.GetRawBuffer(nullptr), L"Windows.System.Threading.ThreadPoolTimer") == 0);
    ULONG count = 0;
    IID* ids = nullptr;
    CHECK(SUCCEEDED(factory->GetIids(&count, &ids)) && count >= 2);
    CoTaskMemFree(ids);
    std::atomic<int> fired{0};
    HANDLE signal = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto handler = Callback<ITimerElapsedHandler>([&](IThreadPoolTimer*) -> HRESULT {
        ++fired;
        SetEvent(signal);
        return S_OK;
    });
    ComPtr<IThreadPoolTimer> timer;
    CHECK(SUCCEEDED(timers->CreateTimer(handler.Get(), TimeSpan{200000}, &timer)));
    CHECK(WaitForSingleObject(signal, 2000) == WAIT_OBJECT_0);
    Sleep(70);
    CHECK(fired == 1);
    TimeSpan delay{};
    CHECK(SUCCEEDED(timer->get_Delay(&delay)) && delay.Duration == 200000);
    timer.Reset();
    fired = 0;
    ResetEvent(signal);
    CHECK(SUCCEEDED(timers->CreatePeriodicTimer(handler.Get(), TimeSpan{100000}, &timer)));
    CHECK(WaitForSingleObject(signal, 2000) == WAIT_OBJECT_0);
    CHECK(SUCCEEDED(timer->Cancel()));
    Sleep(40);
    int stopped = fired;
    Sleep(40);
    CHECK(fired == stopped);
    CHECK(timers->CreatePeriodicTimer(handler.Get(), TimeSpan{0}, &timer) == E_INVALIDARG);
    CHECK(timers->CreateTimer(nullptr, TimeSpan{0}, &timer) == E_INVALIDARG);
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.System.Threading.ThreadPool").Get(), &factory)));
    ComPtr<IThreadPoolStatics> pool;
    CHECK(SUCCEEDED(factory.As(&pool)));
    ComPtr<IAsyncAction> action;
    auto work = Callback<IWorkItemHandler>([](IAsyncAction*) -> HRESULT { return S_OK; });
    CHECK(SUCCEEDED(pool->RunAsync(work.Get(), &action)));
    ComPtr<IAsyncInfo> info;
    CHECK(SUCCEEDED(action.As(&info)));
    AsyncStatus status = AsyncStatus::Started;
    for (int i = 0; i < 200 && status == AsyncStatus::Started; ++i) {
        info->get_Status(&status);
        Sleep(5);
    }
    CHECK(status == AsyncStatus::Completed);
    ResetEvent(signal);
    auto done = Callback<IAsyncActionCompletedHandler>([&](IAsyncAction*, AsyncStatus s) -> HRESULT {
        CHECK(s == AsyncStatus::Completed);
        SetEvent(signal);
        return S_OK;
    });
    CHECK(SUCCEEDED(action->put_Completed(done.Get())));
    CHECK(WaitForSingleObject(signal, 2000) == WAIT_OBJECT_0);
    CHECK(action->put_Completed(done.Get()) == E_ILLEGAL_DELEGATE_ASSIGNMENT);
    CHECK(SUCCEEDED(action->GetResults()));
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Core.CoreCursor").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<ABI::Windows::UI::Core::ICoreCursorFactory> cursors;
    CHECK(SUCCEEDED(factory.As(&cursors)));
    if (!cursors) return 1;
    ComPtr<ABI::Windows::UI::Core::ICoreCursor> cursor;
    CHECK(SUCCEEDED(cursors->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_Arrow, 7, &cursor)));
    UINT32 id = 0;
    cursor->get_Id(&id);
    CHECK(id == 7);
    ABI::Windows::UI::Core::CoreCursorType type{};
    cursor->get_Type(&type);
    CHECK(type == ABI::Windows::UI::Core::CoreCursorType_Arrow);
    UINT32 firstId = 0;
    CHECK(SUCCEEDED(info->get_Id(&firstId)));
    HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto blockedWork = Callback<IWorkItemHandler>([&](IAsyncAction*) -> HRESULT {
        WaitForSingleObject(gate, 2000);
        return E_ACCESSDENIED;
    });
    ComPtr<IAsyncAction> errorAction;
    CHECK(SUCCEEDED(pool->RunAsync(blockedWork.Get(), &errorAction)));
    ComPtr<IAsyncInfo> errorInfo;
    CHECK(SUCCEEDED(errorAction.As(&errorInfo)));
    UINT32 secondId = 0;
    CHECK(SUCCEEDED(errorInfo->get_Id(&secondId)) && firstId != secondId);
    CHECK(errorAction->GetResults() == E_ILLEGAL_METHOD_CALL);
    ResetEvent(signal);
    auto early = Callback<IAsyncActionCompletedHandler>([&](IAsyncAction*, AsyncStatus s) -> HRESULT {
        CHECK(s == AsyncStatus::Error);
        SetEvent(signal);
        return S_OK;
    });
    CHECK(SUCCEEDED(errorAction->put_Completed(early.Get())));
    SetEvent(gate);
    CHECK(WaitForSingleObject(signal, 2000) == WAIT_OBJECT_0);
    CHECK(errorAction->GetResults() == E_ACCESSDENIED);
    HRESULT operationError = S_OK;
    CHECK(SUCCEEDED(errorInfo->get_ErrorCode(&operationError)) && operationError == E_ACCESSDENIED);
    CHECK(SUCCEEDED(errorInfo->Close()));
    CHECK(errorAction->GetResults() == RO_E_CLOSED);
    CloseHandle(gate);
    ResetEvent(signal);
    fired = 0;
    auto selfCancel = Callback<ITimerElapsedHandler>([&](IThreadPoolTimer* active) -> HRESULT {
        ++fired;
        return active->Cancel();
    });
    auto destroyed = Callback<ITimerDestroyedHandler>([&](IThreadPoolTimer*) -> HRESULT {
        SetEvent(signal);
        return S_OK;
    });
    CHECK(SUCCEEDED(
        timers->CreatePeriodicTimerWithCompletion(selfCancel.Get(), TimeSpan{100000}, destroyed.Get(), &timer)));
    CHECK(WaitForSingleObject(signal, 2000) == WAIT_OBJECT_0);
    CHECK(fired == 1);
    // All timers share one MTA scheduler; dropping public references must
    // not dispose callbacks before their scheduled execution.
    ResetEvent(signal);
    constexpr int timerCount = 32;
    std::atomic<DWORD> schedulerThread{0};
    std::atomic<int> received{0}, completed{0}, wrongThread{0};
    auto scheduled = Callback<ITimerElapsedHandler>([&](IThreadPoolTimer*) -> HRESULT {
        DWORD current = GetCurrentThreadId(), expected = 0;
        if (!schedulerThread.compare_exchange_strong(expected, current) && expected != current) ++wrongThread;
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        if (FAILED(CoGetApartmentType(&apartment, &qualifier)) || apartment != APTTYPE_MTA) ++wrongThread;
        ++received;
        return S_OK;
    });
    auto completedTimer = Callback<ITimerDestroyedHandler>([&](IThreadPoolTimer*) -> HRESULT {
        if (GetCurrentThreadId() != schedulerThread.load()) ++wrongThread;
        if (++completed == timerCount) SetEvent(signal);
        return S_OK;
    });
    for (int i = 0; i < timerCount; ++i) {
        ComPtr<IThreadPoolTimer> temporary;
        CHECK(SUCCEEDED(
            timers->CreateTimerWithCompletion(scheduled.Get(), TimeSpan{1000000}, completedTimer.Get(), &temporary)));
    }
    CHECK(WaitForSingleObject(signal, 5000) == WAIT_OBJECT_0);
    CHECK(received == timerCount && completed == timerCount && wrongThread == 0);
    ResetEvent(signal);
    fired = 0;
    std::atomic<int> canceledCompletions{0};
    auto canceledDone = Callback<ITimerDestroyedHandler>([&](IThreadPoolTimer*) -> HRESULT {
        ++canceledCompletions;
        SetEvent(signal);
        return S_OK;
    });
    CHECK(SUCCEEDED(timers->CreateTimerWithCompletion(handler.Get(), TimeSpan{600000000}, canceledDone.Get(), &timer)));
    CHECK(SUCCEEDED(timer->Cancel()));
    CHECK(SUCCEEDED(timer->Cancel()));
    CHECK(WaitForSingleObject(signal, 2000) == WAIT_OBJECT_0);
    Sleep(20);
    CHECK(fired == 0 && canceledCompletions == 1);
    CloseHandle(signal);
    std::printf("shimtest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
