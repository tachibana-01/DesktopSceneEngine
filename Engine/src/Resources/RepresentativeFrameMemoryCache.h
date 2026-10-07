#pragma once

#include "RepresentativeFrameCache.h"
#include "RepresentativeFramePng.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>

namespace DesktopSceneEngine::Resources
{

struct RepresentativeFrameCacheKey
{
    SourceFingerprint sourceFingerprint{};
    std::int64_t timestampMilliseconds = 0;

    [[nodiscard]] bool operator==(const RepresentativeFrameCacheKey&) const = default;
};

// A small, instance-owned byte-bounded LRU for decoded representative frames.
// It deliberately has no process-wide lifetime and no knowledge of callers.
class RepresentativeFrameMemoryCache final
{
public:

    explicit RepresentativeFrameMemoryCache(std::size_t byteBudget);

    [[nodiscard]] std::optional<RepresentativeFrameRgba> find(
        const RepresentativeFrameCacheKey& key
    );

    // Returns false when frame storage is invalid or the frame is larger than
    // the configured byte budget. In either case existing entries are intact.
    [[nodiscard]] bool insert(
        const RepresentativeFrameCacheKey& key,
        const RepresentativeFrameRgba& frame
    );

    void clear() noexcept;

    [[nodiscard]] std::size_t byteBudget() const noexcept;

    [[nodiscard]] std::size_t byteSize() const noexcept;

    [[nodiscard]] std::size_t frameCount() const noexcept;

private:

    struct KeyHash
    {
        [[nodiscard]] std::size_t operator()(const RepresentativeFrameCacheKey& key) const noexcept;
    };

    struct Entry
    {
        RepresentativeFrameRgba frame{};
        std::size_t byteSize = 0U;
        std::list<RepresentativeFrameCacheKey>::iterator recency{};
    };

    void erase(std::unordered_map<RepresentativeFrameCacheKey, Entry, KeyHash>::iterator entry) noexcept;

    std::size_t byteBudget_ = 0U;
    std::size_t byteSize_ = 0U;
    std::list<RepresentativeFrameCacheKey> recency_{};
    std::unordered_map<RepresentativeFrameCacheKey, Entry, KeyHash> entries_{};
};

}
