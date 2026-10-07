#include "Audio/AudioMixer.h"
#include "Audio/AudioMixerInput.h"
#include "Music/BackgroundMusicExecutor.h"
#include "Music/BackgroundMusicOrchestrator.h"
#include "Music/BackgroundMusicRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using DesktopSceneEngine::Audio::AudioInputHandle;
using DesktopSceneEngine::Audio::AudioMixer;
using DesktopSceneEngine::Audio::AudioMixerInput;
using DesktopSceneEngine::Audio::AudioMixerInputReadResult;
using DesktopSceneEngine::Audio::AudioMixerInputReadStatus;
using DesktopSceneEngine::Audio::AudioMixStatus;
using DesktopSceneEngine::Music::ActiveMusicScopeSnapshot;
using DesktopSceneEngine::Music::BackgroundMusicExecutor;
using DesktopSceneEngine::Music::BackgroundMusicOrchestrator;
using DesktopSceneEngine::Music::BackgroundMusicRuntime;
using DesktopSceneEngine::Music::BackgroundMusicPlaybackState;
using DesktopSceneEngine::Music::ItemId;
using DesktopSceneEngine::Music::MusicQueueMode;
using DesktopSceneEngine::Music::MusicQueueSnapshot;
using DesktopSceneEngine::Music::MusicRuntimeAction;
using DesktopSceneEngine::Music::MusicRuntimeEvent;
using DesktopSceneEngine::Music::MusicScope;
using DesktopSceneEngine::Music::MusicSessionPhase;
using DesktopSceneEngine::Music::MusicSourceEnded;
using DesktopSceneEngine::Music::MusicSourceFailed;
using DesktopSceneEngine::Music::MusicSourceFailureClass;
using DesktopSceneEngine::Music::MusicSourceStarted;
using DesktopSceneEngine::Music::MusicSourceToken;
using DesktopSceneEngine::Music::MusicTrackSnapshot;
using DesktopSceneEngine::Music::PlaybackActivity;
using DesktopSceneEngine::Music::PlaybackSessionStarted;
using DesktopSceneEngine::Music::PlaylistId;
using DesktopSceneEngine::Music::SetSourceGain;
using DesktopSceneEngine::Music::StartSource;
using DesktopSceneEngine::Music::StopSource;
using DesktopSceneEngine::Music::TrackId;

constexpr float FloatTolerance = 0.00001F;
constexpr std::size_t StereoChannels = 2U;
constexpr std::size_t FixtureFrames = 1003U;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool approximatelyEqual(float left, float right)
{
    return std::fabs(left - right) <= FloatTolerance;
}

std::filesystem::path fixturePath()
{
    return std::filesystem::path{DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR}
        / "SequentialAudioSourceStereo48k.wav";
}

StartSource startAction(
    std::uint64_t token,
    MusicScope scope = MusicScope::PlaylistGlobal,
    float gain = 1.0F,
    std::filesystem::path path = fixturePath(),
    PlaybackActivity activity = PlaybackActivity::Playing
)
{
    return StartSource{
        scope,
        TrackId{"track-" + std::to_string(token)},
        MusicSourceToken{token},
        std::move(path),
        gain,
        activity};
}

bool isStarted(
    const std::optional<MusicRuntimeEvent>& fact,
    std::uint64_t token
)
{
    const auto* const started = fact ? std::get_if<MusicSourceStarted>(&*fact) : nullptr;
    return started != nullptr && started->token == MusicSourceToken{token};
}

bool isFailure(
    const std::optional<MusicRuntimeEvent>& fact,
    std::uint64_t token,
    MusicSourceFailureClass failure
)
{
    const auto* const failed = fact ? std::get_if<MusicSourceFailed>(&*fact) : nullptr;
    return failed != nullptr && failed->token == MusicSourceToken{token}
        && failed->failure == failure;
}

bool mixFrames(
    AudioMixer& mixer,
    std::span<float> destination
)
{
    const auto result = mixer.mixFrames(destination);
    return result.status == AudioMixStatus::Success
        && result.framesProduced == destination.size() / StereoChannels;
}

class ConstantInput final : public AudioMixerInput
{
public:
    explicit ConstantInput(float sample) noexcept
        : sample_(sample)
    {
    }

    [[nodiscard]] AudioMixerInputReadResult readFrames(
        std::span<float> destination
    ) noexcept override
    {
        if (destination.size() % StereoChannels != 0U)
        {
            return {0U, AudioMixerInputReadStatus::Failure};
        }

        std::fill(destination.begin(), destination.end(), sample_);
        return {destination.size() / StereoChannels, AudioMixerInputReadStatus::Data};
    }

private:
    float sample_ = 0.0F;
};

class FailingAfterStartInput final : public AudioMixerInput
{
public:
    explicit FailingAfterStartInput(
        std::unique_ptr<AudioMixerInput> input
    ) noexcept
        : input_(std::move(input))
    {
    }

    [[nodiscard]] AudioMixerInputReadResult readFrames(
        std::span<float> destination
    ) noexcept override
    {
        (void)destination;
        return {0U, AudioMixerInputReadStatus::Failure};
    }

private:
    std::unique_ptr<AudioMixerInput> input_{};
};

bool verifyStartAndGain()
{
    AudioMixer mixer{};
    BackgroundMusicExecutor executor{mixer};
    if (!require(isStarted(executor.execute(startAction(1U, MusicScope::PlaylistGlobal, 0.5F)), 1U),
            "A valid source must acknowledge Started only after Mixer registration"))
    {
        return false;
    }

    std::vector<float> firstFrame(2U, 0.0F);
    if (!require(mixFrames(mixer, firstFrame)
            && approximatelyEqual(firstFrame[0U], -0.375F)
            && approximatelyEqual(firstFrame[1U], 0.375F),
        "The initial gain must be applied once by the Mixer"))
    {
        return false;
    }

    const std::optional<MusicRuntimeEvent> gainFact = executor.execute(
        SetSourceGain{MusicSourceToken{1U}, 0.25F});
    std::vector<float> secondFrame(2U, 0.0F);
    return require(!gainFact.has_value(), "A live gain update must not emit a semantic fact")
        && require(mixFrames(mixer, secondFrame)
                && approximatelyEqual(secondFrame[0U], -0.0625F)
                && approximatelyEqual(secondFrame[1U], 0.046875F),
            "A live gain update must affect the same source without restart")
        && require(!executor.execute(SetSourceGain{MusicSourceToken{999U}, 0.5F}).has_value(),
            "A stale gain update must be a no-op");
}

bool verifyStartValidationAndMissingPath()
{
    AudioMixer mixer{};
    BackgroundMusicExecutor executor{mixer};
    std::vector<float> silence(2U, 9.0F);
    const std::filesystem::path missing = fixturePath().parent_path() / "missing-background-track.wav";
    if (!require(isFailure(
            executor.execute(startAction(2U, MusicScope::PlaylistGlobal, 1.0F, missing)),
            2U,
            MusicSourceFailureClass::UnplayableTrack),
        "A missing source must be classified as an unplayable track"))
    {
        return false;
    }

    silence.assign(2U, 9.0F);
    return require(mixFrames(mixer, silence)
                && silence == std::vector<float>(2U, 0.0F),
            "A failed source open must not leave a Mixer input")
        && require(!executor.execute(StopSource{MusicSourceToken{2U}}).has_value(),
            "Stopping an unregistered token must be a no-op");
}

bool verifyRegistrationFailure()
{
    AudioMixer mixer{};
    BackgroundMusicExecutor executor{
        mixer,
        [](AudioMixer&, std::unique_ptr<AudioMixerInput>, float) noexcept -> AudioInputHandle
        {
            return {};
        }};

    std::vector<float> silence(2U, 9.0F);
    return require(isFailure(
                executor.execute(startAction(3U)),
                3U,
                MusicSourceFailureClass::TransientResourceFailure),
            "Mixer registration failure must remain transient")
        && require(mixFrames(mixer, silence)
                && silence == std::vector<float>(2U, 0.0F),
            "Registration failure must not leave a Mixer input")
        && require(!executor.execute(StopSource{MusicSourceToken{3U}}).has_value(),
            "A failed registration token must be safely stoppable");
}

bool verifyTerminalFailure()
{
    AudioMixer mixer{};
    BackgroundMusicExecutor executor{
        mixer,
        [](AudioMixer& target, std::unique_ptr<AudioMixerInput> input, float gain) noexcept
            -> AudioInputHandle
        {
            return target.addInput(
                std::make_unique<FailingAfterStartInput>(std::move(input)),
                gain);
        }};
    if (!require(isStarted(executor.execute(startAction(30U)), 30U),
            "A registered source must report Started before a later input failure"))
    {
        return false;
    }

    std::vector<float> output(2U, 1.0F);
    if (!require(mixFrames(mixer, output), "The injected source input must enter its terminal state"))
    {
        return false;
    }

    const std::vector<MusicRuntimeEvent> facts = executor.collectTerminalEvents();
    const auto* const failed = facts.size() == 1U
        ? std::get_if<MusicSourceFailed>(&facts.front())
        : nullptr;
    return require(failed != nullptr
            && failed->token == MusicSourceToken{30U}
            && failed->failure == MusicSourceFailureClass::UnplayableTrack,
        "A terminal input failure must map to UnplayableTrack after cleanup")
        && require(executor.collectTerminalEvents().empty(),
            "A terminal input failure must be harvested exactly once");
}

bool verifyStopAndTwoSources()
{
    AudioMixer mixer{};
    BackgroundMusicExecutor executor{mixer};
    if (!require(isStarted(executor.execute(startAction(4U, MusicScope::PlaylistGlobal, 0.25F)), 4U)
            && isStarted(executor.execute(startAction(5U, MusicScope::Item, 0.5F)), 5U),
        "Playlist and Item sources must start independently"))
    {
        return false;
    }

    std::vector<float> mixed(2U, 0.0F);
    if (!require(mixFrames(mixer, mixed)
            && approximatelyEqual(mixed[0U], -0.5625F)
            && approximatelyEqual(mixed[1U], 0.5625F),
        "Two BGM sources must sum through the shared Mixer"))
    {
        return false;
    }

    if (!require(!executor.execute(StopSource{MusicSourceToken{4U}}).has_value(),
            "Stopping a live source must not emit a fact"))
    {
        return false;
    }

    mixed.assign(2U, 0.0F);
    if (!require(mixFrames(mixer, mixed)
            && approximatelyEqual(mixed[0U], -0.125F)
            && approximatelyEqual(mixed[1U], 0.09375F),
        "Stopping Playlist BGM must leave Item BGM active"))
    {
        return false;
    }

    return require(!executor.execute(StopSource{MusicSourceToken{4U}}).has_value(),
            "Repeated StopSource must be idempotent")
        && require(!executor.execute(StopSource{MusicSourceToken{5U}}).has_value(),
            "Item source StopSource must remove only its own input");
}

bool verifyTerminalHarvestAndStaleTokens()
{
    AudioMixer mixer{};
    BackgroundMusicExecutor executor{mixer};
    if (!require(isStarted(executor.execute(startAction(6U)), 6U),
            "Terminal-harvest setup must start a real source"))
    {
        return false;
    }

    std::vector<float> output((FixtureFrames + 1U) * StereoChannels, 0.0F);
    if (!require(mixFrames(mixer, output), "The source must be pulled to its terminal boundary"))
    {
        return false;
    }

    const std::vector<MusicRuntimeEvent> terminalFacts = executor.collectTerminalEvents();
    const auto* const ended = terminalFacts.size() == 1U
        ? std::get_if<MusicSourceEnded>(&terminalFacts.front())
        : nullptr;
    if (!require(ended != nullptr && ended->token == MusicSourceToken{6U},
            "EOF must produce one Ended fact after cleanup")
        || !require(executor.collectTerminalEvents().empty(),
            "EOF harvesting must be exactly once"))
    {
        return false;
    }

    (void)executor.execute(StopSource{MusicSourceToken{6U}});
    (void)executor.execute(SetSourceGain{MusicSourceToken{6U}, 0.5F});
    if (!require(isStarted(executor.execute(startAction(7U)), 7U),
            "A stale terminal token must not affect a new source"))
    {
        return false;
    }

    std::vector<float> firstFrame(2U, 0.0F);
    return require(mixFrames(mixer, firstFrame)
                && approximatelyEqual(firstFrame[0U], -0.75F)
                && approximatelyEqual(firstFrame[1U], 0.75F),
        "The new source must remain unaffected by stale operations");
}

bool verifyEmbeddedLikeCoexistenceAndTerminalIsolation()
{
    AudioMixer mixer{};
    const AudioInputHandle embeddedHandle = mixer.addInput(std::make_unique<ConstantInput>(0.5F));
    BackgroundMusicExecutor executor{mixer};
    if (!require(static_cast<bool>(embeddedHandle)
            && isStarted(executor.execute(startAction(8U, MusicScope::PlaylistGlobal, 0.5F)), 8U),
        "Embedded-like and BGM inputs must coexist"))
    {
        return false;
    }

    std::vector<float> firstFrame(2U, 0.0F);
    if (!require(mixFrames(mixer, firstFrame)
            && approximatelyEqual(firstFrame[0U], 0.125F)
            && approximatelyEqual(firstFrame[1U], 0.875F),
        "BGM must add to, rather than replace, the embedded-like input"))
    {
        return false;
    }

    std::vector<float> terminalBlock((FixtureFrames + 1U) * StereoChannels, 0.0F);
    if (!require(mixFrames(mixer, terminalBlock), "BGM must reach EOF without affecting embedded input"))
    {
        return false;
    }

    const std::vector<MusicRuntimeEvent> terminalFacts = executor.collectTerminalEvents();
    if (!require(terminalFacts.size() == 1U
            && std::get_if<MusicSourceEnded>(&terminalFacts.front()) != nullptr,
        "BGM terminal cleanup must report only its own terminal fact"))
    {
        return false;
    }

    std::vector<float> embeddedOnly(2U, 0.0F);
    return require(mixFrames(mixer, embeddedOnly)
                && approximatelyEqual(embeddedOnly[0U], 0.5F)
                && approximatelyEqual(embeddedOnly[1U], 0.5F),
        "BGM terminal cleanup must leave the embedded-like Mixer input intact");
}

bool verifyDeterministicTerminalOrder()
{
    AudioMixer mixer{};
    BackgroundMusicExecutor executor{mixer};
    if (!require(isStarted(executor.execute(startAction(10U, MusicScope::Item)), 10U)
            && isStarted(executor.execute(startAction(9U, MusicScope::PlaylistGlobal)), 9U),
        "Terminal-order setup must start both scopes"))
    {
        return false;
    }

    std::vector<float> terminalBlock((FixtureFrames + 1U) * StereoChannels, 0.0F);
    if (!require(mixFrames(mixer, terminalBlock), "Both sources must reach EOF before collection"))
    {
        return false;
    }

    const std::vector<MusicRuntimeEvent> facts = executor.collectTerminalEvents();
    const auto* const playlistEnded = facts.size() == 2U
        ? std::get_if<MusicSourceEnded>(&facts[0U])
        : nullptr;
    const auto* const itemEnded = facts.size() == 2U
        ? std::get_if<MusicSourceEnded>(&facts[1U])
        : nullptr;
    return require(playlistEnded != nullptr && playlistEnded->token == MusicSourceToken{9U}
                && itemEnded != nullptr && itemEnded->token == MusicSourceToken{10U},
        "Terminal facts must be ordered PlaylistGlobal then Item");
}

PlaylistId playlistId(const char* value)
{
    return PlaylistId{value};
}

TrackId trackId(const char* value)
{
    return TrackId{value};
}

MusicTrackSnapshot track(const char* id)
{
    return MusicTrackSnapshot{trackId(id), fixturePath(), 1.0F};
}

bool verifyRealExecutorOrchestration()
{
    AudioMixer mixer{};
    BackgroundMusicExecutor executor{mixer};
    BackgroundMusicRuntime runtime{};
    BackgroundMusicOrchestrator orchestrator{
        runtime,
        [&executor](const MusicRuntimeAction& action)
        {
            return executor.execute(action);
        }};

    const ActiveMusicScopeSnapshot scope{
        playlistId("playlist"),
        std::nullopt,
        MusicQueueSnapshot{
            {track("track-a"), track("track-b")},
            MusicQueueMode::ListLoop,
            1.0F},
        std::nullopt,
        PlaybackActivity::Playing};
    if (!require(orchestrator.processMusicEvent(PlaybackSessionStarted{scope})
                == DesktopSceneEngine::Music::MusicProcessResult::Completed
            && runtime.playbackState() == BackgroundMusicPlaybackState::Playing
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::Playing
            && runtime.playlistSession()->currentTrackId() == std::optional<TrackId>{trackId("track-a")},
        "Runtime must receive real Started feedback through the Orchestrator"))
    {
        return false;
    }

    std::vector<float> terminalBlock((FixtureFrames + 1U) * StereoChannels, 0.0F);
    if (!require(mixFrames(mixer, terminalBlock), "The first real queue source must reach EOF"))
    {
        return false;
    }

    const std::vector<MusicRuntimeEvent> terminalFacts = executor.collectTerminalEvents();
    if (!require(terminalFacts.size() == 1U
            && std::get_if<MusicSourceEnded>(&terminalFacts.front()) != nullptr
            && orchestrator.processMusicEvent(terminalFacts.front())
                == DesktopSceneEngine::Music::MusicProcessResult::Completed,
        "EOF feedback must re-enter the Runtime through the Orchestrator"))
    {
        return false;
    }

    return require(runtime.playlistSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::Playing
            && runtime.playlistSession()->currentTrackId() == std::optional<TrackId>{trackId("track-b")},
        "Runtime queue policy must select and start the successor track");
}

}

int main()
{
    const bool success = verifyStartAndGain()
        && verifyStartValidationAndMissingPath()
        && verifyRegistrationFailure()
        && verifyTerminalFailure()
        && verifyStopAndTwoSources()
        && verifyTerminalHarvestAndStaleTokens()
        && verifyEmbeddedLikeCoexistenceAndTerminalIsolation()
        && verifyDeterministicTerminalOrder()
        && verifyRealExecutorOrchestration();
    return success ? 0 : 1;
}
