#pragma once

#include "DecodedFrame.h"
#include "MediaResource.h"

#include <atomic>
#include <filesystem>
#include <cstdint>
#include <memory>
#include <vector>

namespace DesktopSceneEngine::Resources
{

struct VideoDecodeDiagnostics
{
    std::uint64_t frameAtCalls = 0;
    std::uint64_t seekCalls = 0;
    std::uint64_t flushCalls = 0;
    std::uint64_t packetsRead = 0;
    std::uint64_t decodedFrames = 0;
    std::uint64_t scaleCalls = 0;
    std::uint64_t framesReturned = 0;
    std::uint64_t reusedFrames = 0;
    std::uint64_t cancellationChecks = 0;
    std::uint64_t cancelledRequests = 0;
};

// Internal decode controls used by visual-only preview consumers. Normal Engine
// playback continues to call the single-argument frameAt() overload.
struct VideoFrameDecodeOptions
{
    int maximumOutputWidth = 0;
    int maximumOutputHeight = 0;
    const std::atomic_bool* cancellationRequested = nullptr;
};

enum class VideoFrameDecodeStatus
{
    Ready,
    Cancelled,
    Unavailable
};

class VideoResource final : public MediaResource
{
public:

    VideoResource();
    ~VideoResource() override;

    VideoResource(const VideoResource&) = delete;
    VideoResource& operator=(const VideoResource&) = delete;
    VideoResource(VideoResource&&) noexcept;
    VideoResource& operator=(VideoResource&&) noexcept;

    [[nodiscard]] MediaType type() const noexcept override;

    [[nodiscard]] bool frameAt(float time) noexcept;

    [[nodiscard]] VideoFrameDecodeStatus frameAt(
        float time,
        const VideoFrameDecodeOptions& options
    ) noexcept;

    [[nodiscard]] bool open(const std::filesystem::path& path) noexcept;

    [[nodiscard]] bool nextFrame(float time) noexcept;

    [[nodiscard]] const DecodedFrame& currentFrame() const noexcept;

    [[nodiscard]] bool verifyBufferGuard(const char* phase) const noexcept;

    [[nodiscard]] VideoDecodeDiagnostics diagnostics() const noexcept;

    [[nodiscard]] std::uint64_t cancellationCheckCount() const noexcept;

    [[nodiscard]] float duration() const noexcept;

private:

    struct Impl;
    std::unique_ptr<Impl> impl_{};
    DecodedFrame frame_{};
};

}
