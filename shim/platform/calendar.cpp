#include "generated/globalization.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl.h>
#include <algorithm>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Globalization;
namespace ctr2 {
namespace {
// Gregorian, 24-hour calendar in the local time zone over a UTC DateTime.
// Day/hour arithmetic is exact elapsed time; fields read and write local time.
constexpr INT64 Second = 10000000, Minute = 60 * Second, Hour = 60 * Minute, Day = 24 * Hour;
class Calendar final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultICalendar, FtmBase> {
    InspectableClass(L"Windows.Globalization.Calendar", BaseTrust);
    INT64 ticks_ = 0;
    HRESULT Local(SYSTEMTIME& local) const {
        FILETIME utc{static_cast<DWORD>(ticks_), static_cast<DWORD>(ticks_ >> 32)};
        SYSTEMTIME system{};
        if (!FileTimeToSystemTime(&utc, &system) || !SystemTimeToTzSpecificLocalTime(nullptr, &system, &local))
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }
    HRESULT SetLocal(const SYSTEMTIME& local) {
        SYSTEMTIME system{};
        FILETIME utc{};
        if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &system) || !SystemTimeToFileTime(&system, &utc))
            return E_INVALIDARG;
        ticks_ = (static_cast<INT64>(utc.dwHighDateTime) << 32 | utc.dwLowDateTime) + ticks_ % 10000;
        return S_OK;
    }
    template <class Change> HRESULT Edit(Change change) {
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        if (!change(local)) return E_INVALIDARG;
        return SetLocal(local);
    }
    static int DaysIn(int year, int month) {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29 : days[month - 1];
    }
    HRESULT Field(INT32* value, WORD SYSTEMTIME::* field) const {
        if (!value) return E_POINTER;
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        *value = local.*field;
        return S_OK;
    }
    HRESULT AddTicks(INT64 delta) {
        ticks_ += delta;
        return S_OK;
    }
    HRESULT AddMonthsLocal(INT64 months) {
        return Edit([months](SYSTEMTIME& local) {
            auto total = static_cast<INT64>(local.wYear) * 12 + (local.wMonth - 1) + months;
            if (total < INT64{1601} * 12 || total > INT64{30827} * 12 + 11) return false;
            local.wYear = static_cast<WORD>(total / 12);
            local.wMonth = static_cast<WORD>(total % 12 + 1);
            local.wDay = static_cast<WORD>(std::min(static_cast<int>(local.wDay), DaysIn(local.wYear, local.wMonth)));
            return true;
        });
    }

public:
    Calendar() { SetToNow(); }
    HRESULT STDMETHODCALLTYPE Clone(ICalendar** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto copy = Make<Calendar>();
        if (!copy) return E_OUTOFMEMORY;
        copy->ticks_ = ticks_;
        return copy.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetCalendarSystem(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"GregorianCalendar", 17, value);
    }
    HRESULT STDMETHODCALLTYPE GetClock(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"24HourClock", 11, value);
    }
    HRESULT STDMETHODCALLTYPE GetDateTime(ABI::Windows::Foundation::DateTime* value) override {
        if (!value) return E_POINTER;
        value->UniversalTime = ticks_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDateTime(ABI::Windows::Foundation::DateTime value) override {
        ticks_ = value.UniversalTime;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetToNow() override {
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        ticks_ = static_cast<INT64>(now.dwHighDateTime) << 32 | now.dwLowDateTime;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Year(INT32* value) override { return Field(value, &SYSTEMTIME::wYear); }
    HRESULT STDMETHODCALLTYPE put_Year(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 1601 || value > 30827) return false;
            local.wYear = static_cast<WORD>(value);
            local.wDay = static_cast<WORD>(std::min(static_cast<int>(local.wDay), DaysIn(value, local.wMonth)));
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddYears(INT32 years) override { return AddMonthsLocal(static_cast<INT64>(years) * 12); }
    HRESULT STDMETHODCALLTYPE get_Month(INT32* value) override { return Field(value, &SYSTEMTIME::wMonth); }
    HRESULT STDMETHODCALLTYPE put_Month(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 1 || value > 12) return false;
            local.wMonth = static_cast<WORD>(value);
            return local.wDay <= DaysIn(local.wYear, value);
        });
    }
    HRESULT STDMETHODCALLTYPE AddMonths(INT32 months) override { return AddMonthsLocal(months); }
    HRESULT STDMETHODCALLTYPE AddWeeks(INT32 weeks) override { return AddTicks(static_cast<INT64>(weeks) * 7 * Day); }
    HRESULT STDMETHODCALLTYPE get_FirstDayInThisMonth(INT32* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LastDayInThisMonth(INT32* value) override {
        return get_NumberOfDaysInThisMonth(value);
    }
    HRESULT STDMETHODCALLTYPE get_NumberOfDaysInThisMonth(INT32* value) override {
        if (!value) return E_POINTER;
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        *value = DaysIn(local.wYear, local.wMonth);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Day(INT32* value) override { return Field(value, &SYSTEMTIME::wDay); }
    HRESULT STDMETHODCALLTYPE put_Day(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 1 || value > DaysIn(local.wYear, local.wMonth)) return false;
            local.wDay = static_cast<WORD>(value);
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddDays(INT32 days) override { return AddTicks(days * Day); }
    HRESULT STDMETHODCALLTYPE get_DayOfWeek(ABI::Windows::Globalization::DayOfWeek* value) override {
        if (!value) return E_POINTER;
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        *value = static_cast<ABI::Windows::Globalization::DayOfWeek>(local.wDayOfWeek);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Hour(INT32* value) override { return Field(value, &SYSTEMTIME::wHour); }
    HRESULT STDMETHODCALLTYPE put_Hour(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 0 || value > 23) return false;
            local.wHour = static_cast<WORD>(value);
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddHours(INT32 hours) override { return AddTicks(hours * Hour); }
    HRESULT STDMETHODCALLTYPE get_Minute(INT32* value) override { return Field(value, &SYSTEMTIME::wMinute); }
    HRESULT STDMETHODCALLTYPE put_Minute(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 0 || value > 59) return false;
            local.wMinute = static_cast<WORD>(value);
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddMinutes(INT32 minutes) override { return AddTicks(minutes * Minute); }
    HRESULT STDMETHODCALLTYPE get_Second(INT32* value) override { return Field(value, &SYSTEMTIME::wSecond); }
    HRESULT STDMETHODCALLTYPE put_Second(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 0 || value > 59) return false;
            local.wSecond = static_cast<WORD>(value);
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddSeconds(INT32 seconds) override { return AddTicks(seconds * Second); }
    HRESULT STDMETHODCALLTYPE get_Nanosecond(INT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<INT32>(ticks_ % Second * 100);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Nanosecond(INT32 value) override {
        if (value < 0 || value > 999999999) return E_INVALIDARG;
        ticks_ = ticks_ - ticks_ % Second + value / 100;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddNanoseconds(INT32 nanoseconds) override { return AddTicks(nanoseconds / 100); }
    HRESULT STDMETHODCALLTYPE Compare(ICalendar* other, INT32* result) override {
        if (!other || !result) return E_POINTER;
        ABI::Windows::Foundation::DateTime value{};
        auto hr = other->GetDateTime(&value);
        if (FAILED(hr)) return hr;
        return CompareDateTime(value, result);
    }
    HRESULT STDMETHODCALLTYPE CompareDateTime(ABI::Windows::Foundation::DateTime other, INT32* result) override {
        if (!result) return E_POINTER;
        *result = ticks_ < other.UniversalTime ? -1 : ticks_ > other.UniversalTime ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyTo(ICalendar* other) override {
        if (!other) return E_POINTER;
        return other->SetDateTime({ticks_});
    }
};
class CalendarFactory final : public Factory<> {
public:
    CalendarFactory() : Factory(L"Windows.Globalization.Calendar") {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto calendar = Make<Calendar>();
        return calendar ? calendar.CopyTo(value) : E_OUTOFMEMORY;
    }
};
}
HRESULT MakeCalendarFactory(IActivationFactory** value) {
    auto factory = Make<CalendarFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
