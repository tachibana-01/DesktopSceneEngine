#pragma once

#include "TimelinePreviewPreloadPlanner.h"
#include "TimelinePreviewProvider.h"
#include "TimelinePreviewSource.h"

#include <QImage>
#include <QMetaType>
#include <QObject>

#include <cstdint>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>

namespace DesktopSceneEngine::Player
{

struct TimelinePreviewTiming
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::optional<TimePoint> hoverEvent{};
    std::optional<TimePoint> requestSubmitted{};
    std::optional<TimePoint> workerStarted{};
    std::optional<TimePoint> providerReturned{};
    std::optional<TimePoint> previewReadyEmitted{};
    std::optional<TimePoint> guiApplied{};
};

[[nodiscard]] bool isTimelinePreviewTimingOrdered(
    const TimelinePreviewTiming& timing
) noexcept;

struct TimelinePreviewRequest
{
    TimelinePreviewSource source{};
    float timeSeconds = 0.0F;
    std::int64_t durationMilliseconds = 0;
    std::uint64_t generation = 0U;
    std::uint64_t serial = 0U;
    std::optional<std::int64_t> representativeTimestampMilliseconds{};
    TimelinePreviewTiming timing{};
};

struct TimelinePreviewResult
{
    TimelinePreviewRequest request{};
    QImage image{};
    TimelinePreviewStoreOutcome storeOutcome = TimelinePreviewStoreOutcome::Unavailable;
    TimelinePreviewTiming timing{};
};

struct TimelinePreviewResultContext
{
    bool hoverActive = false;
    std::optional<TimelinePreviewSource> source{};
    std::uint64_t generation = 0U;
    std::uint64_t newestRequestSerial = 0U;
};

enum class TimelinePreviewDecodeOutcome
{
    Completed,
    Cancelled,
    Unavailable
};

enum class TimelinePreviewPreloadOutcome
{
    Completed,
    Cancelled,
    Skipped
};

[[nodiscard]] bool shouldApplyTimelinePreviewResult(
    const TimelinePreviewRequest& request,
    const TimelinePreviewResultContext& context
) noexcept;

// Thread-safe, bounded hand-off state between the GUI and one preview worker.
// It retains only the newest request while a decode is in flight.
class TimelinePreviewRequestState final
{
public:

    struct Diagnostics
    {
        std::uint64_t submitted = 0U;
        std::uint64_t superseded = 0U;
        std::uint64_t decodesStarted = 0U;
        std::uint64_t decodesCompleted = 0U;
        std::uint64_t decodesCancelled = 0U;
        std::uint64_t preloadsStarted = 0U;
        std::uint64_t preloadsCompleted = 0U;
        std::uint64_t preloadsCancelled = 0U;
        std::uint64_t preloadsSkipped = 0U;
        std::uint64_t staleResultsRejected = 0U;
        std::uint64_t maximumPendingCount = 0U;
    };

    [[nodiscard]] bool submit(const TimelinePreviewRequest& request) noexcept;
    [[nodiscard]] std::optional<TimelinePreviewRequest> takeLatest() noexcept;
    [[nodiscard]] bool finishDecode(TimelinePreviewDecodeOutcome outcome) noexcept;
    [[nodiscard]] bool beginPreload(std::uint64_t generation) noexcept;
    [[nodiscard]] bool finishPreload(TimelinePreviewPreloadOutcome outcome) noexcept;
    void cancelInFlight() noexcept;
    void fencePreloadGeneration(std::uint64_t generation) noexcept;
    void invalidatePending() noexcept;
    void recordStaleResult() noexcept;
    void stop() noexcept;
    [[nodiscard]] Diagnostics diagnostics() const noexcept;
    [[nodiscard]] bool cancellationRequested() const noexcept;
    [[nodiscard]] const std::atomic_bool* cancellationToken() const noexcept;

private:

    mutable std::mutex mutex_{};
    std::optional<TimelinePreviewRequest> latestRequest_{};
    Diagnostics diagnostics_{};
    bool workScheduled_ = false;
    bool acceptingRequests_ = true;
    std::uint64_t minimumPreloadGeneration_ = 0U;
    std::atomic_bool cancellationRequested_{false};
};

class TimelinePreviewWorker final : public QObject
{
    Q_OBJECT

public:

    TimelinePreviewWorker(
        TimelinePreviewRequestState& requestState,
        std::filesystem::path mediaCacheRoot
    );

public slots:

    void processNext();
    void processLatest();
    void reconcilePreloadTarget(
        TimelinePreviewSource source,
        std::int64_t durationMilliseconds,
        std::uint64_t generation
    );
    void invalidatePreload(std::uint64_t generation);
    void clearSource();
    void stop();

signals:

    void previewReady(const DesktopSceneEngine::Player::TimelinePreviewResult& result);

private:

    [[nodiscard]] bool configureSource(const TimelinePreviewSource& source);
    void processHoverRequest(const TimelinePreviewRequest& request);
    void processPreloadRequest(const TimelinePreviewPreloadRequest& request);
    void scheduleNextIfNeeded(bool foregroundWorkPending);
    void scheduleProcessNext();

    TimelinePreviewRequestState& requestState_;
    TimelinePreviewProvider provider_{};
    TimelinePreviewPreloadPlanner preloadPlanner_{};
    std::optional<TimelinePreviewSource> configuredSource_{};
    bool configuredSourceAvailable_ = false;
    bool stopping_ = false;
};

}

Q_DECLARE_METATYPE(DesktopSceneEngine::Player::TimelinePreviewResult)
