#include "Core/SharedAudioOutputPolicy.h"

#include <iostream>

namespace
{

using DesktopSceneEngine::Core::PlaybackActivity;
using DesktopSceneEngine::Core::SharedAudioOutputCommand;
using DesktopSceneEngine::Core::SharedAudioOutputState;
using DesktopSceneEngine::Core::resolveSharedAudioOutputTransition;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyDesiredStateTable()
{
    const auto stoppedWithInput = resolveSharedAudioOutputTransition(
        PlaybackActivity::Stopped,
        true,
        SharedAudioOutputState::Running);
    const auto playingWithoutInput = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        false,
        SharedAudioOutputState::Running);
    const auto playingWithInput = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        true,
        SharedAudioOutputState::Stopped);
    const auto pausedWithoutInput = resolveSharedAudioOutputTransition(
        PlaybackActivity::Paused,
        false,
        SharedAudioOutputState::Running);
    const auto pausedWithInput = resolveSharedAudioOutputTransition(
        PlaybackActivity::Paused,
        true,
        SharedAudioOutputState::Stopped);

    return require(
               stoppedWithInput.desiredState == SharedAudioOutputState::Stopped
                   && stoppedWithInput.command == SharedAudioOutputCommand::Stop,
               "Stopped playback must stop shared output even when inputs remain")
        && require(
            playingWithoutInput.desiredState == SharedAudioOutputState::Stopped
                && playingWithoutInput.command == SharedAudioOutputCommand::Stop,
            "Playing without inputs must stop shared output")
        && require(
            playingWithInput.desiredState == SharedAudioOutputState::Running
                && playingWithInput.command == SharedAudioOutputCommand::BeginRunning
                && playingWithInput.shouldPrime,
            "Playing with inputs must begin shared output and request one prime")
        && require(
            pausedWithoutInput.desiredState == SharedAudioOutputState::Stopped
                && pausedWithoutInput.command == SharedAudioOutputCommand::Stop,
            "Paused playback without inputs must stop shared output")
        && require(
            pausedWithInput.desiredState == SharedAudioOutputState::Paused
                && pausedWithInput.command == SharedAudioOutputCommand::ConfigurePaused
                && !pausedWithInput.shouldPrime,
            "Paused playback with inputs must configure output without a prime");
}

bool verifyBgmOnlyAndResumeActivation()
{
    const auto playlistBgmOnly = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        true,
        SharedAudioOutputState::Stopped);
    const auto itemBgmOnly = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        true,
        SharedAudioOutputState::Stopped);
    const auto resume = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        true,
        SharedAudioOutputState::Paused);
    const auto terminalSuccessor = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        true,
        SharedAudioOutputState::Running);

    return require(
               playlistBgmOnly.command == SharedAudioOutputCommand::BeginRunning
                   && playlistBgmOnly.shouldPrime,
               "Playlist BGM alone must activate and prime shared output")
        && require(
            itemBgmOnly.command == SharedAudioOutputCommand::BeginRunning
                && itemBgmOnly.shouldPrime,
            "Item BGM alone must activate and prime shared output")
        && require(
            resume.command == SharedAudioOutputCommand::Resume && resume.shouldPrime,
            "Paused shared output must resume with one prime")
        && require(
            terminalSuccessor.command == SharedAudioOutputCommand::None
                && !terminalSuccessor.shouldPrime,
            "An already-running output must not prime for a successor source");
}

bool verifyNoThrashAndPausedRemoval()
{
    const auto runningSourceReplacement = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        true,
        SharedAudioOutputState::Running);
    const auto pause = resolveSharedAudioOutputTransition(
        PlaybackActivity::Paused,
        true,
        SharedAudioOutputState::Running);
    const auto pausedLastSourceRemoval = resolveSharedAudioOutputTransition(
        PlaybackActivity::Paused,
        false,
        SharedAudioOutputState::Paused);
    const auto mutedSource = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        true,
        SharedAudioOutputState::Running);
    const auto zeroGainSource = resolveSharedAudioOutputTransition(
        PlaybackActivity::Playing,
        true,
        SharedAudioOutputState::Running);

    return require(
               runningSourceReplacement.command == SharedAudioOutputCommand::None
                   && !runningSourceReplacement.shouldPrime,
               "Replacing a source while output is running must not churn or prime it")
        && require(
            pause.command == SharedAudioOutputCommand::Pause && !pause.shouldPrime,
            "Paused playback with inputs must pause shared output")
        && require(
            pausedLastSourceRemoval.command == SharedAudioOutputCommand::Stop,
            "Removing the last paused input must stop shared output")
        && require(
            mutedSource.command == SharedAudioOutputCommand::None,
            "Muted sources must remain present for shared-output policy")
        && require(
            zeroGainSource.command == SharedAudioOutputCommand::None,
            "Zero-gain sources must remain present for shared-output policy");
}

}

int main()
{
    return verifyDesiredStateTable()
            && verifyBgmOnlyAndResumeActivation()
            && verifyNoThrashAndPausedRemoval()
        ? 0
        : 1;
}
