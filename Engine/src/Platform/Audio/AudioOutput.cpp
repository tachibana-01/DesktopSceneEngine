#include "AudioOutput.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>

#if defined(_WIN32)

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#endif

namespace DesktopSceneEngine::Platform::Audio
{

namespace
{

enum class OutputMode
{
    Idle,
    LegacyBorrowed,
    Block
};

using DesktopSceneEngine::Audio::AudioBlockAvailability;
using DesktopSceneEngine::Audio::AudioBlockStatus;

}

#if defined(_WIN32)

void setStreamVolume(IAudioStreamVolume* streamVolume, float volume) noexcept
{
    if (streamVolume == nullptr)
    {
        return;
    }

    UINT32 channelCount = 0;
    if (FAILED(streamVolume->GetChannelCount(&channelCount)))
    {
        return;
    }

    for (UINT32 channel = 0; channel < channelCount; ++channel)
    {
        (void)streamVolume->SetChannelVolume(channel, volume);
    }
}

#endif

struct AudioOutput::Impl
{
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<IMMDevice> device{};
    Microsoft::WRL::ComPtr<IAudioClient> client{};
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient{};
    Microsoft::WRL::ComPtr<IAudioStreamVolume> streamVolume{};
    UINT32 bufferFrameCount = 0;
    bool comInitialized = false;
    bool initialized = false;
    bool started = false;
    bool paused = false;
    OutputMode mode = OutputMode::Idle;
    const std::uint8_t* pcm = nullptr;
    std::size_t totalFrames = 0;
    std::size_t nextFrame = 0;
    bool exhausted = false;
    std::uint64_t sourceStartFrame = 0;
    double mediaStartTime = 0.0;
    float playbackSpeed = 1.0F;
    std::uint64_t submittedFrameCount = 0;
    std::uint64_t underflows = 0;
#else
    bool initialized = false;
#endif
    float volume = 1.0F;
    float volumeBeforeMute = 1.0F;
    bool muted = false;
};

AudioOutput::AudioOutput()
    : impl_(std::make_unique<Impl>())
{
}

AudioOutput::~AudioOutput()
{
    shutdown();
}

bool AudioOutput::initialize() noexcept
{
    if (impl_ == nullptr)
    {
        return false;
    }

#if defined(_WIN32)
    if (impl_->initialized)
    {
        return true;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (comResult == S_OK || comResult == S_FALSE)
    {
        impl_->comInitialized = true;
    }
    else if (comResult != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator{};
    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator)))
        || FAILED(enumerator->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            &impl_->device))
        || FAILED(impl_->device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(impl_->client.GetAddressOf()))))
    {
        shutdown();
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(
        format.nChannels * format.wBitsPerSample / 8U);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    constexpr DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
        | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(impl_->client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            streamFlags,
            1000000,
            0,
            &format,
            nullptr))
        || FAILED(impl_->client->GetBufferSize(&impl_->bufferFrameCount))
        || FAILED(impl_->client->GetService(
            __uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(impl_->renderClient.GetAddressOf()))))
    {
        shutdown();
        return false;
    }

    // IAudioStreamVolume is available for ordinary shared-mode render
    // clients. Keep audio usable if a backend does not expose the optional
    // volume service; the requested volume remains stored for later use.
    (void)impl_->client->GetService(
        __uuidof(IAudioStreamVolume),
        reinterpret_cast<void**>(impl_->streamVolume.GetAddressOf()));

    impl_->initialized = true;
    setVolume(impl_->volume);
    return true;
#else
    return false;
#endif
}

AudioBlockStatus AudioOutput::beginBlockMode(
    AudioBlockStartMode startMode
) noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr
        || !impl_->initialized
        || impl_->client == nullptr
        || impl_->renderClient == nullptr)
    {
        return AudioBlockStatus::NotInitialized;
    }

    if (impl_->mode != OutputMode::Idle)
    {
        return AudioBlockStatus::WrongMode;
    }

    impl_->mode = OutputMode::Block;
    impl_->paused = startMode == AudioBlockStartMode::ConfigurePaused;
    impl_->started = false;
    impl_->exhausted = false;
    impl_->submittedFrameCount = 0U;
    impl_->underflows = 0U;

    if (impl_->paused)
    {
        return AudioBlockStatus::Success;
    }

    if (FAILED(impl_->client->Start()))
    {
        stop();
        return AudioBlockStatus::DeviceFailure;
    }

    impl_->started = true;
    return AudioBlockStatus::Success;
#else
    static_cast<void>(startMode);
    return AudioBlockStatus::NotInitialized;
#endif
}

AudioBlockAvailability AudioOutput::writableFrames() noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr
        || !impl_->initialized
        || impl_->client == nullptr)
    {
        return {};
    }

    if (impl_->mode == OutputMode::Idle)
    {
        return {};
    }

    if (impl_->mode != OutputMode::Block)
    {
        return {0U, AudioBlockStatus::WrongMode};
    }

    if (impl_->paused)
    {
        return {0U, AudioBlockStatus::Paused};
    }

    if (!impl_->started)
    {
        return {};
    }

    UINT32 padding = 0U;
    if (FAILED(impl_->client->GetCurrentPadding(&padding))
        || padding > impl_->bufferFrameCount)
    {
        stop();
        return {0U, AudioBlockStatus::DeviceFailure};
    }

    return {
        static_cast<std::size_t>(impl_->bufferFrameCount - padding),
        AudioBlockStatus::Success};
#else
    return {};
#endif
}

AudioBlockStatus AudioOutput::writeFrames(
    std::span<const std::int16_t> interleavedSamples
) noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr
        || !impl_->initialized
        || impl_->client == nullptr
        || impl_->renderClient == nullptr)
    {
        return AudioBlockStatus::NotInitialized;
    }

    if (impl_->mode == OutputMode::Idle)
    {
        return AudioBlockStatus::NotInitialized;
    }

    if (impl_->mode != OutputMode::Block)
    {
        return AudioBlockStatus::WrongMode;
    }

    if (interleavedSamples.empty())
    {
        return AudioBlockStatus::Success;
    }

    if (impl_->paused)
    {
        return AudioBlockStatus::Paused;
    }

    if (!impl_->started)
    {
        return AudioBlockStatus::NotInitialized;
    }

    if (interleavedSamples.size() % 2U != 0U)
    {
        return AudioBlockStatus::InvalidRequest;
    }

    const std::size_t requestedFrames = interleavedSamples.size() / 2U;
    if (requestedFrames > static_cast<std::size_t>(
            (std::numeric_limits<UINT32>::max)())
        || requestedFrames > static_cast<std::size_t>(
            (std::numeric_limits<std::uint64_t>::max)()
                - impl_->submittedFrameCount))
    {
        return AudioBlockStatus::InvalidRequest;
    }

    UINT32 padding = 0U;
    if (FAILED(impl_->client->GetCurrentPadding(&padding))
        || padding > impl_->bufferFrameCount)
    {
        stop();
        return AudioBlockStatus::DeviceFailure;
    }

    const std::size_t availableFrames = static_cast<std::size_t>(
        impl_->bufferFrameCount - padding);
    if (requestedFrames > availableFrames)
    {
        return AudioBlockStatus::NotWritable;
    }

    BYTE* destination = nullptr;
    if (FAILED(impl_->renderClient->GetBuffer(
            static_cast<UINT32>(requestedFrames),
            &destination))
        || destination == nullptr)
    {
        stop();
        return AudioBlockStatus::DeviceFailure;
    }

    std::memcpy(
        destination,
        interleavedSamples.data(),
        interleavedSamples.size_bytes());
    if (FAILED(impl_->renderClient->ReleaseBuffer(
            static_cast<UINT32>(requestedFrames),
            0)))
    {
        stop();
        return AudioBlockStatus::DeviceFailure;
    }

    if (padding == 0U && impl_->submittedFrameCount > 0U)
    {
        ++impl_->underflows;
    }
    impl_->submittedFrameCount += static_cast<std::uint64_t>(requestedFrames);
    return AudioBlockStatus::Success;
#else
    static_cast<void>(interleavedSamples);
    return AudioBlockStatus::NotInitialized;
#endif
}

bool AudioOutput::submit(
    const void* pcmData,
    std::size_t byteSize,
    int sampleRate,
    int channels
) noexcept
{
    return submitAtFrame(
        pcmData,
        byteSize,
        sampleRate,
        channels,
        0U);
}

bool AudioOutput::submitAtFrame(
    const void* pcmData,
    std::size_t byteSize,
    int sampleRate,
    int channels,
    std::size_t startFrame
) noexcept
{
    const double mediaStartTime = sampleRate > 0
        ? static_cast<double>(startFrame) / static_cast<double>(sampleRate)
        : 0.0;
    return submitAtFrameInternal(
        pcmData,
        byteSize,
        sampleRate,
        channels,
        startFrame,
        mediaStartTime,
        1.0F,
        true);
}

bool AudioOutput::submitAtMediaTime(
    const void* pcmData,
    std::size_t byteSize,
    int sampleRate,
    int channels,
    double mediaTime,
    float playbackSpeed,
    bool startImmediately
) noexcept
{
    constexpr std::size_t frameBytes = sizeof(std::int16_t) * 2U;
    if (!std::isfinite(mediaTime) || mediaTime < 0.0
        || !std::isfinite(playbackSpeed) || playbackSpeed < 0.5F
        || playbackSpeed > 2.0F || sampleRate <= 0
        || byteSize % frameBytes != 0U)
    {
        return false;
    }

    const double outputFrame = mediaTime
        / static_cast<double>(playbackSpeed)
        * static_cast<double>(sampleRate);
    if (!std::isfinite(outputFrame) || outputFrame < 0.0
        || outputFrame >= static_cast<double>(byteSize / frameBytes))
    {
        return false;
    }

    return submitAtFrameInternal(
        pcmData,
        byteSize,
        sampleRate,
        channels,
        static_cast<std::size_t>(outputFrame),
        mediaTime,
        playbackSpeed,
        startImmediately);
}

bool AudioOutput::submitAtFrameInternal(
    const void* pcmData,
    std::size_t byteSize,
    int sampleRate,
    int channels,
    std::size_t startFrame,
    double mediaStartTime,
    float playbackSpeed,
    bool startImmediately
) noexcept
{
#if defined(_WIN32)
    constexpr std::size_t frameBytes = sizeof(std::int16_t) * 2U;
    if (impl_ == nullptr
        || !impl_->initialized
        || impl_->renderClient == nullptr
        || impl_->client == nullptr
        || impl_->mode == OutputMode::Block
        || pcmData == nullptr
        || byteSize == 0U
        || sampleRate != 48000
        || channels != 2
        || byteSize % frameBytes != 0U
        || startFrame >= byteSize / frameBytes)
    {
        return false;
    }

    stop();
    impl_->mode = OutputMode::LegacyBorrowed;
    const std::size_t sourceFrameCount = byteSize / frameBytes;
    const std::size_t startByte = startFrame * frameBytes;
    impl_->pcm = static_cast<const std::uint8_t*>(pcmData) + startByte;
    impl_->totalFrames = sourceFrameCount - startFrame;
    impl_->nextFrame = 0U;
    impl_->paused = !startImmediately;
    impl_->exhausted = false;
    impl_->sourceStartFrame = static_cast<std::uint64_t>(startFrame);
    impl_->mediaStartTime = mediaStartTime;
    impl_->playbackSpeed = playbackSpeed;
    impl_->submittedFrameCount = 0U;
    impl_->underflows = 0U;

    if (!startImmediately)
    {
        return true;
    }

    if (FAILED(impl_->client->Start()))
    {
        stop();
        return false;
    }
    impl_->started = true;
    pump();
    return true;
#else
    static_cast<void>(pcmData);
    static_cast<void>(byteSize);
    static_cast<void>(sampleRate);
    static_cast<void>(channels);
    static_cast<void>(startFrame);
    static_cast<void>(mediaStartTime);
    static_cast<void>(playbackSpeed);
    return false;
#endif
}

void AudioOutput::pump() noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr
        || !impl_->initialized
        || impl_->mode != OutputMode::LegacyBorrowed
        || !impl_->started
        || impl_->paused
        || impl_->exhausted
        || impl_->pcm == nullptr
        || impl_->nextFrame >= impl_->totalFrames)
    {
        if (impl_ != nullptr
            && impl_->mode == OutputMode::LegacyBorrowed
            && impl_->started
            && impl_->nextFrame >= impl_->totalFrames)
        {
            impl_->exhausted = true;
            impl_->pcm = nullptr;
        }
        return;
    }

    UINT32 padding = 0;
    if (FAILED(impl_->client->GetCurrentPadding(&padding))
        || padding >= impl_->bufferFrameCount)
    {
        return;
    }

    const std::size_t availableFrames =
        static_cast<std::size_t>(impl_->bufferFrameCount - padding);
    const std::size_t remainingFrames = impl_->totalFrames - impl_->nextFrame;
    if (padding == 0U && impl_->submittedFrameCount > 0U
        && remainingFrames > 0U)
    {
        ++impl_->underflows;
    }
    const std::size_t framesToWrite = (std::min)(
        availableFrames,
        remainingFrames);
    if (framesToWrite == 0U
        || framesToWrite > static_cast<std::size_t>(
            (std::numeric_limits<UINT32>::max)()))
    {
        return;
    }

    if (framesToWrite > static_cast<std::size_t>(
            (std::numeric_limits<std::uint64_t>::max)()
                - impl_->submittedFrameCount))
    {
        return;
    }

    BYTE* destination = nullptr;
    if (FAILED(impl_->renderClient->GetBuffer(
            static_cast<UINT32>(framesToWrite),
            &destination))
        || destination == nullptr)
    {
        return;
    }

    constexpr std::size_t frameBytes = sizeof(std::int16_t) * 2U;
    std::memcpy(
        destination,
        impl_->pcm + impl_->nextFrame * frameBytes,
        framesToWrite * frameBytes);
    if (FAILED(impl_->renderClient->ReleaseBuffer(
            static_cast<UINT32>(framesToWrite),
            0)))
    {
        return;
    }

    impl_->nextFrame += framesToWrite;
    impl_->submittedFrameCount += static_cast<std::uint64_t>(framesToWrite);
    if (impl_->nextFrame >= impl_->totalFrames)
    {
        // All source bytes have been copied into the WASAPI buffer. The
        // source owner may now be released while the device drains its own
        // buffer.
        impl_->exhausted = true;
        impl_->pcm = nullptr;
    }
#endif
}

AudioBlockStatus AudioOutput::pause() noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr
        || !impl_->initialized
        || impl_->client == nullptr)
    {
        return AudioBlockStatus::NotInitialized;
    }

    if (impl_->mode == OutputMode::Idle)
    {
        return AudioBlockStatus::NotInitialized;
    }

    if (impl_->paused)
    {
        return AudioBlockStatus::Success;
    }

    if (!impl_->started)
    {
        return AudioBlockStatus::WrongMode;
    }

    if (FAILED(impl_->client->Stop()))
    {
        stop();
        return AudioBlockStatus::DeviceFailure;
    }

    impl_->paused = true;
    return AudioBlockStatus::Success;
#else
    return AudioBlockStatus::NotInitialized;
#endif
}

AudioBlockStatus AudioOutput::resume() noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr
        || !impl_->initialized
        || impl_->client == nullptr)
    {
        return AudioBlockStatus::NotInitialized;
    }

    if (impl_->mode == OutputMode::Idle)
    {
        return AudioBlockStatus::NotInitialized;
    }

    if (!impl_->paused)
    {
        return AudioBlockStatus::Success;
    }

    const bool resumeLegacy = impl_->mode == OutputMode::LegacyBorrowed
        && !impl_->exhausted
        && impl_->pcm != nullptr;
    const bool resumeBlock = impl_->mode == OutputMode::Block;
    if (!resumeLegacy && !resumeBlock)
    {
        return AudioBlockStatus::WrongMode;
    }

    if (FAILED(impl_->client->Start()))
    {
        stop();
        return AudioBlockStatus::DeviceFailure;
    }

    impl_->started = true;
    impl_->paused = false;
    if (resumeLegacy)
    {
        pump();
    }

    return AudioBlockStatus::Success;
#else
    return AudioBlockStatus::NotInitialized;
#endif
}

void AudioOutput::stop() noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr)
    {
        return;
    }

    if (impl_->client != nullptr)
    {
        (void)impl_->client->Stop();
        (void)impl_->client->Reset();
    }

    impl_->started = false;
    impl_->paused = false;
    impl_->mode = OutputMode::Idle;
    impl_->pcm = nullptr;
    impl_->totalFrames = 0U;
    impl_->nextFrame = 0U;
    impl_->exhausted = false;
    impl_->sourceStartFrame = 0U;
    impl_->mediaStartTime = 0.0;
    impl_->playbackSpeed = 1.0F;
    impl_->submittedFrameCount = 0U;
    impl_->underflows = 0U;
#endif
}

void AudioOutput::shutdown() noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }

#if defined(_WIN32)
    stop();
    impl_->renderClient.Reset();
    impl_->streamVolume.Reset();
    impl_->client.Reset();
    impl_->device.Reset();
    impl_->initialized = false;
    impl_->bufferFrameCount = 0U;
    if (impl_->comInitialized)
    {
        CoUninitialize();
        impl_->comInitialized = false;
    }
#else
    impl_->initialized = false;
#endif
}

void AudioOutput::setVolume(float volume) noexcept
{
    if (!std::isfinite(volume))
    {
        volume = 1.0F;
    }
    volume = (std::clamp)(volume, 0.0F, 1.0F);

    if (impl_ == nullptr)
    {
        return;
    }

    impl_->volume = volume;
    impl_->volumeBeforeMute = volume;

#if defined(_WIN32)
    if (impl_->streamVolume != nullptr)
    {
        const float effectiveVolume = impl_->muted ? 0.0F : impl_->volume;
        setStreamVolume(impl_->streamVolume.Get(), effectiveVolume);
    }
#endif
}

float AudioOutput::volume() const noexcept
{
    return impl_ != nullptr ? impl_->volume : 1.0F;
}

void AudioOutput::toggleMute() noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }

    impl_->muted = !impl_->muted;
    if (!impl_->muted)
    {
        impl_->volume = impl_->volumeBeforeMute;
    }

#if defined(_WIN32)
    if (impl_->streamVolume != nullptr)
    {
        const float effectiveVolume = impl_->muted ? 0.0F : impl_->volume;
        setStreamVolume(impl_->streamVolume.Get(), effectiveVolume);
    }
#endif
}

bool AudioOutput::isMuted() const noexcept
{
    return impl_ != nullptr && impl_->muted;
}

std::uint64_t AudioOutput::submittedFrames() const noexcept
{
#if defined(_WIN32)
    return impl_ != nullptr ? impl_->submittedFrameCount : 0U;
#else
    return 0U;
#endif
}

std::uint64_t AudioOutput::queuedFrames() const noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr || !impl_->initialized || !impl_->started
        || impl_->client == nullptr)
    {
        return 0U;
    }

    UINT32 padding = 0;
    if (FAILED(impl_->client->GetCurrentPadding(&padding)))
    {
        return 0U;
    }

    return static_cast<std::uint64_t>(padding);
#else
    return 0U;
#endif
}

std::uint64_t AudioOutput::playedFrames() const noexcept
{
    const std::uint64_t submitted = submittedFrames();
    const std::uint64_t queued = queuedFrames();
    return submitted >= queued ? submitted - queued : 0U;
}

double AudioOutput::playbackPositionSeconds() const noexcept
{
#if defined(_WIN32)
    if (impl_ == nullptr || !impl_->initialized)
    {
        return -1.0;
    }

    if (impl_->mode != OutputMode::LegacyBorrowed
        || !std::isfinite(impl_->mediaStartTime)
        || !std::isfinite(impl_->playbackSpeed))
    {
        return -1.0;
    }

    if (!impl_->started)
    {
        return impl_->pcm != nullptr ? impl_->mediaStartTime : -1.0;
    }

    const std::uint64_t played = playedFrames();
    const double outputSeconds = static_cast<double>(played) / 48000.0;
    if (!std::isfinite(outputSeconds))
    {
        return -1.0;
    }

    return impl_->mediaStartTime + outputSeconds
        * static_cast<double>(impl_->playbackSpeed);
#else
    return -1.0;
#endif
}

std::uint64_t AudioOutput::underflowCount() const noexcept
{
#if defined(_WIN32)
    return impl_ != nullptr ? impl_->underflows : 0U;
#else
    return 0U;
#endif
}

bool AudioOutput::hasBorrowedPcm() const noexcept
{
#if defined(_WIN32)
    return impl_ != nullptr && impl_->pcm != nullptr;
#else
    return false;
#endif
}

}
