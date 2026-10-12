#pragma once
#include <atomic>
#include <windows.h>
namespace ctr2 {
inline UINT32 NextAsyncId() {
    static std::atomic<UINT32> next{0};
    return ++next;
}
}
