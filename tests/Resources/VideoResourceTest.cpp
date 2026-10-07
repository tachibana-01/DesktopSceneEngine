#include "Resources/VideoResource.h"

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

unsigned long long checksum(const DesktopSceneEngine::Resources::DecodedFrame& frame)
{
    const auto* pixels = static_cast<const unsigned char*>(frame.data);
    const std::size_t rowBytes = static_cast<std::size_t>(frame.width)
        * static_cast<std::size_t>(frame.channels);
    const std::size_t rowStride = frame.rowStride == 0 ? rowBytes : frame.rowStride;
    unsigned long long result = 1469598103934665603ULL;
    for (int row = 0; row < frame.height; ++row)
    {
        const auto* rowPixels = pixels + static_cast<std::size_t>(row) * rowStride;
        for (std::size_t column = 0; column < rowBytes; ++column)
        {
            result ^= rowPixels[column];
            result *= 1099511628211ULL;
        }
    }
    return result;
}

bool verifyVideo(
    const std::filesystem::path& path
)
{
    DesktopSceneEngine::Resources::VideoResource resource{};
    if (!require(!resource.frameAt(-1.0F), "Negative timestamps should be rejected"))
    {
        return false;
    }

    if (!require(resource.open(path), "VideoResource should open the supplied video"))
    {
        return false;
    }

    if (!require(
            std::isfinite(resource.duration()) && resource.duration() > 0.0F,
            "Video duration should be finite and positive"))
    {
        return false;
    }

    if (!require(resource.frameAt(0.0F), "Frame at zero should decode"))
    {
        return false;
    }

    const auto afterFirst = resource.diagnostics();
    if (!require(afterFirst.seekCalls == 0U,
            "Initial frame should use the sequential decoder path"))
    {
        return false;
    }

    const auto first = resource.currentFrame();
    if (!require(first.data != nullptr, "Decoded frame should contain data")
        || !require(first.width > 0, "Decoded frame width should be positive")
        || !require(first.height > 0, "Decoded frame height should be positive")
        || !require(first.channels == 4, "Decoded frame should be RGBA8")
        || !require(std::isfinite(first.timestampSeconds)
                && first.timestampSeconds >= 0.0,
            "Decoded frame timestamp should be finite"))
    {
        return false;
    }

    const auto* firstPixels = static_cast<const unsigned char*>(first.data);
    (void)firstPixels;
    const unsigned long long firstChecksum = checksum(first);

    if (!require(resource.frameAt(0.1F),
            "A nearby forward frame should decode sequentially"))
    {
        return false;
    }
    const auto afterForward = resource.diagnostics();
    if (!require(afterForward.seekCalls == afterFirst.seekCalls,
            "Nearby forward playback must not seek every frame")
        || !require(afterForward.framesReturned > afterFirst.framesReturned,
            "Forward playback should return a new frame"))
    {
        return false;
    }

    const auto forwardFrame = resource.currentFrame();
    if (!require(std::isfinite(forwardFrame.timestampSeconds)
            && forwardFrame.timestampSeconds >= first.timestampSeconds,
        "Sequential frame timestamps should not move backward"))
    {
        return false;
    }

    const auto beforeReuse = resource.diagnostics();
    if (!require(resource.frameAt(0.1F),
            "Repeated timestamp should reuse the current frame"))
    {
        return false;
    }
    const auto afterReuse = resource.diagnostics();
    if (!require(afterReuse.seekCalls == beforeReuse.seekCalls,
            "Repeated timestamp must not seek")
        || !require(afterReuse.decodedFrames == beforeReuse.decodedFrames,
            "Repeated timestamp must not decode")
        || !require(afterReuse.scaleCalls == beforeReuse.scaleCalls,
            "Repeated timestamp must not scale"))
    {
        return false;
    }

    const float laterTime = std::min(1.0F, resource.duration() * 0.5F);
    if (!require(resource.frameAt(laterTime), "A later frame should decode"))
    {
        return false;
    }

    const auto later = resource.currentFrame();
    if (!require(later.data != nullptr, "Later frame should contain data"))
    {
        return false;
    }
    if (!require(std::isfinite(later.timestampSeconds)
            && later.timestampSeconds >= 0.0,
        "Later frame timestamp should be finite"))
    {
        return false;
    }

    const unsigned long long laterChecksum = checksum(later);

    if (!require(
            firstChecksum != laterChecksum,
            "Different timestamps should normally produce different frames"))
    {
        return false;
    }

    const auto beforeLargeJump = resource.diagnostics();
    if (!require(resource.frameAt(5.0F),
            "A large forward jump should remain seekable"))
    {
        return false;
    }
    const auto afterLargeJump = resource.diagnostics();
    if (!require(afterLargeJump.seekCalls > beforeLargeJump.seekCalls,
            "A large forward jump should use random access"))
    {
        return false;
    }

    const auto beforeBackward = resource.diagnostics();
    if (!require(resource.frameAt(2.0F),
            "A backward request should seek and decode"))
    {
        return false;
    }
    const auto afterBackward = resource.diagnostics();
    if (!require(afterBackward.seekCalls > beforeBackward.seekCalls,
            "A backward request should use random access"))
    {
        return false;
    }

    const auto beforeForwardAfterSeek = resource.diagnostics();
    if (!require(resource.frameAt(2.1F),
            "Forward playback should resume after a backward seek"))
    {
        return false;
    }
    if (!require(resource.diagnostics().seekCalls
            == beforeForwardAfterSeek.seekCalls,
        "Forward playback after a seek should remain sequential"))
    {
        return false;
    }

    if (!require(
            resource.frameAt(0.0F),
            "Backward timestamp requests should decode"))
    {
        return false;
    }
    if (!require(checksum(resource.currentFrame()) == firstChecksum,
            "Repeated timestamp requests should decode consistently"))
    {
        return false;
    }

    const auto beforeEof = resource.diagnostics();
    const float beyondEnd = resource.duration() + 10.0F;
    if (!require(!resource.frameAt(beyondEnd),
            "Requests beyond EOF should fail safely"))
    {
        return false;
    }
    const auto afterEof = resource.diagnostics();
    if (!require(!resource.frameAt(beyondEnd),
            "Repeated EOF requests should fail without looping forever"))
    {
        return false;
    }
    if (!require(resource.diagnostics().seekCalls == afterEof.seekCalls,
            "Repeated EOF requests should not repeat the same seek"))
    {
        return false;
    }

    if (!require(afterEof.seekCalls >= beforeEof.seekCalls,
            "EOF diagnostics should remain monotonic"))
    {
        return false;
    }

    const auto beforeEofRecovery = resource.diagnostics();
    if (!require(resource.frameAt(2.0F),
            "A backward request after EOF should re-enter random access"))
    {
        return false;
    }
    if (!require(resource.diagnostics().seekCalls > beforeEofRecovery.seekCalls,
            "EOF recovery should perform a new seek"))
    {
        return false;
    }

    return true;
}

}

int main(int argc, char* argv[])
{
    DesktopSceneEngine::Resources::VideoResource invalid{};
    if (!require(
            !invalid.open(std::filesystem::path{"missing-video-file.mp4"}),
            "Invalid video paths should be rejected"))
    {
        return 1;
    }

    if (argc < 2)
    {
        std::cout
            << "No real video path supplied; invalid-path validation passed."
            << std::endl;
        return 0;
    }

    return verifyVideo(std::filesystem::path{argv[1]}) ? 0 : 1;
}
