#pragma once

#include "Audio/MixedOutputBridge.h"
#include "Platform/Audio/AudioOutput.h"

#include <cstdint>
#include <functional>

namespace DesktopSceneEngine::Core
{

// Internal deterministic seam for Engine recovery tests. Production leaves
// every callable empty and uses the concrete AudioOutput and bridge.
struct AudioOutputRecoveryTestHooks
{
    std::function<bool()> initialize{};
    std::function<void()> shutdown{};
    std::function<Audio::AudioBlockStatus(Platform::Audio::AudioBlockStartMode)> beginBlockMode{};
    std::function<Audio::AudioBlockStatus()> pause{};
    std::function<Audio::AudioBlockStatus()> resume{};
    std::function<Audio::MixedOutputBridgeResult()> pump{};
    std::function<std::uint64_t()> defaultAudioEndpointGeneration{};
};

}
