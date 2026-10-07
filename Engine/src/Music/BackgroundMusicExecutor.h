#pragma once

#include "Audio/AudioMixer.h"
#include "MusicRuntimeTypes.h"

#include <memory>
#include <optional>
#include <vector>

namespace DesktopSceneEngine::Audio
{

class AudioMixerInput;

}

namespace DesktopSceneEngine::Music
{

// Engine-private source executor. AudioMixer directly owns each registered
// input; this class owns only the registration lifecycle and token metadata.
// The referenced AudioMixer must outlive this executor.
class BackgroundMusicExecutor final
{
public:
    using InputRegistrar = Audio::AudioInputHandle (*) (
        Audio::AudioMixer& mixer,
        std::unique_ptr<Audio::AudioMixerInput> input,
        float gain
    ) noexcept;

    explicit BackgroundMusicExecutor(Audio::AudioMixer& mixer) noexcept;

    BackgroundMusicExecutor(
        Audio::AudioMixer& mixer,
        InputRegistrar registrar
    ) noexcept;

    ~BackgroundMusicExecutor();

    BackgroundMusicExecutor(const BackgroundMusicExecutor&) = delete;
    BackgroundMusicExecutor& operator=(const BackgroundMusicExecutor&) = delete;
    BackgroundMusicExecutor(BackgroundMusicExecutor&&) = delete;
    BackgroundMusicExecutor& operator=(BackgroundMusicExecutor&&) = delete;

    [[nodiscard]] std::optional<MusicRuntimeEvent> execute(
        const MusicRuntimeAction& action
    ) noexcept;

    [[nodiscard]] std::vector<MusicRuntimeEvent> collectTerminalEvents();

    [[nodiscard]] bool hasActiveSources() const noexcept;

private:
    struct ActiveMusicSource
    {
        MusicSourceToken token{};
        Audio::AudioInputHandle mixerHandle{};
        MusicScope scope = MusicScope::PlaylistGlobal;
    };

    [[nodiscard]] std::optional<MusicRuntimeEvent> executeAction(
        const StartSource& action
    ) noexcept;

    [[nodiscard]] std::optional<MusicRuntimeEvent> executeAction(
        const StopSource& action
    ) noexcept;

    [[nodiscard]] std::optional<MusicRuntimeEvent> executeAction(
        const SetSourceGain& action
    ) noexcept;

    [[nodiscard]] static Audio::AudioInputHandle registerWithMixer(
        Audio::AudioMixer& mixer,
        std::unique_ptr<Audio::AudioMixerInput> input,
        float gain
    ) noexcept;

    [[nodiscard]] static bool isValidStartAction(const StartSource& action) noexcept;

    [[nodiscard]] static bool isValidGain(float gain) noexcept;

    [[nodiscard]] std::vector<ActiveMusicSource>::iterator findActiveSource(
        MusicSourceToken token
    ) noexcept;

    void removeActiveSource(
        std::vector<ActiveMusicSource>::iterator source
    ) noexcept;

    Audio::AudioMixer& mixer_;
    InputRegistrar registrar_ = nullptr;
    std::vector<ActiveMusicSource> activeSources_{};
};

}
