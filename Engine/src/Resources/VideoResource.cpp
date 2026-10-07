#include "VideoResource.h"

#include <atomic>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace DesktopSceneEngine::Resources
{

namespace
{

struct FormatCloser
{
    void operator()(AVFormatContext* context) const noexcept
    {
        if (context != nullptr)
        {
            avformat_close_input(&context);
        }
    }
};

struct CodecCloser
{
    void operator()(AVCodecContext* context) const noexcept
    {
        avcodec_free_context(&context);
    }
};

struct PacketCloser
{
    void operator()(AVPacket* packet) const noexcept
    {
        av_packet_free(&packet);
    }
};

struct FrameCloser
{
    void operator()(AVFrame* frame) const noexcept
    {
        av_frame_free(&frame);
    }
};

struct ScaleCloser
{
    void operator()(SwsContext* context) const noexcept
    {
        sws_freeContext(context);
    }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecCloser>;
using PacketPtr = std::unique_ptr<AVPacket, PacketCloser>;
using FramePtr = std::unique_ptr<AVFrame, FrameCloser>;
using ScalePtr = std::unique_ptr<SwsContext, ScaleCloser>;

constexpr std::size_t DiagnosticGuardSize = 64U * 1024U;
constexpr unsigned char DiagnosticGuardValue = 0xA5U;
constexpr unsigned char DiagnosticPaddingValue = 0xCCU;
constexpr std::size_t DiagnosticPaddingLimit = 4096U;

std::atomic_uint64_t nextResourceGeneration{0U};

struct VideoDiagnostics
{
    std::atomic_uint64_t frameAtCalls{0U};
    std::atomic_uint64_t seekCalls{0U};
    std::atomic_uint64_t flushCalls{0U};
    std::atomic_uint64_t packetsRead{0U};
    std::atomic_uint64_t decodedFrames{0U};
    std::atomic_uint64_t scaleCalls{0U};
    std::atomic_uint64_t framesReturned{0U};
    std::atomic_uint64_t reusedFrames{0U};
};

// These aggregate counters only drive diagnostic logging. Individual relaxed
// snapshots may describe different decode operations, which is acceptable for
// diagnostics and avoids synchronizing independent VideoResource instances.
VideoDiagnostics globalDiagnostics{};

std::uint64_t diagnosticValue(const std::atomic_uint64_t& counter) noexcept
{
    return counter.load(std::memory_order_relaxed);
}

std::uint64_t incrementDiagnosticValue(std::atomic_uint64_t& counter) noexcept
{
    return counter.fetch_add(1U, std::memory_order_relaxed) + 1U;
}

std::size_t readDiagnosticSize(
    const char* name,
    std::size_t defaultValue,
    std::size_t maximum) noexcept
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return defaultValue;
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0' || parsed > maximum)
    {
        return defaultValue;
    }

    return static_cast<std::size_t>(parsed);
}

std::size_t readDiagnosticAlignment() noexcept
{
    const std::size_t value = readDiagnosticSize(
        "DSE_VIDEO_ALIGNMENT",
        64U,
        64U);
    return value == 16U || value == 32U || value == 64U ? value : 1U;
}

struct RegionInspection
{
    bool intact = true;
    std::size_t first = 0;
    std::size_t last = 0;
    std::size_t changed = 0;
    std::size_t ranges = 0;
    std::size_t longestRange = 0;
    std::size_t currentRange = 0;
    std::array<unsigned char, 64> firstPattern{};
    std::size_t patternSize = 0;
};

RegionInspection inspectRegion(
    const unsigned char* data,
    std::size_t size,
    unsigned char expected) noexcept
{
    RegionInspection result{};
    bool inRange = false;
    for (std::size_t offset = 0; offset < size; ++offset)
    {
        const unsigned char actual = data[offset];
        if (actual == expected)
        {
            if (inRange)
            {
                result.longestRange = (std::max)(
                    result.longestRange,
                    result.currentRange);
                result.currentRange = 0;
                inRange = false;
            }
            continue;
        }

        result.intact = false;
        if (result.changed == 0)
        {
            result.first = offset;
        }
        result.last = offset;
        ++result.changed;
        if (result.patternSize < result.firstPattern.size())
        {
            result.firstPattern[result.patternSize++] = actual;
        }
        if (!inRange)
        {
            ++result.ranges;
            result.currentRange = 0;
            inRange = true;
        }
        ++result.currentRange;
    }

    if (inRange)
    {
        result.longestRange = (std::max)(
            result.longestRange,
            result.currentRange);
    }
    return result;
}

void printRegionInspection(
    const char* label,
    const RegionInspection& inspection) noexcept
{
    std::fprintf(
        stderr,
        "%s intact=%s first=%zu last=%zu changed=%zu ranges=%zu longest=%zu pattern=",
        label,
        inspection.intact ? "yes" : "no",
        inspection.intact ? 0U : inspection.first,
        inspection.intact ? 0U : inspection.last,
        inspection.changed,
        inspection.ranges,
        inspection.longestRange);
    for (std::size_t i = 0; i < inspection.patternSize; ++i)
    {
        std::fprintf(
            stderr,
            "%s%02X",
            i == 0 ? "" : " ",
            static_cast<unsigned int>(inspection.firstPattern[i]));
    }
    std::fputc('\n', stderr);
}

bool shouldLog(std::uint64_t count) noexcept
{
    return count == 1 || count % 25 == 0;
}

void logFrameProperties(
    const char* stage,
    const AVFrame* frame,
    int width,
    int height,
    AVPixelFormat pixelFormat) noexcept
{
    if (frame == nullptr)
    {
        return;
    }

    std::fprintf(
        stderr,
        "[VideoResource] %s frame=%llu pts=%lld best_pts=%lld decoded=%dx%d format=%d configured=%dx%d configured_format=%d linesize=[%d,%d,%d,%d]\n",
        stage,
        static_cast<unsigned long long>(
            diagnosticValue(globalDiagnostics.decodedFrames)),
        static_cast<long long>(frame->pts),
        static_cast<long long>(frame->best_effort_timestamp),
        frame->width,
        frame->height,
        frame->format,
        width,
        height,
        static_cast<int>(pixelFormat),
        frame->linesize[0],
        frame->linesize[1],
        frame->linesize[2],
        frame->linesize[3]);
    std::fflush(stderr);
}

bool determineOutputDimensions(
    int sourceWidth,
    int sourceHeight,
    const VideoFrameDecodeOptions& options,
    int& outputWidth,
    int& outputHeight
) noexcept
{
    if (sourceWidth <= 0 || sourceHeight <= 0
        || options.maximumOutputWidth < 0 || options.maximumOutputHeight < 0)
    {
        return false;
    }

    if (options.maximumOutputWidth == 0 && options.maximumOutputHeight == 0)
    {
        outputWidth = sourceWidth;
        outputHeight = sourceHeight;
        return true;
    }

    const int maximumWidth = options.maximumOutputWidth > 0
        ? options.maximumOutputWidth
        : sourceWidth;
    const int maximumHeight = options.maximumOutputHeight > 0
        ? options.maximumOutputHeight
        : sourceHeight;
    const double scale = (std::min)(
        1.0,
        (std::min)(
            static_cast<double>(maximumWidth) / static_cast<double>(sourceWidth),
            static_cast<double>(maximumHeight) / static_cast<double>(sourceHeight)));
    if (!std::isfinite(scale) || scale <= 0.0)
    {
        return false;
    }

    const double scaledWidth = static_cast<double>(sourceWidth) * scale;
    const double scaledHeight = static_cast<double>(sourceHeight) * scale;
    if (scaledWidth > static_cast<double>((std::numeric_limits<int>::max)())
        || scaledHeight > static_cast<double>((std::numeric_limits<int>::max)()))
    {
        return false;
    }

    outputWidth = (std::max)(1, static_cast<int>(std::lround(scaledWidth)));
    outputHeight = (std::max)(1, static_cast<int>(std::lround(scaledHeight)));
    return outputWidth <= maximumWidth && outputHeight <= maximumHeight;
}

}

struct VideoResource::Impl
{
    Impl()
        : resourceGeneration(nextResourceGeneration.fetch_add(
              1U,
              std::memory_order_relaxed) + 1U),
          writablePaddingSize(readDiagnosticSize(
              "DSE_VIDEO_PADDING_BYTES",
              0U,
              DiagnosticPaddingLimit)),
          destinationAlignment(readDiagnosticAlignment()),
          packet(av_packet_alloc()),
          decodedFrame(av_frame_alloc())
    {
    }

    FormatPtr format{};
    CodecPtr codec{};
    PacketPtr packet{};
    FramePtr decodedFrame{};
    ScalePtr scaler{};
    std::vector<unsigned char> pixels{};
    std::string sourcePath{};
    std::uint64_t resourceGeneration = 0;
    std::uint64_t bufferGeneration = 0;
    std::size_t logicalPixelSize = 0;
    std::size_t writablePaddingSize = 0;
    std::size_t allocatedDiagnosticSize = 0;
    std::size_t backingStorageSize = 0;
    std::size_t guardSize = DiagnosticGuardSize;
    std::size_t destinationAlignment = 1;
    int videoStreamIndex = -1;
    AVRational timeBase{0, 1};
    int width = 0;
    int height = 0;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    float mediaDuration = 0.0F;
    int scalerSourceWidth = 0;
    int scalerSourceHeight = 0;
    AVPixelFormat scalerSourceFormat = AV_PIX_FMT_NONE;
    int scalerDestinationWidth = 0;
    int scalerDestinationHeight = 0;
    AVPixelFormat scalerDestinationFormat = AV_PIX_FMT_RGBA;
    float lastRequestedTime = 0.0F;
    int64_t lastSeekTimestamp = 0;
    int lastDecodedWidth = 0;
    int lastDecodedHeight = 0;
    AVPixelFormat lastDecodedFormat = AV_PIX_FMT_NONE;
    int lastDecodedLinesize[4]{};
    int64_t lastFramePts = AV_NOPTS_VALUE;
    int64_t lastBestEffortTimestamp = AV_NOPTS_VALUE;
    std::size_t lastDestinationRowStride = 0;
    int lastScaledRows = 0;
    VideoDecodeDiagnostics decodeDiagnostics{};
    VideoFrameDecodeOptions activeDecodeOptions{};
    std::atomic_uint64_t cancellationCheckCount{0U};
    bool cancellationObserved = false;
    bool sequentialStateValid = false;
    bool inputEof = false;
    bool decoderDrainSent = false;
    bool packetPending = false;
    bool eofReached = false;
    bool hasReturnedFrame = false;
    bool forceSeekOnNextRequest = false;
    double lastDecodedTimestampSeconds = -1.0;
    double lastReturnedTimestampSeconds = -1.0;
};

VideoResource::VideoResource()
    : impl_(std::make_unique<Impl>())
{
}

VideoResource::~VideoResource()
{
    (void)verifyBufferGuard("before-destruction");
    std::fprintf(
        stderr,
        "[VideoResource] destroy impl=%p frame_data=%p\n",
        static_cast<void*>(impl_.get()),
        frame_.data);
    std::fflush(stderr);
}

bool VideoResource::verifyBufferGuard(const char* phase) const noexcept
{
    if (!impl_ || impl_->logicalPixelSize == 0)
    {
        return true;
    }

    const std::size_t paddingAndGuard = impl_->writablePaddingSize
        + impl_->guardSize;
    const bool sizeValid = impl_->logicalPixelSize <= impl_->backingStorageSize
        && paddingAndGuard <= impl_->backingStorageSize - impl_->logicalPixelSize
        && impl_->allocatedDiagnosticSize == impl_->logicalPixelSize
            + impl_->writablePaddingSize
        && impl_->pixels.size() >= impl_->backingStorageSize
        && impl_->pixels.data() != nullptr;
    if (!sizeValid)
    {
        std::fprintf(
            stderr,
            "[VideoResource] GUARD INVALID phase=%s resource-generation=%llu buffer-generation=%llu path=%s logical=%zu padding=%zu guard=%zu backing=%zu vector-size=%zu capacity=%zu\n",
            phase != nullptr ? phase : "unknown",
            static_cast<unsigned long long>(impl_->resourceGeneration),
            static_cast<unsigned long long>(impl_->bufferGeneration),
            impl_->sourcePath.c_str(),
            impl_->logicalPixelSize,
            impl_->writablePaddingSize,
            impl_->guardSize,
            impl_->backingStorageSize,
            impl_->pixels.size(),
            impl_->pixels.capacity());
        std::fflush(stderr);
        return false;
    }

    const unsigned char* const paddingBegin =
        impl_->pixels.data() + impl_->logicalPixelSize;
    const unsigned char* const guardBegin =
        paddingBegin + impl_->writablePaddingSize;
    const RegionInspection padding = inspectRegion(
        paddingBegin,
        impl_->writablePaddingSize,
        DiagnosticPaddingValue);
    const RegionInspection guard = inspectRegion(
        guardBegin,
        impl_->guardSize,
        DiagnosticGuardValue);

    if (padding.intact && guard.intact)
    {
        return true;
    }

    std::fprintf(
        stderr,
        "[VideoResource] %s phase=%s resource-generation=%llu buffer-generation=%llu path=%s requested-time=%.6f seek=%lld frameAt=%llu decoded=%llu sws=%llu logical=%zu padding=%zu guard=%zu backing=%zu ptr=%p padding-start=%p guard-start=%p decoded=%dx%d format=%d linesize=[%d,%d,%d,%d] scaler-source=%dx%d/%d scaler-destination=%dx%d/%d destination-stride=%zu scaled-rows=%d pts=%lld best-pts=%lld\n",
        guard.intact ? "PADDING CHANGE" : "GUARD CORRUPTION",
        phase != nullptr ? phase : "unknown",
        static_cast<unsigned long long>(impl_->resourceGeneration),
        static_cast<unsigned long long>(impl_->bufferGeneration),
        impl_->sourcePath.c_str(),
        static_cast<double>(impl_->lastRequestedTime),
        static_cast<long long>(impl_->lastSeekTimestamp),
        static_cast<unsigned long long>(
            diagnosticValue(globalDiagnostics.frameAtCalls)),
        static_cast<unsigned long long>(
            diagnosticValue(globalDiagnostics.decodedFrames)),
        static_cast<unsigned long long>(
            diagnosticValue(globalDiagnostics.scaleCalls)),
        impl_->logicalPixelSize,
        impl_->writablePaddingSize,
        impl_->guardSize,
        impl_->backingStorageSize,
        static_cast<const void*>(impl_->pixels.data()),
        static_cast<const void*>(paddingBegin),
        static_cast<const void*>(guardBegin),
        impl_->lastDecodedWidth,
        impl_->lastDecodedHeight,
        static_cast<int>(impl_->lastDecodedFormat),
        impl_->lastDecodedLinesize[0],
        impl_->lastDecodedLinesize[1],
        impl_->lastDecodedLinesize[2],
        impl_->lastDecodedLinesize[3],
        impl_->scalerSourceWidth,
        impl_->scalerSourceHeight,
        static_cast<int>(impl_->scalerSourceFormat),
        impl_->scalerDestinationWidth,
        impl_->scalerDestinationHeight,
        static_cast<int>(impl_->scalerDestinationFormat),
        impl_->lastDestinationRowStride,
        impl_->lastScaledRows,
        static_cast<long long>(impl_->lastFramePts),
        static_cast<long long>(impl_->lastBestEffortTimestamp));
    printRegionInspection("[VideoResource] padding", padding);
    printRegionInspection("[VideoResource] guard", guard);
    if (padding.intact && !guard.intact)
    {
        std::fprintf(
            stderr,
            "[VideoResource] tail-write escaped padding: maximum-bytes-beyond-logical=%zu\n",
            impl_->writablePaddingSize + guard.last + 1U);
    }
    else if (!padding.intact && guard.intact)
    {
        std::fprintf(
            stderr,
            "[VideoResource] tail-write contained by padding: maximum-bytes-beyond-logical=%zu\n",
            padding.last + 1U);
    }
    std::fflush(stderr);
    return guard.intact;
}

VideoResource::VideoResource(VideoResource&&) noexcept = default;

VideoResource& VideoResource::operator=(VideoResource&&) noexcept = default;

MediaType VideoResource::type() const noexcept
{
    return MediaType::Video;
}

bool VideoResource::nextFrame(float time) noexcept
{
    return frameAt(time);
}

bool VideoResource::open(const std::filesystem::path& path) noexcept
{
    if (!impl_ || path.empty())
    {
        return false;
    }

    std::unique_ptr<Impl> candidate{};
    try
    {
        candidate = std::make_unique<Impl>();
    }
    catch (...)
    {
        return false;
    }

    if (!candidate->packet || !candidate->decodedFrame)
    {
        return false;
    }

    const std::u8string utf8Path = path.u8string();
    const std::string ffmpegPath{
        reinterpret_cast<const char*>(utf8Path.data()),
        utf8Path.size()
    };
    candidate->sourcePath = ffmpegPath;

    AVFormatContext* rawFormat = nullptr;
    if (avformat_open_input(&rawFormat, ffmpegPath.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }
    candidate->format.reset(rawFormat);

    if (avformat_find_stream_info(candidate->format.get(), nullptr) < 0)
    {
        return false;
    }

    const AVCodec* decoder = nullptr;
    candidate->videoStreamIndex = av_find_best_stream(
        candidate->format.get(),
        AVMEDIA_TYPE_VIDEO,
        -1,
        -1,
        &decoder,
        0);
    if (candidate->videoStreamIndex < 0 || decoder == nullptr)
    {
        return false;
    }

    const AVStream* stream = candidate->format->streams[candidate->videoStreamIndex];
    candidate->codec.reset(avcodec_alloc_context3(decoder));
    if (!candidate->codec
        || avcodec_parameters_to_context(candidate->codec.get(), stream->codecpar) < 0
        || avcodec_open2(candidate->codec.get(), decoder, nullptr) < 0)
    {
        return false;
    }

    candidate->timeBase = stream->time_base;
    candidate->width = candidate->codec->width;
    candidate->height = candidate->codec->height;
    if (candidate->timeBase.num <= 0 || candidate->timeBase.den <= 0
        || candidate->width <= 0 || candidate->height <= 0)
    {
        return false;
    }

    const int64_t duration = stream->duration > 0
        ? stream->duration
        : candidate->format->duration;
    if (duration <= 0)
    {
        return false;
    }

    const AVRational durationBase = stream->duration > 0
        ? stream->time_base
        : AVRational{1, AV_TIME_BASE};
    const double mediaDuration = duration * av_q2d(durationBase);
    if (!std::isfinite(mediaDuration)
        || mediaDuration <= 0.0
        || mediaDuration > static_cast<double>(
            (std::numeric_limits<float>::max)()))
    {
        return false;
    }
    candidate->mediaDuration = static_cast<float>(mediaDuration);

    std::fprintf(
        stderr,
        "[VideoResource] open path=%s container=%s codec=%s codec_id=%d streams=%u video_stream=%d size=%dx%d pixel_format=%d duration=%.6f time_base=%d/%d\n",
        ffmpegPath.c_str(),
        candidate->format->iformat != nullptr
            ? candidate->format->iformat->name : "unknown",
        decoder->name != nullptr ? decoder->name : "unknown",
        static_cast<int>(decoder->id),
        candidate->format->nb_streams,
        candidate->videoStreamIndex,
        candidate->width,
        candidate->height,
        candidate->codec->pix_fmt,
        static_cast<double>(candidate->mediaDuration),
        candidate->timeBase.num,
        candidate->timeBase.den);
    std::fflush(stderr);

    impl_ = std::move(candidate);
    frame_ = DecodedFrame{};
    return true;
}

bool VideoResource::frameAt(float time) noexcept
{
    const std::uint64_t frameAtCall = incrementDiagnosticValue(
        globalDiagnostics.frameAtCalls);
    if (impl_ != nullptr)
    {
        ++impl_->decodeDiagnostics.frameAtCalls;
    }

    if (shouldLog(frameAtCall))
    {
        std::fprintf(
            stderr,
            "[VideoResource] frameAt=%llu time=%.6f\n",
            static_cast<unsigned long long>(frameAtCall),
            static_cast<double>(time));
        std::fflush(stderr);
    }

    if (!impl_ || !impl_->format || !impl_->codec || !impl_->packet
        || !impl_->decodedFrame || !std::isfinite(time) || time < 0.0F)
    {
        return false;
    }

    impl_->cancellationObserved = false;
    impl_->cancellationCheckCount.store(0U, std::memory_order_relaxed);
    const auto cancellationRequested = [this]() noexcept
    {
        const std::atomic_bool* const cancellation = impl_->activeDecodeOptions
            .cancellationRequested;
        if (cancellation == nullptr)
        {
            return false;
        }

        impl_->cancellationCheckCount.fetch_add(1U, std::memory_order_relaxed);
        ++impl_->decodeDiagnostics.cancellationChecks;
        if (!cancellation->load(std::memory_order_acquire))
        {
            return false;
        }

        if (!impl_->cancellationObserved)
        {
            impl_->cancellationObserved = true;
            ++impl_->decodeDiagnostics.cancelledRequests;
            av_packet_unref(impl_->packet.get());
            impl_->packetPending = false;
            impl_->sequentialStateValid = false;
            impl_->inputEof = false;
            impl_->decoderDrainSent = false;
            impl_->eofReached = false;
            impl_->hasReturnedFrame = false;
            impl_->forceSeekOnNextRequest = true;
            frame_ = DecodedFrame{};
        }
        return true;
    };

    if (cancellationRequested())
    {
        return false;
    }

    if (!verifyBufferGuard("before-frameAt"))
    {
        return false;
    }

    constexpr double TimestampEpsilon = 0.000001;
    constexpr float SequentialSeekThresholdSeconds = 2.0F;

    const double timestampMicros = static_cast<double>(time)
        * static_cast<double>(AV_TIME_BASE);
    if (!std::isfinite(timestampMicros)
        || timestampMicros < 0.0
        || timestampMicros > static_cast<double>(
            (std::numeric_limits<int64_t>::max)()))
    {
        return false;
    }

    const int64_t targetTimestamp = av_rescale_q(
        static_cast<int64_t>(timestampMicros),
        AVRational{1, AV_TIME_BASE},
        impl_->timeBase);

    if (impl_->hasReturnedFrame && frame_.data != nullptr)
    {
        const double currentFrameTime = frame_.hasTimestamp
            && std::isfinite(frame_.timestampSeconds)
            && frame_.timestampSeconds >= 0.0
            ? frame_.timestampSeconds
            : impl_->lastReturnedTimestampSeconds;
        if (std::isfinite(currentFrameTime)
            && static_cast<double>(time)
                >= static_cast<double>(impl_->lastRequestedTime)
                    - TimestampEpsilon
            && static_cast<double>(time) <= currentFrameTime + TimestampEpsilon)
        {
            (void)incrementDiagnosticValue(globalDiagnostics.reusedFrames);
            ++impl_->decodeDiagnostics.reusedFrames;
            return true;
        }
    }

    if (impl_->eofReached
        && std::fabs(static_cast<double>(time)
            - static_cast<double>(impl_->lastRequestedTime))
            <= TimestampEpsilon)
    {
        return false;
    }

    bool requiresSeek = false;
    const char* seekReason = nullptr;
    if (impl_->forceSeekOnNextRequest)
    {
        requiresSeek = true;
        seekReason = "cancelled-request";
    }
    else if (impl_->sequentialStateValid)
    {
        if (static_cast<double>(time)
                < static_cast<double>(impl_->lastRequestedTime)
                    - TimestampEpsilon)
        {
            requiresSeek = true;
            seekReason = "backward";
        }
        else if (static_cast<double>(time)
                - static_cast<double>(impl_->lastRequestedTime)
                > static_cast<double>(SequentialSeekThresholdSeconds))
        {
            requiresSeek = true;
            seekReason = "large-forward";
        }
    }
    else if (time > SequentialSeekThresholdSeconds)
    {
        requiresSeek = true;
        seekReason = "initial-large-forward";
    }

    if (impl_->eofReached
        && static_cast<double>(time)
            < static_cast<double>(impl_->lastRequestedTime)
                - TimestampEpsilon)
    {
        requiresSeek = true;
        seekReason = "backward-after-eof";
    }

    if (impl_->eofReached && !requiresSeek)
    {
        return false;
    }

    if (requiresSeek)
    {
        impl_->lastSeekTimestamp = targetTimestamp;
        (void)incrementDiagnosticValue(globalDiagnostics.seekCalls);
        ++impl_->decodeDiagnostics.seekCalls;
        if (av_seek_frame(
                impl_->format.get(),
                impl_->videoStreamIndex,
                targetTimestamp,
            AVSEEK_FLAG_BACKWARD) < 0)
        {
            impl_->lastRequestedTime = time;
            impl_->sequentialStateValid = false;
            impl_->eofReached = true;
            return false;
        }

        avcodec_flush_buffers(impl_->codec.get());
        (void)incrementDiagnosticValue(globalDiagnostics.flushCalls);
        ++impl_->decodeDiagnostics.flushCalls;
        av_packet_unref(impl_->packet.get());
        impl_->packetPending = false;
        impl_->sequentialStateValid = true;
        impl_->inputEof = false;
        impl_->decoderDrainSent = false;
        impl_->eofReached = false;
        impl_->hasReturnedFrame = false;
        impl_->forceSeekOnNextRequest = false;
        impl_->lastDecodedTimestampSeconds = -1.0;
        impl_->lastReturnedTimestampSeconds = -1.0;

        std::fprintf(
            stderr,
            "[VideoResource] random-access reason=%s requested=%.6f seek=%lld\n",
            seekReason != nullptr ? seekReason : "unknown",
            static_cast<double>(time),
            static_cast<long long>(targetTimestamp));
        std::fflush(stderr);
    }
    else
    {
        impl_->sequentialStateValid = true;
    }

    auto convertCurrentFrame = [&](int64_t frameTimestamp) noexcept -> bool
    {
        if (impl_->decodedFrame->width <= 0
            || impl_->decodedFrame->height <= 0
            || impl_->decodedFrame->format < 0)
        {
            std::fprintf(
                stderr,
                "[VideoResource] invalid decoded frame properties width=%d height=%d format=%d\n",
                impl_->decodedFrame->width,
                impl_->decodedFrame->height,
                impl_->decodedFrame->format);
            std::fflush(stderr);
            return false;
        }

        int outputWidth = 0;
        int outputHeight = 0;
        if (!determineOutputDimensions(
                impl_->decodedFrame->width,
                impl_->decodedFrame->height,
                impl_->activeDecodeOptions,
                outputWidth,
                outputHeight))
        {
            return false;
        }

        if (impl_->scaler == nullptr
            || impl_->scalerSourceWidth != impl_->decodedFrame->width
            || impl_->scalerSourceHeight != impl_->decodedFrame->height
            || impl_->scalerSourceFormat != impl_->decodedFrame->format
            || impl_->scalerDestinationWidth != outputWidth
            || impl_->scalerDestinationHeight != outputHeight)
        {
            impl_->scaler.reset(sws_getContext(
                impl_->decodedFrame->width,
                impl_->decodedFrame->height,
                static_cast<AVPixelFormat>(impl_->decodedFrame->format),
                outputWidth,
                outputHeight,
                AV_PIX_FMT_RGBA,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr));
            impl_->width = outputWidth;
            impl_->height = outputHeight;
            impl_->pixelFormat = static_cast<AVPixelFormat>(
                impl_->decodedFrame->format);
            impl_->scalerSourceWidth = impl_->decodedFrame->width;
            impl_->scalerSourceHeight = impl_->decodedFrame->height;
            impl_->scalerSourceFormat = static_cast<AVPixelFormat>(
                impl_->decodedFrame->format);
            impl_->scalerDestinationWidth = outputWidth;
            impl_->scalerDestinationHeight = outputHeight;
            impl_->scalerDestinationFormat = AV_PIX_FMT_RGBA;
            if (!impl_->scaler)
            {
                return false;
            }
        }

        if (impl_->decodedFrame->width != impl_->scalerSourceWidth
            || impl_->decodedFrame->height != impl_->scalerSourceHeight
            || impl_->decodedFrame->format != impl_->scalerSourceFormat)
        {
            std::fprintf(
                stderr,
                "[VideoResource] scaler source mismatch decoded=%dx%d/%d configured=%dx%d/%d\n",
                impl_->decodedFrame->width,
                impl_->decodedFrame->height,
                impl_->decodedFrame->format,
                impl_->scalerSourceWidth,
                impl_->scalerSourceHeight,
                static_cast<int>(impl_->scalerSourceFormat));
            std::fflush(stderr);
            return false;
        }

        const int requiredSize = av_image_get_buffer_size(
            AV_PIX_FMT_RGBA,
            impl_->width,
            impl_->height,
            static_cast<int>(impl_->destinationAlignment));
        if (requiredSize <= 0)
        {
            return false;
        }

        const std::size_t logicalPixelSize =
            static_cast<std::size_t>(requiredSize);
        if (logicalPixelSize > (std::numeric_limits<std::size_t>::max)()
                - impl_->writablePaddingSize
            || logicalPixelSize + impl_->writablePaddingSize
                > (std::numeric_limits<std::size_t>::max)()
                    - impl_->guardSize)
        {
            return false;
        }

        const std::size_t allocatedDiagnosticSize =
            logicalPixelSize + impl_->writablePaddingSize;
        const std::size_t backingStorageSize =
            allocatedDiagnosticSize + impl_->guardSize;
        const unsigned char* oldData = impl_->pixels.data();
        const std::size_t oldCapacity = impl_->pixels.capacity();

        try
        {
            impl_->pixels.resize(backingStorageSize);
        }
        catch (...)
        {
            return false;
        }

        if (oldData != impl_->pixels.data()
            || oldCapacity != impl_->pixels.capacity()
            || impl_->bufferGeneration == 0)
        {
            ++impl_->bufferGeneration;
            std::fprintf(
                stderr,
                "[VideoResource] buffer-generation=%llu resource-generation=%llu ptr=%p logical=%zu allocated=%zu size=%zu capacity=%zu dimensions=%dx%d stride-pending\n",
                static_cast<unsigned long long>(impl_->bufferGeneration),
                static_cast<unsigned long long>(impl_->resourceGeneration),
                static_cast<const void*>(impl_->pixels.data()),
                logicalPixelSize,
                backingStorageSize,
                impl_->pixels.size(),
                impl_->pixels.capacity(),
                impl_->width,
                impl_->height);
            std::fflush(stderr);
        }

        impl_->logicalPixelSize = logicalPixelSize;
        impl_->allocatedDiagnosticSize = allocatedDiagnosticSize;
        impl_->backingStorageSize = backingStorageSize;

        uint8_t* destinationData[4]{};
        int destinationLinesize[4]{};
        if (av_image_fill_arrays(
                destinationData,
                destinationLinesize,
                impl_->pixels.data(),
                AV_PIX_FMT_RGBA,
                impl_->width,
                impl_->height,
                static_cast<int>(impl_->destinationAlignment)) < 0)
        {
            return false;
        }

        const std::size_t rowStride =
            static_cast<std::size_t>(destinationLinesize[0]);
        if (impl_->height <= 0)
        {
            return false;
        }
        const std::size_t rowCount =
            static_cast<std::size_t>(impl_->height);
        if (rowStride > (std::numeric_limits<std::size_t>::max)()
                / rowCount)
        {
            return false;
        }

        const std::size_t byteSize = rowStride * rowCount;
        if (destinationLinesize[0] <= 0
            || rowStride < static_cast<std::size_t>(impl_->width) * 4U
            || byteSize > impl_->logicalPixelSize
            || byteSize > impl_->pixels.size())
        {
            return false;
        }

        impl_->lastDestinationRowStride = rowStride;
        std::fill(
            impl_->pixels.begin() + static_cast<std::ptrdiff_t>(
                impl_->logicalPixelSize),
            impl_->pixels.begin() + static_cast<std::ptrdiff_t>(
                impl_->allocatedDiagnosticSize),
            DiagnosticPaddingValue);
        std::fill(
            impl_->pixels.begin() + static_cast<std::ptrdiff_t>(
                impl_->allocatedDiagnosticSize),
            impl_->pixels.begin() + static_cast<std::ptrdiff_t>(
                impl_->backingStorageSize),
            DiagnosticGuardValue);
        const bool guardBeforeScale = verifyBufferGuard(
            "before-sws_scale");
        const std::uint64_t nextScaleCall = diagnosticValue(
            globalDiagnostics.scaleCalls) + 1U;
        const bool logScaleDiagnostics = shouldLog(nextScaleCall);
        if (logScaleDiagnostics)
        {
            std::fprintf(
            stderr,
            "[VideoResource] GUARD-BEFORE-SCALE resource-generation=%llu buffer-generation=%llu frameAt=%llu sws=%llu guard-intact=%s ptr=%p guard=%p logical=%zu padding=%zu backing=%zu rowStride=%zu width=%d height=%d align=%zu decoded=%dx%d/%d linesize=[%d,%d,%d,%d] pts=%lld best_pts=%lld scaler-source=%dx%d/%d scaler-destination=%dx%d/%d\n",
            static_cast<unsigned long long>(impl_->resourceGeneration),
            static_cast<unsigned long long>(impl_->bufferGeneration),
            static_cast<unsigned long long>(
                diagnosticValue(globalDiagnostics.frameAtCalls)),
            static_cast<unsigned long long>(nextScaleCall),
            guardBeforeScale ? "yes" : "no",
            static_cast<const void*>(impl_->pixels.data()),
            static_cast<const void*>(impl_->pixels.data()
                + impl_->allocatedDiagnosticSize),
            impl_->logicalPixelSize,
            impl_->writablePaddingSize,
            impl_->backingStorageSize,
            rowStride,
            impl_->width,
            impl_->height,
            impl_->destinationAlignment,
            impl_->lastDecodedWidth,
            impl_->lastDecodedHeight,
            static_cast<int>(impl_->lastDecodedFormat),
            impl_->lastDecodedLinesize[0],
            impl_->lastDecodedLinesize[1],
            impl_->lastDecodedLinesize[2],
            impl_->lastDecodedLinesize[3],
            static_cast<long long>(impl_->lastFramePts),
            static_cast<long long>(impl_->lastBestEffortTimestamp),
            impl_->scalerSourceWidth,
            impl_->scalerSourceHeight,
            static_cast<int>(impl_->scalerSourceFormat),
            impl_->scalerDestinationWidth,
            impl_->scalerDestinationHeight,
            static_cast<int>(impl_->scalerDestinationFormat));
            std::fflush(stderr);
        }
        if (!guardBeforeScale)
        {
            return false;
        }
        (void)incrementDiagnosticValue(globalDiagnostics.scaleCalls);
        ++impl_->decodeDiagnostics.scaleCalls;
        const int scaledRows = sws_scale(
            impl_->scaler.get(),
            impl_->decodedFrame->data,
            impl_->decodedFrame->linesize,
            0,
            impl_->decodedFrame->height,
            destinationData,
            destinationLinesize);
        impl_->lastScaledRows = scaledRows;
        if (scaledRows != impl_->height)
        {
            std::fprintf(
                stderr,
                "[VideoResource] unexpected sws_scale rows=%d expected=%d\n",
                scaledRows,
                impl_->height);
            std::fflush(stderr);
            return false;
        }
        if (!verifyBufferGuard("after-sws_scale"))
        {
            return false;
        }

        double timestampSeconds = static_cast<double>(time);
        bool hasTimestamp = false;
        if (frameTimestamp != AV_NOPTS_VALUE)
        {
            const double candidateTimestamp =
                static_cast<double>(frameTimestamp)
                * av_q2d(impl_->timeBase);
            if (std::isfinite(candidateTimestamp)
                && candidateTimestamp >= 0.0)
            {
                timestampSeconds = candidateTimestamp;
                hasTimestamp = true;
            }
        }

        frame_ = DecodedFrame{
            impl_->pixels.data(),
            impl_->width,
            impl_->height,
            4,
            rowStride,
            byteSize,
            timestampSeconds,
            hasTimestamp
        };
        impl_->lastReturnedTimestampSeconds = timestampSeconds;
        impl_->hasReturnedFrame = true;
        impl_->lastRequestedTime = time;
        (void)incrementDiagnosticValue(globalDiagnostics.framesReturned);
        ++impl_->decodeDiagnostics.framesReturned;
        return true;
    };

    for (;;)
    {
        if (cancellationRequested())
        {
            return false;
        }

        if (impl_->packetPending)
        {
            const int sendResult = avcodec_send_packet(
                impl_->codec.get(),
                impl_->packet.get());
            if (sendResult == 0 || sendResult == AVERROR_EOF)
            {
                impl_->packetPending = false;
                av_packet_unref(impl_->packet.get());
                continue;
            }

            if (sendResult != AVERROR(EAGAIN))
            {
                impl_->sequentialStateValid = false;
                impl_->eofReached = true;
                av_packet_unref(impl_->packet.get());
                return false;
            }
        }

        const int receiveResult = avcodec_receive_frame(
            impl_->codec.get(),
            impl_->decodedFrame.get());
        if (receiveResult >= 0)
        {
            (void)incrementDiagnosticValue(globalDiagnostics.decodedFrames);
            ++impl_->decodeDiagnostics.decodedFrames;
            impl_->lastDecodedWidth = impl_->decodedFrame->width;
            impl_->lastDecodedHeight = impl_->decodedFrame->height;
            impl_->lastDecodedFormat = static_cast<AVPixelFormat>(
                impl_->decodedFrame->format);
            impl_->lastDecodedLinesize[0] = impl_->decodedFrame->linesize[0];
            impl_->lastDecodedLinesize[1] = impl_->decodedFrame->linesize[1];
            impl_->lastDecodedLinesize[2] = impl_->decodedFrame->linesize[2];
            impl_->lastDecodedLinesize[3] = impl_->decodedFrame->linesize[3];
            impl_->lastFramePts = impl_->decodedFrame->pts;
            impl_->lastBestEffortTimestamp =
                impl_->decodedFrame->best_effort_timestamp;

            const int64_t frameTimestamp =
                impl_->decodedFrame->best_effort_timestamp != AV_NOPTS_VALUE
                ? impl_->decodedFrame->best_effort_timestamp
                : impl_->decodedFrame->pts;
            if (frameTimestamp != AV_NOPTS_VALUE)
            {
                const double candidateTimestamp =
                    static_cast<double>(frameTimestamp)
                    * av_q2d(impl_->timeBase);
                if (std::isfinite(candidateTimestamp)
                    && candidateTimestamp >= 0.0)
                {
                    impl_->lastDecodedTimestampSeconds = candidateTimestamp;
                }
            }

            if (time >= 14.0F && time <= 18.0F)
            {
                logFrameProperties(
                    "near-target",
                    impl_->decodedFrame.get(),
                    impl_->width,
                    impl_->height,
                    impl_->pixelFormat);
            }

            if (frameTimestamp != AV_NOPTS_VALUE
                && frameTimestamp < targetTimestamp)
            {
                av_frame_unref(impl_->decodedFrame.get());
                continue;
            }

            if (cancellationRequested())
            {
                av_frame_unref(impl_->decodedFrame.get());
                return false;
            }

            const bool converted = convertCurrentFrame(frameTimestamp);
            av_frame_unref(impl_->decodedFrame.get());
            if (!converted)
            {
                return false;
            }

            if (shouldLog(diagnosticValue(globalDiagnostics.frameAtCalls)))
            {
                std::fprintf(
                    stderr,
                    "[VideoResource] success frameAt=%llu seek=%llu flush=%llu packets=%llu decoded=%llu returned=%llu reused=%llu scale=%llu requested=%.6f actual=%.6f timestamp-valid=%s bytes=%zu stride=%zu\n",
                    static_cast<unsigned long long>(
                        diagnosticValue(globalDiagnostics.frameAtCalls)),
                    static_cast<unsigned long long>(
                        diagnosticValue(globalDiagnostics.seekCalls)),
                    static_cast<unsigned long long>(
                        diagnosticValue(globalDiagnostics.flushCalls)),
                    static_cast<unsigned long long>(
                        diagnosticValue(globalDiagnostics.packetsRead)),
                    static_cast<unsigned long long>(
                        diagnosticValue(globalDiagnostics.decodedFrames)),
                    static_cast<unsigned long long>(
                        diagnosticValue(globalDiagnostics.framesReturned)),
                    static_cast<unsigned long long>(
                        diagnosticValue(globalDiagnostics.reusedFrames)),
                    static_cast<unsigned long long>(
                        diagnosticValue(globalDiagnostics.scaleCalls)),
                    static_cast<double>(time),
                    frame_.timestampSeconds,
                    frame_.hasTimestamp ? "yes" : "no",
                    frame_.byteSize,
                    frame_.rowStride);
                std::fflush(stderr);
            }
            return true;
        }

        if (receiveResult == AVERROR_EOF)
        {
            impl_->lastRequestedTime = time;
            impl_->eofReached = true;
            impl_->sequentialStateValid = false;
            return false;
        }

        if (receiveResult != AVERROR(EAGAIN))
        {
            impl_->sequentialStateValid = false;
            impl_->eofReached = true;
            return false;
        }

        if (impl_->packetPending)
        {
            continue;
        }

        if (impl_->inputEof)
        {
            if (!impl_->decoderDrainSent)
            {
                const int drainResult = avcodec_send_packet(
                    impl_->codec.get(),
                    nullptr);
                if (drainResult != 0 && drainResult != AVERROR_EOF)
                {
                    if (drainResult == AVERROR(EAGAIN))
                    {
                        continue;
                    }
                    impl_->sequentialStateValid = false;
                    impl_->eofReached = true;
                    return false;
                }
                impl_->decoderDrainSent = true;
                continue;
            }

            impl_->lastRequestedTime = time;
            impl_->eofReached = true;
            impl_->sequentialStateValid = false;
            return false;
        }

        if (cancellationRequested())
        {
            return false;
        }

        const int readResult = av_read_frame(
            impl_->format.get(),
            impl_->packet.get());
        if (readResult == AVERROR_EOF)
        {
            impl_->inputEof = true;
            continue;
        }
        if (readResult < 0)
        {
            impl_->sequentialStateValid = false;
            impl_->eofReached = true;
            return false;
        }

        (void)incrementDiagnosticValue(globalDiagnostics.packetsRead);
        ++impl_->decodeDiagnostics.packetsRead;
        if (impl_->packet->stream_index != impl_->videoStreamIndex)
        {
            av_packet_unref(impl_->packet.get());
            continue;
        }

        impl_->packetPending = true;
    }
}

VideoFrameDecodeStatus VideoResource::frameAt(
    float time,
    const VideoFrameDecodeOptions& options
) noexcept
{
    if (!impl_)
    {
        return VideoFrameDecodeStatus::Unavailable;
    }

    impl_->activeDecodeOptions = options;
    const bool decoded = frameAt(time);
    const bool cancelled = impl_->cancellationObserved;
    impl_->activeDecodeOptions = {};
    if (cancelled)
    {
        return VideoFrameDecodeStatus::Cancelled;
    }

    return decoded ? VideoFrameDecodeStatus::Ready
                   : VideoFrameDecodeStatus::Unavailable;
}

const DecodedFrame& VideoResource::currentFrame() const noexcept
{
    return frame_;
}

VideoDecodeDiagnostics VideoResource::diagnostics() const noexcept
{
    if (!impl_)
    {
        return {};
    }

    return impl_->decodeDiagnostics;
}

std::uint64_t VideoResource::cancellationCheckCount() const noexcept
{
    return impl_ ? impl_->cancellationCheckCount.load(std::memory_order_relaxed) : 0U;
}

float VideoResource::duration() const noexcept
{
    return impl_ ? impl_->mediaDuration : 0.0F;
}

}
