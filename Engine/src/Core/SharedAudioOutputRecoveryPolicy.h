#pragma once

#include "SharedAudioOutputPolicy.h"

#include <chrono>
#include <cstdint>

namespace DesktopSceneEngine::Core
{

enum class AudioOutputOperationalState : std::uint8_t
{
    Healthy,
    DeviceUnavailable,
    ContractFaulted
};

enum class SharedAudioOutputRecoveryAction : std::uint8_t
{
    None,
    CancelRecovery,
    RetryRunning,
    RetryPaused
};

inline constexpr std::chrono::milliseconds SharedAudioOutputRecoveryCooldown{500};

[[nodiscard]] constexpr SharedAudioOutputRecoveryAction
resolveSharedAudioOutputRecoveryAction(
    AudioOutputOperationalState operationalState,
    SharedAudioOutputState desiredState,
    std::chrono::milliseconds retryElapsed
) noexcept
{
    if (desiredState == SharedAudioOutputState::Stopped)
    {
        return SharedAudioOutputRecoveryAction::CancelRecovery;
    }

    if (operationalState != AudioOutputOperationalState::DeviceUnavailable
        || retryElapsed < SharedAudioOutputRecoveryCooldown)
    {
        return SharedAudioOutputRecoveryAction::None;
    }

    return desiredState == SharedAudioOutputState::Running
        ? SharedAudioOutputRecoveryAction::RetryRunning
        : SharedAudioOutputRecoveryAction::RetryPaused;
}

}
