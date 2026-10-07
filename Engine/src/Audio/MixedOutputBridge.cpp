#include "MixedOutputBridge.h"

#include "AudioSampleConversion.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace DesktopSceneEngine::Audio
{

namespace
{

constexpr std::size_t StereoChannelCount = 2U;

MixedOutputBridgeResult mapWriteStatus(
    AudioBlockStatus writeStatus,
    std::size_t frameCount,
    MixedOutputBridgeStatus successStatus
) noexcept
{
    if (writeStatus == AudioBlockStatus::Success)
    {
        return {frameCount, successStatus};
    }

    if (writeStatus == AudioBlockStatus::DeviceFailure)
    {
        return {0U, MixedOutputBridgeStatus::DeviceFailure};
    }

    return {0U, MixedOutputBridgeStatus::OutputContractFailure};
}

}

MixedOutputBridge::MixedOutputBridge(
    AudioMixer& mixer,
    AudioBlockSink& sink
) noexcept
    : mixer_(mixer),
      sink_(sink)
{
}

MixedOutputBridgeResult MixedOutputBridge::pump() noexcept
{
    const AudioBlockAvailability availability = sink_.writableFrames();
    switch (availability.status)
    {
    case AudioBlockStatus::Paused:
    case AudioBlockStatus::WrongMode:
    case AudioBlockStatus::NotInitialized:
        return {0U, MixedOutputBridgeStatus::OutputInactive};
    case AudioBlockStatus::DeviceFailure:
        return {0U, MixedOutputBridgeStatus::DeviceFailure};
    case AudioBlockStatus::Success:
        break;
    case AudioBlockStatus::InvalidRequest:
    case AudioBlockStatus::NotWritable:
        return {0U, MixedOutputBridgeStatus::OutputContractFailure};
    }

    if (availability.writableFrames == 0U)
    {
        return {0U, MixedOutputBridgeStatus::NoWritableFrames};
    }

    if (!prepareBuffers(availability.writableFrames))
    {
        return {0U, MixedOutputBridgeStatus::ResourceFailure};
    }

    const AudioMixResult mixerResult = mixer_.mixFrames(mixedF32_);
    return submitMixerResult(mixerResult, availability.writableFrames);
}

bool MixedOutputBridge::prepareBuffers(std::size_t frameCount) noexcept
{
    if (frameCount > (std::numeric_limits<std::size_t>::max)()
            / StereoChannelCount)
    {
        return false;
    }

    const std::size_t sampleCount = frameCount * StereoChannelCount;
    try
    {
        mixedF32_.resize(sampleCount);
        deviceS16_.resize(sampleCount);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }

    return true;
}

MixedOutputBridgeResult MixedOutputBridge::submitMixerResult(
    const AudioMixResult& mixerResult,
    std::size_t frameCount
) noexcept
{
    if (frameCount > (std::numeric_limits<std::size_t>::max)()
            / StereoChannelCount)
    {
        return {0U, MixedOutputBridgeStatus::ResourceFailure};
    }

    const std::size_t sampleCount = frameCount * StereoChannelCount;
    if (mixedF32_.size() != sampleCount || deviceS16_.size() != sampleCount)
    {
        return {0U, MixedOutputBridgeStatus::ResourceFailure};
    }

    MixedOutputBridgeStatus resultStatus = MixedOutputBridgeStatus::WroteFrames;
    if (mixerResult.status == AudioMixStatus::ResourceFailure)
    {
        std::fill(deviceS16_.begin(), deviceS16_.end(), 0);
        resultStatus = MixedOutputBridgeStatus::MixerResourceFailureSilenced;
    }
    else if (mixerResult.status != AudioMixStatus::Success
        || mixerResult.framesProduced != frameCount
        || !convertF32SamplesToS16(mixedF32_, deviceS16_))
    {
        std::fill(deviceS16_.begin(), deviceS16_.end(), 0);
        resultStatus = MixedOutputBridgeStatus::MixerContractFailureSilenced;
    }

    return mapWriteStatus(
        sink_.writeFrames(deviceS16_),
        frameCount,
        resultStatus);
}

MixedOutputBridgeResult MixedOutputBridgeTestAccess::submitMixerResult(
    MixedOutputBridge& bridge,
    const AudioMixResult& mixerResult,
    std::size_t frameCount
) noexcept
{
    if (!bridge.prepareBuffers(frameCount))
    {
        return {0U, MixedOutputBridgeStatus::ResourceFailure};
    }

    return bridge.submitMixerResult(mixerResult, frameCount);
}

}
