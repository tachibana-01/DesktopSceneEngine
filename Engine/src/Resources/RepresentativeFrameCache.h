#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace DesktopSceneEngine::Resources
{

inline constexpr std::int64_t CacheFormatVersion = 1;
inline constexpr std::int64_t SamplingPolicyVersion = 1;

struct SourceIdentity
{
    std::filesystem::path normalizedPath{};
    std::uintmax_t fileSize = 0;
    std::int64_t lastWriteTimestamp = 0;

    [[nodiscard]] bool operator==(const SourceIdentity&) const = default;
};

struct SourceFingerprint
{
    std::string value{};

    [[nodiscard]] bool operator==(const SourceFingerprint&) const = default;
};

struct RepresentativeFrameDimensions
{
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(const RepresentativeFrameDimensions&) const = default;
};

struct RepresentativeFrameCacheMetadata
{
    std::int64_t cacheFormatVersion = CacheFormatVersion;
    std::int64_t samplingPolicyVersion = SamplingPolicyVersion;
    SourceIdentity sourceIdentity{};
    std::optional<std::int64_t> durationMilliseconds{};
    std::optional<RepresentativeFrameDimensions> frameDimensions{};
    std::int64_t samplingIntervalMilliseconds = 0;
    std::vector<std::int64_t> generatedTimestampsMilliseconds{};

    [[nodiscard]] bool operator==(const RepresentativeFrameCacheMetadata&) const = default;
};

class RepresentativeFrameCacheLayout final
{
public:

    RepresentativeFrameCacheLayout(std::filesystem::path mediaCacheRoot, SourceFingerprint fingerprint);

    [[nodiscard]] const std::filesystem::path& mediaCacheRoot() const noexcept;

    [[nodiscard]] const SourceFingerprint& sourceFingerprint() const noexcept;

    [[nodiscard]] std::filesystem::path cacheDirectory() const;

    [[nodiscard]] std::filesystem::path metadataPath() const;

    [[nodiscard]] std::filesystem::path framesDirectory() const;

    [[nodiscard]] std::filesystem::path framePath(std::uint64_t timestampMilliseconds) const;

private:

    std::filesystem::path mediaCacheRoot_{};
    SourceFingerprint fingerprint_{};
};

[[nodiscard]] std::optional<SourceIdentity> sourceIdentityForFile(
    const std::filesystem::path& path
) noexcept;

[[nodiscard]] SourceFingerprint makeSourceFingerprint(const SourceIdentity& identity);

[[nodiscard]] std::string serializeRepresentativeFrameCacheMetadata(
    const RepresentativeFrameCacheMetadata& metadata
);

[[nodiscard]] std::optional<RepresentativeFrameCacheMetadata>
deserializeRepresentativeFrameCacheMetadata(std::string_view serialized) noexcept;

[[nodiscard]] bool isRepresentativeFrameCacheReusable(
    const RepresentativeFrameCacheMetadata& metadata,
    const SourceIdentity& sourceIdentity
) noexcept;

[[nodiscard]] std::vector<std::int64_t> missingRepresentativeFrameTimestamps(
    const std::vector<std::int64_t>& desiredTimestampsMilliseconds,
    const std::vector<std::int64_t>& generatedTimestampsMilliseconds
);

}
