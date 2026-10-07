#pragma once

#include "Audio/AudioBlockSink.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace DesktopSceneEngine::Platform::Audio
{

enum class AudioBlockStartMode
{
    StartImmediately,
    ConfigurePaused
};

class AudioOutput final : public DesktopSceneEngine::Audio::AudioBlockSink
{
public:

    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&&) = delete;
    AudioOutput& operator=(AudioOutput&&) = delete;

    [[nodiscard]] bool initialize() noexcept;

    [[nodiscard]] DesktopSceneEngine::Audio::AudioBlockStatus beginBlockMode(
        AudioBlockStartMode startMode
    ) noexcept;

    [[nodiscard]] DesktopSceneEngine::Audio::AudioBlockAvailability
    writableFrames() noexcept override;

    [[nodiscard]] DesktopSceneEngine::Audio::AudioBlockStatus writeFrames(
        std::span<const std::int16_t> interleavedSamples
    ) noexcept override;

    // The caller retains ownership of pcmData and must keep it alive until
    // stop() is called. AudioOutput only borrows the submitted PCM buffer.
    [[nodiscard]] bool submit(
        const void* pcmData,
        std::size_t byteSize,
        int sampleRate,
        int channels
    ) noexcept;

    // Starts the submitted PCM at a frame offset. The caller retains
    // ownership of the source buffer until stop() is called.
    [[nodiscard]] bool submitAtFrame(
        const void* pcmData,
        std::size_t byteSize,
        int sampleRate,
        int channels,
        std::size_t startFrame
    ) noexcept;

    // Configures PCM that has been tempo-processed from a media timeline. The
    // output frame offset is derived from mediaTime and playbackSpeed so
    // playbackPositionSeconds remains expressed in media seconds. Passing
    // false for startImmediately positions the borrowed PCM for resume()
    // without starting or pumping the audio client.
    [[nodiscard]] bool submitAtMediaTime(
        const void* pcmData,
        std::size_t byteSize,
        int sampleRate,
        int channels,
        double mediaTime,
        float playbackSpeed,
        bool startImmediately = true
    ) noexcept;

    void pump() noexcept;

    DesktopSceneEngine::Audio::AudioBlockStatus pause() noexcept;

    DesktopSceneEngine::Audio::AudioBlockStatus resume() noexcept;
    void stop() noexcept;
    void shutdown() noexcept;

    void setVolume(float volume) noexcept;

    [[nodiscard]] float volume() const noexcept;

    void toggleMute() noexcept;

    [[nodiscard]] bool isMuted() const noexcept;

    // These values describe the current WASAPI stream. submittedFrames is
    // relative to the current submission; playbackPositionSeconds remains on
    // the media timeline even when the submitted PCM was tempo-processed.
    [[nodiscard]] std::uint64_t submittedFrames() const noexcept;

    [[nodiscard]] std::uint64_t queuedFrames() const noexcept;

    [[nodiscard]] std::uint64_t playedFrames() const noexcept;

    [[nodiscard]] double playbackPositionSeconds() const noexcept;

    [[nodiscard]] std::uint64_t underflowCount() const noexcept;

    // True only while AudioOutput retains a borrow of caller-owned PCM.
    [[nodiscard]] bool hasBorrowedPcm() const noexcept;

private:

    struct Impl;

    [[nodiscard]] bool submitAtFrameInternal(
        const void* pcmData,
        std::size_t byteSize,
        int sampleRate,
        int channels,
        std::size_t startFrame,
        double mediaStartTime,
        float playbackSpeed,
        bool startImmediately
    ) noexcept;

    std::unique_ptr<Impl> impl_{};
};

}
