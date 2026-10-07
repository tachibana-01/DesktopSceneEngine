#include <DesktopSceneEngine/Resources/PreviewVideoDecoder.h>

#include "VideoResource.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace DesktopSceneEngine::Resources
{

namespace
{

bool hasValidRgbaStorage(const DecodedFrame& frame) noexcept
{
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0
        || frame.channels != 4)
    {
        return false;
    }

    const std::size_t width = static_cast<std::size_t>(frame.width);
    const std::size_t height = static_cast<std::size_t>(frame.height);
    if (width > (std::numeric_limits<std::size_t>::max)() / 4U)
    {
        return false;
    }

    const std::size_t minimumRowStride = width * 4U;
    if (frame.rowStride < minimumRowStride
        || frame.rowStride > (std::numeric_limits<std::size_t>::max)() / height)
    {
        return false;
    }

    return frame.byteSize >= frame.rowStride * height;
}

}

struct PreviewVideoDecoder::Impl
{
    VideoResource video{};
    bool open = false;
};

PreviewVideoDecoder::PreviewVideoDecoder() = default;

PreviewVideoDecoder::~PreviewVideoDecoder() = default;

PreviewVideoDecoder::PreviewVideoDecoder(PreviewVideoDecoder&&) noexcept = default;

PreviewVideoDecoder& PreviewVideoDecoder::operator=(PreviewVideoDecoder&&) noexcept = default;

bool PreviewVideoDecoder::open(const std::filesystem::path& path) noexcept
{
    if (path.empty())
    {
        return false;
    }

    try
    {
        auto candidate = std::make_unique<Impl>();
        if (!candidate->video.open(path))
        {
            return false;
        }

        candidate->open = true;
        impl_ = std::move(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}

std::optional<PreviewFrame> PreviewVideoDecoder::frameAt(float timeSeconds) noexcept
{
    constexpr PreviewDecodeOptions DefaultPreviewSize{320, 320};
    PreviewDecodeResult result = frameAt(
        timeSeconds,
        DefaultPreviewSize,
        nullptr);
    if (result.status != PreviewDecodeStatus::Ready)
    {
        return std::nullopt;
    }

    return std::move(result.frame);
}

PreviewDecodeResult PreviewVideoDecoder::frameAt(
    float timeSeconds,
    const PreviewDecodeOptions& options,
    const std::atomic_bool* cancellationRequested
) noexcept
{
    PreviewDecodeResult result{};
    if (!impl_ || !impl_->open || !std::isfinite(timeSeconds)
        || timeSeconds < 0.0F)
    {
        return result;
    }

    const VideoFrameDecodeStatus decodeStatus = impl_->video.frameAt(
        timeSeconds,
        {options.maximumWidth, options.maximumHeight, cancellationRequested});
    if (decodeStatus == VideoFrameDecodeStatus::Cancelled)
    {
        result.status = PreviewDecodeStatus::Cancelled;
        return result;
    }
    if (decodeStatus != VideoFrameDecodeStatus::Ready)
    {
        return result;
    }

    if (cancellationRequested != nullptr
        && cancellationRequested->load(std::memory_order_acquire))
    {
        result.status = PreviewDecodeStatus::Cancelled;
        return result;
    }

    const DecodedFrame& decoded = impl_->video.currentFrame();
    if (!hasValidRgbaStorage(decoded))
    {
        return result;
    }

    const std::size_t byteCount = decoded.rowStride
        * static_cast<std::size_t>(decoded.height);
    try
    {
        result.frame.rgba.resize(byteCount);
        std::memcpy(result.frame.rgba.data(), decoded.data, byteCount);
        result.frame.width = decoded.width;
        result.frame.height = decoded.height;
        result.frame.rowStride = decoded.rowStride;
        result.frame.timestampSeconds = decoded.timestampSeconds;
        result.frame.hasTimestamp = decoded.hasTimestamp;
        result.status = PreviewDecodeStatus::Ready;
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return result;
    }
}

void PreviewVideoDecoder::clear() noexcept
{
    impl_.reset();
}

bool PreviewVideoDecoder::isOpen() const noexcept
{
    return impl_ && impl_->open;
}

float PreviewVideoDecoder::duration() const noexcept
{
    return impl_ && impl_->open ? impl_->video.duration() : 0.0F;
}

PreviewDecodeDiagnostics PreviewVideoDecoder::diagnostics() const noexcept
{
    if (!impl_ || !impl_->open)
    {
        return {};
    }

    const VideoDecodeDiagnostics diagnostics = impl_->video.diagnostics();
    return {
        diagnostics.decodedFrames,
        diagnostics.cancellationChecks,
        diagnostics.cancelledRequests};
}

std::uint64_t PreviewVideoDecoder::cancellationCheckCount() const noexcept
{
    return impl_ && impl_->open ? impl_->video.cancellationCheckCount() : 0U;
}

}
