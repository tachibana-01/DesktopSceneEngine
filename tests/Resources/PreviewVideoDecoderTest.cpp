#include <DesktopSceneEngine/Resources/PreviewVideoDecoder.h>

#include "Resources/VideoResource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

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

std::vector<std::filesystem::path> findVideoFixtures()
{
    const std::filesystem::path root{DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    std::vector<std::filesystem::path> videoPaths{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root})
    {
        if (entry.is_regular_file() && entry.path().extension() == ".mp4")
        {
            videoPaths.push_back(entry.path());
        }
    }

    return videoPaths;
}

unsigned long long checksum(const DesktopSceneEngine::Resources::PreviewFrame& frame)
{
    unsigned long long result = 1469598103934665603ULL;
    for (const std::uint8_t pixel : frame.rgba)
    {
        result ^= pixel;
        result *= 1099511628211ULL;
    }
    return result;
}

bool verifyPreviewSizingAndCancellation(const std::filesystem::path& videoPath)
{
    using DesktopSceneEngine::Resources::PreviewDecodeOptions;
    using DesktopSceneEngine::Resources::PreviewDecodeStatus;
    using DesktopSceneEngine::Resources::PreviewVideoDecoder;

    PreviewVideoDecoder preview{};
    DesktopSceneEngine::Resources::VideoResource source{};
    if (!require(preview.open(videoPath), "Preview decoder should open the fixture")
        || !require(source.open(videoPath), "Source decoder should open the fixture")
        || !require(source.frameAt(0.0F), "Source decoder should decode the first frame"))
    {
        return false;
    }

    constexpr PreviewDecodeOptions PreviewSize{320, 320};
    const auto sized = preview.frameAt(0.0F, PreviewSize, nullptr);
    if (!require(sized.status == PreviewDecodeStatus::Ready,
            "A bounded preview request should decode a frame")
        || !require(sized.frame.width > 0 && sized.frame.height > 0
                && sized.frame.width <= PreviewSize.maximumWidth
                && sized.frame.height <= PreviewSize.maximumHeight,
            "Preview output must be bounded by the requested dimensions"))
    {
        return false;
    }

    const auto& sourceFrame = source.currentFrame();
    if (!require(std::fabs(
            static_cast<double>(sized.frame.width) / static_cast<double>(sized.frame.height)
            - static_cast<double>(sourceFrame.width) / static_cast<double>(sourceFrame.height))
            <= 0.01,
            "Preview output must preserve the decoded source aspect ratio"))
    {
        return false;
    }

    std::atomic_bool cancelled{false};
    const float longDecodeTarget = (std::min)(4.0F, preview.duration() * 0.5F);
    const auto beforeCancellation = preview.diagnostics();
    auto future = std::async(
        std::launch::async,
        [&preview, longDecodeTarget, &cancelled, PreviewSize]()
        {
            return preview.frameAt(longDecodeTarget, PreviewSize, &cancelled);
        });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    bool observedMultipleChecks = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (future.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready)
        {
            break;
        }

        if (preview.cancellationCheckCount() >= 8U)
        {
            observedMultipleChecks = true;
            cancelled.store(true, std::memory_order_release);
            break;
        }

        std::this_thread::yield();
    }

    if (!observedMultipleChecks)
    {
        cancelled.store(true, std::memory_order_release);
    }
    const auto abandoned = future.get();
    const auto afterCancellation = preview.diagnostics();

    PreviewVideoDecoder completed{};
    if (!require(completed.open(videoPath), "Comparison decoder should open the fixture")
        || !require(completed.frameAt(longDecodeTarget, PreviewSize, nullptr).status
                == PreviewDecodeStatus::Ready,
            "Comparison decoder should complete the same random target"))
    {
        return false;
    }

    const std::uint64_t cancelledDecodedFrames = afterCancellation.decodedFrames
        - beforeCancellation.decodedFrames;
    cancelled.store(false, std::memory_order_release);
    const float recoveryForwardTime = (std::min)(
        preview.duration() * 0.9F,
        longDecodeTarget + 0.5F);
    const auto forwardRecovery = preview.frameAt(
        recoveryForwardTime,
        PreviewSize,
        nullptr);
    const float recoveryBackwardTime = (std::max)(
        0.0F,
        recoveryForwardTime * 0.25F);
    const auto backwardRecovery = preview.frameAt(
        recoveryBackwardTime,
        PreviewSize,
        nullptr);
    return require(observedMultipleChecks,
               "The cancellation test must observe several packet/frame-loop checkpoints")
        && require(abandoned.status == PreviewDecodeStatus::Cancelled,
            "An obsolete in-flight preview request must report cancellation, not unavailability")
        && require(afterCancellation.cancelledRequests
                == beforeCancellation.cancelledRequests + 1U,
            "The cancelled preview request should be counted exactly once")
        && require(cancelledDecodedFrames < completed.diagnostics().decodedFrames,
            "Cancellation must stop before decoding the full obsolete target path")
        && require(forwardRecovery.status == PreviewDecodeStatus::Ready
                && !forwardRecovery.frame.rgba.empty()
                && (!forwardRecovery.frame.hasTimestamp
                    || forwardRecovery.frame.timestampSeconds
                        >= static_cast<double>(recoveryForwardTime) - 0.1),
            "The cancelled decoder must recover on a later request")
        && require(backwardRecovery.status == PreviewDecodeStatus::Ready
                && !backwardRecovery.frame.rgba.empty()
                && (!backwardRecovery.frame.hasTimestamp
                    || backwardRecovery.frame.timestampSeconds
                        < forwardRecovery.frame.timestampSeconds),
            "The same decoder must recover on a backward random request")
        && require(afterCancellation.cancelledRequests
                == beforeCancellation.cancelledRequests + 1U,
            "Cancellation must not report a completed stale frame");
}

bool verifyPreviewSizingForFixture(const std::filesystem::path& videoPath)
{
    using DesktopSceneEngine::Resources::PreviewDecodeOptions;
    using DesktopSceneEngine::Resources::PreviewDecodeStatus;
    using DesktopSceneEngine::Resources::PreviewVideoDecoder;
    using DesktopSceneEngine::Resources::VideoResource;

    constexpr PreviewDecodeOptions PreviewSize{320, 320};
    PreviewVideoDecoder preview{};
    VideoResource source{};
    if (!require(preview.open(videoPath), "Preview decoder should open every video fixture")
        || !require(source.open(videoPath), "Source decoder should open every video fixture")
        || !require(source.frameAt(0.0F), "Source decoder should decode every first frame"))
    {
        return false;
    }

    const auto result = preview.frameAt(0.0F, PreviewSize, nullptr);
    const auto& sourceFrame = source.currentFrame();
    return require(result.status == PreviewDecodeStatus::Ready,
               "Every video fixture should produce a bounded preview frame")
        && require(result.frame.width > 0 && result.frame.height > 0
                && result.frame.width <= PreviewSize.maximumWidth
                && result.frame.height <= PreviewSize.maximumHeight,
            "Every preview frame must remain within the configured output bounds")
        && require(std::fabs(
            static_cast<double>(result.frame.width) / static_cast<double>(result.frame.height)
            - static_cast<double>(sourceFrame.width) / static_cast<double>(sourceFrame.height))
            <= 0.01,
            "Every preview frame must preserve its source aspect ratio");
}

bool verifyIndependentDecoder(const std::filesystem::path& videoPath)
{
    using DesktopSceneEngine::Resources::PreviewVideoDecoder;
    using DesktopSceneEngine::Resources::VideoResource;

    VideoResource playback{};
    PreviewVideoDecoder preview{};
    if (!require(playback.open(videoPath), "Playback decoder should open the fixture")
        || !require(preview.open(videoPath), "Preview decoder should open the same fixture")
        || !require(playback.frameAt(0.0F), "Playback decoder should decode its first frame"))
    {
        return false;
    }

    const auto playbackBefore = playback.diagnostics();
    const float randomTime = std::min(3.0F, preview.duration() * 0.5F);
    const auto firstPreview = preview.frameAt(0.0F);
    const auto randomPreview = preview.frameAt(randomTime);
    if (!require(firstPreview.has_value() && randomPreview.has_value(),
            "Preview decoder should return owned frames for independent requests"))
    {
        return false;
    }

    const unsigned long long firstChecksum = checksum(*firstPreview);
    if (!require(preview.frameAt(0.1F).has_value(),
            "A second preview decode should succeed")
        || !require(checksum(*firstPreview) == firstChecksum,
            "Previously returned preview frames must remain owned and unchanged")
        || !require(playback.frameAt(0.1F),
            "Playback decoder should continue sequentially after preview seeks"))
    {
        return false;
    }

    const auto playbackAfter = playback.diagnostics();
    return require(playbackAfter.seekCalls == playbackBefore.seekCalls,
        "Preview random access must not alter playback decoder seek state");
}

bool verifyConcurrentIndependentDecoders(const std::filesystem::path& videoPath)
{
    using DesktopSceneEngine::Resources::PreviewDecodeOptions;
    using DesktopSceneEngine::Resources::PreviewDecodeStatus;
    using DesktopSceneEngine::Resources::PreviewVideoDecoder;

    constexpr PreviewDecodeOptions PreviewSize{320, 320};
    PreviewVideoDecoder first{};
    PreviewVideoDecoder second{};
    if (!require(first.open(videoPath) && second.open(videoPath),
            "Independent concurrent preview decoders should open the fixture"))
    {
        return false;
    }

    std::atomic_bool start{false};
    const auto decodeSequence = [&start, PreviewSize](PreviewVideoDecoder& decoder)
    {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        constexpr std::array<float, 8> Times{
            0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F};
        for (const float time : Times)
        {
            if (decoder.frameAt(time, PreviewSize, nullptr).status
                != PreviewDecodeStatus::Ready)
            {
                return false;
            }
        }

        return true;
    };

    auto firstFuture = std::async(std::launch::async, decodeSequence, std::ref(first));
    auto secondFuture = std::async(std::launch::async, decodeSequence, std::ref(second));
    start.store(true, std::memory_order_release);
    return require(firstFuture.get() && secondFuture.get(),
        "Concurrent independent preview decoders should remain usable");
}

}

int main()
{
    const std::vector<std::filesystem::path> videoPaths = findVideoFixtures();
    if (!require(!videoPaths.empty(), "At least one video fixture must be available"))
    {
        return 1;
    }

    for (const std::filesystem::path& videoPath : videoPaths)
    {
        if (!verifyPreviewSizingForFixture(videoPath))
        {
            return 1;
        }
    }

    return verifyIndependentDecoder(videoPaths.front())
            && verifyPreviewSizingAndCancellation(videoPaths.front())
            && verifyConcurrentIndependentDecoders(videoPaths.front())
        ? 0
        : 1;
}
