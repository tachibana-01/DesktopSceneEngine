#include "SequentialAudioSource.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace DesktopSceneEngine::Resources
{

namespace
{

constexpr int OutputChannels = 2;
constexpr int OutputSampleRate = 48000;
constexpr std::size_t MaximumPendingPcmBytes = 1024U * 1024U;
constexpr std::size_t MaximumPendingFrames = MaximumPendingPcmBytes
    / (static_cast<std::size_t>(OutputChannels) * sizeof(float));

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

struct ResamplerCloser
{
    void operator()(SwrContext* context) const noexcept
    {
        swr_free(&context);
    }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecCloser>;
using PacketPtr = std::unique_ptr<AVPacket, PacketCloser>;
using FramePtr = std::unique_ptr<AVFrame, FrameCloser>;
using ResamplerPtr = std::unique_ptr<SwrContext, ResamplerCloser>;

struct ChannelLayoutGuard
{
    AVChannelLayout value{};

    ChannelLayoutGuard() = default;

    ~ChannelLayoutGuard()
    {
        av_channel_layout_uninit(&value);
    }

    ChannelLayoutGuard(const ChannelLayoutGuard&) = delete;
    ChannelLayoutGuard& operator=(const ChannelLayoutGuard&) = delete;
};

bool checkedProduct(
    std::size_t left,
    std::size_t right,
    std::size_t& result
) noexcept
{
    if (right != 0U && left > (std::numeric_limits<std::size_t>::max)() / right)
    {
        return false;
    }

    result = left * right;
    return true;
}

std::string pathAsUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return {
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

}

struct SequentialAudioSourceImpl
{
    enum class Phase
    {
        ReadingPackets,
        DemuxExhausted,
        DecoderDraining,
        ResamplerDraining,
        Finished,
        Failed
    };

    // Members are declared in ownership order so reverse destruction releases
    // pending PCM, then swresample, frame, packet, codec, and finally demuxer.
    FormatPtr format{};
    CodecPtr codec{};
    PacketPtr packet{};
    FramePtr frame{};
    ResamplerPtr resampler{};
    std::vector<float> pendingPcm{};

    int audioStreamIndex = -1;
    std::size_t pendingReadSample = 0U;
    std::size_t peakPendingFrames = 0U;
    std::size_t sendPacketEagainCount = 0U;
    std::size_t decoderFramesDuringDrain = 0U;
    std::size_t resamplerFramesDuringDrain = 0U;
    bool packetPending = false;
    Phase phase = Phase::ReadingPackets;
    AudioSourceStatus failureStatus = AudioSourceStatus::DecodeFailure;
};

struct SequentialAudioSource::Impl final : SequentialAudioSourceImpl
{
};

namespace
{

enum class ReceiveResult
{
    Produced,
    NeedsInput,
    DecoderFinished,
    Failed
};

bool hasPendingPcm(const SequentialAudioSourceImpl& impl) noexcept
{
    return impl.pendingReadSample < impl.pendingPcm.size();
}

void discardPendingPcm(SequentialAudioSourceImpl& impl) noexcept
{
    impl.pendingPcm.clear();
    impl.pendingReadSample = 0U;
}

void abandonPacket(SequentialAudioSourceImpl& impl) noexcept
{
    if (impl.packetPending && impl.packet)
    {
        av_packet_unref(impl.packet.get());
    }
    impl.packetPending = false;
}

void fail(
    SequentialAudioSourceImpl& impl,
    AudioSourceStatus status
) noexcept
{
    abandonPacket(impl);
    discardPendingPcm(impl);
    impl.failureStatus = status;
    impl.phase = SequentialAudioSourceImpl::Phase::Failed;
}

bool appendConvertedPcm(
    SequentialAudioSourceImpl& impl,
    const std::uint8_t* const* input,
    int inputSamples
) noexcept
{
    const int reportedCapacity = swr_get_out_samples(
        impl.resampler.get(),
        inputSamples);
    if (reportedCapacity < 0)
    {
        fail(impl, AudioSourceStatus::ResampleFailure);
        return false;
    }

    const int outputCapacity = (std::max)(reportedCapacity, 1);
    const std::size_t outputFrames = static_cast<std::size_t>(outputCapacity);
    std::size_t outputSamples = 0U;
    if (outputFrames > MaximumPendingFrames
        || !checkedProduct(
            outputFrames,
            static_cast<std::size_t>(OutputChannels),
            outputSamples))
    {
        fail(impl, AudioSourceStatus::ResourceFailure);
        return false;
    }

    try
    {
        discardPendingPcm(impl);
        impl.pendingPcm.resize(outputSamples);
    }
    catch (...)
    {
        fail(impl, AudioSourceStatus::ResourceFailure);
        return false;
    }

    std::uint8_t* output[]{
        reinterpret_cast<std::uint8_t*>(impl.pendingPcm.data())
    };
    const int converted = swr_convert(
        impl.resampler.get(),
        output,
        outputCapacity,
        input,
        inputSamples);
    if (converted < 0 || converted > outputCapacity)
    {
        fail(impl, AudioSourceStatus::ResampleFailure);
        return false;
    }

    std::size_t convertedSamples = 0U;
    if (!checkedProduct(
            static_cast<std::size_t>(converted),
            static_cast<std::size_t>(OutputChannels),
            convertedSamples))
    {
        fail(impl, AudioSourceStatus::ResourceFailure);
        return false;
    }

    try
    {
        impl.pendingPcm.resize(convertedSamples);
    }
    catch (...)
    {
        fail(impl, AudioSourceStatus::ResourceFailure);
        return false;
    }
    impl.pendingReadSample = 0U;
    impl.peakPendingFrames = (std::max)(
        impl.peakPendingFrames,
        static_cast<std::size_t>(converted));
    if (input == nullptr && inputSamples == 0)
    {
        impl.resamplerFramesDuringDrain += static_cast<std::size_t>(converted);
    }
    return true;
}

ReceiveResult receiveFrame(
    SequentialAudioSourceImpl& impl,
    bool draining
) noexcept
{
    const int result = avcodec_receive_frame(
        impl.codec.get(),
        impl.frame.get());
    if (result == AVERROR(EAGAIN))
    {
        return ReceiveResult::NeedsInput;
    }
    if (result == AVERROR_EOF)
    {
        return ReceiveResult::DecoderFinished;
    }
    if (result < 0)
    {
        fail(impl, AudioSourceStatus::DecodeFailure);
        return ReceiveResult::Failed;
    }

    const bool validFrame = impl.frame->nb_samples > 0
        && impl.frame->extended_data != nullptr;
    if (!validFrame)
    {
        av_frame_unref(impl.frame.get());
        fail(impl, AudioSourceStatus::DecodeFailure);
        return ReceiveResult::Failed;
    }

    const bool converted = appendConvertedPcm(
        impl,
        impl.frame->extended_data,
        impl.frame->nb_samples);
    av_frame_unref(impl.frame.get());
    if (!converted)
    {
        return ReceiveResult::Failed;
    }
    if (draining)
    {
        ++impl.decoderFramesDuringDrain;
    }

    if (!draining && impl.phase != SequentialAudioSourceImpl::Phase::ReadingPackets)
    {
        fail(impl, AudioSourceStatus::DecodeFailure);
        return ReceiveResult::Failed;
    }

    return ReceiveResult::Produced;
}

bool advanceUntilPcmOrTerminal(SequentialAudioSourceImpl& impl) noexcept
{
    while (!hasPendingPcm(impl)
        && impl.phase != SequentialAudioSourceImpl::Phase::Finished
        && impl.phase != SequentialAudioSourceImpl::Phase::Failed)
    {
        switch (impl.phase)
        {
        case SequentialAudioSourceImpl::Phase::ReadingPackets:
        {
            if (impl.packetPending)
            {
                const int sent = avcodec_send_packet(
                    impl.codec.get(),
                    impl.packet.get());
                if (sent == 0)
                {
                    abandonPacket(impl);
                    const ReceiveResult received = receiveFrame(impl, false);
                    if (received == ReceiveResult::DecoderFinished)
                    {
                        fail(impl, AudioSourceStatus::DecodeFailure);
                    }
                    continue;
                }
                if (sent == AVERROR(EAGAIN))
                {
                    // The packet remains owned by this source. Receive output
                    // and retry precisely this same packet on the next step.
                    ++impl.sendPacketEagainCount;
                    const ReceiveResult received = receiveFrame(impl, false);
                    if (received == ReceiveResult::NeedsInput
                        || received == ReceiveResult::DecoderFinished)
                    {
                        fail(impl, AudioSourceStatus::DecodeFailure);
                    }
                    continue;
                }

                fail(impl, AudioSourceStatus::DecodeFailure);
                continue;
            }

            const int read = av_read_frame(impl.format.get(), impl.packet.get());
            if (read >= 0)
            {
                if (impl.packet->stream_index != impl.audioStreamIndex)
                {
                    av_packet_unref(impl.packet.get());
                    continue;
                }

                impl.packetPending = true;
                continue;
            }

            if (read == AVERROR_EOF)
            {
                impl.phase = SequentialAudioSourceImpl::Phase::DemuxExhausted;
                continue;
            }

            fail(impl, AudioSourceStatus::DecodeFailure);
            continue;
        }

        case SequentialAudioSourceImpl::Phase::DemuxExhausted:
        {
            const int sent = avcodec_send_packet(impl.codec.get(), nullptr);
            if (sent == 0)
            {
                impl.phase = SequentialAudioSourceImpl::Phase::DecoderDraining;
                continue;
            }
            if (sent == AVERROR(EAGAIN))
            {
                const ReceiveResult received = receiveFrame(impl, true);
                if (received == ReceiveResult::NeedsInput)
                {
                    fail(impl, AudioSourceStatus::DecodeFailure);
                }
                else if (received == ReceiveResult::DecoderFinished)
                {
                    impl.phase = SequentialAudioSourceImpl::Phase::ResamplerDraining;
                }
                continue;
            }
            if (sent == AVERROR_EOF)
            {
                impl.phase = SequentialAudioSourceImpl::Phase::ResamplerDraining;
                continue;
            }

            fail(impl, AudioSourceStatus::DecodeFailure);
            continue;
        }

        case SequentialAudioSourceImpl::Phase::DecoderDraining:
        {
            const ReceiveResult received = receiveFrame(impl, true);
            if (received == ReceiveResult::NeedsInput)
            {
                fail(impl, AudioSourceStatus::DecodeFailure);
            }
            else if (received == ReceiveResult::DecoderFinished)
            {
                impl.phase = SequentialAudioSourceImpl::Phase::ResamplerDraining;
            }
            continue;
        }

        case SequentialAudioSourceImpl::Phase::ResamplerDraining:
        {
            const int delayedSamples = swr_get_out_samples(impl.resampler.get(), 0);
            if (delayedSamples < 0)
            {
                fail(impl, AudioSourceStatus::ResampleFailure);
                continue;
            }
            // A final null-input conversion is also made when FFmpeg reports
            // no remaining capacity. Its zero result is the actual resampler
            // drain boundary rather than an inferred EOF shortcut.
            if (!appendConvertedPcm(impl, nullptr, 0))
            {
                continue;
            }
            if (!hasPendingPcm(impl))
            {
                // swr_convert() returning zero while flushing is the terminal
                // drain signal; no delayed PCM remains to expose.
                impl.phase = SequentialAudioSourceImpl::Phase::Finished;
            }
            continue;
        }

        case SequentialAudioSourceImpl::Phase::Finished:
        case SequentialAudioSourceImpl::Phase::Failed:
            break;
        }
    }

    return hasPendingPcm(impl);
}

bool initializeSession(
    const std::filesystem::path& path,
    SequentialAudioSourceImpl& candidate,
    AudioSourceStatus& failureStatus
) noexcept
{
    try
    {
        candidate.packet.reset(av_packet_alloc());
        candidate.frame.reset(av_frame_alloc());
        if (!candidate.packet || !candidate.frame)
        {
            failureStatus = AudioSourceStatus::ResourceFailure;
            return false;
        }

        const std::string pathUtf8 = pathAsUtf8(path);
        AVFormatContext* rawFormat = nullptr;
        if (avformat_open_input(&rawFormat, pathUtf8.c_str(), nullptr, nullptr) < 0)
        {
            failureStatus = AudioSourceStatus::OpenFailure;
            return false;
        }
        candidate.format.reset(rawFormat);
        if (avformat_find_stream_info(candidate.format.get(), nullptr) < 0)
        {
            failureStatus = AudioSourceStatus::OpenFailure;
            return false;
        }

        const AVCodec* decoder = nullptr;
        candidate.audioStreamIndex = av_find_best_stream(
            candidate.format.get(),
            AVMEDIA_TYPE_AUDIO,
            -1,
            -1,
            &decoder,
            0);
        if (candidate.audioStreamIndex < 0 || decoder == nullptr)
        {
            failureStatus = AudioSourceStatus::NoAudioStream;
            return false;
        }

        const AVStream* stream = candidate.format->streams[candidate.audioStreamIndex];
        candidate.codec.reset(avcodec_alloc_context3(decoder));
        if (!candidate.codec
            || avcodec_parameters_to_context(
                candidate.codec.get(),
                stream->codecpar) < 0
            || avcodec_open2(candidate.codec.get(), decoder, nullptr) < 0)
        {
            failureStatus = AudioSourceStatus::DecoderUnavailable;
            return false;
        }
        if (candidate.codec->sample_rate <= 0
            || candidate.codec->ch_layout.nb_channels <= 0
            || candidate.codec->sample_fmt == AV_SAMPLE_FMT_NONE)
        {
            failureStatus = AudioSourceStatus::DecoderUnavailable;
            return false;
        }

        ChannelLayoutGuard outputLayout{};
        av_channel_layout_default(&outputLayout.value, OutputChannels);
        SwrContext* rawResampler = nullptr;
        if (swr_alloc_set_opts2(
                &rawResampler,
                &outputLayout.value,
                AV_SAMPLE_FMT_FLT,
                OutputSampleRate,
                &candidate.codec->ch_layout,
                candidate.codec->sample_fmt,
                candidate.codec->sample_rate,
                0,
                nullptr) < 0
            || rawResampler == nullptr)
        {
            if (rawResampler != nullptr)
            {
                swr_free(&rawResampler);
            }
            failureStatus = AudioSourceStatus::ResampleFailure;
            return false;
        }
        candidate.resampler.reset(rawResampler);
        if (swr_init(candidate.resampler.get()) < 0)
        {
            failureStatus = AudioSourceStatus::ResampleFailure;
            return false;
        }

        return true;
    }
    catch (...)
    {
        failureStatus = AudioSourceStatus::ResourceFailure;
        return false;
    }
}

}

SequentialAudioSource::SequentialAudioSource() = default;

SequentialAudioSource::~SequentialAudioSource() = default;

bool SequentialAudioSource::open(const std::filesystem::path& source) noexcept
{
    try
    {
        impl_.reset();
        sourcePath_ = source;
        status_ = AudioSourceStatus::OpenFailure;
        if (sourcePath_.empty())
        {
            return false;
        }

        std::unique_ptr<Impl> candidate{};
        AudioSourceStatus failureStatus = AudioSourceStatus::OpenFailure;
        candidate = std::make_unique<Impl>();
        if (!initializeSession(sourcePath_, *candidate, failureStatus))
        {
            status_ = failureStatus;
            return false;
        }

        impl_ = std::move(candidate);
        status_ = AudioSourceStatus::Ready;
        return true;
    }
    catch (...)
    {
        impl_.reset();
        status_ = AudioSourceStatus::ResourceFailure;
        return false;
    }
}

bool SequentialAudioSource::reset() noexcept
{
    if (sourcePath_.empty())
    {
        impl_.reset();
        status_ = AudioSourceStatus::NotOpened;
        return false;
    }

    return open(sourcePath_);
}

bool SequentialAudioSource::isOpen() const noexcept
{
    return impl_ != nullptr
        && status_ == AudioSourceStatus::Ready
        && impl_->phase != Impl::Phase::Failed;
}

AudioSourceStatus SequentialAudioSource::status() const noexcept
{
    return status_;
}

AudioPcmFormat SequentialAudioSource::format() const noexcept
{
    return {};
}

AudioSourceDiagnostics SequentialAudioSource::diagnostics() const noexcept
{
    return impl_ != nullptr ? AudioSourceDiagnostics{
                                impl_->peakPendingFrames,
                                impl_->sendPacketEagainCount,
                                impl_->decoderFramesDuringDrain,
                                impl_->resamplerFramesDuringDrain}
                            : AudioSourceDiagnostics{};
}

AudioReadResult SequentialAudioSource::readFrames(
    std::span<float> interleavedDestination) noexcept
{
    if (interleavedDestination.empty() || interleavedDestination.size() % 2U != 0U)
    {
        return {0U, AudioReadStatus::InvalidRequest};
    }
    if (impl_ == nullptr)
    {
        return {
            0U,
            status_ == AudioSourceStatus::NotOpened
                ? AudioReadStatus::NotReady
                : AudioReadStatus::Failure
        };
    }
    if (impl_->phase == Impl::Phase::Failed || status_ != AudioSourceStatus::Ready)
    {
        return {0U, AudioReadStatus::Failure};
    }
    if (impl_->phase == Impl::Phase::Finished)
    {
        return {0U, AudioReadStatus::EndOfStream};
    }

    std::size_t writtenSamples = 0U;
    try
    {
        while (writtenSamples < interleavedDestination.size())
        {
            if (hasPendingPcm(*impl_))
            {
                const std::size_t available = impl_->pendingPcm.size() - impl_->pendingReadSample;
                const std::size_t requested = interleavedDestination.size() - writtenSamples;
                const std::size_t copied = (std::min)(available, requested);
                std::copy_n(
                    impl_->pendingPcm.data() + impl_->pendingReadSample,
                    copied,
                    interleavedDestination.data() + writtenSamples);
                impl_->pendingReadSample += copied;
                writtenSamples += copied;
                if (!hasPendingPcm(*impl_))
                {
                    discardPendingPcm(*impl_);
                }

                // Do not perform look-ahead at an exact caller boundary. This
                // preserves the required Data-then-EndOfStream behavior.
                if (writtenSamples == interleavedDestination.size())
                {
                    return {
                        writtenSamples / static_cast<std::size_t>(OutputChannels),
                        AudioReadStatus::Data
                    };
                }
            }

            if (impl_->phase == Impl::Phase::Finished)
            {
                return {
                    writtenSamples / static_cast<std::size_t>(OutputChannels),
                    AudioReadStatus::EndOfStream
                };
            }
            if (impl_->phase == Impl::Phase::Failed)
            {
                status_ = impl_->failureStatus;
                return {
                    writtenSamples / static_cast<std::size_t>(OutputChannels),
                    AudioReadStatus::Failure
                };
            }

            (void)advanceUntilPcmOrTerminal(*impl_);
        }
    }
    catch (...)
    {
        fail(*impl_, AudioSourceStatus::ResourceFailure);
        status_ = AudioSourceStatus::ResourceFailure;
        return {
            writtenSamples / static_cast<std::size_t>(OutputChannels),
            AudioReadStatus::Failure
        };
    }

    return {
        writtenSamples / static_cast<std::size_t>(OutputChannels),
        AudioReadStatus::Data
    };
}

}
