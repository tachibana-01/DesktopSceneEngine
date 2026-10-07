#pragma once

#include <cstdint>

namespace DesktopSceneEngine::Core
{

enum class PlaybackActivity : std::uint8_t
{
    Stopped,
    Playing,
    Paused
};

enum class SharedAudioOutputState : std::uint8_t
{
    Stopped,
    Running,
    Paused
};

enum class SharedAudioOutputCommand : std::uint8_t
{
    None,
    BeginRunning,
    ConfigurePaused,
    Pause,
    Resume,
    Stop
};

struct SharedAudioOutputTransition
{
    SharedAudioOutputState desiredState = SharedAudioOutputState::Stopped;
    SharedAudioOutputCommand command = SharedAudioOutputCommand::None;
    bool shouldPrime = false;
};

[[nodiscard]] constexpr SharedAudioOutputTransition resolveSharedAudioOutputTransition(
    PlaybackActivity activity,
    bool hasAudioInputPresence,
    SharedAudioOutputState currentState
) noexcept
{
    const SharedAudioOutputState desiredState = !hasAudioInputPresence
            || activity == PlaybackActivity::Stopped
        ? SharedAudioOutputState::Stopped
        : activity == PlaybackActivity::Paused
            ? SharedAudioOutputState::Paused
            : SharedAudioOutputState::Running;

    if (desiredState == currentState)
    {
        return {desiredState, SharedAudioOutputCommand::None, false};
    }

    switch (desiredState)
    {
    case SharedAudioOutputState::Stopped:
        return {desiredState, SharedAudioOutputCommand::Stop, false};

    case SharedAudioOutputState::Running:
        return currentState == SharedAudioOutputState::Paused
            ? SharedAudioOutputTransition{
                desiredState,
                SharedAudioOutputCommand::Resume,
                true}
            : SharedAudioOutputTransition{
                desiredState,
                SharedAudioOutputCommand::BeginRunning,
                true};

    case SharedAudioOutputState::Paused:
        return currentState == SharedAudioOutputState::Running
            ? SharedAudioOutputTransition{
                desiredState,
                SharedAudioOutputCommand::Pause,
                false}
            : SharedAudioOutputTransition{
                desiredState,
                SharedAudioOutputCommand::ConfigurePaused,
                false};
    }

    return {SharedAudioOutputState::Stopped, SharedAudioOutputCommand::Stop, false};
}

}
