#include "Platform/Audio/DefaultAudioEndpointWatcher.h"

#if defined(_WIN32)

#include <Windows.h>
#include <mmdeviceapi.h>

#endif

#include <cstdint>
#include <iostream>

namespace DesktopSceneEngine::Platform::Audio
{

class DefaultAudioEndpointWatcherTestAccess final
{
public:
    static void notifyDefaultDeviceChanged(
        DefaultAudioEndpointWatcher& watcher,
        std::uint32_t flow,
        std::uint32_t role,
        const wchar_t* deviceId
    ) noexcept
    {
        watcher.recordDefaultDeviceChangedForTesting(flow, role, deviceId);
    }

    static void notifyOtherCallbacks(DefaultAudioEndpointWatcher& watcher) noexcept
    {
        watcher.recordOtherCallbacksForTesting();
    }

    [[nodiscard]] static bool hasValidCallbackComContract() noexcept
    {
        return DefaultAudioEndpointWatcher::verifyCallbackComContractForTesting();
    }
};

}

namespace
{

using DesktopSceneEngine::Platform::Audio::DefaultAudioEndpointWatcher;
using DesktopSceneEngine::Platform::Audio::DefaultAudioEndpointWatcherTestAccess;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyGenerationFiltering()
{
    DefaultAudioEndpointWatcher watcher{};
    if (!require(watcher.generation() == 0U,
            "A new watcher must begin at generation zero"))
    {
        return false;
    }

#if defined(_WIN32)
    DefaultAudioEndpointWatcherTestAccess::notifyDefaultDeviceChanged(
        watcher,
        static_cast<std::uint32_t>(eCapture),
        static_cast<std::uint32_t>(eConsole),
        L"capture-device");
    DefaultAudioEndpointWatcherTestAccess::notifyDefaultDeviceChanged(
        watcher,
        static_cast<std::uint32_t>(eRender),
        static_cast<std::uint32_t>(eMultimedia),
        L"multimedia-device");
    DefaultAudioEndpointWatcherTestAccess::notifyDefaultDeviceChanged(
        watcher,
        static_cast<std::uint32_t>(eRender),
        static_cast<std::uint32_t>(eCommunications),
        L"communications-device");
    if (!require(watcher.generation() == 0U,
            "Capture and non-console notifications must be ignored"))
    {
        return false;
    }

    DefaultAudioEndpointWatcherTestAccess::notifyDefaultDeviceChanged(
        watcher,
        static_cast<std::uint32_t>(eRender),
        static_cast<std::uint32_t>(eConsole),
        L"console-device-a");
    DefaultAudioEndpointWatcherTestAccess::notifyDefaultDeviceChanged(
        watcher,
        static_cast<std::uint32_t>(eRender),
        static_cast<std::uint32_t>(eConsole),
        nullptr);
    DefaultAudioEndpointWatcherTestAccess::notifyDefaultDeviceChanged(
        watcher,
        static_cast<std::uint32_t>(eRender),
        static_cast<std::uint32_t>(eConsole),
        L"console-device-c");
    if (!require(watcher.generation() == 3U,
            "Every matching notification, including null device ID, must increment generation"))
    {
        return false;
    }
#endif

    DefaultAudioEndpointWatcherTestAccess::notifyOtherCallbacks(watcher);
    return require(watcher.generation()
#if defined(_WIN32)
            == 3U,
#else
            == 0U,
#endif
        "Non-default callback methods must not alter the generation");
}

bool verifyCallbackComContract()
{
    return require(
        DefaultAudioEndpointWatcherTestAccess::hasValidCallbackComContract(),
        "The callback must support IUnknown and IMMNotificationClient only");
}

bool verifyLifecycle()
{
    DefaultAudioEndpointWatcher watcher{};
    const bool firstInitialize = watcher.initialize();
    if (firstInitialize && !require(watcher.initialize(),
            "Repeated initialize must succeed without duplicate registration"))
    {
        return false;
    }

    watcher.shutdown();
    watcher.shutdown();

    if (!firstInitialize)
    {
        std::cout << "Default endpoint watcher unavailable; callback contract checks passed."
                  << std::endl;
        return true;
    }

    const bool secondInitialize = watcher.initialize();
    watcher.shutdown();
    return require(secondInitialize,
        "A watcher must be reusable after shutdown");
}

}

int main()
{
    return verifyGenerationFiltering()
            && verifyCallbackComContract()
            && verifyLifecycle()
        ? 0 : 1;
}
