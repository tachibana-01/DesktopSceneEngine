#include "TimelinePreviewWorker.h"

#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <limits>
#include <utility>

namespace DesktopSceneEngine::Player
{

bool isTimelinePreviewTimingOrdered(const TimelinePreviewTiming& timing) noexcept
{
    return timing.hoverEvent.has_value() && timing.requestSubmitted.has_value()
        && timing.workerStarted.has_value() && timing.providerReturned.has_value()
        && timing.previewReadyEmitted.has_value() && timing.guiApplied.has_value()
        && *timing.hoverEvent <= *timing.requestSubmitted
        && *timing.requestSubmitted <= *timing.workerStarted
        && *timing.workerStarted <= *timing.providerReturned
        && *timing.providerReturned <= *timing.previewReadyEmitted
        && *timing.previewReadyEmitted <= *timing.guiApplied;
}

bool shouldApplyTimelinePreviewResult(
    const TimelinePreviewRequest& request,
    const TimelinePreviewResultContext& context
) noexcept
{
    return context.hoverActive && context.source.has_value()
        && *context.source == request.source
        && context.generation == request.generation
        && context.newestRequestSerial == request.serial;
}

bool TimelinePreviewRequestState::submit(const TimelinePreviewRequest& request) noexcept
{
    std::scoped_lock lock{mutex_};
    if (!acceptingRequests_)
    {
        return false;
    }

    ++diagnostics_.submitted;
    if (latestRequest_.has_value())
    {
        ++diagnostics_.superseded;
    }
    latestRequest_ = request;
    diagnostics_.maximumPendingCount = (std::max)(
        diagnostics_.maximumPendingCount,
        std::uint64_t{1U});

    if (workScheduled_)
    {
        cancellationRequested_.store(true, std::memory_order_release);
        return false;
    }

    workScheduled_ = true;
    return true;
}

std::optional<TimelinePreviewRequest> TimelinePreviewRequestState::takeLatest() noexcept
{
    std::scoped_lock lock{mutex_};
    if (!acceptingRequests_ || !latestRequest_.has_value())
    {
        return std::nullopt;
    }

    ++diagnostics_.decodesStarted;
    cancellationRequested_.store(false, std::memory_order_release);
    return std::exchange(latestRequest_, std::nullopt);
}

bool TimelinePreviewRequestState::finishDecode(TimelinePreviewDecodeOutcome outcome) noexcept
{
    std::scoped_lock lock{mutex_};
    if (outcome == TimelinePreviewDecodeOutcome::Completed)
    {
        ++diagnostics_.decodesCompleted;
    }
    else if (outcome == TimelinePreviewDecodeOutcome::Cancelled)
    {
        ++diagnostics_.decodesCancelled;
    }
    if (!acceptingRequests_ || !latestRequest_.has_value())
    {
        workScheduled_ = false;
        return false;
    }

    return true;
}

bool TimelinePreviewRequestState::beginPreload(std::uint64_t generation) noexcept
{
    std::scoped_lock lock{mutex_};
    if (!acceptingRequests_ || workScheduled_ || latestRequest_.has_value()
        || generation < minimumPreloadGeneration_)
    {
        return false;
    }

    workScheduled_ = true;
    cancellationRequested_.store(false, std::memory_order_release);
    ++diagnostics_.preloadsStarted;
    return true;
}

bool TimelinePreviewRequestState::finishPreload(
    TimelinePreviewPreloadOutcome outcome
) noexcept
{
    std::scoped_lock lock{mutex_};
    if (outcome == TimelinePreviewPreloadOutcome::Completed)
    {
        ++diagnostics_.preloadsCompleted;
    }
    else if (outcome == TimelinePreviewPreloadOutcome::Cancelled)
    {
        ++diagnostics_.preloadsCancelled;
    }
    else
    {
        ++diagnostics_.preloadsSkipped;
    }

    if (!acceptingRequests_ || !latestRequest_.has_value())
    {
        workScheduled_ = false;
        return false;
    }

    return true;
}

void TimelinePreviewRequestState::cancelInFlight() noexcept
{
    cancellationRequested_.store(true, std::memory_order_release);
}

void TimelinePreviewRequestState::fencePreloadGeneration(std::uint64_t generation) noexcept
{
    std::scoped_lock lock{mutex_};
    minimumPreloadGeneration_ = (std::max)(minimumPreloadGeneration_, generation);
    cancellationRequested_.store(true, std::memory_order_release);
}

void TimelinePreviewRequestState::invalidatePending() noexcept
{
    std::scoped_lock lock{mutex_};
    latestRequest_.reset();
    cancellationRequested_.store(true, std::memory_order_release);
}

void TimelinePreviewRequestState::recordStaleResult() noexcept
{
    std::scoped_lock lock{mutex_};
    ++diagnostics_.staleResultsRejected;
}

void TimelinePreviewRequestState::stop() noexcept
{
    std::scoped_lock lock{mutex_};
    acceptingRequests_ = false;
    latestRequest_.reset();
    cancellationRequested_.store(true, std::memory_order_release);
}

TimelinePreviewRequestState::Diagnostics TimelinePreviewRequestState::diagnostics() const noexcept
{
    std::scoped_lock lock{mutex_};
    return diagnostics_;
}

bool TimelinePreviewRequestState::cancellationRequested() const noexcept
{
    return cancellationRequested_.load(std::memory_order_acquire);
}

const std::atomic_bool* TimelinePreviewRequestState::cancellationToken() const noexcept
{
    return &cancellationRequested_;
}

TimelinePreviewWorker::TimelinePreviewWorker(
    TimelinePreviewRequestState& requestState,
    std::filesystem::path mediaCacheRoot
)
    : requestState_(requestState)
    , provider_(std::move(mediaCacheRoot))
{
}

void TimelinePreviewWorker::processNext()
{
    if (stopping_)
    {
        return;
    }

    const std::optional<TimelinePreviewRequest> request = requestState_.takeLatest();
    if (request.has_value())
    {
        processHoverRequest(*request);
        return;
    }

    const std::optional<TimelinePreviewPreloadRequest> preload = preloadPlanner_.next();
    if (!preload.has_value() || !requestState_.beginPreload(preload->generation))
    {
        return;
    }

    processPreloadRequest(*preload);
}

void TimelinePreviewWorker::processLatest()
{
    processNext();
}

void TimelinePreviewWorker::reconcilePreloadTarget(
    TimelinePreviewSource source,
    std::int64_t durationMilliseconds,
    std::uint64_t generation
)
{
    if (!stopping_ && preloadPlanner_.reconcile(source, durationMilliseconds, generation))
    {
        scheduleProcessNext();
    }
}

void TimelinePreviewWorker::invalidatePreload(std::uint64_t generation)
{
    preloadPlanner_.invalidate(generation);
}

void TimelinePreviewWorker::clearSource()
{
    if (stopping_)
    {
        return;
    }

    provider_.clear();
    configuredSource_.reset();
    configuredSourceAvailable_ = false;
}

void TimelinePreviewWorker::stop()
{
    if (stopping_)
    {
        return;
    }

    stopping_ = true;
    requestState_.stop();
    preloadPlanner_.invalidate((std::numeric_limits<std::uint64_t>::max)());
    provider_.clear();
    configuredSource_.reset();
    configuredSourceAvailable_ = false;
    QThread::currentThread()->quit();
}

bool TimelinePreviewWorker::configureSource(const TimelinePreviewSource& source)
{
    if (!configuredSource_.has_value() || *configuredSource_ != source)
    {
        provider_.clear();
        configuredSource_ = source;
        configuredSourceAvailable_ = provider_.setSource(
            source.itemId,
            source.source,
            source.mediaType);
    }

    return configuredSourceAvailable_;
}

void TimelinePreviewWorker::processHoverRequest(const TimelinePreviewRequest& request)
{
    TimelinePreviewDecodeOutcome outcome = TimelinePreviewDecodeOutcome::Unavailable;
    TimelinePreviewResult result{};
    result.request = request;
    result.timing = request.timing;
    result.timing.workerStarted = TimelinePreviewTiming::Clock::now();

    if (request.source.mediaType == WorkspaceMediaType::Video
        && configureSource(request.source))
    {
        const float requestedTimeSeconds = request.representativeTimestampMilliseconds.has_value()
            ? static_cast<float>(static_cast<double>(
                *request.representativeTimestampMilliseconds) / 1'000.0)
            : request.timeSeconds;
        const TimelinePreviewVideoFrameResult preview = provider_.videoFrameAt(
            requestedTimeSeconds,
            request.durationMilliseconds,
            requestState_.cancellationToken());
        result.storeOutcome = preview.storeOutcome;
        if (preview.cancelled)
        {
            outcome = TimelinePreviewDecodeOutcome::Cancelled;
        }
        else
        {
            result.image = preview.image;
            outcome = result.image.isNull()
                ? TimelinePreviewDecodeOutcome::Unavailable
                : TimelinePreviewDecodeOutcome::Completed;
        }
    }

    result.timing.providerReturned = TimelinePreviewTiming::Clock::now();

    if (outcome != TimelinePreviewDecodeOutcome::Cancelled)
    {
        result.timing.previewReadyEmitted = TimelinePreviewTiming::Clock::now();
        emit previewReady(result);
    }

    scheduleNextIfNeeded(requestState_.finishDecode(outcome));
}

void TimelinePreviewWorker::processPreloadRequest(
    const TimelinePreviewPreloadRequest& request
)
{
    TimelinePreviewPreloadOutcome outcome = TimelinePreviewPreloadOutcome::Skipped;
    if (configureSource(request.source))
    {
        const TimelinePreviewVideoFrameResult preload = provider_.videoFrameAt(
            static_cast<float>(static_cast<double>(request.timestampMilliseconds) / 1'000.0),
            request.durationMilliseconds,
            requestState_.cancellationToken());
        if (preload.cancelled)
        {
            outcome = TimelinePreviewPreloadOutcome::Cancelled;
            preloadPlanner_.markCancelled(request);
        }
        else if (!preload.image.isNull())
        {
            outcome = TimelinePreviewPreloadOutcome::Completed;
            preloadPlanner_.markCompleted(request);
        }
        else
        {
            preloadPlanner_.markSkipped(request);
        }
    }
    else
    {
        preloadPlanner_.markSkipped(request);
    }

    scheduleNextIfNeeded(requestState_.finishPreload(outcome));
}

void TimelinePreviewWorker::scheduleNextIfNeeded(bool foregroundWorkPending)
{
    if (!stopping_ && (foregroundWorkPending || preloadPlanner_.hasPendingWork()))
    {
        scheduleProcessNext();
    }
}

void TimelinePreviewWorker::scheduleProcessNext()
{
    (void)QMetaObject::invokeMethod(
        this,
        &TimelinePreviewWorker::processNext,
        Qt::QueuedConnection);
}

}
