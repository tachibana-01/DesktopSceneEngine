#include "Resources/RepresentativeFrameCache.h"
#include "Resources/RepresentativeFrameSampling.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using DesktopSceneEngine::Resources::RepresentativeFrameCacheLayout;
using DesktopSceneEngine::Resources::RepresentativeFrameCacheMetadata;
using DesktopSceneEngine::Resources::SourceFingerprint;
using DesktopSceneEngine::Resources::SourceIdentity;

constexpr std::int64_t SecondMilliseconds = 1000;
constexpr std::int64_t MinuteMilliseconds = 60 * SecondMilliseconds;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

class TemporaryDirectory final
{
public:

    TemporaryDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("DesktopSceneEngine-RepresentativeFrameCacheTest-" + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:

    std::filesystem::path path_{};
};

bool verifySourceIdentityAndFingerprint()
{
    TemporaryDirectory temporary{};
    const std::filesystem::path sourcePath = temporary.path() / "source.bin";
    {
        std::ofstream source{sourcePath, std::ios::binary};
        source << "abc";
    }

    const auto initial = DesktopSceneEngine::Resources::sourceIdentityForFile(sourcePath);
    if (!require(initial.has_value(), "an existing source file must produce an identity")
        || !require(!DesktopSceneEngine::Resources::sourceIdentityForFile(
                         temporary.path() / "missing.bin")
                         .has_value(),
            "a missing source file must not produce an identity"))
    {
        return false;
    }

    const SourceFingerprint initialFingerprint =
        DesktopSceneEngine::Resources::makeSourceFingerprint(*initial);

    {
        std::ofstream source{sourcePath, std::ios::binary | std::ios::app};
        source << "d";
    }

    const auto resized = DesktopSceneEngine::Resources::sourceIdentityForFile(sourcePath);
    if (!require(resized.has_value() && resized->fileSize != initial->fileSize,
            "source identity must include file size")
        || !require(DesktopSceneEngine::Resources::makeSourceFingerprint(*resized)
                    != initialFingerprint,
            "a size change must change the persistent fingerprint"))
    {
        return false;
    }

    std::error_code error{};
    const auto originalWriteTime = std::filesystem::last_write_time(sourcePath, error);
    std::filesystem::last_write_time(
        sourcePath,
        originalWriteTime + std::chrono::seconds{2},
        error);
    const auto retimestamped = DesktopSceneEngine::Resources::sourceIdentityForFile(sourcePath);
    const auto normalized = DesktopSceneEngine::Resources::sourceIdentityForFile(
        temporary.path() / "." / "source.bin");
    const std::filesystem::path renamedPath = temporary.path() / "renamed-source.bin";
    std::filesystem::copy_file(sourcePath, renamedPath, error);
    std::filesystem::last_write_time(renamedPath, originalWriteTime + std::chrono::seconds{2}, error);
    const auto renamed = DesktopSceneEngine::Resources::sourceIdentityForFile(renamedPath);

    return require(!error,
               "the test must be able to change its temporary file timestamp")
        && require(retimestamped.has_value()
                && retimestamped->lastWriteTimestamp != resized->lastWriteTimestamp,
            "source identity must include the last-write timestamp")
        && require(normalized == retimestamped,
            "normalized path spelling must preserve source identity")
        && require(renamed.has_value() && renamed->normalizedPath != retimestamped->normalizedPath,
            "source identity must retain a normalized path")
        && require(DesktopSceneEngine::Resources::makeSourceFingerprint(*renamed)
                    != DesktopSceneEngine::Resources::makeSourceFingerprint(*retimestamped),
            "a path change must change the persistent fingerprint")
        && require(DesktopSceneEngine::Resources::makeSourceFingerprint(*retimestamped)
                    != DesktopSceneEngine::Resources::makeSourceFingerprint(*resized),
            "a timestamp change must change the persistent fingerprint");
}

bool verifyDeterministicFingerprint()
{
    const SourceIdentity identity{
        std::filesystem::path{"C:/Media/clip.mp4"},
        1234567U,
        987654321LL};

    const SourceFingerprint fingerprint =
        DesktopSceneEngine::Resources::makeSourceFingerprint(identity);
    return require(fingerprint.value == "2213eed17d023ca4",
        "the FNV-1a source fingerprint must remain deterministic");
}

bool verifySamplingPolicyAndTimestamps()
{
    using DesktopSceneEngine::Resources::makeRepresentativeFrameSamplingPlan;

    const auto belowFiveMinutes = makeRepresentativeFrameSamplingPlan(5 * MinuteMilliseconds - 1);
    const auto atFiveMinutes = makeRepresentativeFrameSamplingPlan(5 * MinuteMilliseconds);
    const auto atTenMinutes = makeRepresentativeFrameSamplingPlan(10 * MinuteMilliseconds);
    const auto atTwentyMinutes = makeRepresentativeFrameSamplingPlan(20 * MinuteMilliseconds);
    const auto atThirtyMinutes = makeRepresentativeFrameSamplingPlan(30 * MinuteMilliseconds);
    const auto atSixtyMinutes = makeRepresentativeFrameSamplingPlan(60 * MinuteMilliseconds);
    const auto atOneHundredTwentyMinutes = makeRepresentativeFrameSamplingPlan(120 * MinuteMilliseconds);
    const auto shortPlan = makeRepresentativeFrameSamplingPlan(12 * SecondMilliseconds);
    const auto boundedPlan = makeRepresentativeFrameSamplingPlan(24 * 60 * MinuteMilliseconds);

    return require(belowFiveMinutes.intervalMilliseconds == 5 * SecondMilliseconds,
               "durations below five minutes must sample every five seconds")
        && require(atFiveMinutes.intervalMilliseconds == 10 * SecondMilliseconds,
            "five minutes must begin the ten-second sampling band")
        && require(atTenMinutes.intervalMilliseconds == 15 * SecondMilliseconds,
            "ten minutes must begin the fifteen-second sampling band")
        && require(atTwentyMinutes.intervalMilliseconds == 20 * SecondMilliseconds,
            "twenty minutes must begin the twenty-second sampling band")
        && require(atThirtyMinutes.intervalMilliseconds == 30 * SecondMilliseconds,
            "thirty minutes must begin the thirty-second sampling band")
        && require(atSixtyMinutes.intervalMilliseconds == 60 * SecondMilliseconds,
            "sixty minutes must begin the sixty-second sampling band")
        && require(atOneHundredTwentyMinutes.intervalMilliseconds == 120 * SecondMilliseconds,
            "one hundred twenty minutes must begin the one hundred twenty-second band")
        && require(shortPlan.timestampsMilliseconds
                    == std::vector<std::int64_t>{0, 5 * SecondMilliseconds, 10 * SecondMilliseconds},
            "timestamps must start at zero, avoid exact EOF, and use the selected interval")
        && require(boundedPlan.intervalMilliseconds == 12 * MinuteMilliseconds,
            "the maximum-frame rule must increase the interval in five-second increments")
        && require(boundedPlan.timestampsMilliseconds.size() <= 120U,
            "the maximum-frame rule must bound representative timestamps per source");
}

bool verifyCacheLayoutMetadataAndInvalidation()
{
    using DesktopSceneEngine::Resources::CacheFormatVersion;
    using DesktopSceneEngine::Resources::SamplingPolicyVersion;

    const SourceIdentity identity{
        std::filesystem::path{"C:/Media/clip.mp4"},
        1234567U,
        987654321LL};
    const SourceFingerprint fingerprint =
        DesktopSceneEngine::Resources::makeSourceFingerprint(identity);
    const RepresentativeFrameCacheLayout layout{
        std::filesystem::path{"Cache/Media"},
        fingerprint};
    const RepresentativeFrameCacheMetadata metadata{
        CacheFormatVersion,
        SamplingPolicyVersion,
        identity,
        12000,
        DesktopSceneEngine::Resources::RepresentativeFrameDimensions{1920, 1080},
        5000,
        {0, 5000, 10000}};

    const auto restored = DesktopSceneEngine::Resources::deserializeRepresentativeFrameCacheMetadata(
        DesktopSceneEngine::Resources::serializeRepresentativeFrameCacheMetadata(metadata));
    RepresentativeFrameCacheMetadata policyChanged = metadata;
    ++policyChanged.samplingPolicyVersion;
    RepresentativeFrameCacheMetadata formatChanged = metadata;
    ++formatChanged.cacheFormatVersion;

    return require(layout.metadataPath()
                    == std::filesystem::path{"Cache/Media"} / fingerprint.value / "metadata.json",
               "metadata must have a deterministic source-fingerprint path")
        && require(layout.framePath(5000)
                    == std::filesystem::path{"Cache/Media"} / fingerprint.value / "frames"
                        / "000005000.png",
            "representative frames must use fixed-width millisecond PNG names")
        && require(restored == std::optional<RepresentativeFrameCacheMetadata>{metadata},
            "cache metadata must round-trip through its internal JSON representation")
        && require(!DesktopSceneEngine::Resources::deserializeRepresentativeFrameCacheMetadata(
                         "{\"cacheFormatVersion\": 1}")
                         .has_value(),
            "incomplete cache metadata must be rejected")
        && require(!DesktopSceneEngine::Resources::deserializeRepresentativeFrameCacheMetadata(
                         "not JSON")
                         .has_value(),
            "malformed cache metadata must be rejected")
        && require(DesktopSceneEngine::Resources::isRepresentativeFrameCacheReusable(metadata, identity),
            "matching source identity and cache format must be reusable")
        && require(DesktopSceneEngine::Resources::isRepresentativeFrameCacheReusable(policyChanged, identity),
            "sampling-policy changes alone must not invalidate compatible cache artifacts")
        && require(!DesktopSceneEngine::Resources::isRepresentativeFrameCacheReusable(formatChanged, identity),
            "cache-format changes must invalidate incompatible cache artifacts")
        && require(DesktopSceneEngine::Resources::missingRepresentativeFrameTimestamps(
                       {0, 5000, 10000},
                       {0, 10000})
                    == std::vector<std::int64_t>{5000},
            "only desired timestamps absent from metadata may be marked missing");
}

}

int main()
{
    return verifySourceIdentityAndFingerprint()
            && verifyDeterministicFingerprint()
            && verifySamplingPolicyAndTimestamps()
            && verifyCacheLayoutMetadataAndInvalidation()
        ? 0
        : 1;
}
