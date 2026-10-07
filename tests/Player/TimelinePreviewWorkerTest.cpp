#include "TimelinePreviewWorker.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

class TemporaryDirectory final
{
public:

    TemporaryDirectory()
        : path_{std::filesystem::temp_directory_path()
            / ("DesktopSceneEngine-TimelinePreviewWorkerTest-"
                + std::to_string(std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()))}
    {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:

    std::filesystem::path path_{};
};

DesktopSceneEngine::Player::TimelinePreviewSource makeSource(
    const QString& source,
    DesktopSceneEngine::Player::WorkspaceMediaType mediaType
)
{
    return {
        DesktopSceneEngine::Player::ItemId{QUuid::createUuid()},
        source,
        mediaType};
}

DesktopSceneEngine::Player::TimelinePreviewRequest makeRequest(
    const DesktopSceneEngine::Player::TimelinePreviewSource& source,
    float timeSeconds,
    std::uint64_t generation,
    std::uint64_t serial
)
{
    return {source, timeSeconds, 60'000, generation, serial};
}

std::filesystem::path findVideoFixture()
{
    const std::filesystem::path root{DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root})
    {
        if (entry.is_regular_file() && entry.path().extension() == ".mp4")
        {
            return entry.path();
        }
    }

    return {};
}

std::filesystem::path findSecondVideoFixture(const std::filesystem::path& firstVideo)
{
    const std::filesystem::path root{DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root})
    {
        if (entry.is_regular_file() && entry.path().extension() == ".mp4"
            && entry.path() != firstVideo)
        {
            return entry.path();
        }
    }

    return {};
}

bool containsPersistentPng(const std::filesystem::path& root)
{
    std::error_code error{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root, error})
    {
        if (error)
        {
            return false;
        }

        if (entry.is_regular_file() && entry.path().extension() == ".png")
        {
            return true;
        }
    }

    return false;
}

bool verifyLatestRequestReplacement()
{
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::TimelinePreviewDecodeOutcome;
    using DesktopSceneEngine::Player::WorkspaceMediaType;

    TimelinePreviewRequestState state{};
    const auto source = makeSource(QStringLiteral("a.mp4"), WorkspaceMediaType::Video);
    const auto first = makeRequest(source, 10.0F, 1U, 1U);
    const auto second = makeRequest(source, 20.0F, 1U, 2U);
    const auto third = makeRequest(source, 30.0F, 1U, 3U);
    const auto fourth = makeRequest(source, 40.0F, 1U, 4U);

    const bool firstSchedulesWork = state.submit(first);
    const auto inFlight = state.takeLatest();
    const bool secondSchedulesWork = state.submit(second);
    const bool thirdSchedulesWork = state.submit(third);
    const bool fourthSchedulesWork = state.submit(fourth);
    const bool cancellationRequested = state.cancellationRequested();
    const bool nextDecodeRequired = state.finishDecode(
        TimelinePreviewDecodeOutcome::Cancelled);
    const auto next = state.takeLatest();
    const bool noFurtherDecodeRequired = state.finishDecode(
        TimelinePreviewDecodeOutcome::Completed);
    const auto diagnostics = state.diagnostics();

    return require(firstSchedulesWork, "The first request should schedule worker work")
        && require(inFlight.has_value() && inFlight->serial == 1U,
            "The first request should become the in-flight decode")
        && require(!secondSchedulesWork && !thirdSchedulesWork && !fourthSchedulesWork,
            "Requests while decoding must replace pending work without queueing more wakeups")
        && require(cancellationRequested,
            "A newer request must cooperatively cancel the obsolete in-flight decode")
        && require(nextDecodeRequired, "The newest pending request should schedule one follow-up decode")
        && require(next.has_value() && next->serial == 4U
                && next->timeSeconds == 40.0F,
            "Only the latest pending request should be decoded after the in-flight request")
        && require(!noFurtherDecodeRequired,
            "The worker should become idle after consuming the newest pending request")
        && require(diagnostics.submitted == 4U && diagnostics.superseded == 2U
                && diagnostics.decodesStarted == 2U && diagnostics.decodesCompleted == 1U
                && diagnostics.decodesCancelled == 1U
                && diagnostics.maximumPendingCount == 1U,
            "The state must retain one bounded pending request and count superseded requests");
}

bool verifySourceSwitchReplacesOldPendingRequest()
{
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::TimelinePreviewDecodeOutcome;
    using DesktopSceneEngine::Player::WorkspaceMediaType;

    TimelinePreviewRequestState state{};
    const auto sourceA = makeSource(QStringLiteral("a.mp4"), WorkspaceMediaType::Video);
    const auto sourceB = makeSource(QStringLiteral("b.mp4"), WorkspaceMediaType::Video);
    const auto inFlight = makeRequest(sourceA, 10.0F, 10U, 10U);
    const auto obsoletePending = makeRequest(sourceA, 20.0F, 10U, 11U);
    const auto replacement = makeRequest(sourceB, 30.0F, 11U, 12U);

    if (!require(state.submit(inFlight), "The first source request should schedule work")
        || !require(state.takeLatest().has_value(),
            "The first source request should become in flight"))
    {
        return false;
    }

    (void)state.submit(obsoletePending);
    state.invalidatePending();
    (void)state.submit(replacement);
    const bool hasReplacement = state.finishDecode(
        TimelinePreviewDecodeOutcome::Cancelled);
    const auto next = state.takeLatest();
    (void)state.finishDecode(TimelinePreviewDecodeOutcome::Completed);

    return require(hasReplacement,
               "A source switch should leave its latest request available after current work")
        && require(next.has_value() && next->source == sourceB
                && next->generation == 11U && next->serial == 12U,
            "A source switch must discard old pending work and retain only the new source request");
}

bool verifyLeaveAndShutdownCancellation()
{
    using DesktopSceneEngine::Player::TimelinePreviewDecodeOutcome;
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::WorkspaceMediaType;

    TimelinePreviewRequestState leaveState{};
    const auto source = makeSource(QStringLiteral("a.mp4"), WorkspaceMediaType::Video);
    if (!require(leaveState.submit(makeRequest(source, 10.0F, 1U, 1U)),
            "A leave-cancellation request should schedule work")
        || !require(leaveState.takeLatest().has_value(),
            "A leave-cancellation request should become in-flight"))
    {
        return false;
    }

    leaveState.invalidatePending();
    const bool leaveSchedulesMoreWork = leaveState.finishDecode(
        TimelinePreviewDecodeOutcome::Cancelled);
    const auto leaveDiagnostics = leaveState.diagnostics();

    TimelinePreviewRequestState shutdownState{};
    if (!require(shutdownState.submit(makeRequest(source, 20.0F, 2U, 2U)),
            "A shutdown-cancellation request should schedule work")
        || !require(shutdownState.takeLatest().has_value(),
            "A shutdown-cancellation request should become in-flight"))
    {
        return false;
    }

    shutdownState.stop();
    const bool shutdownSchedulesMoreWork = shutdownState.finishDecode(
        TimelinePreviewDecodeOutcome::Cancelled);
    const auto shutdownDiagnostics = shutdownState.diagnostics();

    return require(leaveState.cancellationRequested(),
               "Hover leave must mark its in-flight decode obsolete")
        && require(!leaveSchedulesMoreWork,
            "Hover leave must not schedule another decode after cancellation")
        && require(leaveDiagnostics.decodesCancelled == 1U,
            "Hover leave cancellation should be counted")
        && require(shutdownState.cancellationRequested(),
            "Worker shutdown must mark its in-flight decode obsolete")
        && require(!shutdownSchedulesMoreWork,
            "Worker shutdown must not schedule another decode after cancellation")
        && require(shutdownDiagnostics.decodesCancelled == 1U,
            "Shutdown cancellation should be counted");
}

bool verifyPreloadYieldsToForegroundHover()
{
    using DesktopSceneEngine::Player::TimelinePreviewDecodeOutcome;
    using DesktopSceneEngine::Player::TimelinePreviewPreloadOutcome;
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::WorkspaceMediaType;

    TimelinePreviewRequestState state{};
    const auto source = makeSource(QStringLiteral("a.mp4"), WorkspaceMediaType::Video);
    const auto hover = makeRequest(source, 10.0F, 1U, 1U);

    const bool beganPreload = state.beginPreload(0U);
    const bool hoverSchedulesSeparateWork = state.submit(hover);
    const bool preempted = state.cancellationRequested();
    const bool foregroundRequired = state.finishPreload(TimelinePreviewPreloadOutcome::Cancelled);
    const auto pendingHover = state.takeLatest();
    (void)state.finishDecode(TimelinePreviewDecodeOutcome::Completed);
    const auto diagnostics = state.diagnostics();

    return require(beganPreload,
               "Idle preview state must allow one background preload to begin")
        && require(!hoverSchedulesSeparateWork,
            "Hover arriving during preload must replace work without adding another wakeup")
        && require(preempted,
            "Hover arriving during preload must cooperatively cancel the preload")
        && require(foregroundRequired && pendingHover.has_value()
                && pendingHover->serial == hover.serial,
            "The pending hover must be the next foreground work item")
        && require(diagnostics.preloadsStarted == 1U && diagnostics.preloadsCancelled == 1U,
            "Cancelled preload work must be recorded separately from hover decode work");
}

bool verifyQueuedSourceSwitchRejectsStalePreload()
{
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::TimelinePreviewWorker;
    using DesktopSceneEngine::Player::WorkspaceMediaType;

    TimelinePreviewRequestState state{};
    TemporaryDirectory cache{};
    TimelinePreviewWorker worker{state, cache.path()};
    const auto sourceA = makeSource(QStringLiteral("source-a.mp4"), WorkspaceMediaType::Video);
    const auto sourceB = makeSource(QStringLiteral("source-b.mp4"), WorkspaceMediaType::Video);

    // Source A has already queued a worker turn. The GUI accepts source B before
    // that queued turn runs, so source A must not clear the replacement cancellation.
    worker.reconcilePreloadTarget(sourceA, 10'000, 1U);
    state.fencePreloadGeneration(2U);
    QCoreApplication::sendPostedEvents(&worker, QEvent::MetaCall);

    const auto staleTurnDiagnostics = state.diagnostics();
    const bool staleTurnWasCancelled = state.cancellationRequested();

    // This models the later queued source-B reconciliation. Its next worker turn
    // must be the first preload allowed to begin after the replacement request.
    worker.reconcilePreloadTarget(sourceB, 10'000, 2U);
    worker.processNext();
    const auto replacementDiagnostics = state.diagnostics();

    return require(staleTurnDiagnostics.preloadsStarted == 0U,
               "A queued preload for the replaced source must not begin decoding")
        && require(staleTurnWasCancelled,
            "A rejected stale preload must not clear replacement cancellation")
        && require(replacementDiagnostics.preloadsStarted == 1U,
            "The replacement source must become the next preload allowed to begin");
}

bool verifyResultInvalidation()
{
    using DesktopSceneEngine::Player::TimelinePreviewRequest;
    using DesktopSceneEngine::Player::TimelinePreviewResultContext;
    using DesktopSceneEngine::Player::WorkspaceMediaType;
    using DesktopSceneEngine::Player::shouldApplyTimelinePreviewResult;

    const auto sourceA = makeSource(QStringLiteral("a.mp4"), WorkspaceMediaType::Video);
    const auto sourceB = makeSource(QStringLiteral("b.mp4"), WorkspaceMediaType::Video);
    const TimelinePreviewRequest request = makeRequest(sourceA, 10.0F, 10U, 100U);
    TimelinePreviewResultContext context{true, sourceA, 10U, 100U};

    if (!require(shouldApplyTimelinePreviewResult(request, context),
            "A current active request should be applicable"))
    {
        return false;
    }

    context.generation = 11U;
    const bool staleGenerationRejected = !shouldApplyTimelinePreviewResult(request, context);
    context.generation = 10U;
    context.source = sourceB;
    const bool staleSourceRejected = !shouldApplyTimelinePreviewResult(request, context);
    context.source = sourceA;
    context.newestRequestSerial = 101U;
    const bool staleSerialRejected = !shouldApplyTimelinePreviewResult(request, context);
    context.newestRequestSerial = 100U;
    context.hoverActive = false;
    const bool leaveRejected = !shouldApplyTimelinePreviewResult(request, context);

    return require(staleGenerationRejected,
               "Changing source generation must reject old preview results")
        && require(staleSourceRejected,
            "Changing active source must reject old preview results")
        && require(staleSerialRejected,
            "A newer hover request must reject an older preview result")
        && require(leaveRejected,
            "Leaving the timeline must reject a returning preview result");
}

bool verifyWorkerDecodeAndShutdown(const std::filesystem::path& videoPath)
{
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::TimelinePreviewWorker;

    TimelinePreviewRequestState state{};
    TemporaryDirectory cache{};
    QThread thread{};
    auto* worker = new TimelinePreviewWorker{state, cache.path()};
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    std::atomic_bool decoded{false};
    std::atomic_bool timingOrdered{false};
    QObject::connect(
        worker,
        &TimelinePreviewWorker::previewReady,
        worker,
        [&decoded, &timingOrdered](const DesktopSceneEngine::Player::TimelinePreviewResult& result)
        {
            decoded.store(
                result.request.source.mediaType
                    == DesktopSceneEngine::Player::WorkspaceMediaType::Video
                    && !result.image.isNull(),
                std::memory_order_relaxed);
            auto timing = result.timing;
            timing.guiApplied = DesktopSceneEngine::Player::TimelinePreviewTiming::Clock::now();
            timingOrdered.store(
                DesktopSceneEngine::Player::isTimelinePreviewTimingOrdered(timing),
                std::memory_order_relaxed);
        },
        Qt::DirectConnection);
    thread.start();

    const auto source = makeSource(
        QString::fromStdWString(videoPath.native()),
        DesktopSceneEngine::Player::WorkspaceMediaType::Video);
    auto request = makeRequest(source, 0.0F, 1U, 1U);
    request.representativeTimestampMilliseconds = 0;
    request.timing.hoverEvent = DesktopSceneEngine::Player::TimelinePreviewTiming::Clock::now();
    request.timing.requestSubmitted = DesktopSceneEngine::Player::TimelinePreviewTiming::Clock::now();
    const bool scheduled = state.submit(request);
    const bool decodeQueued = QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::processLatest,
        Qt::QueuedConnection);
    const bool stopQueued = QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::stop,
        Qt::QueuedConnection);
    const bool finished = scheduled && decodeQueued && stopQueued && thread.wait(15000U);
    if (!finished && thread.isRunning())
    {
        thread.quit();
        (void)thread.wait(5000U);
    }

    return require(scheduled && decodeQueued,
               "A worker-owned video request should be scheduled for decoding")
        && require(stopQueued, "Worker shutdown should be queued to the worker thread")
        && require(decoded.load(std::memory_order_relaxed),
            "The worker should decode its independently owned video source before shutdown")
        && require(timingOrdered.load(std::memory_order_relaxed),
            "A successful preview result must preserve T0 through T5 ordering")
        && require(finished && !thread.isRunning(),
            "Worker shutdown must finish the dedicated QThread without a deadlock");
}

bool verifyWorkerPreloadsWithoutHover(const std::filesystem::path& videoPath)
{
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::TimelinePreviewWorker;

    TimelinePreviewRequestState state{};
    TemporaryDirectory cache{};
    QThread thread{};
    auto* worker = new TimelinePreviewWorker{state, cache.path()};
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);

    std::atomic_uint64_t previewSignals{0U};
    QObject::connect(
        worker,
        &TimelinePreviewWorker::previewReady,
        worker,
        [&previewSignals](const DesktopSceneEngine::Player::TimelinePreviewResult&)
        {
            previewSignals.fetch_add(1U, std::memory_order_relaxed);
        },
        Qt::DirectConnection);
    thread.start();

    const auto source = makeSource(
        QString::fromStdWString(videoPath.native()),
        DesktopSceneEngine::Player::WorkspaceMediaType::Video);
    const bool configured = QMetaObject::invokeMethod(
        worker,
        [worker, source]
        {
            worker->reconcilePreloadTarget(source, 10'000, 1U);
        },
        Qt::QueuedConnection);

    const auto completionDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds{15};
    while ((!containsPersistentPng(cache.path())
            || state.diagnostics().preloadsCompleted == 0U)
        && std::chrono::steady_clock::now() < completionDeadline)
    {
        std::this_thread::yield();
    }

    state.stop();
    const bool stopQueued = QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::stop,
        Qt::QueuedConnection);
    const bool finished = stopQueued && thread.wait(15000U);
    if (!finished && thread.isRunning())
    {
        thread.quit();
        (void)thread.wait(5000U);
    }

    const auto diagnostics = state.diagnostics();
    return require(configured,
               "An active video target must queue proactive preload work")
        && require(containsPersistentPng(cache.path()) && diagnostics.preloadsStarted >= 1U
                && diagnostics.preloadsCompleted >= 1U,
            "Preload must generate a persistent representative frame without hover")
        && require(previewSignals.load(std::memory_order_relaxed) == 0U,
            "Background preload must never emit a hover preview result")
        && require(finished && !thread.isRunning(),
            "Shutdown during active preload must finish the worker thread cleanly");
}

bool verifyWorkerCancelsObsoleteDecode(const std::filesystem::path& videoPath)
{
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::TimelinePreviewWorker;

    TimelinePreviewRequestState state{};
    TemporaryDirectory cache{};
    QThread thread{};
    auto* worker = new TimelinePreviewWorker{state, cache.path()};
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);

    std::atomic_uint64_t emittedSerial{0U};
    std::atomic_uint64_t emittedCount{0U};
    QObject::connect(
        worker,
        &TimelinePreviewWorker::previewReady,
        worker,
        [&emittedSerial, &emittedCount](
            const DesktopSceneEngine::Player::TimelinePreviewResult& result)
        {
            if (!result.image.isNull())
            {
                emittedSerial.store(result.request.serial, std::memory_order_release);
                emittedCount.fetch_add(1U, std::memory_order_relaxed);
            }
        },
        Qt::DirectConnection);
    thread.start();

    const auto source = makeSource(
        QString::fromStdWString(videoPath.native()),
        DesktopSceneEngine::Player::WorkspaceMediaType::Video);
    const bool firstScheduled = state.submit(makeRequest(source, 4.0F, 1U, 1U));
    const bool firstQueued = QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::processLatest,
        Qt::QueuedConnection);

    const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (state.diagnostics().decodesStarted == 0U
        && std::chrono::steady_clock::now() < startDeadline)
    {
        std::this_thread::yield();
    }

    const bool secondScheduled = state.submit(makeRequest(source, 0.25F, 1U, 2U));
    const auto completionDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds{15};
    while (emittedSerial.load(std::memory_order_acquire) != 2U
        && std::chrono::steady_clock::now() < completionDeadline)
    {
        std::this_thread::yield();
    }

    const bool stopQueued = QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::stop,
        Qt::QueuedConnection);
    const bool finished = stopQueued && thread.wait(15000U);
    if (!finished && thread.isRunning())
    {
        thread.quit();
        (void)thread.wait(5000U);
    }

    const auto diagnostics = state.diagnostics();
    return require(firstScheduled && firstQueued,
               "The initial obsolete request should start worker decoding")
        && require(diagnostics.decodesStarted >= 2U && diagnostics.decodesCancelled == 1U,
            "An in-flight obsolete decode should be cancelled before the newest request")
        && require(!secondScheduled,
            "The newest request should replace pending work without queuing another worker event")
        && require(emittedSerial.load(std::memory_order_acquire) == 2U
                && emittedCount.load(std::memory_order_relaxed) == 1U,
            "Only the newest request should produce a visible preview result")
        && require(finished && !thread.isRunning(),
            "Cancellation must preserve orderly worker shutdown");
}

bool verifyWorkerSourceSwitch(
    const std::filesystem::path& firstVideo,
    const std::filesystem::path& secondVideo
)
{
    using DesktopSceneEngine::Player::TimelinePreviewRequestState;
    using DesktopSceneEngine::Player::TimelinePreviewWorker;

    TimelinePreviewRequestState state{};
    TemporaryDirectory cache{};
    QThread thread{};
    auto* worker = new TimelinePreviewWorker{state, cache.path()};
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);

    const auto sourceA = makeSource(
        QString::fromStdWString(firstVideo.native()),
        DesktopSceneEngine::Player::WorkspaceMediaType::Video);
    const auto sourceB = makeSource(
        QString::fromStdWString(secondVideo.native()),
        DesktopSceneEngine::Player::WorkspaceMediaType::Video);
    std::atomic_uint64_t usableAResults{0U};
    std::atomic_uint64_t usableBResults{0U};
    QObject::connect(
        worker,
        &TimelinePreviewWorker::previewReady,
        worker,
        [&sourceA, &sourceB, &usableAResults, &usableBResults](
            const DesktopSceneEngine::Player::TimelinePreviewResult& result)
        {
            if (result.image.isNull())
            {
                return;
            }

            if (result.request.source == sourceA)
            {
                usableAResults.fetch_add(1U, std::memory_order_relaxed);
            }
            else if (result.request.source == sourceB)
            {
                usableBResults.fetch_add(1U, std::memory_order_relaxed);
            }
        },
        Qt::DirectConnection);
    thread.start();

    const bool firstScheduled = state.submit(makeRequest(sourceA, 4.0F, 1U, 1U));
    const bool firstQueued = QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::processLatest,
        Qt::QueuedConnection);
    const auto startedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (state.diagnostics().decodesStarted == 0U
        && std::chrono::steady_clock::now() < startedDeadline)
    {
        std::this_thread::yield();
    }

    state.invalidatePending();
    const bool clearQueued = QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::clearSource,
        Qt::QueuedConnection);
    const bool secondScheduled = state.submit(makeRequest(sourceB, 0.25F, 2U, 2U));
    const bool secondQueued = !secondScheduled || QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::processLatest,
        Qt::QueuedConnection);
    const auto completionDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds{15};
    while (usableBResults.load(std::memory_order_acquire) == 0U
        && std::chrono::steady_clock::now() < completionDeadline)
    {
        std::this_thread::yield();
    }

    const bool stopQueued = QMetaObject::invokeMethod(
        worker,
        &TimelinePreviewWorker::stop,
        Qt::QueuedConnection);
    const bool finished = stopQueued && thread.wait(15000U);
    if (!finished && thread.isRunning())
    {
        thread.quit();
        (void)thread.wait(5000U);
    }

    const auto diagnostics = state.diagnostics();
    return require(firstScheduled && firstQueued,
               "The first source should start a real worker decode")
        && require(clearQueued && secondQueued,
            "The production-equivalent clear and replacement request should be queued")
        && require(diagnostics.decodesCancelled >= 1U,
            "The in-flight source A request should be cancelled")
        && require(usableAResults.load(std::memory_order_acquire) == 0U,
            "Cancelled source A must not emit a usable preview")
        && require(usableBResults.load(std::memory_order_acquire) == 1U,
            "Replacement source B should emit the only usable preview")
        && require(finished && !thread.isRunning(),
            "A source switch must preserve orderly worker shutdown");
}

}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);

    const std::filesystem::path videoPath = findVideoFixture();
    const std::filesystem::path secondVideoPath = findSecondVideoFixture(videoPath);
    if (!require(!videoPath.empty(), "A video fixture must be available")
        || !require(!secondVideoPath.empty(), "A second video fixture must be available"))
    {
        return 1;
    }

    return verifyLatestRequestReplacement() && verifySourceSwitchReplacesOldPendingRequest()
            && verifyLeaveAndShutdownCancellation()
            && verifyPreloadYieldsToForegroundHover()
            && verifyQueuedSourceSwitchRejectsStalePreload()
            && verifyResultInvalidation()
            && verifyWorkerDecodeAndShutdown(videoPath)
            && verifyWorkerPreloadsWithoutHover(videoPath)
            && verifyWorkerCancelsObsoleteDecode(videoPath)
            && verifyWorkerSourceSwitch(videoPath, secondVideoPath)
        ? 0
        : 1;
}
