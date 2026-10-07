#pragma once

#include "TimelinePreviewSource.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace DesktopSceneEngine::Player
{

struct TimelinePreviewPreloadRequest
{
    TimelinePreviewSource source{};
    std::int64_t durationMilliseconds = 0;
    std::uint64_t generation = 0U;
    std::int64_t timestampMilliseconds = 0;

    friend bool operator==(const TimelinePreviewPreloadRequest&, const TimelinePreviewPreloadRequest&) = default;
};

class TimelinePreviewPreloadPlanner final
{
public:

    [[nodiscard]] bool reconcile(
        const TimelinePreviewSource& source,
        std::int64_t durationMilliseconds,
        std::uint64_t generation
    );

    void invalidate(std::uint64_t generation) noexcept;

    [[nodiscard]] std::optional<TimelinePreviewPreloadRequest> next() const;

    void markCompleted(const TimelinePreviewPreloadRequest& request) noexcept;
    void markSkipped(const TimelinePreviewPreloadRequest& request) noexcept;
    void markCancelled(const TimelinePreviewPreloadRequest& request) noexcept;

    [[nodiscard]] bool hasPendingWork() const noexcept;

private:

    bool isCurrent(const TimelinePreviewPreloadRequest& request) const noexcept;

    std::optional<TimelinePreviewSource> source_{};
    std::int64_t durationMilliseconds_ = 0;
    std::uint64_t generation_ = 0U;
    std::vector<std::int64_t> timestampsMilliseconds_{};
    std::size_t nextIndex_ = 0U;
};

}
