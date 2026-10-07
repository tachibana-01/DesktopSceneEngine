#include "TimelinePreviewPreloadPlanner.h"

#include "Resources/RepresentativeFrameSampling.h"

#include <iostream>

namespace
{

using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::TimelinePreviewPreloadPlanner;
using DesktopSceneEngine::Player::TimelinePreviewSource;
using DesktopSceneEngine::Player::WorkspaceMediaType;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

TimelinePreviewSource makeSource(
    const QString& source,
    WorkspaceMediaType mediaType
)
{
    return {ItemId{QUuid::createUuid()}, source, mediaType};
}

bool verifySamplingAndVideoOnly()
{
    constexpr std::int64_t DurationMilliseconds = 22'000;
    const TimelinePreviewSource video = makeSource(
        QStringLiteral("video.mp4"), WorkspaceMediaType::Video);
    const TimelinePreviewSource image = makeSource(
        QStringLiteral("image.png"), WorkspaceMediaType::Image);

    TimelinePreviewPreloadPlanner planner{};
    const bool configured = planner.reconcile(video, DurationMilliseconds, 1U);
    const auto first = planner.next();
    const auto expected = DesktopSceneEngine::Resources::makeRepresentativeFrameSamplingPlan(
        DurationMilliseconds);
    std::vector<std::int64_t> actualTimestamps{};
    while (const auto request = planner.next())
    {
        actualTimestamps.push_back(request->timestampMilliseconds);
        planner.markCompleted(*request);
    }

    TimelinePreviewPreloadPlanner imagePlanner{};
    const bool imageConfigured = imagePlanner.reconcile(image, DurationMilliseconds, 1U);
    TimelinePreviewPreloadPlanner invalidDurationPlanner{};
    const bool invalidDurationConfigured = invalidDurationPlanner.reconcile(video, 0, 1U);
    TimelinePreviewPreloadPlanner emptySourcePlanner{};
    TimelinePreviewSource emptySource = video;
    emptySource.source.clear();
    const bool emptySourceConfigured = emptySourcePlanner.reconcile(
        emptySource, DurationMilliseconds, 1U);

    return require(configured,
               "A valid video target must create preload work")
        && require(first.has_value() && first->timestampMilliseconds == 0,
               "The first preload timestamp must be zero")
        && require(actualTimestamps == expected.timestampsMilliseconds,
               "Planner timestamps must exactly reuse the committed sampling plan")
        && require(!actualTimestamps.empty() && actualTimestamps.front() == 0
                && actualTimestamps.back() < DurationMilliseconds,
               "Planner timestamps must begin at zero and exclude exact EOF")
        && require(!imageConfigured && !imagePlanner.hasPendingWork(),
               "Image targets must not create preload work")
        && require(!invalidDurationConfigured && !invalidDurationPlanner.hasPendingWork(),
               "Nonpositive durations must not create preload work")
        && require(!emptySourceConfigured && !emptySourcePlanner.hasPendingWork(),
               "Empty sources must not create preload work");
}

bool verifyDedupCancellationFailureAndSourceSwitch()
{
    constexpr std::int64_t DurationMilliseconds = 20'000;
    const TimelinePreviewSource sourceA = makeSource(
        QStringLiteral("a.mp4"), WorkspaceMediaType::Video);
    const TimelinePreviewSource sourceB = makeSource(
        QStringLiteral("b.mp4"), WorkspaceMediaType::Video);

    TimelinePreviewPreloadPlanner planner{};
    if (!require(planner.reconcile(sourceA, DurationMilliseconds, 10U),
            "The first source should configure a plan"))
    {
        return false;
    }

    const auto first = planner.next();
    if (!require(first.has_value(), "The configured source must expose its first timestamp"))
    {
        return false;
    }

    planner.markCompleted(*first);
    const auto afterComplete = planner.next();
    const bool deduplicated = !planner.reconcile(sourceA, DurationMilliseconds, 10U);
    const auto afterDedup = planner.next();
    planner.markCancelled(*afterDedup);
    const auto afterCancelled = planner.next();
    planner.markSkipped(*afterCancelled);
    const auto afterSkipped = planner.next();

    const bool switched = planner.reconcile(sourceB, DurationMilliseconds, 11U);
    const auto firstB = planner.next();
    planner.markCompleted(*afterSkipped);
    const auto afterStaleCompletion = planner.next();

    return require(deduplicated,
               "An unchanged target must be recognized as already configured")
        && require(afterComplete.has_value() && afterDedup.has_value()
                && afterComplete->timestampMilliseconds == afterDedup->timestampMilliseconds,
               "An unchanged target must not reset planner progress")
        && require(afterCancelled.has_value()
                && afterCancelled->timestampMilliseconds == afterDedup->timestampMilliseconds,
               "Cancelled preload work must remain eligible")
        && require(afterSkipped.has_value()
                && afterSkipped->timestampMilliseconds > afterCancelled->timestampMilliseconds,
               "Unavailable preload work must be skipped for the current pass")
        && require(switched && firstB.has_value() && firstB->source == sourceB
                && firstB->generation == 11U && firstB->timestampMilliseconds == 0,
               "A source switch must replace the old generation with a fresh plan")
        && require(afterStaleCompletion.has_value() && afterStaleCompletion->source == sourceB
                && afterStaleCompletion->timestampMilliseconds == 0,
               "A stale source completion must not advance the replacement plan");
}

}

int main()
{
    return verifySamplingAndVideoOnly() && verifyDedupCancellationFailureAndSourceSwitch()
        ? 0
        : 1;
}
