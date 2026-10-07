#pragma once

#include "RepresentativeFrameCache.h"
#include "RepresentativeFrameMemoryCache.h"
#include "RepresentativeFramePng.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace DesktopSceneEngine::Resources
{

inline constexpr std::size_t DefaultRepresentativeFrameHotCacheByteBudget = 32U * 1024U * 1024U;

enum class RepresentativeFrameGenerationStatus
{
    Ready,
    Cancelled,
    Unavailable
};

struct RepresentativeFrameGenerationResult
{
    RepresentativeFrameGenerationStatus status = RepresentativeFrameGenerationStatus::Unavailable;
    RepresentativeFrameRgba frame{};
    std::optional<std::int64_t> durationMilliseconds{};
};

using RepresentativeFrameGenerationCallback = std::function<RepresentativeFrameGenerationResult(
    const SourceIdentity& source,
    std::int64_t timestampMilliseconds,
    const std::atomic_bool* cancellationRequested)>;

enum class RepresentativeFrameStoreStatus
{
    RamHit,
    DiskHit,
    Generated,
    Cancelled,
    SourceChanged,
    DecodeUnavailable,
    InvalidRequest
};

struct RepresentativeFrameStoreResult
{
    RepresentativeFrameStoreStatus status = RepresentativeFrameStoreStatus::InvalidRequest;
    RepresentativeFrameRgba frame{};
    // A decoded/readable frame remains usable when the cache persistence or
    // metadata inventory update fails.
    bool persistenceFailed = false;
};

// Coordinates one synchronous Resources-local representative-frame cache. It
// owns its RAM LRU and one isolated preview decoder; it does not know Player,
// Timeline, active playback, ItemId, or presentation state.
class RepresentativeFrameStore final
{
public:

    explicit RepresentativeFrameStore(
        std::filesystem::path mediaCacheRoot,
        std::size_t hotCacheByteBudget = DefaultRepresentativeFrameHotCacheByteBudget
    );

    RepresentativeFrameStore(
        std::filesystem::path mediaCacheRoot,
        std::size_t hotCacheByteBudget,
        RepresentativeFrameGenerationCallback generator
    );

    ~RepresentativeFrameStore();

    RepresentativeFrameStore(const RepresentativeFrameStore&) = delete;
    RepresentativeFrameStore& operator=(const RepresentativeFrameStore&) = delete;
    RepresentativeFrameStore(RepresentativeFrameStore&&) noexcept;
    RepresentativeFrameStore& operator=(RepresentativeFrameStore&&) noexcept;

    [[nodiscard]] RepresentativeFrameStoreResult loadOrGenerate(
        const std::filesystem::path& sourcePath,
        std::int64_t timestampMilliseconds,
        const std::atomic_bool* cancellationRequested = nullptr
    ) noexcept;

    [[nodiscard]] std::size_t hotCacheByteSize() const noexcept;

    [[nodiscard]] std::size_t hotCacheFrameCount() const noexcept;

private:

    struct Impl;

    std::unique_ptr<Impl> impl_{};
};

}
