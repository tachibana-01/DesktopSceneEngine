#include "TimelinePreviewPreloadPlanner.h"

#include "Resources/RepresentativeFrameSampling.h"

#include <utility>

namespace DesktopSceneEngine::Player
{

bool TimelinePreviewPreloadPlanner::reconcile(
    const TimelinePreviewSource& source,
    std::int64_t durationMilliseconds,
    std::uint64_t generation
)
{
    if (source.mediaType != WorkspaceMediaType::Video
        || source.source.trimmed().isEmpty()
        || durationMilliseconds <= 0)
    {
        invalidate(generation);
        return false;
    }

    if (generation < generation_)
    {
        return false;
    }

    if (source_.has_value() && *source_ == source
        && durationMilliseconds_ == durationMilliseconds
        && generation_ == generation)
    {
        return false;
    }

    const Resources::RepresentativeFrameSamplingPlan samplingPlan =
        Resources::makeRepresentativeFrameSamplingPlan(durationMilliseconds);
    source_ = source;
    durationMilliseconds_ = durationMilliseconds;
    generation_ = generation;
    timestampsMilliseconds_ = samplingPlan.timestampsMilliseconds;
    nextIndex_ = 0U;
    return !timestampsMilliseconds_.empty();
}

void TimelinePreviewPreloadPlanner::invalidate(std::uint64_t generation) noexcept
{
    if (generation < generation_)
    {
        return;
    }

    source_.reset();
    durationMilliseconds_ = 0;
    generation_ = generation;
    timestampsMilliseconds_.clear();
    nextIndex_ = 0U;
}

std::optional<TimelinePreviewPreloadRequest> TimelinePreviewPreloadPlanner::next() const
{
    if (!hasPendingWork())
    {
        return std::nullopt;
    }

    return TimelinePreviewPreloadRequest{
        *source_,
        durationMilliseconds_,
        generation_,
        timestampsMilliseconds_[nextIndex_]};
}

void TimelinePreviewPreloadPlanner::markCompleted(
    const TimelinePreviewPreloadRequest& request
) noexcept
{
    if (isCurrent(request))
    {
        ++nextIndex_;
    }
}

void TimelinePreviewPreloadPlanner::markSkipped(
    const TimelinePreviewPreloadRequest& request
) noexcept
{
    markCompleted(request);
}

void TimelinePreviewPreloadPlanner::markCancelled(
    const TimelinePreviewPreloadRequest&) noexcept
{
}

bool TimelinePreviewPreloadPlanner::hasPendingWork() const noexcept
{
    return source_.has_value() && nextIndex_ < timestampsMilliseconds_.size();
}

bool TimelinePreviewPreloadPlanner::isCurrent(
    const TimelinePreviewPreloadRequest& request
) const noexcept
{
    return hasPendingWork() && *source_ == request.source
        && durationMilliseconds_ == request.durationMilliseconds
        && generation_ == request.generation
        && timestampsMilliseconds_[nextIndex_] == request.timestampMilliseconds;
}

}
