#include "RepresentativeFrameMemoryCache.h"

#include <limits>
#include <new>
#include <string_view>

namespace DesktopSceneEngine::Resources
{

namespace
{

[[nodiscard]] std::optional<std::size_t> frameByteSize(
    const RepresentativeFrameRgba& frame
) noexcept
{
    if (frame.width <= 0 || frame.height <= 0)
    {
        return std::nullopt;
    }

    const std::size_t width = static_cast<std::size_t>(frame.width);
    if (width > (std::numeric_limits<std::size_t>::max)() / 4U)
    {
        return std::nullopt;
    }

    const std::size_t minimumStride = width * 4U;
    if (frame.rowStride < minimumStride)
    {
        return std::nullopt;
    }

    const std::size_t height = static_cast<std::size_t>(frame.height);
    if (height > (std::numeric_limits<std::size_t>::max)() / frame.rowStride)
    {
        return std::nullopt;
    }

    const std::size_t requiredBytes = frame.rowStride * height;
    if (frame.rgba.size() < requiredBytes)
    {
        return std::nullopt;
    }

    return frame.rgba.size();
}

[[nodiscard]] std::size_t combineHash(std::size_t seed, std::size_t value) noexcept
{
    return seed ^ (value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U));
}

}

std::size_t RepresentativeFrameMemoryCache::KeyHash::operator()(
    const RepresentativeFrameCacheKey& key
) const noexcept
{
    const std::size_t fingerprint = std::hash<std::string_view>{}(key.sourceFingerprint.value);
    const std::size_t timestamp = std::hash<std::int64_t>{}(key.timestampMilliseconds);
    return combineHash(fingerprint, timestamp);
}

RepresentativeFrameMemoryCache::RepresentativeFrameMemoryCache(std::size_t byteBudget)
    : byteBudget_{byteBudget}
{
}

std::optional<RepresentativeFrameRgba> RepresentativeFrameMemoryCache::find(
    const RepresentativeFrameCacheKey& key
)
{
    const auto entry = entries_.find(key);
    if (entry == entries_.end())
    {
        return std::nullopt;
    }

    recency_.splice(recency_.begin(), recency_, entry->second.recency);
    return entry->second.frame;
}

bool RepresentativeFrameMemoryCache::insert(
    const RepresentativeFrameCacheKey& key,
    const RepresentativeFrameRgba& frame
)
{
    const auto newByteSize = frameByteSize(frame);
    if (!newByteSize.has_value() || *newByteSize > byteBudget_)
    {
        return false;
    }

    const auto existing = entries_.find(key);
    if (existing != entries_.end())
    {
        erase(existing);
    }

    while (!recency_.empty()
           && (byteSize_ > byteBudget_ - *newByteSize))
    {
        const RepresentativeFrameCacheKey& oldestKey = recency_.back();
        const auto oldest = entries_.find(oldestKey);
        if (oldest == entries_.end())
        {
            return false;
        }

        erase(oldest);
    }

    try
    {
        recency_.push_front(key);
        const auto recency = recency_.begin();
        entries_.emplace(key, Entry{frame, *newByteSize, recency});
        byteSize_ += *newByteSize;
        return true;
    }
    catch (const std::bad_alloc&)
    {
        if (!recency_.empty() && recency_.front() == key)
        {
            recency_.pop_front();
        }

        return false;
    }
}

void RepresentativeFrameMemoryCache::clear() noexcept
{
    entries_.clear();
    recency_.clear();
    byteSize_ = 0U;
}

std::size_t RepresentativeFrameMemoryCache::byteBudget() const noexcept
{
    return byteBudget_;
}

std::size_t RepresentativeFrameMemoryCache::byteSize() const noexcept
{
    return byteSize_;
}

std::size_t RepresentativeFrameMemoryCache::frameCount() const noexcept
{
    return entries_.size();
}

void RepresentativeFrameMemoryCache::erase(
    std::unordered_map<RepresentativeFrameCacheKey, Entry, KeyHash>::iterator entry
) noexcept
{
    byteSize_ -= entry->second.byteSize;
    recency_.erase(entry->second.recency);
    entries_.erase(entry);
}

}
