#include "Resources/SequentialAudioSource.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace
{

using DesktopSceneEngine::Resources::AudioReadResult;
using DesktopSceneEngine::Resources::AudioReadStatus;
using DesktopSceneEngine::Resources::AudioSourceDiagnostics;
using DesktopSceneEngine::Resources::AudioSourceStatus;
using DesktopSceneEngine::Resources::SequentialAudioSource;

constexpr std::size_t StereoFrameCount = 1003U;
constexpr std::size_t CompressedStereoFrameCount = 96000U;
constexpr std::array<std::int16_t, 5U> LeftPattern{
    -24576, -8192, 4096, 16384, 28672
};
constexpr std::array<std::int16_t, 5U> RightPattern{
    24576, 6144, -12288, -28672, 20480
};
constexpr float Sentinel = -1234.5F;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool matches(
    const AudioReadResult& result,
    std::size_t framesProduced,
    AudioReadStatus status)
{
    return result.framesProduced == framesProduced && result.status == status;
}

template<typename Value>
bool writeLittleEndian(std::ofstream& output, Value value)
{
    for (std::size_t byte = 0U; byte < sizeof(Value); ++byte)
    {
        output.put(static_cast<char>((static_cast<std::uint64_t>(value) >> (byte * 8U)) & 0xFFU));
    }

    return static_cast<bool>(output);
}

bool writePcmWave(
    const std::filesystem::path& path,
    int sampleRate,
    int channels,
    const std::vector<std::int16_t>& samples)
{
    if (sampleRate <= 0 || channels <= 0
        || samples.size() % static_cast<std::size_t>(channels) != 0U
        || samples.size() > (std::numeric_limits<std::uint32_t>::max)() / sizeof(std::int16_t))
    {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        return false;
    }

    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const std::uint32_t riffBytes = 36U + dataBytes;
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * static_cast<int>(sizeof(std::int16_t)));
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate)
        * static_cast<std::uint32_t>(blockAlign);

    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output)
    {
        return false;
    }

    output.write("RIFF", 4);
    return writeLittleEndian(output, riffBytes)
        && static_cast<bool>(output.write("WAVE", 4))
        && static_cast<bool>(output.write("fmt ", 4))
        && writeLittleEndian(output, std::uint32_t{16U})
        && writeLittleEndian(output, std::uint16_t{1U})
        && writeLittleEndian(output, static_cast<std::uint16_t>(channels))
        && writeLittleEndian(output, static_cast<std::uint32_t>(sampleRate))
        && writeLittleEndian(output, byteRate)
        && writeLittleEndian(output, blockAlign)
        && writeLittleEndian(output, std::uint16_t{16U})
        && static_cast<bool>(output.write("data", 4))
        && writeLittleEndian(output, dataBytes)
        && [&output, &samples]()
        {
            for (const std::int16_t sample : samples)
            {
                if (!writeLittleEndian(output, static_cast<std::uint16_t>(sample)))
                {
                    return false;
                }
            }
            return true;
        }();
}

std::vector<std::int16_t> stereoSamples(std::size_t frames)
{
    std::vector<std::int16_t> samples{};
    samples.reserve(frames * 2U);
    for (std::size_t frame = 0U; frame < frames; ++frame)
    {
        samples.push_back(LeftPattern[frame % LeftPattern.size()]);
        samples.push_back(RightPattern[frame % RightPattern.size()]);
    }
    return samples;
}

float expectedLeft(std::size_t frame)
{
    return static_cast<float>(LeftPattern[frame % LeftPattern.size()]) / 32768.0F;
}

float expectedRight(std::size_t frame)
{
    return static_cast<float>(RightPattern[frame % RightPattern.size()]) / 32768.0F;
}

bool approximatelyEqual(float left, float right)
{
    return std::fabs(left - right) <= 0.000002F;
}

bool expectFrame(const std::vector<float>& data, std::size_t outputFrame, std::size_t sourceFrame)
{
    return outputFrame * 2U + 1U < data.size()
        && approximatelyEqual(data[outputFrame * 2U], expectedLeft(sourceFrame))
        && approximatelyEqual(data[outputFrame * 2U + 1U], expectedRight(sourceFrame));
}

bool verifyInitialAndInvalidRequests(const std::filesystem::path& fixture)
{
    SequentialAudioSource unopened{};
    std::array<float, 2U> preOpenDestination{1.0F, 2.0F};
    if (!require(!unopened.isOpen(), "A new source must not be open")
        || !require(unopened.status() == AudioSourceStatus::NotOpened,
            "A new source must report NotOpened")
        || !require(unopened.format().sampleRate == 48000 && unopened.format().channels == 2,
            "The canonical format must be available before open")
        || !require(matches(
                unopened.readFrames(std::span<float>{preOpenDestination}),
                0U,
                AudioReadStatus::NotReady),
            "A valid read before open must report NotReady")
        || !require(matches(
                unopened.readFrames(std::span<float>{}),
                0U,
                AudioReadStatus::InvalidRequest),
            "An empty read must report InvalidRequest")
        || !require(!unopened.reset() && unopened.status() == AudioSourceStatus::NotOpened,
            "Reset without a stored source must remain NotOpened"))
    {
        return false;
    }

    SequentialAudioSource afterInvalid{};
    SequentialAudioSource reference{};
    if (!require(afterInvalid.open(fixture), "The deterministic fixture must open")
        || !require(reference.open(fixture), "The reference fixture must open"))
    {
        return false;
    }

    std::vector<float> odd(3U, Sentinel);
    if (!require(matches(
                afterInvalid.readFrames(std::span<float>{}),
                0U,
                AudioReadStatus::InvalidRequest),
            "An empty read must not advance an opened source")
        || !require(matches(
                afterInvalid.readFrames(std::span<float>{odd}),
                0U,
                AudioReadStatus::InvalidRequest),
            "An odd destination must not advance an opened source")
        || !require(odd == std::vector<float>(3U, Sentinel),
            "Invalid reads must not write to the destination"))
    {
        return false;
    }

    std::vector<float> after(8U, Sentinel);
    std::vector<float> expected(8U, Sentinel);
    const AudioReadResult afterResult = afterInvalid.readFrames(std::span<float>{after});
    const AudioReadResult expectedResult = reference.readFrames(std::span<float>{expected});
    return require(
        matches(afterResult, expectedResult.framesProduced, expectedResult.status)
            && after == expected,
        "Invalid reads must leave the first valid PCM block unchanged");
}

bool verifyVariedBlockReads(const std::filesystem::path& fixture)
{
    constexpr std::array<std::size_t, 7U> blockFrames{1U, 17U, 255U, 5U, 64U, 129U, 31U};

    SequentialAudioSource source{};
    if (!require(source.open(fixture), "The deterministic fixture must open for varied reads"))
    {
        return false;
    }

    std::size_t sourceFrame = 0U;
    std::size_t blockIndex = 0U;
    while (sourceFrame < StereoFrameCount)
    {
        const std::size_t requestedFrames = blockFrames[blockIndex % blockFrames.size()];
        std::vector<float> destination(requestedFrames * 2U, Sentinel);
        const AudioReadResult result = source.readFrames(std::span<float>{destination});
        const std::size_t expectedFrames = (std::min)(
            requestedFrames,
            StereoFrameCount - sourceFrame);
        const AudioReadStatus expectedStatus = expectedFrames < requestedFrames
            ? AudioReadStatus::EndOfStream
            : AudioReadStatus::Data;
        if (!require(matches(result, expectedFrames, expectedStatus),
                "Varied reads must report the exact available sequential frame count"))
        {
            return false;
        }
        for (std::size_t frame = 0U; frame < result.framesProduced; ++frame)
        {
            if (!require(expectFrame(destination, frame, sourceFrame + frame),
                    "Varied reads must concatenate to the original PCM sequence"))
            {
                return false;
            }
        }
        sourceFrame += result.framesProduced;
        ++blockIndex;
    }

    std::vector<float> afterEnd(2U, Sentinel);
    return require(matches(
                source.readFrames(std::span<float>{afterEnd}),
                0U,
                AudioReadStatus::EndOfStream),
        "Varied reads must leave the source at stable EOF");
}

bool verifySequentialReadAndEof(const std::filesystem::path& fixture)
{
    SequentialAudioSource source{};
    if (!require(source.open(fixture), "The deterministic fixture must open")
        || !require(source.isOpen(), "A successful open must create an active session")
        || !require(source.status() == AudioSourceStatus::Ready,
            "A successful open must report Ready"))
    {
        return false;
    }

    std::size_t sourceFrame = 0U;
    for (std::size_t block = 0U; block < 7U; ++block)
    {
        std::vector<float> destination(128U * 2U, Sentinel);
        const AudioReadResult result = source.readFrames(std::span<float>{destination});
        if (!require(matches(result, 128U, AudioReadStatus::Data),
                "A complete 128-frame read must report Data"))
        {
            return false;
        }

        for (std::size_t frame = 0U; frame < result.framesProduced; ++frame)
        {
            if (!require(expectFrame(destination, frame, sourceFrame + frame),
                    "Sequential PCM must preserve source frame order and L/R order"))
            {
                return false;
            }
        }
        sourceFrame += result.framesProduced;
    }

    std::vector<float> finalDestination(128U * 2U, Sentinel);
    const AudioReadResult finalResult = source.readFrames(std::span<float>{finalDestination});
    if (!require(matches(finalResult, 107U, AudioReadStatus::EndOfStream),
            "The final partial block must report its exact frame count and EOF"))
    {
        return false;
    }
    for (std::size_t frame = 0U; frame < finalResult.framesProduced; ++frame)
    {
        if (!require(expectFrame(finalDestination, frame, sourceFrame + frame),
                "The partial final block must preserve source and L/R order"))
        {
            return false;
        }
    }
    for (std::size_t sample = finalResult.framesProduced * 2U;
         sample < finalDestination.size();
         ++sample)
    {
        if (!require(finalDestination[sample] == Sentinel,
                "The source must not overwrite unused caller destination samples"))
        {
            return false;
        }
    }

    std::vector<float> repeated(2U, Sentinel);
    return require(matches(
                source.readFrames(std::span<float>{repeated}),
                0U,
                AudioReadStatus::EndOfStream)
            && matches(
                source.readFrames(std::span<float>{repeated}),
                0U,
                AudioReadStatus::EndOfStream),
        "EOF must be stable across repeated reads");
}

bool verifyExactBoundaryAndReset(const std::filesystem::path& fixture)
{
    SequentialAudioSource source{};
    if (!require(source.open(fixture), "The deterministic fixture must open"))
    {
        return false;
    }

    std::vector<float> exact(StereoFrameCount * 2U, Sentinel);
    if (!require(matches(
                source.readFrames(std::span<float>{exact}),
                StereoFrameCount,
                AudioReadStatus::Data),
            "A physical EOF exactly at caller capacity must return Data")
        || !require(expectFrame(exact, 0U, 0U)
            && expectFrame(exact, StereoFrameCount - 1U, StereoFrameCount - 1U),
            "The exact-boundary read must contain the complete PCM range"))
    {
        return false;
    }

    std::vector<float> next(2U, Sentinel);
    if (!require(matches(
                source.readFrames(std::span<float>{next}),
                0U,
                AudioReadStatus::EndOfStream),
            "The read after exact physical EOF must return EndOfStream")
        || !require(source.reset(), "Reset must reopen the stored source path")
        || !require(source.isOpen() && source.status() == AudioSourceStatus::Ready,
            "A successful reset must create a fresh Ready session"))
    {
        return false;
    }

    std::vector<float> beginning(2U, Sentinel);
    return require(matches(
                source.readFrames(std::span<float>{beginning}),
                1U,
                AudioReadStatus::Data)
            && approximatelyEqual(beginning[0], expectedLeft(0U))
            && approximatelyEqual(beginning[1], expectedRight(0U)),
        "Reset must reproduce the beginning of the canonical stream");
}

bool verifyFailureBehavior(
    const std::filesystem::path& fixture,
    const std::filesystem::path& temporaryDirectory)
{
    const std::filesystem::path missing = temporaryDirectory / "missing.wav";
    const std::filesystem::path corrupt = temporaryDirectory / "corrupt.bin";
    {
        std::ofstream output{corrupt, std::ios::binary | std::ios::trunc};
        output << "not a media stream";
    }

    SequentialAudioSource source{};
    std::vector<float> destination(2U, Sentinel);
    if (!require(source.open(fixture), "A valid source must open before destructive replacement")
        || !require(!source.open(missing), "A missing source must fail to open")
        || !require(!source.isOpen(), "A failed open must discard any source session")
        || !require(source.status() == AudioSourceStatus::OpenFailure,
            "A missing file must map to OpenFailure")
        || !require(matches(
                source.readFrames(std::span<float>{destination}),
                0U,
                AudioReadStatus::Failure),
            "A valid read after an open failure must report stable Failure")
        || !require(matches(
                source.readFrames(std::span<float>{destination}),
                0U,
                AudioReadStatus::Failure),
            "A terminal failure must not resume work on subsequent reads")
        || !require(!source.reset(), "Reset must destructively retry the stored missing path")
        || !require(matches(
                source.readFrames(std::span<float>{destination}),
                0U,
                AudioReadStatus::Failure),
            "A failed reset must remain terminal until a later open"))
    {
        return false;
    }

    SequentialAudioSource corruptSource{};
    return require(!corruptSource.open(corrupt)
            && corruptSource.status() != AudioSourceStatus::Ready
            && matches(
                corruptSource.readFrames(std::span<float>{destination}),
                0U,
                AudioReadStatus::Failure),
        "Corrupt input must report a non-EOF terminal failure");
}

bool verifyNormalizationAndBoundedMemory(
    const std::filesystem::path& temporaryDirectory,
    std::size_t& peakPendingFrames)
{
    const std::filesystem::path monoPath = temporaryDirectory / "mono44100.wav";
    std::vector<std::int16_t> mono{};
    mono.reserve(1001U);
    for (std::size_t frame = 0U; frame < 1001U; ++frame)
    {
        mono.push_back(static_cast<std::int16_t>((frame % 2U == 0U) ? 12000 : -12000));
    }
    if (!require(writePcmWave(monoPath, 44100, 1, mono), "The temporary mono fixture must be created"))
    {
        return false;
    }

    SequentialAudioSource monoSource{};
    std::vector<float> normalized(2048U * 2U, Sentinel);
    const AudioReadResult normalization = monoSource.open(monoPath)
        ? monoSource.readFrames(std::span<float>{normalized})
        : AudioReadResult{};
    if (!require(normalization.framesProduced > 0U
            && (normalization.status == AudioReadStatus::Data
                || normalization.status == AudioReadStatus::EndOfStream),
            "44.1 kHz mono input must produce canonical output")
        || !require(monoSource.format().sampleRate == 48000 && monoSource.format().channels == 2,
            "The normalization output format must be 48 kHz stereo"))
    {
        return false;
    }
    for (std::size_t frame = 0U; frame < normalization.framesProduced; ++frame)
    {
        if (!require(std::isfinite(normalized[frame * 2U])
                && approximatelyEqual(normalized[frame * 2U], normalized[frame * 2U + 1U]),
                "Mono normalization must duplicate each source sample into L/R"))
        {
            return false;
        }
    }

    const std::filesystem::path longPath = temporaryDirectory / "ten-seconds.wav";
    if (!require(writePcmWave(longPath, 48000, 2, stereoSamples(480000U)),
            "The temporary long fixture must be created"))
    {
        return false;
    }

    SequentialAudioSource longSource{};
    if (!require(longSource.open(longPath), "The temporary long fixture must open"))
    {
        return false;
    }
    while (true)
    {
        std::vector<float> block(127U * 2U, Sentinel);
        const AudioReadResult result = longSource.readFrames(std::span<float>{block});
        if (result.status == AudioReadStatus::EndOfStream)
        {
            break;
        }
        if (!require(result.status == AudioReadStatus::Data && result.framesProduced > 0U,
                "A long sequential source must continue producing bounded blocks"))
        {
            return false;
        }
    }

    peakPendingFrames = longSource.diagnostics().peakPendingFrames;
    return require(peakPendingFrames * 2U * sizeof(float) <= 1024U * 1024U,
        "The retained pending PCM must remain within the 1 MiB safety cap");
}

struct DecodedCompressedStream
{
    std::vector<float> samples{};
    std::size_t peakPendingFrames = 0U;
    AudioSourceDiagnostics diagnostics{};
    bool observedPartialEndOfStream = false;
};

bool decodeCompressedStream(
    const std::filesystem::path& fixture,
    std::size_t requestedFrames,
    DecodedCompressedStream& decoded)
{
    SequentialAudioSource source{};
    if (!require(source.open(fixture), "A compressed fixture must open"))
    {
        return false;
    }
    if (!require(source.format().sampleRate == 48000 && source.format().channels == 2,
            "Compressed decoding must expose canonical 48 kHz stereo output"))
    {
        return false;
    }

    bool reachedEnd = false;
    while (!reachedEnd)
    {
        std::vector<float> destination(requestedFrames * 2U, Sentinel);
        const AudioReadResult result = source.readFrames(std::span<float>{destination});
        if (result.status == AudioReadStatus::Failure
            || result.status == AudioReadStatus::NotReady
            || result.status == AudioReadStatus::InvalidRequest)
        {
            return require(false, "Compressed reads must not enter a failure state");
        }
        if (result.status == AudioReadStatus::Data && result.framesProduced == 0U)
        {
            return require(false, "Compressed data reads must make progress");
        }
        if (result.framesProduced > requestedFrames)
        {
            return require(false, "Compressed reads must respect the caller frame capacity");
        }

        const std::size_t producedSamples = result.framesProduced * 2U;
        for (std::size_t sample = 0U; sample < producedSamples; ++sample)
        {
            if (!require(std::isfinite(destination[sample]),
                    "Compressed PCM must contain only finite float samples"))
            {
                return false;
            }
        }
        decoded.samples.insert(
            decoded.samples.end(),
            destination.begin(),
            destination.begin() + static_cast<std::ptrdiff_t>(producedSamples));

        if (result.status == AudioReadStatus::EndOfStream)
        {
            decoded.observedPartialEndOfStream = result.framesProduced > 0U;
            reachedEnd = true;
        }
    }

    decoded.diagnostics = source.diagnostics();
    decoded.peakPendingFrames = decoded.diagnostics.peakPendingFrames;
    if (!require(decoded.peakPendingFrames * 2U * sizeof(float) <= 1024U * 1024U,
            "Compressed decoding must retain no more than 1 MiB of pending PCM"))
    {
        return false;
    }

    std::vector<float> terminal(2U, Sentinel);
    return require(matches(
                source.readFrames(std::span<float>{terminal}),
                0U,
                AudioReadStatus::EndOfStream)
            && matches(
                source.readFrames(std::span<float>{terminal}),
                0U,
                AudioReadStatus::EndOfStream),
        "Compressed EOF must remain stable after all decoded PCM is consumed");
}

bool streamsMatch(
    const std::vector<float>& left,
    const std::vector<float>& right,
    float tolerance)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t sample = 0U; sample < left.size(); ++sample)
    {
        if (std::fabs(left[sample] - right[sample]) > tolerance)
        {
            return false;
        }
    }
    return true;
}

bool verifiesStereoDistinction(const std::vector<float>& samples)
{
    for (std::size_t sample = 0U; sample + 1U < samples.size(); sample += 2U)
    {
        if (std::fabs(samples[sample] - samples[sample + 1U]) > 0.01F)
        {
            return true;
        }
    }
    return false;
}

bool verifyCompressedReset(const std::filesystem::path& fixture)
{
    SequentialAudioSource source{};
    if (!require(source.open(fixture), "A compressed fixture must open before reset"))
    {
        return false;
    }

    constexpr std::size_t PrefixFrames = 1024U;
    auto readPrefix = [&source]()
    {
        std::vector<float> prefix{};
        while (prefix.size() < PrefixFrames * 2U)
        {
            std::vector<float> destination(257U * 2U, Sentinel);
            const AudioReadResult result = source.readFrames(std::span<float>{destination});
            if (result.status != AudioReadStatus::Data || result.framesProduced == 0U)
            {
                return std::vector<float>{};
            }
            prefix.insert(
                prefix.end(),
                destination.begin(),
                destination.begin() + static_cast<std::ptrdiff_t>(result.framesProduced * 2U));
        }
        prefix.resize(PrefixFrames * 2U);
        return prefix;
    };

    const std::vector<float> initial = readPrefix();
    if (!require(initial.size() == PrefixFrames * 2U,
            "Compressed input must produce a nontrivial prefix before reset")
        || !require(source.reset(), "Compressed reset must recreate a decoder session"))
    {
        return false;
    }

    const std::vector<float> afterReset = readPrefix();
    return require(
        streamsMatch(initial, afterReset, 0.000001F),
        "Compressed reset must reproduce the same decoded stream prefix");
}

bool verifyFlacCompressedFixture(
    const std::filesystem::path& fixture,
    AudioSourceDiagnostics& diagnostics)
{
    constexpr std::array<std::size_t, 4U> PullSizes{1U, 17U, 511U, 4096U};
    DecodedCompressedStream reference{};
    if (!decodeCompressedStream(fixture, PullSizes.front(), reference)
        || !require(reference.samples.size() == CompressedStereoFrameCount * 2U,
            "FLAC must retain every deterministic source frame"))
    {
        return false;
    }
    diagnostics = reference.diagnostics;
    for (std::size_t frame = 0U; frame < CompressedStereoFrameCount; ++frame)
    {
        if (!require(expectFrame(reference.samples, frame, frame),
                "FLAC must preserve deterministic sample ordering and L/R channels"))
        {
            return false;
        }
    }

    for (std::size_t index = 1U; index < PullSizes.size(); ++index)
    {
        DecodedCompressedStream candidate{};
        if (!decodeCompressedStream(fixture, PullSizes[index], candidate)
            || !require(candidate.observedPartialEndOfStream,
                "FLAC must deliver its final partial caller block with EndOfStream")
            || !require(streamsMatch(reference.samples, candidate.samples, 0.000002F),
                "FLAC output must be independent of caller pull block size"))
        {
            return false;
        }
    }

    return verifyCompressedReset(fixture);
}

bool verifyMp3CompressedFixture(
    const std::filesystem::path& fixture,
    std::size_t& decodedFrameCount,
    std::size_t& peakPendingFrames,
    AudioSourceDiagnostics& diagnostics)
{
    constexpr std::array<std::size_t, 4U> PullSizes{1U, 128U, 1024U, 4096U};
    DecodedCompressedStream reference{};
    if (!decodeCompressedStream(fixture, PullSizes.front(), reference)
        || !require(!reference.samples.empty(), "MP3 must produce decoded PCM")
        || !require(verifiesStereoDistinction(reference.samples),
            "MP3 must retain a distinguishable left/right signal"))
    {
        return false;
    }

    decodedFrameCount = reference.samples.size() / 2U;
    peakPendingFrames = reference.peakPendingFrames;
    diagnostics = reference.diagnostics;
    for (std::size_t index = 1U; index < PullSizes.size(); ++index)
    {
        DecodedCompressedStream candidate{};
        if (!decodeCompressedStream(fixture, PullSizes[index], candidate)
            || !require(candidate.observedPartialEndOfStream,
                "MP3 must deliver its final partial caller block with EndOfStream")
            || !require(streamsMatch(reference.samples, candidate.samples, 0.000001F),
                "MP3 output must be independent of caller pull block size"))
        {
            return false;
        }
    }

    return verifyCompressedReset(fixture);
}

}

int main()
{
    std::error_code error;
    const std::filesystem::path temporaryDirectory = std::filesystem::temp_directory_path(error)
        / "DesktopSceneEngine_SequentialAudioSourceTest";
    if (error)
    {
        std::cerr << "Test failed: unable to determine a temporary directory" << std::endl;
        return 1;
    }
    std::filesystem::remove_all(temporaryDirectory, error);
    error.clear();

    const std::filesystem::path fixture = std::filesystem::path{
        DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR
    } / "SequentialAudioSourceStereo48k.wav";
    const std::filesystem::path flacFixture = std::filesystem::path{
        DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR
    } / "SequentialAudioSourceCompressed.flac";
    const std::filesystem::path mp3Fixture = std::filesystem::path{
        DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR
    } / "SequentialAudioSourceCompressed.mp3";
    // Both compressed fixtures were generated offline from 96,000 frames of
    // the deterministic 48 kHz stereoSamples pattern above, using the
    // project-declared FFmpeg FLAC and MP3 encoders. They contain no external
    // or copyrighted media.
    std::size_t peakPendingFrames = 0U;
    std::size_t mp3DecodedFrameCount = 0U;
    std::size_t mp3PeakPendingFrames = 0U;
    AudioSourceDiagnostics flacDiagnostics{};
    AudioSourceDiagnostics mp3Diagnostics{};
    const bool success = require(
            std::filesystem::is_regular_file(fixture),
            "The committed deterministic stereo fixture must be present")
        && require(std::filesystem::is_regular_file(flacFixture),
            "The committed deterministic FLAC fixture must be present")
        && require(std::filesystem::is_regular_file(mp3Fixture),
            "The committed deterministic MP3 fixture must be present")
        && verifyInitialAndInvalidRequests(fixture)
        && verifySequentialReadAndEof(fixture)
        && verifyVariedBlockReads(fixture)
        && verifyExactBoundaryAndReset(fixture)
        && verifyFailureBehavior(fixture, temporaryDirectory)
        && verifyNormalizationAndBoundedMemory(temporaryDirectory, peakPendingFrames)
        && verifyFlacCompressedFixture(flacFixture, flacDiagnostics)
        && verifyMp3CompressedFixture(
            mp3Fixture,
            mp3DecodedFrameCount,
            mp3PeakPendingFrames,
            mp3Diagnostics);

    std::filesystem::remove_all(temporaryDirectory, error);
    if (success)
    {
        std::cout << "wavPeakPendingFrames=" << peakPendingFrames
                  << " flacPeakPendingFrames=" << flacDiagnostics.peakPendingFrames
                  << " flacSendPacketEagainCount=" << flacDiagnostics.sendPacketEagainCount
                  << " flacDecoderFramesDuringDrain=" << flacDiagnostics.decoderFramesDuringDrain
                  << " flacResamplerFramesDuringDrain=" << flacDiagnostics.resamplerFramesDuringDrain
                  << " mp3DecodedFrameCount=" << mp3DecodedFrameCount
                  << " mp3PeakPendingFrames=" << mp3PeakPendingFrames
                  << " mp3SendPacketEagainCount=" << mp3Diagnostics.sendPacketEagainCount
                  << " mp3DecoderFramesDuringDrain=" << mp3Diagnostics.decoderFramesDuringDrain
                  << " mp3ResamplerFramesDuringDrain=" << mp3Diagnostics.resamplerFramesDuringDrain
                  << std::endl;
    }
    return success ? 0 : 1;
}
