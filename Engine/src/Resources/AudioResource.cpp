#include "AudioResource.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libswresample/swresample.h>
}

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <filesystem>
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

constexpr int OutputChannels = 2;
constexpr int OutputSampleRate = 48000;
constexpr std::size_t MaximumPcmBytes = 512U * 1024U * 1024U;
constexpr std::size_t MaximumChunkBytes = 16U * 1024U * 1024U;
constexpr std::size_t FilterChunkFrames = 4096U;

std::string pathAsUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return {
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

bool checkedProduct(
    std::size_t left,
    std::size_t right,
    std::size_t& result
) noexcept
{
    if (right != 0 && left > (std::numeric_limits<std::size_t>::max)() / right)
    {
        return false;
    }

    result = left * right;
    return true;
}

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

struct FilterGraphCloser
{
    void operator()(AVFilterGraph* graph) const noexcept
    {
        if (graph != nullptr)
        {
            avfilter_graph_free(&graph);
        }
    }
};

using FilterGraphPtr = std::unique_ptr<AVFilterGraph, FilterGraphCloser>;

struct FilterFrameCloser
{
    void operator()(AVFrame* frame) const noexcept
    {
        av_frame_free(&frame);
    }
};

using FilterFramePtr = std::unique_ptr<AVFrame, FilterFrameCloser>;

bool validTempo(float tempo) noexcept
{
    return std::isfinite(tempo) && tempo >= 0.5F && tempo <= 2.0F;
}

bool appendFilteredFrame(
    std::vector<std::int16_t>& output,
    const AVFrame& frame) noexcept
{
    if (frame.nb_samples <= 0
        || frame.data[0] == nullptr
        || frame.format != AV_SAMPLE_FMT_S16
        || frame.ch_layout.nb_channels != OutputChannels)
    {
        return frame.nb_samples == 0;
    }

    std::size_t sampleCount = 0;
    std::size_t byteCount = 0;
    if (!checkedProduct(
            static_cast<std::size_t>(frame.nb_samples),
            static_cast<std::size_t>(OutputChannels),
            sampleCount)
        || !checkedProduct(sampleCount, sizeof(std::int16_t), byteCount)
        || byteCount > MaximumChunkBytes
        || output.size() > (MaximumPcmBytes - byteCount)
                / sizeof(std::int16_t))
    {
        return false;
    }

    const auto* samples = reinterpret_cast<const std::int16_t*>(frame.data[0]);
    try
    {
        output.insert(output.end(), samples, samples + sampleCount);
    }
    catch (...)
    {
        return false;
    }

    return true;
}

bool buildTempoPcm(
    const std::vector<std::int16_t>& source,
    float tempo,
    std::vector<std::int16_t>& output) noexcept
{
    if (!validTempo(tempo) || source.empty()
        || source.size() % OutputChannels != 0U)
    {
        return false;
    }

    const AVFilter* sourceFilter = avfilter_get_by_name("abuffer");
    const AVFilter* tempoFilter = avfilter_get_by_name("atempo");
    const AVFilter* sinkFilter = avfilter_get_by_name("abuffersink");
    if (sourceFilter == nullptr || tempoFilter == nullptr || sinkFilter == nullptr)
    {
        return false;
    }

    FilterGraphPtr graph{avfilter_graph_alloc()};
    if (!graph)
    {
        return false;
    }

    AVFilterContext* sourceContext = nullptr;
    AVFilterContext* tempoContext = nullptr;
    AVFilterContext* sinkContext = nullptr;
    const std::string sourceArgs =
        "time_base=1/48000:sample_rate=48000:sample_fmt=s16:channel_layout=stereo";
    const std::string tempoArgs = "tempo=" + std::to_string(tempo);
    if (avfilter_graph_create_filter(
            &sourceContext,
            sourceFilter,
            "source",
            sourceArgs.c_str(),
            nullptr,
            graph.get()) < 0
        || avfilter_graph_create_filter(
            &tempoContext,
            tempoFilter,
            "tempo",
            tempoArgs.c_str(),
            nullptr,
            graph.get()) < 0
        || avfilter_graph_create_filter(
            &sinkContext,
            sinkFilter,
            "sink",
            nullptr,
            nullptr,
            graph.get()) < 0
        || avfilter_link(sourceContext, 0, tempoContext, 0) < 0
        || avfilter_link(tempoContext, 0, sinkContext, 0) < 0
        || avfilter_graph_config(graph.get(), nullptr) < 0)
    {
        return false;
    }

    const std::size_t sourceFrames = source.size()
        / static_cast<std::size_t>(OutputChannels);
    output.clear();
    try
    {
        output.reserve(source.size());
    }
    catch (...)
    {
        return false;
    }

    for (std::size_t frameOffset = 0; frameOffset < sourceFrames; )
    {
        const std::size_t frames = (std::min)(
            FilterChunkFrames,
            sourceFrames - frameOffset);
        FilterFramePtr input{av_frame_alloc()};
        FilterFramePtr filtered{av_frame_alloc()};
        if (!input || !filtered)
        {
            return false;
        }

        input->format = AV_SAMPLE_FMT_S16;
        input->sample_rate = OutputSampleRate;
        av_channel_layout_default(&input->ch_layout, OutputChannels);
        input->nb_samples = static_cast<int>(frames);
        input->pts = static_cast<std::int64_t>(frameOffset);
        input->data[0] = reinterpret_cast<std::uint8_t*>(
            const_cast<std::int16_t*>(source.data())
            + frameOffset * static_cast<std::size_t>(OutputChannels));
        input->linesize[0] = static_cast<int>(
            frames * static_cast<std::size_t>(OutputChannels)
            * sizeof(std::int16_t));
        if (av_buffersrc_add_frame_flags(
                sourceContext,
                input.get(),
                AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
        {
            return false;
        }

        frameOffset += frames;
        while (true)
        {
            const int result = av_buffersink_get_frame(
                sinkContext,
                filtered.get());
            if (result == AVERROR(EAGAIN))
            {
                break;
            }
            if (result < 0 || !appendFilteredFrame(output, *filtered))
            {
                return false;
            }
            av_frame_unref(filtered.get());
        }
    }

    if (av_buffersrc_add_frame_flags(sourceContext, nullptr, 0) < 0)
    {
        return false;
    }

    FilterFramePtr filtered{av_frame_alloc()};
    if (!filtered)
    {
        return false;
    }
    while (true)
    {
        const int result = av_buffersink_get_frame(sinkContext, filtered.get());
        if (result == AVERROR_EOF)
        {
            break;
        }
        if (result < 0 || !appendFilteredFrame(output, *filtered))
        {
            return false;
        }
        av_frame_unref(filtered.get());
    }

    return !output.empty();
}

}

struct AudioResource::Impl
{
    FormatPtr format{};
    CodecPtr codec{};
    PacketPtr packet{};
    FramePtr decodedFrame{};
    ResamplerPtr resampler{};
    std::vector<std::int16_t> pcm{};
    std::vector<std::int16_t> tempoPcm{};
    std::string sourcePath{};
    int audioStreamIndex = -1;
    int sourceSampleRate = 0;
    int sourceChannels = 0;
    AVSampleFormat sourceSampleFormat = AV_SAMPLE_FMT_NONE;
    AudioOpenStatus status = AudioOpenStatus::NotOpened;
    float tempo = 1.0F;
};

bool AudioResource::appendConvertedSamples(
    AudioResource::Impl& impl,
    const uint8_t* const* input,
    int inputSamples
) noexcept
{
    const int outputCapacity = swr_get_out_samples(
        impl.resampler.get(),
        inputSamples);
    if (outputCapacity < 0)
    {
        return false;
    }

    if (outputCapacity == 0)
    {
        return true;
    }

    std::size_t chunkSamples = 0;
    std::size_t chunkBytes = 0;
    if (!checkedProduct(
            static_cast<std::size_t>(outputCapacity),
            static_cast<std::size_t>(OutputChannels),
            chunkSamples)
        || !checkedProduct(
            chunkSamples,
            sizeof(std::int16_t),
            chunkBytes)
        || chunkBytes > MaximumChunkBytes)
    {
        return false;
    }

    std::vector<std::int16_t> chunk{};
    try
    {
        chunk.resize(chunkSamples);
    }
    catch (...)
    {
        return false;
    }

    uint8_t* output[1]{
        reinterpret_cast<uint8_t*>(chunk.data())
    };
    const int converted = swr_convert(
        impl.resampler.get(),
        output,
        outputCapacity,
        input,
        inputSamples);
    if (converted < 0)
    {
        return false;
    }

    std::size_t convertedSamples = 0;
    std::size_t convertedBytes = 0;
    if (!checkedProduct(
            static_cast<std::size_t>(converted),
            static_cast<std::size_t>(OutputChannels),
            convertedSamples)
        || !checkedProduct(
            convertedSamples,
            sizeof(std::int16_t),
            convertedBytes)
        || convertedBytes > MaximumPcmBytes
        || impl.pcm.size() > (MaximumPcmBytes - convertedBytes)
                / sizeof(std::int16_t))
    {
        return false;
    }

    try
    {
        impl.pcm.insert(
            impl.pcm.end(),
            chunk.begin(),
            chunk.begin() + static_cast<std::ptrdiff_t>(convertedSamples));
    }
    catch (...)
    {
        return false;
    }

    return true;
}

bool AudioResource::flushResampler(Impl& impl) noexcept
{
    constexpr std::size_t maximumFlushIterations = 1024U;
    for (std::size_t iteration = 0;
         iteration < maximumFlushIterations;
         ++iteration)
    {
        const int pending = swr_get_out_samples(impl.resampler.get(), 0);
        if (pending < 0)
        {
            return false;
        }
        if (pending == 0)
        {
            return true;
        }

        const std::size_t previousSize = impl.pcm.size();
        if (!appendConvertedSamples(impl, nullptr, 0))
        {
            return false;
        }

        // A resampler can report delayed samples that do not produce a
        // further output sample when flushed. The decoded PCM is complete at
        // that boundary; discard only that sub-sample delay and finish.
        if (impl.pcm.size() == previousSize)
        {
            return true;
        }
    }

    return true;
}

bool AudioResource::receiveDecodedFrames(Impl& impl) noexcept
{
    while (true)
    {
        const int result = avcodec_receive_frame(
            impl.codec.get(),
            impl.decodedFrame.get());
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
        {
            return true;
        }
        if (result < 0)
        {
            return false;
        }

        if (impl.decodedFrame->nb_samples <= 0
            || impl.decodedFrame->extended_data == nullptr
            || !appendConvertedSamples(
                impl,
                impl.decodedFrame->extended_data,
                impl.decodedFrame->nb_samples))
        {
            return false;
        }

        av_frame_unref(impl.decodedFrame.get());
    }
}

bool AudioResource::decodePcm(Impl& impl) noexcept
{
    while (av_read_frame(impl.format.get(), impl.packet.get()) >= 0)
    {
        if (impl.packet->stream_index == impl.audioStreamIndex)
        {
            if (avcodec_send_packet(impl.codec.get(), impl.packet.get()) < 0
                || !receiveDecodedFrames(impl))
            {
                av_packet_unref(impl.packet.get());
                return false;
            }
        }

        av_packet_unref(impl.packet.get());
    }

    if (avcodec_send_packet(impl.codec.get(), nullptr) < 0
        || !receiveDecodedFrames(impl)
        || !flushResampler(impl))
    {
        return false;
    }

    return !impl.pcm.empty();
}

AudioResource::AudioResource()
    : impl_(std::make_unique<Impl>())
{
}

AudioResource::~AudioResource() = default;

AudioResource::AudioResource(AudioResource&&) noexcept = default;

AudioResource& AudioResource::operator=(AudioResource&&) noexcept = default;

MediaType AudioResource::type() const noexcept
{
    return MediaType::Audio;
}

bool AudioResource::open(const std::filesystem::path& path) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->status = AudioOpenStatus::DecodeFailure;
    }

    if (path.empty())
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

    candidate->packet.reset(av_packet_alloc());
    candidate->decodedFrame.reset(av_frame_alloc());
    if (!candidate->packet || !candidate->decodedFrame)
    {
        return false;
    }

    const std::string utf8Path = pathAsUtf8(path);
    candidate->sourcePath = utf8Path;

    AVFormatContext* rawFormat = nullptr;
    if (avformat_open_input(&rawFormat, utf8Path.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }
    candidate->format.reset(rawFormat);

    if (avformat_find_stream_info(candidate->format.get(), nullptr) < 0)
    {
        return false;
    }

    const AVCodec* decoder = nullptr;
    candidate->audioStreamIndex = av_find_best_stream(
        candidate->format.get(),
        AVMEDIA_TYPE_AUDIO,
        -1,
        -1,
        &decoder,
        0);
    if (candidate->audioStreamIndex < 0 || decoder == nullptr)
    {
        if (impl_ != nullptr)
        {
            impl_->status = AudioOpenStatus::NoAudioStream;
        }
        return false;
    }

    const AVStream* stream = candidate->format->streams[
        candidate->audioStreamIndex];
    candidate->codec.reset(avcodec_alloc_context3(decoder));
    if (!candidate->codec
        || avcodec_parameters_to_context(
               candidate->codec.get(),
               stream->codecpar) < 0
        || avcodec_open2(candidate->codec.get(), decoder, nullptr) < 0)
    {
        return false;
    }

    if (candidate->codec->sample_rate <= 0
        || candidate->codec->ch_layout.nb_channels <= 0
        || candidate->codec->sample_fmt == AV_SAMPLE_FMT_NONE)
    {
        return false;
    }

    ChannelLayoutGuard outputLayout{};
    av_channel_layout_default(&outputLayout.value, OutputChannels);

    SwrContext* rawResampler = nullptr;
    if (swr_alloc_set_opts2(
            &rawResampler,
            &outputLayout.value,
            AV_SAMPLE_FMT_S16,
            OutputSampleRate,
            &candidate->codec->ch_layout,
            candidate->codec->sample_fmt,
            candidate->codec->sample_rate,
            0,
            nullptr) < 0
        || rawResampler == nullptr)
    {
        if (rawResampler != nullptr)
        {
            swr_free(&rawResampler);
        }
        return false;
    }
    candidate->resampler.reset(rawResampler);
    if (swr_init(candidate->resampler.get()) < 0)
    {
        return false;
    }

    candidate->sourceSampleRate = candidate->codec->sample_rate;
    candidate->sourceChannels = candidate->codec->ch_layout.nb_channels;
    candidate->sourceSampleFormat = candidate->codec->sample_fmt;

    if (!decodePcm(*candidate))
    {
        return false;
    }

    impl_ = std::move(candidate);
    impl_->status = AudioOpenStatus::Ready;
    impl_->tempoPcm = impl_->pcm;
    return true;
}

bool AudioResource::setTempo(float tempo) noexcept
{
    if (!impl_ || impl_->status != AudioOpenStatus::Ready
        || !validTempo(tempo))
    {
        return false;
    }

    if (std::fabs(tempo - 1.0F) < 0.0001F)
    {
        impl_->tempoPcm = impl_->pcm;
        impl_->tempo = 1.0F;
        return true;
    }

    std::vector<std::int16_t> candidate{};
    if (!buildTempoPcm(impl_->pcm, tempo, candidate))
    {
        return false;
    }

    impl_->tempoPcm = std::move(candidate);
    impl_->tempo = tempo;
    return true;
}

float AudioResource::tempo() const noexcept
{
    return impl_ ? impl_->tempo : 1.0F;
}

AudioOpenStatus AudioResource::openStatus() const noexcept
{
    return impl_ != nullptr ? impl_->status : AudioOpenStatus::NotOpened;
}

const std::int16_t* AudioResource::data() const noexcept
{
    return impl_ && !impl_->tempoPcm.empty() ? impl_->tempoPcm.data() : nullptr;
}

std::size_t AudioResource::sampleCount() const noexcept
{
    return impl_ ? impl_->tempoPcm.size() : 0U;
}

std::size_t AudioResource::frameCount() const noexcept
{
    return impl_ != nullptr && !impl_->tempoPcm.empty()
        ? impl_->tempoPcm.size() / static_cast<std::size_t>(OutputChannels)
        : 0U;
}

double AudioResource::durationSeconds() const noexcept
{
    if (sampleRate() <= 0)
    {
        return 0.0;
    }

    return static_cast<double>(mediaFrameCount())
        / static_cast<double>(sampleRate());
}

std::size_t AudioResource::mediaFrameCount() const noexcept
{
    return impl_ != nullptr && !impl_->pcm.empty()
        ? impl_->pcm.size() / static_cast<std::size_t>(OutputChannels)
        : 0U;
}

double AudioResource::outputDurationSeconds() const noexcept
{
    if (sampleRate() <= 0 || !impl_ || impl_->tempoPcm.empty())
    {
        return 0.0;
    }

    return static_cast<double>(impl_->tempoPcm.size()
        / static_cast<std::size_t>(OutputChannels))
        / static_cast<double>(sampleRate());
}

std::size_t AudioResource::byteSize() const noexcept
{
    if (!impl_ || impl_->tempoPcm.size() > (std::numeric_limits<std::size_t>::max)()
            / sizeof(std::int16_t))
    {
        return 0U;
    }

    return impl_->tempoPcm.size() * sizeof(std::int16_t);
}

int AudioResource::channels() const noexcept
{
    return impl_ && !impl_->tempoPcm.empty() ? OutputChannels : 0;
}

int AudioResource::sampleRate() const noexcept
{
    return impl_ && !impl_->tempoPcm.empty() ? OutputSampleRate : 0;
}

}
