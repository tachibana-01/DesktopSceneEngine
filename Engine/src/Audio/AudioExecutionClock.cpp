#include "AudioExecutionTypes.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <chrono>
#endif

namespace DesktopSceneEngine::Audio
{

AudioMonotonicTime audioMonotonicNow() noexcept
{
#if defined(_WIN32)
    static const std::int64_t frequency = []() noexcept
    {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) ? value.QuadPart : 0LL;
    }();
    LARGE_INTEGER now{};
    if (frequency <= 0 || !QueryPerformanceCounter(&now)) return 0;
    // Divide before multiplying: raw QPC*1e9 overflows after minutes.
    return (now.QuadPart / frequency) * 1'000'000'000LL
        + ((now.QuadPart % frequency) * 1'000'000'000LL) / frequency;
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

}
