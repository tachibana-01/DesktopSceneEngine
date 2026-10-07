#pragma once

#include "AudioBlockSink.h"
#include "AudioMixer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace DesktopSceneEngine::Audio
{

enum class MixedOutputBridgeStatus
{
    NoWritableFrames,
    OutputInactive,
    WroteFrames,
    MixerResourceFailureSilenced,
    MixerContractFailureSilenced,
    ResourceFailure,
    DeviceFailure,
    OutputContractFailure
};

struct MixedOutputBridgeResult
{
    std::size_t frames = 0U;
    MixedOutputBridgeStatus status = MixedOutputBridgeStatus::OutputInactive;
};

class MixedOutputBridgeTestAccess;

class MixedOutputBridge final
{
public:
    MixedOutputBridge(AudioMixer& mixer, AudioBlockSink& sink) noexcept;

    MixedOutputBridge(const MixedOutputBridge&) = delete;
    MixedOutputBridge& operator=(const MixedOutputBridge&) = delete;
    MixedOutputBridge(MixedOutputBridge&&) = delete;
    MixedOutputBridge& operator=(MixedOutputBridge&&) = delete;

    [[nodiscard]] MixedOutputBridgeResult pump() noexcept;

private:
    [[nodiscard]] bool prepareBuffers(std::size_t frameCount) noexcept;

    [[nodiscard]] MixedOutputBridgeResult submitMixerResult(
        const AudioMixResult& mixerResult,
        std::size_t frameCount
    ) noexcept;

    friend class MixedOutputBridgeTestAccess;

    AudioMixer& mixer_;
    AudioBlockSink& sink_;
    std::vector<float> mixedF32_{};
    std::vector<std::int16_t> deviceS16_{};
};

// A3a-local test seam for the AudioMixer ResourceFailure path, which A2
// intentionally does not expose as an injectable production behavior.
class MixedOutputBridgeTestAccess final
{
public:
    [[nodiscard]] static MixedOutputBridgeResult submitMixerResult(
        MixedOutputBridge& bridge,
        const AudioMixResult& mixerResult,
        std::size_t frameCount
    ) noexcept;
};

}
