#pragma once

#include "AudioBlockSink.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace DesktopSceneEngine::Audio
{

enum class AudioDeviceMuteResult : std::uint8_t { Applied, Unavailable, DeviceFailure, ContractFailure };

// Focused protocol/device-failure seam. A test supplies a bounded, externally
// retained device. The real A loop, command queue, mixer and retirement are
// unchanged. Production constructs AudioExecution() and uses WASAPI.
class AudioExecutionTestDevice : public AudioBlockSink
{
public:
    ~AudioExecutionTestDevice() override = default;
    virtual bool open() noexcept = 0;
    virtual void close() noexcept = 0;
    virtual AudioBlockStatus configure(bool paused) noexcept = 0;
    virtual AudioBlockStatus pause() noexcept = 0;
    virtual AudioBlockStatus resume() noexcept = 0;
    virtual void stop() noexcept = 0;
    // Existing focused devices may delegate their infallible reset. Fault
    // fixtures override this; production A uses the checked native API.
    virtual AudioBlockStatus stopAndResetChecked() noexcept
    { stop(); return AudioBlockStatus::Success; }
    virtual AudioDeviceMuteResult setMuted(bool muted) noexcept = 0;
    virtual std::size_t bufferFrames() const noexcept = 0;
    virtual std::uint64_t queuedFrames() const noexcept = 0;
    virtual std::uint64_t underflowCount() const noexcept = 0;
    // Null means native timeout readiness in focused tests. Production uses
    // the event callback handle installed on its shared WASAPI stream.
    virtual void* readinessHandle() const noexcept { return nullptr; }
    virtual std::uint64_t endpointGeneration() const noexcept { return 0U; }
};

}
