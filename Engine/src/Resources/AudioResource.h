#pragma once

#include "MediaResource.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace DesktopSceneEngine::Resources
{

enum class AudioOpenStatus
{
    NotOpened,
    Ready,
    NoAudioStream,
    DecodeFailure
};

// Decodes one audio stream into the stable PCM format used by AudioOutput:
// signed 16-bit, interleaved stereo at 48 kHz.
class AudioResource final : public MediaResource
{
public:

    AudioResource();
    ~AudioResource() override;

    AudioResource(const AudioResource&) = delete;
    AudioResource& operator=(const AudioResource&) = delete;
    AudioResource(AudioResource&&) noexcept;
    AudioResource& operator=(AudioResource&&) noexcept;

    [[nodiscard]] MediaType type() const noexcept override;

    // Opens the first audio stream and decodes it synchronously into PCM.
    // A media file without an audio stream returns false without throwing.
    [[nodiscard]] bool open(const std::filesystem::path& path) noexcept;

    // Rebuilds the output PCM for a supported playback tempo while keeping
    // the decoded media PCM available for later speed transitions.
    [[nodiscard]] bool setTempo(float tempo) noexcept;

    [[nodiscard]] float tempo() const noexcept;

    [[nodiscard]] AudioOpenStatus openStatus() const noexcept;

    [[nodiscard]] const std::int16_t* data() const noexcept;

    [[nodiscard]] std::size_t sampleCount() const noexcept;

    // Frame count of the currently selected tempo-processed output PCM.
    [[nodiscard]] std::size_t frameCount() const noexcept;

    // Frame count on the original media timeline.
    [[nodiscard]] std::size_t mediaFrameCount() const noexcept;

    // Media timeline duration, independent of the selected tempo.
    [[nodiscard]] double durationSeconds() const noexcept;

    // Duration of the currently selected output PCM.
    [[nodiscard]] double outputDurationSeconds() const noexcept;

    [[nodiscard]] std::size_t byteSize() const noexcept;

    [[nodiscard]] int channels() const noexcept;

    [[nodiscard]] int sampleRate() const noexcept;

private:

    struct Impl;

    static bool appendConvertedSamples(
        Impl& impl,
        const std::uint8_t* const* input,
        int inputSamples
    ) noexcept;

    static bool flushResampler(Impl& impl) noexcept;

    static bool receiveDecodedFrames(Impl& impl) noexcept;

    static bool decodePcm(Impl& impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

}
