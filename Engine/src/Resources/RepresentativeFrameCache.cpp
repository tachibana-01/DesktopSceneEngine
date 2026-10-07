#include "RepresentativeFrameCache.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace DesktopSceneEngine::Resources
{

namespace
{

std::string pathAsUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

void appendFingerprintField(std::uint64_t& hash, std::string_view value) noexcept
{
    constexpr std::uint64_t FnvPrime = 1099511628211ULL;
    for (const unsigned char byte : value)
    {
        hash ^= byte;
        hash *= FnvPrime;
    }
}

nlohmann::json toJson(const SourceIdentity& identity)
{
    return nlohmann::json{
        {"normalizedPath", pathAsUtf8(identity.normalizedPath)},
        {"fileSize", identity.fileSize},
        {"lastWriteTimestamp", identity.lastWriteTimestamp}};
}

std::optional<SourceIdentity> sourceIdentityFromJson(const nlohmann::json& document)
{
    if (!document.is_object())
    {
        return std::nullopt;
    }

    const std::string normalizedPath = document.at("normalizedPath").get<std::string>();
    if (normalizedPath.empty())
    {
        return std::nullopt;
    }

    std::u8string utf8Path{};
    utf8Path.resize(normalizedPath.size());
    std::memcpy(utf8Path.data(), normalizedPath.data(), normalizedPath.size());

    return SourceIdentity{
        std::filesystem::path{utf8Path},
        document.at("fileSize").get<std::uintmax_t>(),
        document.at("lastWriteTimestamp").get<std::int64_t>()};
}

bool hasValidGeneratedTimestamps(const std::vector<std::int64_t>& timestamps) noexcept
{
    std::int64_t previous = -1;
    for (const std::int64_t timestamp : timestamps)
    {
        if (timestamp < 0 || timestamp <= previous)
        {
            return false;
        }

        previous = timestamp;
    }

    return true;
}

}

RepresentativeFrameCacheLayout::RepresentativeFrameCacheLayout(
    std::filesystem::path mediaCacheRoot,
    SourceFingerprint fingerprint
)
    : mediaCacheRoot_{std::move(mediaCacheRoot)}
    , fingerprint_{std::move(fingerprint)}
{
}

const std::filesystem::path& RepresentativeFrameCacheLayout::mediaCacheRoot() const noexcept
{
    return mediaCacheRoot_;
}

const SourceFingerprint& RepresentativeFrameCacheLayout::sourceFingerprint() const noexcept
{
    return fingerprint_;
}

std::filesystem::path RepresentativeFrameCacheLayout::cacheDirectory() const
{
    return mediaCacheRoot_ / fingerprint_.value;
}

std::filesystem::path RepresentativeFrameCacheLayout::metadataPath() const
{
    return cacheDirectory() / "metadata.json";
}

std::filesystem::path RepresentativeFrameCacheLayout::framesDirectory() const
{
    return cacheDirectory() / "frames";
}

std::filesystem::path RepresentativeFrameCacheLayout::framePath(std::uint64_t timestampMilliseconds) const
{
    std::ostringstream fileName{};
    fileName << std::setfill('0') << std::setw(9) << timestampMilliseconds << ".png";
    return framesDirectory() / fileName.str();
}

std::optional<SourceIdentity> sourceIdentityForFile(const std::filesystem::path& path) noexcept
{
    if (path.empty())
    {
        return std::nullopt;
    }

    try
    {
        std::error_code error{};
        std::filesystem::path normalizedPath = std::filesystem::absolute(path, error);
        if (error)
        {
            return std::nullopt;
        }

        const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(normalizedPath, error);
        if (!error)
        {
            normalizedPath = canonicalPath;
        }
        else
        {
            error.clear();
            normalizedPath = normalizedPath.lexically_normal();
        }

        if (!std::filesystem::is_regular_file(normalizedPath, error) || error)
        {
            return std::nullopt;
        }

        const std::uintmax_t fileSize = std::filesystem::file_size(normalizedPath, error);
        if (error)
        {
            return std::nullopt;
        }

        const auto lastWriteTime = std::filesystem::last_write_time(normalizedPath, error);
        if (error)
        {
            return std::nullopt;
        }

        const auto ticks = lastWriteTime.time_since_epoch().count();
        if (ticks > static_cast<decltype(ticks)>((std::numeric_limits<std::int64_t>::max)())
            || ticks < static_cast<decltype(ticks)>((std::numeric_limits<std::int64_t>::min)()))
        {
            return std::nullopt;
        }

        return SourceIdentity{
            std::move(normalizedPath),
            fileSize,
            static_cast<std::int64_t>(ticks)};
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return std::nullopt;
    }
}

SourceFingerprint makeSourceFingerprint(const SourceIdentity& identity)
{
    constexpr std::uint64_t FnvOffsetBasis = 14695981039346656037ULL;
    std::uint64_t hash = FnvOffsetBasis;
    appendFingerprintField(hash, pathAsUtf8(identity.normalizedPath));
    appendFingerprintField(hash, std::string_view{"\0", 1U});
    appendFingerprintField(hash, std::to_string(identity.fileSize));
    appendFingerprintField(hash, std::string_view{"\0", 1U});
    appendFingerprintField(hash, std::to_string(identity.lastWriteTimestamp));

    std::ostringstream formatted{};
    formatted << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
    return SourceFingerprint{formatted.str()};
}

std::string serializeRepresentativeFrameCacheMetadata(
    const RepresentativeFrameCacheMetadata& metadata
)
{
    nlohmann::json document{
        {"cacheFormatVersion", metadata.cacheFormatVersion},
        {"samplingPolicyVersion", metadata.samplingPolicyVersion},
        {"sourceIdentity", toJson(metadata.sourceIdentity)},
        {"samplingIntervalMilliseconds", metadata.samplingIntervalMilliseconds},
        {"generatedTimestampsMilliseconds", metadata.generatedTimestampsMilliseconds}};

    if (metadata.durationMilliseconds.has_value())
    {
        document["durationMilliseconds"] = *metadata.durationMilliseconds;
    }

    if (metadata.frameDimensions.has_value())
    {
        document["frameDimensions"] = {
            {"width", metadata.frameDimensions->width},
            {"height", metadata.frameDimensions->height}};
    }

    return document.dump();
}

std::optional<RepresentativeFrameCacheMetadata>
deserializeRepresentativeFrameCacheMetadata(std::string_view serialized) noexcept
{
    try
    {
        const nlohmann::json document = nlohmann::json::parse(serialized);
        if (!document.is_object())
        {
            return std::nullopt;
        }

        const auto sourceIdentity = sourceIdentityFromJson(document.at("sourceIdentity"));
        if (!sourceIdentity.has_value())
        {
            return std::nullopt;
        }

        RepresentativeFrameCacheMetadata metadata{
            document.at("cacheFormatVersion").get<std::int64_t>(),
            document.at("samplingPolicyVersion").get<std::int64_t>(),
            *sourceIdentity,
            std::nullopt,
            std::nullopt,
            document.at("samplingIntervalMilliseconds").get<std::int64_t>(),
            document.at("generatedTimestampsMilliseconds").get<std::vector<std::int64_t>>()};

        if (metadata.cacheFormatVersion <= 0 || metadata.samplingPolicyVersion <= 0
            || metadata.samplingIntervalMilliseconds <= 0
            || !hasValidGeneratedTimestamps(metadata.generatedTimestampsMilliseconds))
        {
            return std::nullopt;
        }

        if (document.contains("durationMilliseconds"))
        {
            const std::int64_t durationMilliseconds =
                document.at("durationMilliseconds").get<std::int64_t>();
            if (durationMilliseconds < 0)
            {
                return std::nullopt;
            }

            metadata.durationMilliseconds = durationMilliseconds;
        }

        if (document.contains("frameDimensions"))
        {
            const nlohmann::json& dimensions = document.at("frameDimensions");
            const RepresentativeFrameDimensions value{
                dimensions.at("width").get<int>(),
                dimensions.at("height").get<int>()};
            if (value.width <= 0 || value.height <= 0)
            {
                return std::nullopt;
            }

            metadata.frameDimensions = value;
        }

        return metadata;
    }
    catch (const nlohmann::json::exception&)
    {
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return std::nullopt;
    }
}

bool isRepresentativeFrameCacheReusable(
    const RepresentativeFrameCacheMetadata& metadata,
    const SourceIdentity& sourceIdentity
) noexcept
{
    return metadata.cacheFormatVersion == CacheFormatVersion
        && metadata.sourceIdentity == sourceIdentity;
}

std::vector<std::int64_t> missingRepresentativeFrameTimestamps(
    const std::vector<std::int64_t>& desiredTimestampsMilliseconds,
    const std::vector<std::int64_t>& generatedTimestampsMilliseconds
)
{
    const std::unordered_set<std::int64_t> generated{
        generatedTimestampsMilliseconds.begin(),
        generatedTimestampsMilliseconds.end()};
    std::vector<std::int64_t> missing{};
    missing.reserve(desiredTimestampsMilliseconds.size());

    for (const std::int64_t timestamp : desiredTimestampsMilliseconds)
    {
        if (!generated.contains(timestamp))
        {
            missing.push_back(timestamp);
        }
    }

    return missing;
}

}
