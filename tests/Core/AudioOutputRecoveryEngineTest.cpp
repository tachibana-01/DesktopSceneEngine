#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/Engine.h>

#include "Core/AudioOutputRecoveryTestHooks.h"
#include "Core/SharedAudioOutputRecoveryPolicy.h"
#include "Core/SharedAudioOutputPolicy.h"
#include "Audio/AudioMixer.h"
#include "Audio/AudioMixerInput.h"
#include "Music/BackgroundMusicExecutor.h"
#include "Music/BackgroundMusicRuntime.h"
#include "Resources/AudioResource.h"
#include "Runtime/Playback/PlaybackRuntime.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <utility>
#include <vector>

namespace DesktopSceneEngine::Core
{

class AudioOutputRecoveryEngineTestAccess final
{
public:
    static void preparePlayingEngine(Engine& engine, const Domain::Project& project)
    {
        engine.initialized_ = true;
        engine.projectBound_ = true;
        engine.boundProject_ = &project;
        engine.playbackRuntime_ = std::make_unique<Runtime::PlaybackRuntime>();

        Domain::Playback playback{};
        playback.speed = 1.0F;
        playback.loop = true;
        engine.playbackRuntime_->configure(playback, 600.0F);
    }

    static void installHooks(Engine& engine, AudioOutputRecoveryTestHooks hooks)
    {
        engine.audioOutputRecoveryTestHooks_ = std::make_unique<AudioOutputRecoveryTestHooks>(
            std::move(hooks));
    }

    [[nodiscard]] static double retryElapsedSeconds(const Engine& engine) noexcept
    {
        return engine.audioOutputRetryElapsedSeconds_;
    }

    [[nodiscard]] static AudioOutputOperationalState operationalState(
        const Engine& engine
    ) noexcept
    {
        return engine.audioOutputOperationalState_;
    }

    [[nodiscard]] static SharedAudioOutputState sharedOutputState(
        const Engine& engine
    ) noexcept
    {
        return static_cast<SharedAudioOutputState>(engine.sharedAudioOutputState_);
    }

    [[nodiscard]] static const void* audioOutputAddress(const Engine& engine) noexcept
    {
        return engine.audioOutput_.get();
    }

    [[nodiscard]] static bool hasActiveBackgroundMusic(const Engine& engine) noexcept
    {
        return engine.backgroundMusicExecutor_->hasActiveSources();
    }

    [[nodiscard]] static std::optional<Music::MusicSourceToken> playlistToken(
        const Engine& engine
    ) noexcept
    {
        const Music::MusicSession* const session = engine.backgroundMusicRuntime_->playlistSession();
        return session == nullptr ? std::nullopt : session->sourceToken();
    }

    [[nodiscard]] static bool addSyntheticEmbeddedInput(
        Engine& engine,
        std::unique_ptr<Audio::AudioMixerInput> input
    ) noexcept
    {
        const Audio::AudioInputHandle handle = engine.audioMixer_->addInput(std::move(input), 1.0F);
        if (!handle)
        {
            return false;
        }

        engine.embeddedAudioInputHandleValue_ = handle.value;
        return true;
    }

    [[nodiscard]] static bool hasEmbeddedInput(const Engine& engine) noexcept
    {
        return engine.embeddedAudioInputHandleValue_ != 0U;
    }

    [[nodiscard]] static bool reconcileOutput(Engine& engine) noexcept
    {
        return engine.reconcileAudioOutputState(true);
    }

    [[nodiscard]] static bool configureEmbeddedAudio(
        Engine& engine,
        const std::filesystem::path& path
    )
    {
        auto resource = std::make_unique<Resources::AudioResource>();
        if (!resource->open(path) || !resource->setTempo(1.0F))
        {
            return false;
        }

        engine.audioResource_ = std::move(resource);
        return engine.configureEmbeddedAudioInputAt(
            engine.playbackRuntime_->currentTime(),
            engine.playbackRuntime_->configuration().speed);
    }

    [[nodiscard]] static std::uint64_t embeddedInputHandle(const Engine& engine) noexcept
    {
        return engine.embeddedAudioInputHandleValue_;
    }
};

}

namespace
{

using DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot;
using DesktopSceneEngine::BackgroundMusicControl::Event;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackSessionStarted;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistId;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicConfigChanged;
using DesktopSceneEngine::BackgroundMusicControl::QueueMode;
using DesktopSceneEngine::BackgroundMusicControl::QueueSnapshot;
using DesktopSceneEngine::BackgroundMusicControl::TrackId;
using DesktopSceneEngine::BackgroundMusicControl::TrackSnapshot;
using DesktopSceneEngine::Audio::MixedOutputBridgeResult;
using DesktopSceneEngine::Audio::MixedOutputBridgeStatus;
using DesktopSceneEngine::Audio::AudioMixerInput;
using DesktopSceneEngine::Audio::AudioMixerInputReadResult;
using DesktopSceneEngine::Audio::AudioMixerInputReadStatus;
using DesktopSceneEngine::Core::AudioOutputRecoveryEngineTestAccess;
using DesktopSceneEngine::Core::AudioOutputRecoveryTestHooks;
using DesktopSceneEngine::Core::AudioOutputOperationalState;
using DesktopSceneEngine::Core::SharedAudioOutputState;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

std::filesystem::path fixturePath()
{
    return std::filesystem::path{DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR}
        / "SequentialAudioSourceStereo48k.wav";
}

Event makeStartEvent()
{
    return PlaybackSessionStarted{ActiveScopeSnapshot{
        PlaylistId{"recovery-playlist"},
        QueueSnapshot{
            {TrackSnapshot{TrackId{"recovery-track"}, fixturePath(), 1.0}},
            QueueMode::ListLoop,
            1.0},
        std::nullopt,
        std::nullopt}};
}

Event makeGainChangeEvent(double trackVolume)
{
    return PlaylistMusicConfigChanged{
        PlaylistId{"recovery-playlist"},
        QueueSnapshot{
            {TrackSnapshot{TrackId{"recovery-track"}, fixturePath(), trackVolume}},
            QueueMode::ListLoop,
            1.0}};
}

struct ScriptedOutput
{
    std::vector<bool> initializeResults{true};
    std::vector<DesktopSceneEngine::Audio::AudioBlockStatus> beginResults{
        DesktopSceneEngine::Audio::AudioBlockStatus::Success};
    std::vector<DesktopSceneEngine::Audio::AudioBlockStatus> pauseResults{
        DesktopSceneEngine::Audio::AudioBlockStatus::Success};
    std::vector<DesktopSceneEngine::Audio::AudioBlockStatus> resumeResults{
        DesktopSceneEngine::Audio::AudioBlockStatus::Success};
    std::vector<MixedOutputBridgeResult> pumpResults{
        {0U, MixedOutputBridgeStatus::WroteFrames}};
    std::size_t initializeCalls = 0U;
    std::size_t shutdownCalls = 0U;
    std::size_t beginCalls = 0U;
    std::size_t pauseCalls = 0U;
    std::size_t resumeCalls = 0U;
    std::size_t pumpCalls = 0U;
    std::uint64_t defaultAudioEndpointGeneration = 0U;
    bool exposeDefaultAudioEndpointGeneration = false;
    std::function<void()> onBegin{};
    std::vector<DesktopSceneEngine::Platform::Audio::AudioBlockStartMode> beginModes{};

    [[nodiscard]] AudioOutputRecoveryTestHooks hooks()
    {
        AudioOutputRecoveryTestHooks result{};
        result.initialize = [this]()
        {
            const std::size_t index = initializeCalls++;
            return index < initializeResults.size() ? initializeResults[index] : true;
        };
        result.shutdown = [this]()
        {
            ++shutdownCalls;
        };
        result.beginBlockMode = [this](const auto mode)
        {
            beginModes.push_back(mode);
            const std::size_t index = beginCalls++;
            if (onBegin)
            {
                onBegin();
            }
            return index < beginResults.size()
                ? beginResults[index]
                : DesktopSceneEngine::Audio::AudioBlockStatus::Success;
        };
        result.pause = [this]()
        {
            const std::size_t index = pauseCalls++;
            return index < pauseResults.size()
                ? pauseResults[index]
                : DesktopSceneEngine::Audio::AudioBlockStatus::Success;
        };
        result.resume = [this]()
        {
            const std::size_t index = resumeCalls++;
            return index < resumeResults.size()
                ? resumeResults[index]
                : DesktopSceneEngine::Audio::AudioBlockStatus::Success;
        };
        result.pump = [this]()
        {
            const std::size_t index = pumpCalls++;
            return index < pumpResults.size()
                ? pumpResults[index]
                : MixedOutputBridgeResult{0U, MixedOutputBridgeStatus::WroteFrames};
        };
        if (exposeDefaultAudioEndpointGeneration)
        {
            result.defaultAudioEndpointGeneration = [this]()
            {
                return defaultAudioEndpointGeneration;
            };
        }
        return result;
    }
};

class SilentInput final : public AudioMixerInput
{
public:
    [[nodiscard]] AudioMixerInputReadResult readFrames(
        std::span<float> destination
    ) noexcept override
    {
        std::fill(destination.begin(), destination.end(), 0.0F);
        return {destination.size() / 2U, AudioMixerInputReadStatus::Data};
    }
};

bool prepareBgmOnlyEngine(
    DesktopSceneEngine::Engine& engine,
    const DesktopSceneEngine::Domain::Project& project,
    ScriptedOutput& output
)
{
    AudioOutputRecoveryEngineTestAccess::preparePlayingEngine(engine, project);
    AudioOutputRecoveryEngineTestAccess::installHooks(engine, output.hooks());
    return engine.processBackgroundMusicControl(makeStartEvent());
}

bool verifyDeviceFailureDoesNotRecoverBeforeCooldown()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::DeviceFailure},
        {0U, MixedOutputBridgeStatus::WroteFrames}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "BGM-only setup must register a source and start the shared output")
        || !require(output.initializeCalls == 1U && output.beginCalls == 1U
                && output.pumpCalls == 1U,
            "Initial BGM setup must perform exactly one scripted output start and prime"))
    {
        return false;
    }
    const void* const audioOutputAddress = AudioOutputRecoveryEngineTestAccess::audioOutputAddress(engine);
    const auto token = AudioOutputRecoveryEngineTestAccess::playlistToken(engine);

    engine.update(0.0F);
    if (!require(output.pumpCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable
            && AudioOutputRecoveryEngineTestAccess::hasActiveBackgroundMusic(engine)
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token,
            "The first Engine update must preserve BGM state while entering DeviceUnavailable"))
    {
        return false;
    }

    engine.update(0.499F);
    if (!require(output.initializeCalls == 1U && output.pumpCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable
            && AudioOutputRecoveryEngineTestAccess::retryElapsedSeconds(engine) < 0.5,
            "A device outage must not retry before the 500 ms recovery cooldown"))
    {
        return false;
    }

    engine.update(0.001F);
    if (!require(output.initializeCalls == 2U && output.beginCalls == 2U
            && output.pumpCalls == 3U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::Healthy
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(engine)
                == SharedAudioOutputState::Running
            && AudioOutputRecoveryEngineTestAccess::retryElapsedSeconds(engine) == 0.0
            && AudioOutputRecoveryEngineTestAccess::audioOutputAddress(engine) == audioOutputAddress
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token,
        "A DeviceFailure must make one same-object recovery attempt at 500 ms without restarting BGM"))
    {
        return false;
    }

    engine.update(0.1F);
    return require(output.initializeCalls == 2U && output.beginCalls == 2U,
        "A successful recovery must not schedule an additional lifecycle attempt on the next update");
}

bool verifyFailureDoesNotConsumePreFailureUpdateDelta()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::DeviceFailure},
        {0U, MixedOutputBridgeStatus::WroteFrames}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Pre-failure-delta setup must start BGM"))
    {
        return false;
    }

    engine.update(0.5F);
    if (!require(output.initializeCalls == 1U && output.pumpCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable
            && AudioOutputRecoveryEngineTestAccess::retryElapsedSeconds(engine) == 0.0,
            "A DeviceFailure during update must not consume that update's pre-failure delta"))
    {
        return false;
    }

    engine.update(0.499F);
    if (!require(output.initializeCalls == 1U,
            "Only post-failure elapsed time may count toward the recovery cooldown"))
    {
        return false;
    }

    engine.update(0.001F);
    return require(output.initializeCalls == 2U && output.beginCalls == 2U
            && output.pumpCalls == 3U,
        "Recovery must occur once a full 500 ms has elapsed after DeviceFailure");
}

bool verifyInitialAndBeginDeviceFailuresUseRecovery()
{
    DesktopSceneEngine::Domain::Project project{};

    DesktopSceneEngine::Engine initializeEngine{};
    ScriptedOutput initializeOutput{};
    initializeOutput.initializeResults = {false, true};
    if (!require(prepareBgmOnlyEngine(initializeEngine, project, initializeOutput)
            && AudioOutputRecoveryEngineTestAccess::operationalState(initializeEngine)
                == AudioOutputOperationalState::DeviceUnavailable
            && initializeOutput.initializeCalls == 1U && initializeOutput.beginCalls == 0U,
            "An output initialization failure must enter DeviceUnavailable without beginning output"))
    {
        return false;
    }
    initializeEngine.update(0.5F);
    if (!require(initializeOutput.initializeCalls == 2U && initializeOutput.beginCalls == 1U
            && initializeOutput.pumpCalls == 1U
            && AudioOutputRecoveryEngineTestAccess::operationalState(initializeEngine)
                == AudioOutputOperationalState::Healthy,
            "Initialization failure must recover through the same cooldown path"))
    {
        return false;
    }

    DesktopSceneEngine::Engine beginEngine{};
    ScriptedOutput beginOutput{};
    beginOutput.beginResults = {
        DesktopSceneEngine::Audio::AudioBlockStatus::DeviceFailure,
        DesktopSceneEngine::Audio::AudioBlockStatus::Success};
    if (!require(prepareBgmOnlyEngine(beginEngine, project, beginOutput)
            && AudioOutputRecoveryEngineTestAccess::operationalState(beginEngine)
                == AudioOutputOperationalState::DeviceUnavailable
            && beginOutput.initializeCalls == 1U && beginOutput.beginCalls == 1U,
            "A begin-block device failure must enter DeviceUnavailable"))
    {
        return false;
    }

    beginEngine.update(0.5F);
    return require(beginOutput.initializeCalls == 2U && beginOutput.beginCalls == 2U
            && beginOutput.pumpCalls == 1U
            && AudioOutputRecoveryEngineTestAccess::operationalState(beginEngine)
                == AudioOutputOperationalState::Healthy,
        "Begin-block device failure must recover through the same-object retry path");
}

bool verifyFailedRecoveryResetsCooldown()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.initializeResults = {true, false, true};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::DeviceFailure},
        {0U, MixedOutputBridgeStatus::WroteFrames}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Failed-recovery setup must start a BGM-only shared output"))
    {
        return false;
    }

    engine.update(0.0F);
    engine.update(0.5F);
    if (!require(output.initializeCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable
            && AudioOutputRecoveryEngineTestAccess::retryElapsedSeconds(engine) == 0.0,
            "A failed retry must remain unavailable and reset the cooldown"))
    {
        return false;
    }

    engine.update(0.499F);
    if (!require(output.initializeCalls == 2U && output.pumpCalls == 2U,
            "A failed retry must not retry again before another full cooldown"))
    {
        return false;
    }

    engine.update(0.001F);
    return require(output.initializeCalls == 3U && output.beginCalls == 2U
            && output.pumpCalls == 3U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::Healthy,
        "The next full cooldown must perform one successful recovery attempt");
}

bool verifyStopCancelsUnavailableRecovery()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::DeviceFailure}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Stop-cancel setup must start BGM"))
    {
        return false;
    }
    if (!require(AudioOutputRecoveryEngineTestAccess::addSyntheticEmbeddedInput(
                    engine,
                    std::make_unique<SilentInput>()),
            "Stop-cancel setup must also register embedded presence"))
    {
        return false;
    }

    engine.update(0.0F);
    if (!require(engine.processBackgroundMusicControl(
                    DesktopSceneEngine::BackgroundMusicControl::PlaybackStopped{}),
            "Stopping BGM during an outage must be admitted"))
    {
        return false;
    }

    engine.update(10.0F);
    return require(output.initializeCalls == 1U && output.beginCalls == 1U
            && !AudioOutputRecoveryEngineTestAccess::hasActiveBackgroundMusic(engine)
            && !AudioOutputRecoveryEngineTestAccess::hasEmbeddedInput(engine)
            && AudioOutputRecoveryEngineTestAccess::retryElapsedSeconds(engine) == 0.0,
        "Stopping the final input during an outage must cancel recovery without reopening output");
}

bool verifyContractFaultDoesNotRetry()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::OutputContractFailure}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Contract-fault setup must start BGM"))
    {
        return false;
    }
    const auto token = AudioOutputRecoveryEngineTestAccess::playlistToken(engine);

    engine.update(0.0F);
    engine.update(10.0F);
    return require(output.initializeCalls == 1U && output.beginCalls == 1U
            && output.pumpCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::ContractFaulted
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token,
        "Output contract failure must fail safe without timed recovery or BGM token replacement");
}

bool verifyNonDeviceBridgeStatusDoesNotRetryAndConfigRemainsLive()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::DeviceFailure},
        {0U, MixedOutputBridgeStatus::WroteFrames}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Config-during-outage setup must start BGM"))
    {
        return false;
    }
    const auto token = AudioOutputRecoveryEngineTestAccess::playlistToken(engine);
    engine.update(0.0F);
    if (!require(engine.processBackgroundMusicControl(makeGainChangeEvent(0.25))
            && output.pumpCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token,
            "Gain configuration must remain live during an outage without pulling audio"))
    {
        return false;
    }
    engine.update(0.5F);
    if (!require(output.pumpCalls == 3U
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token,
            "Recovered output must use the surviving current BGM registration"))
    {
        return false;
    }

    DesktopSceneEngine::Engine inactiveEngine{};
    ScriptedOutput inactiveOutput{};
    inactiveOutput.pumpResults = {
        {0U, MixedOutputBridgeStatus::OutputInactive},
        {0U, MixedOutputBridgeStatus::OutputInactive}};
    if (!require(prepareBgmOnlyEngine(inactiveEngine, project, inactiveOutput),
            "OutputInactive setup must start BGM"))
    {
        return false;
    }
    inactiveEngine.update(1.0F);
    return require(inactiveOutput.initializeCalls == 1U && inactiveOutput.beginCalls == 1U
            && inactiveOutput.pumpCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::operationalState(inactiveEngine)
                == AudioOutputOperationalState::Healthy,
        "OutputInactive must remain a diagnostic mismatch, not trigger recovery retry");
}

bool verifyPausedAndResumedRecovery()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.pauseResults = {DesktopSceneEngine::Audio::AudioBlockStatus::DeviceFailure};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::WroteFrames}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Paused-recovery setup must start BGM"))
    {
        return false;
    }

    engine.togglePause();
    if (!require(AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable,
            "A pause DeviceFailure must enter DeviceUnavailable"))
    {
        return false;
    }

    engine.update(0.5F);
    if (!require(output.initializeCalls == 2U && output.beginCalls == 2U
            && output.pumpCalls == 1U
            && !output.beginModes.empty()
            && output.beginModes.back()
                == DesktopSceneEngine::Platform::Audio::AudioBlockStartMode::ConfigurePaused
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(engine)
                == SharedAudioOutputState::Paused,
            "Paused recovery must configure output paused without pulling Mixer data"))
    {
        return false;
    }

    DesktopSceneEngine::Engine resumeEngine{};
    ScriptedOutput resumeOutput{};
    resumeOutput.resumeResults = {DesktopSceneEngine::Audio::AudioBlockStatus::DeviceFailure};
    resumeOutput.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::WroteFrames}};
    if (!require(prepareBgmOnlyEngine(resumeEngine, project, resumeOutput),
            "Resume-recovery setup must start BGM"))
    {
        return false;
    }
    resumeEngine.togglePause();
    resumeEngine.togglePause();
    if (!require(AudioOutputRecoveryEngineTestAccess::operationalState(resumeEngine)
                == AudioOutputOperationalState::DeviceUnavailable,
            "A resume DeviceFailure must enter DeviceUnavailable"))
    {
        return false;
    }

    resumeEngine.update(0.5F);
    return require(resumeOutput.initializeCalls == 2U && resumeOutput.beginCalls == 2U
            && resumeOutput.pumpCalls == 2U
            && !resumeOutput.beginModes.empty()
            && resumeOutput.beginModes.back()
                == DesktopSceneEngine::Platform::Audio::AudioBlockStartMode::StartImmediately
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(resumeEngine)
                == SharedAudioOutputState::Running,
        "Resume during an outage must recover Running output with one recovery prime");
}

bool verifyEmbeddedPresenceSurvivesOutageUntilRecoveryPreparation()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::DeviceFailure}};
    AudioOutputRecoveryEngineTestAccess::preparePlayingEngine(engine, project);
    AudioOutputRecoveryEngineTestAccess::installHooks(engine, output.hooks());
    if (!require(AudioOutputRecoveryEngineTestAccess::addSyntheticEmbeddedInput(
                    engine,
                    std::make_unique<SilentInput>()),
            "Synthetic embedded setup must register one Mixer input"))
    {
        return false;
    }

    if (!require(AudioOutputRecoveryEngineTestAccess::reconcileOutput(engine),
            "Synthetic embedded setup must start the shared output"))
    {
        return false;
    }

    engine.update(0.0F);
    if (!require(AudioOutputRecoveryEngineTestAccess::hasEmbeddedInput(engine)
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable,
            "Device failure must retain the embedded registration as recovery-presence evidence"))
    {
        return false;
    }

    engine.update(0.5F);
    return require(output.initializeCalls == 2U
            && !AudioOutputRecoveryEngineTestAccess::hasEmbeddedInput(engine)
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::Healthy
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(engine)
                == SharedAudioOutputState::Stopped,
        "Recovery preparation must replace the old embedded registration only after opening output");
}

bool verifyEmbeddedAndBgmRecoveryPreservesBgm()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::DeviceFailure},
        {0U, MixedOutputBridgeStatus::WroteFrames}};
    if (!require(prepareBgmOnlyEngine(engine, project, output)
            && AudioOutputRecoveryEngineTestAccess::addSyntheticEmbeddedInput(
                engine,
                std::make_unique<SilentInput>()),
            "Embedded+BGM setup must register both inputs"))
    {
        return false;
    }
    const auto token = AudioOutputRecoveryEngineTestAccess::playlistToken(engine);

    engine.update(0.0F);
    if (!require(AudioOutputRecoveryEngineTestAccess::hasEmbeddedInput(engine)
            && AudioOutputRecoveryEngineTestAccess::hasActiveBackgroundMusic(engine),
            "Outage entry must retain both embedded and BGM registrations"))
    {
        return false;
    }

    engine.update(0.5F);
    return require(!AudioOutputRecoveryEngineTestAccess::hasEmbeddedInput(engine)
            && AudioOutputRecoveryEngineTestAccess::hasActiveBackgroundMusic(engine)
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token
            && output.initializeCalls == 2U && output.beginCalls == 2U
            && output.pumpCalls == 3U,
        "Recovery must replace only embedded state while preserving the BGM registration and token");
}

bool verifyDefaultEndpointChangeReroutesRunningBgm()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Default-endpoint reroute setup must start BGM"))
    {
        return false;
    }

    const void* const audioOutputAddress = AudioOutputRecoveryEngineTestAccess::audioOutputAddress(engine);
    const auto token = AudioOutputRecoveryEngineTestAccess::playlistToken(engine);
    output.defaultAudioEndpointGeneration = 3U;

    engine.update(0.1F);
    return require(output.shutdownCalls == 1U
            && output.initializeCalls == 2U
            && output.beginCalls == 2U
            && output.pumpCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::audioOutputAddress(engine) == audioOutputAddress
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::Healthy
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(engine)
                == SharedAudioOutputState::Running
            && AudioOutputRecoveryEngineTestAccess::hasActiveBackgroundMusic(engine)
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token,
        "A generation change must suppress the old pump and perform one same-object Running reroute");
}

bool verifyDefaultEndpointChangeReroutesPausedOutputWithoutPull()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Paused reroute setup must start BGM"))
    {
        return false;
    }

    const auto token = AudioOutputRecoveryEngineTestAccess::playlistToken(engine);
    engine.togglePause();
    output.defaultAudioEndpointGeneration = 1U;
    engine.update(1.0F);

    return require(output.shutdownCalls == 1U
            && output.initializeCalls == 2U
            && output.beginCalls == 2U
            && output.pumpCalls == 1U
            && !output.beginModes.empty()
            && output.beginModes.back()
                == DesktopSceneEngine::Platform::Audio::AudioBlockStartMode::ConfigurePaused
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(engine)
                == SharedAudioOutputState::Paused
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token,
        "A paused reroute must reopen ConfigurePaused without a Mixer pull or BGM replacement");
}

bool verifyDefaultEndpointChangeWhileStoppedInvalidatesWithoutReopen()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;

    if (!require(prepareBgmOnlyEngine(engine, project, output)
            && engine.processBackgroundMusicControl(
                DesktopSceneEngine::BackgroundMusicControl::PlaybackStopped{}),
            "Stopped reroute setup must remove the active BGM source"))
    {
        return false;
    }

    output.defaultAudioEndpointGeneration = 1U;
    engine.update(0.1F);

    return require(output.shutdownCalls == 1U
            && output.initializeCalls == 1U
            && output.beginCalls == 1U
            && output.pumpCalls == 1U
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(engine)
                == SharedAudioOutputState::Stopped,
        "A stopped reroute must invalidate the stale endpoint without reopening output");
}

bool verifyDefaultEndpointChangeImmediatelyRetriesDeviceUnavailable()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::DeviceFailure},
        {0U, MixedOutputBridgeStatus::WroteFrames}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Immediate-retry setup must start BGM"))
    {
        return false;
    }

    engine.update(0.0F);
    output.defaultAudioEndpointGeneration = 1U;
    engine.update(0.1F);

    return require(output.shutdownCalls == 1U
            && output.initializeCalls == 2U
            && output.beginCalls == 2U
            && output.pumpCalls == 3U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::Healthy
            && AudioOutputRecoveryEngineTestAccess::retryElapsedSeconds(engine) == 0.0,
        "A new endpoint generation must bypass the unavailable cooldown with one immediate retry");
}

bool verifyDefaultEndpointChangeDoesNotClearContractFault()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;
    output.pumpResults = {
        {0U, MixedOutputBridgeStatus::WroteFrames},
        {0U, MixedOutputBridgeStatus::OutputContractFailure}};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Contract-fault reroute setup must start BGM"))
    {
        return false;
    }

    engine.update(0.0F);
    output.defaultAudioEndpointGeneration = 1U;
    engine.update(1.0F);

    return require(output.shutdownCalls == 0U
            && output.initializeCalls == 1U
            && output.beginCalls == 1U
            && output.pumpCalls == 2U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::ContractFaulted,
        "A route change must be consumed without retrying or clearing ContractFaulted");
}

bool verifyEmbeddedAndBgmDefaultEndpointReroutePreservesBgm()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;

    if (!require(prepareBgmOnlyEngine(engine, project, output)
            && AudioOutputRecoveryEngineTestAccess::configureEmbeddedAudio(
                engine,
                fixturePath().parent_path() / "SequentialAudioSourceCompressed.flac"),
            "Embedded+BGM reroute setup must create the embedded adapter"))
    {
        return false;
    }

    const std::uint64_t oldEmbeddedHandle =
        AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(engine);
    const auto token = AudioOutputRecoveryEngineTestAccess::playlistToken(engine);
    output.defaultAudioEndpointGeneration = 1U;
    engine.update(0.1F);

    return require(oldEmbeddedHandle != 0U
            && AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(engine) > oldEmbeddedHandle
            && AudioOutputRecoveryEngineTestAccess::hasActiveBackgroundMusic(engine)
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token
            && output.shutdownCalls == 1U
            && output.initializeCalls == 2U
            && output.beginCalls == 2U
            && output.pumpCalls == 2U,
        "A reroute must replace embedded input at current playback time while preserving BGM state");
}

bool verifyGenerationArrivingDuringRerouteWaitsForNextUpdate()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;
    output.onBegin = [&output]()
    {
        if (output.beginCalls == 2U)
        {
            output.defaultAudioEndpointGeneration = 2U;
        }
    };

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Generation-during-reroute setup must start BGM"))
    {
        return false;
    }

    output.defaultAudioEndpointGeneration = 1U;
    engine.update(0.1F);
    if (!require(output.initializeCalls == 2U && output.beginCalls == 2U && output.pumpCalls == 2U,
            "A generation raised during reroute must not recurse in the same update"))
    {
        return false;
    }

    engine.update(0.1F);
    return require(output.initializeCalls == 3U && output.beginCalls == 3U && output.pumpCalls == 3U,
        "A generation raised during reroute must remain pending for the next update");
}

bool verifyDefaultEndpointRerouteFailureUsesNormalRecoveryState()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;
    output.initializeResults = {true, false};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Reroute-failure setup must start BGM"))
    {
        return false;
    }

    const auto token = AudioOutputRecoveryEngineTestAccess::playlistToken(engine);
    output.defaultAudioEndpointGeneration = 1U;
    engine.update(0.1F);

    return require(output.shutdownCalls == 1U
            && output.initializeCalls == 2U
            && output.beginCalls == 1U
            && output.pumpCalls == 1U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable
            && AudioOutputRecoveryEngineTestAccess::retryElapsedSeconds(engine) == 0.0
            && AudioOutputRecoveryEngineTestAccess::playlistToken(engine) == token,
        "A reroute initialize failure must enter normal DeviceUnavailable recovery without replacing BGM");
}

bool verifyDefaultEndpointRerouteBeginFailureUsesNormalRecoveryState()
{
    DesktopSceneEngine::Engine engine{};
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.exposeDefaultAudioEndpointGeneration = true;
    output.beginResults = {
        DesktopSceneEngine::Audio::AudioBlockStatus::Success,
        DesktopSceneEngine::Audio::AudioBlockStatus::DeviceFailure};

    if (!require(prepareBgmOnlyEngine(engine, project, output),
            "Reroute-begin-failure setup must start BGM"))
    {
        return false;
    }

    output.defaultAudioEndpointGeneration = 1U;
    engine.update(0.1F);

    return require(output.shutdownCalls == 1U
            && output.initializeCalls == 2U
            && output.beginCalls == 2U
            && output.pumpCalls == 1U
            && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable
            && AudioOutputRecoveryEngineTestAccess::retryElapsedSeconds(engine) == 0.0,
        "A reroute begin device failure must enter DeviceUnavailable without an extra old-output pump");
}

}

int main()
{
    return verifyDeviceFailureDoesNotRecoverBeforeCooldown()
            && verifyFailureDoesNotConsumePreFailureUpdateDelta()
            && verifyInitialAndBeginDeviceFailuresUseRecovery()
            && verifyFailedRecoveryResetsCooldown()
            && verifyStopCancelsUnavailableRecovery()
            && verifyContractFaultDoesNotRetry()
            && verifyNonDeviceBridgeStatusDoesNotRetryAndConfigRemainsLive()
            && verifyPausedAndResumedRecovery()
            && verifyEmbeddedPresenceSurvivesOutageUntilRecoveryPreparation()
            && verifyEmbeddedAndBgmRecoveryPreservesBgm()
            && verifyDefaultEndpointChangeReroutesRunningBgm()
            && verifyDefaultEndpointChangeReroutesPausedOutputWithoutPull()
            && verifyDefaultEndpointChangeWhileStoppedInvalidatesWithoutReopen()
            && verifyDefaultEndpointChangeImmediatelyRetriesDeviceUnavailable()
            && verifyDefaultEndpointChangeDoesNotClearContractFault()
            && verifyEmbeddedAndBgmDefaultEndpointReroutePreservesBgm()
            && verifyGenerationArrivingDuringRerouteWaitsForNextUpdate()
            && verifyDefaultEndpointRerouteFailureUsesNormalRecoveryState()
            && verifyDefaultEndpointRerouteBeginFailureUsesNormalRecoveryState()
        ? 0 : 1;
}
