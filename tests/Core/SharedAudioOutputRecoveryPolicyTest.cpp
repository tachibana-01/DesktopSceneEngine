#include "Core/SharedAudioOutputRecoveryPolicy.h"

#include <chrono>
#include <iostream>

namespace
{

using DesktopSceneEngine::Core::AudioOutputOperationalState;
using DesktopSceneEngine::Core::SharedAudioOutputRecoveryAction;
using DesktopSceneEngine::Core::SharedAudioOutputRecoveryCooldown;
using DesktopSceneEngine::Core::SharedAudioOutputState;
using DesktopSceneEngine::Core::resolveSharedAudioOutputRecoveryAction;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyHealthyAndStoppedStates()
{
    return require(
               resolveSharedAudioOutputRecoveryAction(
                   AudioOutputOperationalState::Healthy,
                   SharedAudioOutputState::Running,
                   std::chrono::hours{1})
                   == SharedAudioOutputRecoveryAction::None,
               "Healthy running output must not recover")
        && require(
            resolveSharedAudioOutputRecoveryAction(
                AudioOutputOperationalState::Healthy,
                SharedAudioOutputState::Paused,
                std::chrono::hours{1})
                == SharedAudioOutputRecoveryAction::None,
            "Healthy paused output must not recover")
        && require(
            resolveSharedAudioOutputRecoveryAction(
                AudioOutputOperationalState::DeviceUnavailable,
                SharedAudioOutputState::Stopped,
                std::chrono::hours{1})
                == SharedAudioOutputRecoveryAction::CancelRecovery,
            "Stopped output must cancel a pending device recovery");
}

bool verifyCooldownAndRetryMode()
{
    return require(
               SharedAudioOutputRecoveryCooldown == std::chrono::milliseconds{500},
               "Recovery cooldown must remain fixed at 500 milliseconds")
        && require(
            resolveSharedAudioOutputRecoveryAction(
                AudioOutputOperationalState::DeviceUnavailable,
                SharedAudioOutputState::Running,
                std::chrono::milliseconds{499})
                == SharedAudioOutputRecoveryAction::None,
            "Device recovery must not retry before the exact cooldown boundary")
        && require(
            resolveSharedAudioOutputRecoveryAction(
                AudioOutputOperationalState::DeviceUnavailable,
                SharedAudioOutputState::Running,
                std::chrono::milliseconds{500})
                == SharedAudioOutputRecoveryAction::RetryRunning,
            "Device recovery must retry running output at the exact cooldown boundary")
        && require(
            resolveSharedAudioOutputRecoveryAction(
                AudioOutputOperationalState::DeviceUnavailable,
                SharedAudioOutputState::Running,
                std::chrono::milliseconds{501})
                == SharedAudioOutputRecoveryAction::RetryRunning,
            "One policy evaluation may request only the current due retry")
        && require(
            resolveSharedAudioOutputRecoveryAction(
                AudioOutputOperationalState::DeviceUnavailable,
                SharedAudioOutputState::Paused,
                std::chrono::milliseconds{500})
                == SharedAudioOutputRecoveryAction::RetryPaused,
            "Paused output recovery must preserve the paused desired state");
}

bool verifyFaultIsolationAndRepeatedFailureModel()
{
    const auto firstRetry = resolveSharedAudioOutputRecoveryAction(
        AudioOutputOperationalState::DeviceUnavailable,
        SharedAudioOutputState::Running,
        std::chrono::milliseconds{500});
    const auto retryAfterExternalReset = resolveSharedAudioOutputRecoveryAction(
        AudioOutputOperationalState::DeviceUnavailable,
        SharedAudioOutputState::Running,
        std::chrono::milliseconds{499});
    const auto nextRetry = resolveSharedAudioOutputRecoveryAction(
        AudioOutputOperationalState::DeviceUnavailable,
        SharedAudioOutputState::Running,
        std::chrono::milliseconds{500});

    return require(
               firstRetry == SharedAudioOutputRecoveryAction::RetryRunning,
               "A due device failure must request its first retry")
        && require(
            retryAfterExternalReset == SharedAudioOutputRecoveryAction::None,
            "A failed retry reset must prevent an immediate retry loop")
        && require(
            nextRetry == SharedAudioOutputRecoveryAction::RetryRunning,
            "A later elapsed cooldown must permit the next single retry")
        && require(
            resolveSharedAudioOutputRecoveryAction(
                AudioOutputOperationalState::ContractFaulted,
                SharedAudioOutputState::Running,
                std::chrono::hours{24})
                == SharedAudioOutputRecoveryAction::None,
            "Contract faults must never enter timed device recovery")
        && require(
            resolveSharedAudioOutputRecoveryAction(
                AudioOutputOperationalState::ContractFaulted,
                SharedAudioOutputState::Paused,
                std::chrono::hours{24})
                == SharedAudioOutputRecoveryAction::None,
            "Contract faults must not retry paused output either");
}

}

int main()
{
    const bool success = verifyHealthyAndStoppedStates()
        && verifyCooldownAndRetryMode()
        && verifyFaultIsolationAndRepeatedFailureModel();
    return success ? 0 : 1;
}
