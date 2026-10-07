#include "Resources/AudioResource.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

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

bool verifyAudio(const std::filesystem::path& path)
{
    DesktopSceneEngine::Resources::AudioResource resource{};
    if (!require(resource.open(path), "AudioResource should open audio media"))
    {
        return false;
    }

    if (!require(resource.type()
            == DesktopSceneEngine::Resources::MediaType::Audio,
            "AudioResource should report the Audio media type")
        || !require(resource.data() != nullptr, "PCM data should be present")
        || !require(resource.sampleCount() > 0U, "PCM should contain samples")
        || !require(resource.frameCount() == resource.sampleCount() / 2U,
            "PCM frame count should match stereo samples")
        || !require(std::isfinite(resource.durationSeconds())
                && resource.durationSeconds() > 0.0,
            "PCM duration should be finite and positive")
        || !require(std::fabs(
                resource.durationSeconds()
                    - static_cast<double>(resource.frameCount())
                        / static_cast<double>(resource.sampleRate()))
                < 0.000001,
            "PCM duration should map frames to media time")
        || !require(resource.byteSize() == resource.sampleCount() * sizeof(std::int16_t),
            "PCM byte size should match sample count")
        || !require(resource.channels() == 2, "PCM should be stereo")
        || !require(resource.sampleRate() == 48000, "PCM should be 48 kHz"))
    {
        return false;
    }

    const double mediaDuration = resource.durationSeconds();
    if (!require(resource.setTempo(0.5F),
            "AudioResource should build 0.5x tempo PCM")
        || !require(std::fabs(
                resource.outputDurationSeconds() - mediaDuration * 2.0)
                <= (std::max)(0.05, mediaDuration * 0.08),
            "0.5x output duration should be approximately double media duration")
        || !require(resource.data() != nullptr,
            "0.5x tempo PCM should remain available")
        || !require(resource.setTempo(2.0F),
            "AudioResource should build 2.0x tempo PCM")
        || !require(std::fabs(
                resource.outputDurationSeconds() - mediaDuration / 2.0)
                <= (std::max)(0.05, mediaDuration * 0.08),
            "2.0x output duration should be approximately half media duration")
        || !require(resource.setTempo(1.0F),
            "AudioResource should restore 1.0x tempo PCM")
        || !require(std::fabs(
                resource.outputDurationSeconds() - mediaDuration)
                <= (std::max)(0.05, mediaDuration * 0.02),
            "1.0x output duration should match media duration"))
    {
        return false;
    }

    return true;
}

}

int main(int argc, char* argv[])
{
    DesktopSceneEngine::Resources::AudioResource invalid{};
    if (!require(
            !invalid.open(std::filesystem::path{"missing-audio-file.mp3"}),
            "Invalid audio paths should be rejected"))
    {
        return 1;
    }

    if (argc < 2)
    {
        std::cout
            << "No real audio path supplied; invalid-path validation passed."
            << std::endl;
        return 0;
    }

    return verifyAudio(std::filesystem::path{argv[1]}) ? 0 : 1;
}
