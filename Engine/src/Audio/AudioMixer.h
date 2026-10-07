#pragma once

#include "AudioMixerInput.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace DesktopSceneEngine::Audio
{

struct AudioInputHandle
{
    std::uint64_t value = 0U;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value != 0U;
    }

    friend bool operator==(const AudioInputHandle&, const AudioInputHandle&) = default;
};

enum class AudioInputState
{
    Active,
    Ended,
    Failed
};

enum class AudioMixStatus
{
    Success,
    InvalidRequest,
    ResourceFailure
};

struct AudioMixResult
{
    std::size_t framesProduced = 0U;
    AudioMixStatus status = AudioMixStatus::Success;
};

class AudioMixer final
{
public:
    AudioMixer() = default;
    ~AudioMixer() = default;

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;
    AudioMixer(AudioMixer&&) = delete;
    AudioMixer& operator=(AudioMixer&&) = delete;

    [[nodiscard]] AudioInputHandle addInput(
        std::unique_ptr<AudioMixerInput> input,
        float gain = 1.0F
    ) noexcept;

    [[nodiscard]] bool removeInput(AudioInputHandle handle) noexcept;

    [[nodiscard]] bool setInputGain(AudioInputHandle handle, float gain) noexcept;

    [[nodiscard]] AudioMixResult mixFrames(
        std::span<float> interleavedDestination
    ) noexcept;

    [[nodiscard]] bool consumeTerminalState(
        AudioInputHandle handle,
        AudioInputState& terminalState
    ) noexcept;

private:
    struct InputSlot
    {
        AudioInputHandle handle{};
        std::unique_ptr<AudioMixerInput> input{};
        float gain = 1.0F;
        AudioInputState state = AudioInputState::Active;
        bool terminalNotificationPending = false;
    };

    [[nodiscard]] InputSlot* findInput(AudioInputHandle handle) noexcept;

    static bool isValidGain(float gain) noexcept;

    std::vector<InputSlot> inputs_{};
    std::vector<float> scratch_{};
    std::uint64_t nextHandleValue_ = 1U;
};

}
