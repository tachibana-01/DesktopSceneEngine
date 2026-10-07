#include "Platform/Audio/AudioOutput.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

}

int main()
{
    using DesktopSceneEngine::Platform::Audio::AudioBlockStartMode;
    using DesktopSceneEngine::Audio::AudioBlockStatus;
    using DesktopSceneEngine::Platform::Audio::AudioOutput;

    AudioOutput output{};
    if (!require(std::fabs(output.volume() - 1.0F) < 0.0001F,
            "Audio output should start at full volume")
        || !require(!output.isMuted(), "Audio output should start unmuted"))
    {
        return 1;
    }

    output.setVolume(1.5F);
    if (!require(std::fabs(output.volume() - 1.0F) < 0.0001F,
            "Volume should clamp to one"))
    {
        return 1;
    }

    output.setVolume(-0.5F);
    if (!require(std::fabs(output.volume()) < 0.0001F,
            "Volume should clamp to zero"))
    {
        return 1;
    }

    output.setVolume(0.6F);
    output.toggleMute();
    if (!require(output.isMuted(), "Mute should be enabled")
        || !require(std::fabs(output.volume() - 0.6F) < 0.0001F,
            "Mute should preserve the configured volume"))
    {
        return 1;
    }
    output.toggleMute();
    if (!require(!output.isMuted(), "Mute should be disabled")
        || !require(std::fabs(output.volume() - 0.6F) < 0.0001F,
            "Unmute should restore the configured volume"))
    {
        return 1;
    }

    output.setVolume(NAN);
    if (!require(std::fabs(output.volume() - 1.0F) < 0.0001F,
            "Non-finite volume should normalize to one"))
    {
        return 1;
    }

    output.setVolume(std::numeric_limits<float>::infinity());
    if (!require(std::fabs(output.volume() - 1.0F) < 0.0001F,
            "Infinite volume should normalize to one"))
    {
        return 1;
    }

    // Lifecycle calls are intentionally safe before initialization and when
    // repeated. This also validates the non-Windows fallback build.
    if (!require(output.pause() == AudioBlockStatus::NotInitialized,
            "Pause before initialization must report the output contract state")
        || !require(output.resume() == AudioBlockStatus::NotInitialized,
            "Resume before initialization must report the output contract state"))
    {
        return 1;
    }
    output.stop();
    output.stop();

    const std::vector<std::int16_t> preInitializationBlock{0, 0};
    if (!require(
            output.beginBlockMode(AudioBlockStartMode::StartImmediately)
                == AudioBlockStatus::NotInitialized,
            "Block mode must require initialized output")
        || !require(
            output.writableFrames().status == AudioBlockStatus::NotInitialized,
            "Writable block capacity must report uninitialized output")
        || !require(
            output.writeFrames(preInitializationBlock) == AudioBlockStatus::NotInitialized,
            "Block writes must require initialized output"))
    {
        return 1;
    }

    output.shutdown();
    output.shutdown();

    if (!output.initialize())
    {
        std::cout
            << "Audio output unavailable; lifecycle and volume checks passed."
            << std::endl;
        return 0;
    }

    constexpr std::size_t frameCount = 48000U;
    std::vector<std::int16_t> pcm(frameCount * 2U, 0);

    if (!require(
            output.beginBlockMode(AudioBlockStartMode::ConfigurePaused)
                == AudioBlockStatus::Success,
            "Block mode should configure from Idle")
        || !require(!output.hasBorrowedPcm(),
            "Block mode must not retain caller PCM")
        || !require(output.writableFrames().status == AudioBlockStatus::Paused,
            "Configured paused block mode must report Paused")
        || !require(output.writeFrames(pcm) == AudioBlockStatus::Paused,
            "Non-empty block writes must reject while paused")
        || !require(output.writeFrames({}) == AudioBlockStatus::Success,
            "A zero block write must be a valid no-op in block mode")
        || !require(!output.submit(
            pcm.data(),
            pcm.size() * sizeof(std::int16_t),
            48000,
            2),
            "Legacy submission must not steal active block mode"))
    {
        output.shutdown();
        return 1;
    }

    if (!require(output.resume() == AudioBlockStatus::Success,
            "Resuming configured paused block mode must report success"))
    {
        output.shutdown();
        return 1;
    }
    const auto blockAvailability = output.writableFrames();
    std::vector<std::int16_t> shortBlock{100, -100, 200, -200};
    const std::vector<std::int16_t> oddBlock{100, -100, 200};
    const std::vector<std::int16_t> silentBlock{0, 0, 0, 0};
    if (!require(blockAvailability.status == AudioBlockStatus::Success,
            "Resumed block mode must report writable capacity")
        || !require(output.writeFrames(oddBlock) == AudioBlockStatus::InvalidRequest,
            "Odd interleaved sample counts must be rejected")
        || !require(output.writeFrames(shortBlock) == AudioBlockStatus::Success,
            "A writable canonical block must be copied into the output")
        || !require(output.writeFrames(silentBlock) == AudioBlockStatus::Success,
            "A writable silent block must be copied into the output")
        || !require(!output.hasBorrowedPcm(),
            "Block writes must not retain caller-owned storage"))
    {
        output.shutdown();
        return 1;
    }
    shortBlock.assign(shortBlock.size(), 0);

    if (!require(output.pause() == AudioBlockStatus::Success,
            "Pausing active block mode must report success")
        || !require(output.writableFrames().status == AudioBlockStatus::Paused,
            "Pausing an active block mode must freeze writable output")
        || !require(output.writeFrames(silentBlock) == AudioBlockStatus::Paused,
            "Pausing an active block mode must reject non-empty writes"))
    {
        output.shutdown();
        return 1;
    }
    if (!require(output.resume() == AudioBlockStatus::Success,
            "Resuming active block mode must report success")
        || !require(output.writableFrames().status == AudioBlockStatus::Success,
            "Resuming an active block mode must restore writable output"))
    {
        output.shutdown();
        return 1;
    }

    output.stop();
    if (!require(
            output.beginBlockMode(AudioBlockStartMode::StartImmediately)
                == AudioBlockStatus::Success,
            "Block mode should start directly from Idle")
        || !require(output.writableFrames().status == AudioBlockStatus::Success,
            "Immediately started block mode must report writable capacity"))
    {
        output.shutdown();
        return 1;
    }
    output.stop();

    if (!require(output.pause() == AudioBlockStatus::NotInitialized,
            "Pause from idle mode must report the output contract state")
        || !require(output.resume() == AudioBlockStatus::NotInitialized,
            "Resume from idle mode must report the output contract state"))
    {
        output.shutdown();
        return 1;
    }

    if (!require(output.writeFrames(shortBlock) == AudioBlockStatus::NotInitialized,
            "Stop must return block operations to the Idle state")
        || !require(output.submit(
            pcm.data(),
            pcm.size() * sizeof(std::int16_t),
            48000,
            2),
            "Legacy submission must work after block stop")
        || !require(
            output.beginBlockMode(AudioBlockStartMode::StartImmediately)
                == AudioBlockStatus::WrongMode,
            "Block mode must not steal an active legacy submission")
        || !require(output.writeFrames(shortBlock) == AudioBlockStatus::WrongMode,
            "Block writes must reject while legacy PCM is active"))
    {
        output.shutdown();
        return 1;
    }
    output.stop();

    if (!require(output.submitAtMediaTime(
            pcm.data(),
            pcm.size() * sizeof(std::int16_t),
            48000,
            2,
            0.25,
            1.0F,
            false),
            "Audio output should configure a paused media offset")
        || !require(
            output.hasBorrowedPcm(),
            "Paused audio configuration should retain the caller PCM borrow")
        || !require(
            output.submittedFrames() == 0U,
            "Paused audio configuration must not start or pump audio")
        || !require(
            output.playbackPositionSeconds() >= 0.25,
            "Paused audio configuration should retain the requested media offset"))
    {
        output.shutdown();
        return 1;
    }

    if (!require(output.resume() == AudioBlockStatus::Success,
            "Resuming a paused legacy PCM configuration must report success"))
    {
        output.shutdown();
        return 1;
    }
    if (!require(
            output.submittedFrames() > 0U,
            "Resuming a paused audio configuration should begin submitting audio"))
    {
        output.shutdown();
        return 1;
    }

    output.stop();
    if (!require(
            !output.hasBorrowedPcm(),
            "Stop should release the caller PCM borrow before any buffer replacement"))
    {
        output.shutdown();
        return 1;
    }

    if (!require(output.submit(
            pcm.data(),
            pcm.size() * sizeof(std::int16_t),
            48000,
            2),
            "Audio output should accept valid PCM")
        || !require(output.submitAtFrame(
            pcm.data(),
            pcm.size() * sizeof(std::int16_t),
            48000,
            2,
            120U),
            "Audio output should accept a valid frame offset"))
    {
        output.shutdown();
        return 1;
    }

    if (!require(output.submitAtMediaTime(
            pcm.data(),
            pcm.size() * sizeof(std::int16_t),
            48000,
            2,
            0.25,
            2.0F),
            "Audio output should accept a tempo-mapped media offset")
        || !require(
            output.playbackPositionSeconds() >= 0.20,
            "Tempo-mapped playback position should remain on the media timeline"))
    {
        output.shutdown();
        return 1;
    }

    if (!require(output.submittedFrames() > 0U,
            "Audio output should report submitted frames")
        || !require(output.queuedFrames() <= output.submittedFrames(),
            "Queued frames should not exceed submitted frames")
        || !require(output.playedFrames() <= output.submittedFrames(),
            "Played frames should not exceed submitted frames")
        || !require(std::isfinite(output.playbackPositionSeconds())
                && output.playbackPositionSeconds() >= 0.0,
            "Audio output should report a finite playback position")
        || !require(output.playbackPositionSeconds() >= 120.0 / 48000.0,
            "Playback position should include the source frame offset"))
    {
        output.shutdown();
        return 1;
    }

    (void)output.pause();
    (void)output.pause();
    (void)output.resume();
    (void)output.resume();
    output.stop();
    output.stop();
    output.shutdown();
    output.shutdown();

    if (!require(output.initialize(),
            "The same AudioOutput object must initialize again after shutdown"))
    {
        return 1;
    }
    output.stop();
    output.shutdown();
    return 0;
}
