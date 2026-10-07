#pragma once

#include <cstdint>
#include <memory>

namespace DesktopSceneEngine::Platform::Audio
{

class DefaultAudioEndpointWatcherTestAccess;

// Internal Platform watcher. initialize() and shutdown() must be called on
// the same Engine/control thread. Notification callbacks only advance the
// generation and never touch Engine or audio-output state.
class DefaultAudioEndpointWatcher final
{
public:
    DefaultAudioEndpointWatcher();
    ~DefaultAudioEndpointWatcher();

    DefaultAudioEndpointWatcher(const DefaultAudioEndpointWatcher&) = delete;
    DefaultAudioEndpointWatcher& operator=(const DefaultAudioEndpointWatcher&) = delete;
    DefaultAudioEndpointWatcher(DefaultAudioEndpointWatcher&&) = delete;
    DefaultAudioEndpointWatcher& operator=(DefaultAudioEndpointWatcher&&) = delete;

    // Returns false when endpoint-notification capability is unavailable.
    // Callers must treat this independently from AudioOutput availability.
    [[nodiscard]] bool initialize() noexcept;

    void shutdown() noexcept;

    [[nodiscard]] std::uint64_t generation() const noexcept;

private:
    friend class DefaultAudioEndpointWatcherTestAccess;

    struct Impl;

    void recordDefaultDeviceChangedForTesting(
        std::uint32_t flow,
        std::uint32_t role,
        const wchar_t* deviceId
    ) noexcept;

    void recordOtherCallbacksForTesting() noexcept;

    [[nodiscard]] static bool verifyCallbackComContractForTesting() noexcept;

    std::unique_ptr<Impl> impl_{};
};

}
