#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

namespace DesktopSceneEngine::Resources
{

enum class AudioSourceStatus
{
    NotOpened,
    Ready,
    OpenFailure,
    NoAudioStream,
    DecoderUnavailable,
    DecodeFailure,
    ResampleFailure,
    ResourceFailure
};

enum class AudioReadStatus
{
    Data,
    EndOfStream,
    NotReady,
    InvalidRequest,
    Failure
};

struct AudioPcmFormat
{
    int sampleRate = 48000;
    int channels = 2;
};

struct AudioReadResult
{
    std::size_t framesProduced = 0;
    AudioReadStatus status = AudioReadStatus::NotReady;
};

struct AudioSourceDiagnostics
{
    std::size_t peakPendingFrames = 0;
    std::size_t sendPacketEagainCount = 0;
    std::size_t decoderFramesDuringDrain = 0;
    std::size_t resamplerFramesDuringDrain = 0;
};

class SequentialAudioSource final
{
public:

    SequentialAudioSource();
    ~SequentialAudioSource();

    SequentialAudioSource(const SequentialAudioSource&) = delete;
    SequentialAudioSource& operator=(const SequentialAudioSource&) = delete;
    SequentialAudioSource(SequentialAudioSource&&) = delete;
    SequentialAudioSource& operator=(SequentialAudioSource&&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& source) noexcept;
    [[nodiscard]] bool reset() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] AudioSourceStatus status() const noexcept;
    [[nodiscard]] AudioPcmFormat format() const noexcept;
    [[nodiscard]] AudioSourceDiagnostics diagnostics() const noexcept;

    [[nodiscard]] AudioReadResult readFrames(
        std::span<float> interleavedDestination) noexcept;

private:

    struct Impl;
    std::unique_ptr<Impl> impl_{};
    std::filesystem::path sourcePath_{};
    AudioSourceStatus status_ = AudioSourceStatus::NotOpened;
};

}
