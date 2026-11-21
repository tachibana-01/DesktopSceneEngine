// Current Session runtime contract. The real autonomous control executor owns
// playback, authored resource swaps, and the bounded Close result lane.
#include "SessionApplication.h"
#include "SessionControlExecutor.h"
#include "MainWindowTestDisplay.h"
#include "TimelineUtils.h"
#include "Core/EngineAudioControl.h"

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

namespace DesktopSceneEngine::Core
{
class BackgroundMusicEngineTestAccess
{
public:
    static Audio::AudioClockAnchor clockAnchor(const Engine& engine) { return engine.audioControl_->clock; }
};
}

class SessionRuntimeOperationTestAccess
{
public:
    static WorkspaceModel& model(SessionApplication& application) { return application.workspaceModel_; }
    static Audio::AudioClockAnchor clockAnchor(const SessionApplication& application)
    { return Core::BackgroundMusicEngineTestAccess::clockAnchor(application.engine_); }
    static void clean(SessionApplication& application)
    { application.documentState_.markClean(application.workspaceModel_.authoredRevision()); }
};

namespace
{
using Access = SessionRuntimeOperationTestAccess;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

RuntimeRequest intent(RuntimeAction action, PlaylistId playlist = {}, ItemId item = {})
{
    RuntimeRequest request;
    request.action = action;
    request.playlistId = playlist.value;
    request.itemId = item.value;
    return request;
}

template <typename Result>
Result controlResult(std::future<Result>& result)
{
    if (result.wait_for(std::chrono::seconds{3}) != std::future_status::ready)
        throw std::runtime_error("Test timed out awaiting its detached control result");
    return result.get();
}

template <typename Command>
auto callControl(SessionControlExecutor& control, Command command)
{
    using Result = std::invoke_result_t<Command, SessionApplication&>;
    auto task = std::make_shared<std::packaged_task<Result(SessionApplication&)>>(std::move(command));
    auto result = task->get_future();
    require(control.submit([task](SessionApplication& application) { (*task)(application); }),
        "The exact control executor admits the owned test request");
    return controlResult(result);
}

struct AutonomousPlaybackFacts
{
    std::optional<ItemId> active;
    QString binding;
    Audio::AudioClockAnchor clock;
    Audio::AudioMonotonicTime capturedAt;
    bool completionPending;
};

AutonomousPlaybackFacts readAutonomousPlayback(const SessionApplication& application)
{
    return {application.workspace().activeItemId(), application.bindingToken(),
        Access::clockAnchor(application), Audio::audioMonotonicNow(),
        application.presentationEngine().naturalSceneCompletionPending()};
}

void verifyAutonomousCompletionWhileGuiBlocked(const QString& image)
{
    auto executor = std::make_unique<SessionControlExecutor>();
    auto& control = *executor;
    auto ready = std::make_shared<std::promise<bool>>();
    auto readyResult = ready->get_future();
    const auto* guiThread = QThread::currentThread();
    control.start(TestSupport::mainWindowTestDisplayId(), {}, {}, [ready, guiThread](bool started) {
        ready->set_value(started && QThread::currentThread() != guiThread);
    });
    require(controlResult(readyResult), "The production SessionApplication and Engine start on non-GUI C");

    const auto [playlist, first, successor] = callControl(control, [image](SessionApplication& application) {
        if (!application.enableAutonomousMediaPreparation())
            throw std::runtime_error("Unable to start the production media producer");
        application.setPresentationCapabilities(false, true, true);
        auto& model = Access::model(application);
        const auto playlist = model.addPlaylist();
        const auto first = model.addItem(playlist, WorkspaceMediaType::Image, image, QStringLiteral("Finite image"));
        const auto successor = model.addItem(playlist, WorkspaceMediaType::Image, image, QStringLiteral("Successor"));
        if (!first || !successor || !model.setImageDisplayDuration(*first, 1.0)
            || !model.setImageDisplayDuration(*successor, 3600.0))
            throw std::runtime_error("Unable to author the owned finite image sequence on C");
        Access::clean(application);
        return std::tuple{playlist, *first, *successor};
    });
    auto playbackStart = std::make_shared<std::promise<RuntimeResult>>();
    auto playbackStartResult = playbackStart->get_future();
    require(control.submit([playlist, first, playbackStart](SessionApplication& application) {
        application.applyRuntimeOperationDeferred(intent(RuntimeAction::PlayItem, playlist, first),
            [playbackStart](RuntimeResult result) { playbackStart->set_value(std::move(result)); });
    }), "The first real image is admitted by the production resource boundary");
    const auto start = controlResult(playbackStartResult);
    require(start.status == "pending" || start.status == "applied", "Prepared media commits on C after resource readiness");

    QElapsedTimer startup;
    startup.start();
    auto before = callControl(control, readAutonomousPlayback);
    while (!before.clock.valid() && startup.elapsed() < 1000)
    {
        QThread::msleep(4);
        before = callControl(control, readAutonomousPlayback);
    }
    const auto firstPosition = Audio::resolveAudioClockPosition(before.clock, before.capturedAt);
    require(before.active == first && !before.binding.isEmpty() && before.clock.valid()
        && before.clock.mode == Audio::AudioClockMode::Running && before.clock.duration == 1
        && firstPosition < before.clock.duration,
        "C confirms the first real image and its monotonic clock before GUI blocking");

    auto delivered = std::make_shared<std::atomic_bool>(false);
    auto capture = std::make_shared<std::promise<std::pair<AutonomousPlaybackFacts, bool>>>();
    auto captureResult = capture->get_future();
    QTimer::singleShot(0, QApplication::instance(), [delivered] { delivered->store(true); });
    const auto guiBlockBegan = Audio::audioMonotonicNow();
    require(control.submit([capture, delivered](SessionApplication& application) {
        QTimer::singleShot(1500, Qt::PreciseTimer, &application, [capture, delivered, &application] {
            capture->set_value({readAutonomousPlayback(application), delivered->load()});
        });
    }), "C owns an observation-only capture while the GUI event queue is blocked");
    const auto [during, guiDeliveredAtCapture] = controlResult(captureResult);
    const auto guiBlockEnded = Audio::audioMonotonicNow();

    const auto firstAtCapture = Audio::resolveAudioClockPosition(before.clock, during.capturedAt);
    const auto successorAtCapture = Audio::resolveAudioClockPosition(during.clock, during.capturedAt);
    require(!guiDeliveredAtCapture && !delivered->load() && during.capturedAt >= guiBlockBegan
        && during.capturedAt <= guiBlockEnded && firstAtCapture == before.clock.duration,
        "The passive C capture occurs inside the real GUI stall after the first deadline");
    require(during.active == successor && during.binding != before.binding && !during.completionPending
        && during.clock.valid() && during.clock.binding != before.clock.binding
        && during.clock.mode == Audio::AudioClockMode::Running && during.clock.duration == 3600
        && successorAtCapture > 0, "C consumes natural completion and advances the live successor before GUI delivery");
    QApplication::processEvents();
    require(delivered->load(), "The delayed GUI sentinel is delivered after resumption");

    callControl(control, [](SessionApplication& application) { application.requestClose(); });
    const auto closeFacts = [](SessionApplication& application) {
        return std::pair{application.closeState(),
            !application.engineInitialized() && !application.activeProjection()
                && application.presentationEngine().audioShutdownComplete()};
    };
    QElapsedTimer closeWait;
    closeWait.start();
    auto retirement = callControl(control, closeFacts);
    while (retirement.first == SessionApplication::CloseState::Retiring && closeWait.elapsed() < 3000)
    {
        QThread::msleep(4);
        retirement = callControl(control, closeFacts);
    }
    require(retirement.first == SessionApplication::CloseState::Ready && retirement.second,
        "A/P and active runtime retire before platform Close is accepted");
    const auto accepted = callControl(control, [](SessionApplication& application) {
        application.completePlatformClose(true);
        return application.closeState();
    });
    require(accepted == SessionApplication::CloseState::Accepted, "Detached platform acknowledgement completes Close");
    control.stop();
    require(!control.submit([](SessionApplication&) {}), "Stopped C closes new command admission");
    executor.reset(); // Join before reporting or releasing the owned temporary fixture.

    std::cout << QJsonDocument(QJsonObject{
        {"status", "PASS"},
        {"case", "detached production control completes image sequence during GUI stall"},
        {"guiThreadBlockedMs", static_cast<double>(guiBlockEnded - guiBlockBegan) / 1.0e6},
        {"captureAfterGuiBlockMs", static_cast<double>(during.capturedAt - guiBlockBegan) / 1.0e6},
        {"guiDeliveredAtCapture", guiDeliveredAtCapture},
        {"positionBeforeStallSeconds", firstPosition},
        {"firstDurationSeconds", before.clock.duration},
        {"firstAnchorPositionAtCaptureSeconds", firstAtCapture},
        {"successorPositionAtCaptureSeconds", successorAtCapture},
        {"firstItem", first.value.toString(QUuid::WithoutBraces)},
        {"successor", successor.value.toString(QUuid::WithoutBraces)},
        {"activeAtCapture", during.active->value.toString(QUuid::WithoutBraces)},
        {"bindingChangedAtCapture", during.binding != before.binding},
        {"closeAcceptedAndControlJoined", true},
        {"presentation", "detached; no Player, Model C, or displayed-frame claim"},
    }).toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

void verifyCloseAdmissionAtCommandCapacity()
{
    SessionControlExecutor control;
    auto ready = std::make_shared<std::promise<bool>>();
    auto started = ready->get_future();
    control.start(TestSupport::mainWindowTestDisplayId(), {}, {}, [ready](bool value) { ready->set_value(value); });
    require(controlResult(started), "Control starts for Close admission saturation");

    auto parked = std::make_shared<std::promise<void>>();
    auto parkedResult = parked->get_future();
    std::promise<void> release;
    auto releaseResult = release.get_future().share();
    bool released = false;
    const auto guaranteeRelease = qScopeGuard([&] { if (!released) release.set_value(); });
    require(control.submit([parked, releaseResult](SessionApplication&) {
        parked->set_value();
        releaseResult.wait();
    }), "Machine fixture parked the dedicated control loop");
    controlResult(parkedResult);

    auto executed = std::make_shared<std::size_t>(0);
    std::size_t admitted = 0;
    for (; admitted < 512; ++admitted)
        if (!control.submit([executed](SessionApplication&) { ++*executed; })) break;
    require(admitted > 0 && admitted < 512, "Ordinary mailbox has a finite admission bound");
    auto close = std::make_shared<std::promise<std::size_t>>();
    auto closeResult = close->get_future();
    require(control.submitPlatformResult([close, executed](SessionApplication& application) {
        application.requestClose(true);
        close->set_value(*executed);
    }), "Reserved native Close remains deliverable at ordinary queue capacity");
    release.set_value();
    released = true;
    require(controlResult(closeResult) == admitted, "Reserved Close preserves earlier command ordering");
    QElapsedTimer deadline;
    deadline.start();
    const auto readClose = [](SessionApplication& application) { return application.closeState(); };
    auto current = callControl(control, readClose);
    while (current == SessionApplication::CloseState::Retiring && deadline.elapsed() < 3000)
    {
        QThread::msleep(4);
        current = callControl(control, readClose);
    }
    require(current == SessionApplication::CloseState::Ready, "Close reached the real resource retirement barrier");
    require(callControl(control, [](SessionApplication& application) {
        application.completePlatformClose(true);
        return application.closeState();
    }) == SessionApplication::CloseState::Accepted, "Native Close acknowledgment settles exactly once");
    std::cout << "CP3 reserved Close PASS at " << admitted << " ordinary commands\n";
}

void verifyAutonomousAuthoredRebuild(const QString& image, const QString& activeSource)
{
    require(QFile::copy(image, activeSource), "Create independently owned active media for the authored failure case");
    SessionControlExecutor control;
    auto ready = std::make_shared<std::promise<bool>>();
    auto readyResult = ready->get_future();
    control.start(TestSupport::mainWindowTestDisplayId(), {}, {}, [ready](bool started) { ready->set_value(started); });
    require(controlResult(readyResult), "Production C starts for authored rebuild proof");
    const auto [playlist, first, successor] = callControl(control, [&](SessionApplication& application) {
        require(application.enableAutonomousMediaPreparation(), "Enable the actual asynchronous Engine media producer");
        application.setPresentationCapabilities(false, true, true);
        auto& model = Access::model(application);
        const auto playlist = model.addPlaylist();
        const auto first = model.addItem(playlist, WorkspaceMediaType::Image, activeSource, QStringLiteral("Owned active"));
        const auto successor = model.addItem(playlist, WorkspaceMediaType::Image, image, QStringLiteral("Next"));
        if (!first || !successor || !model.setImageDisplayDuration(*first, 60.0))
            throw std::runtime_error("Unable to create the authored active and successor media");
        return std::tuple{playlist, *first, *successor};
    });
    const auto runtime = [&](RuntimeRequest request) {
        auto completed = std::make_shared<std::promise<RuntimeResult>>();
        auto result = completed->get_future();
        require(control.submit([request, completed](SessionApplication& application) {
            application.applyRuntimeOperationDeferred(request, [completed](RuntimeResult value) {
                completed->set_value(std::move(value));
            });
        }), "Admit a production runtime request on C");
        return controlResult(result);
    };
    const auto authored = [&](AuthoredRequest request) {
        auto completed = std::make_shared<std::promise<AuthoredResult>>();
        auto result = completed->get_future();
        require(control.submit([request, completed](SessionApplication& application) {
            application.applyAuthoredOperationDeferred(request, [completed](AuthoredResult value) {
                completed->set_value(std::move(value));
            });
        }), "Admit a production authored request on C");
        return controlResult(result);
    };
    const auto settle = [&] {
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 3000)
        {
            if (callControl(control, [](SessionApplication& application) {
                const auto state = application.presentationEngine().audioExecutionState();
                return std::none_of(state.effects.begin(), state.effects.end(),
                    [](const auto& effect) { return effect.pending(); });
            })) return;
            QThread::msleep(4);
        }
        throw std::runtime_error("The actual audio effect failed to settle");
    };

    const auto started = runtime(intent(RuntimeAction::PlayItem, playlist, first));
    require(started.status == "pending" || started.status == "applied", "Bind authored media on C");
    settle();
    const auto paused = runtime(intent(RuntimeAction::PlayPause));
    require(paused.status == "pending" || paused.status == "applied", "Pause the actual binding");
    settle();
    const auto before = callControl(control, readAutonomousPlayback);
    auto seek = intent(RuntimeAction::Seek, playlist, first);
    seek.bindingToken = before.binding;
    seek.duration = 60.0;
    seek.value = TimelineSliderMaximum / 10;
    const auto sought = runtime(seek);
    require(sought.status == "pending" || sought.status == "applied", "Seek remains in the paused source Item");
    settle();
    const auto soughtFacts = callControl(control, readAutonomousPlayback);
    require(soughtFacts.clock.mode == Audio::AudioClockMode::Paused && std::abs(soughtFacts.clock.position - 6) < 0.01,
        "Establish the native paused anchor before authored geometry changes");

    AuthoredRequest pose;
    pose.action = AuthoredAction::SetItemPresentation;
    pose.playlistId = playlist;
    pose.itemId = first;
    pose.presentation.scale = 1.25;
    const auto restored = authored(pose);
    settle();
    const auto afterRestore = callControl(control, readAutonomousPlayback);
    require(restored.status == AuthoredStatus::Applied && restored.authored == AuthoredOutcome::Committed
        && restored.positionRestore == PositionRestoreOutcome::Restored && afterRestore.active == first
        && afterRestore.binding != soughtFacts.binding && afterRestore.clock.mode == Audio::AudioClockMode::Paused
        && std::abs(afterRestore.clock.position - soughtFacts.clock.position) < 0.01,
        "Deferred authored rebuild has a terminal position result and retains paused playback");

    require(QFile::remove(activeSource), "Retire only the owned active test source");
    pose.presentation.scale = 1.5;
    const auto failed = authored(pose);
    const auto afterFailure = callControl(control, readAutonomousPlayback);
    require(failed.status == AuthoredStatus::Applied && failed.authored == AuthoredOutcome::Committed
        && failed.runtime == RuntimeOutcome::Unchanged && !failed.warnings.isEmpty()
        && afterFailure.active == first && afterFailure.binding == afterRestore.binding,
        "A real unavailable resource preserves last-good runtime and reports the committed authored edit");

    AuthoredRequest remove;
    remove.action = AuthoredAction::RemoveItem;
    remove.playlistId = playlist;
    remove.itemId = first;
    const auto removed = authored(remove);
    settle();
    const auto afterRemoval = callControl(control, readAutonomousPlayback);
    require(removed.status == AuthoredStatus::Applied && removed.authored == AuthoredOutcome::Committed
        && (removed.runtime == RuntimeOutcome::Synchronized || removed.runtimeOnApplied == RuntimeOutcome::Synchronized)
        && afterRemoval.active == successor && afterRemoval.binding != afterRestore.binding,
        "Active authored deletion binds the real authored-order successor");
    std::cout << "CP3 authoritative deferred authored restore / failed media / active deletion: PASS\n";
}

void writeSilentMusic(const QString& path)
{
    constexpr std::uint32_t samples = 48000U * 20U * 2U;
    constexpr std::uint32_t dataBytes = samples * 2U;
    QByteArray bytes(44, '\0');
    bytes.replace(0, 4, "RIFF");
    qToLittleEndian<std::uint32_t>(dataBytes + 36U, bytes.data() + 4);
    bytes.replace(8, 8, "WAVEfmt ");
    qToLittleEndian<std::uint32_t>(16, bytes.data() + 16);
    qToLittleEndian<std::uint16_t>(1, bytes.data() + 20);
    qToLittleEndian<std::uint16_t>(2, bytes.data() + 22);
    qToLittleEndian<std::uint32_t>(48000, bytes.data() + 24);
    qToLittleEndian<std::uint32_t>(192000, bytes.data() + 28);
    qToLittleEndian<std::uint16_t>(4, bytes.data() + 32);
    qToLittleEndian<std::uint16_t>(16, bytes.data() + 34);
    bytes.replace(36, 4, "data");
    qToLittleEndian<std::uint32_t>(dataBytes, bytes.data() + 40);
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
        && file.write(QByteArray(dataBytes, '\0')) == dataBytes,
        "An owned silent stereo PCM fixture was written");
}

void verifyAutonomousBgmAuthority(const QString& image, const QString& music, const QString& corruptMusic)
{
    writeSilentMusic(music);
    {
        QFile file(corruptMusic);
        const auto corrupt = QByteArrayLiteral("Not a WAV stream");
        require(file.open(QIODevice::WriteOnly) && file.write(corrupt) == corrupt.size(),
            "The dedicated malformed BGM source was written");
    }
    SessionControlExecutor control;
    auto ready = std::make_shared<std::promise<bool>>();
    auto started = ready->get_future();
    control.start(TestSupport::mainWindowTestDisplayId(), {}, {}, [ready](bool state) { ready->set_value(state); });
    require(controlResult(started), "Autonomous control owns the actual Session and BGM runtime");

    const auto [playlist, item, first, second, badTrack, permit] = callControl(control, [&](SessionApplication& application) {
        require(application.enableAutonomousMediaPreparation(), "Actual media preparation is available");
        application.setPresentationCapabilities(true, true, true);
        auto& model = Access::model(application);
        const auto playlist = model.addPlaylist();
        const auto item = model.addItem(playlist, WorkspaceMediaType::Image, image);
        const auto first = model.addPlaylistMusicTrack(playlist, music, QStringLiteral("First"));
        const auto second = model.addPlaylistMusicTrack(playlist, music, QStringLiteral("Second"));
        const auto badTrack = model.addPlaylistMusicTrack(playlist, corruptMusic, QStringLiteral("Invalid audio fixture"));
        const auto permit = application.issueBgmEditPermit(playlist);
        require(item && first && second && badTrack && !permit.isNull()
            && model.setImageDisplayDuration(*item, 3600.0), "Owned media and scoped permit are ready");
        const auto canonical = [](TrackId track) {
            return Domain::TrackId{track.value.toString(QUuid::WithoutBraces).toStdString()};
        };
        return std::tuple{playlist, *item, canonical(*first), canonical(*second), canonical(*badTrack), permit};
    });
    const BgmTarget target = PlaylistBgmTarget{playlist};
    auto playback = std::make_shared<std::promise<RuntimeResult>>();
    auto playbackResult = playback->get_future();
    require(control.submit([playlist, item, playback](SessionApplication& application) {
        application.applyRuntimeOperationDeferred(intent(RuntimeAction::PlayItem, playlist, item),
            [playback](RuntimeResult result) { playback->set_value(std::move(result)); });
    }), "PlayItem is admitted on the production control executor");
    const auto result = controlResult(playbackResult);
    require(result.status == "pending" || result.status == "applied", "Actual visual playback binds");

    const auto musicState = [&] { return callControl(control, [&](SessionApplication& application) {
        return application.bgmRuntimeState(permit, target);
    }); };
    const auto awaitTrack = [&](const Domain::TrackId& expected) {
        QElapsedTimer timer;
        timer.start();
        do
        {
            const auto seen = musicState();
            if (seen.status == BgmRuntimeCommandStatus::Ready && seen.state && seen.state->scope.switchEligible
                && seen.state->scope.phase == BackgroundMusicObservation::Phase::Playing
                && seen.state->scope.currentTrackId && seen.state->scope.currentTrackId->value == expected.value())
            {
                // Current is published before source start, and start may
                // precede the release of the structural audio effect slot.
                if (callControl(control, [](SessionApplication& application) {
                    const auto effects = application.presentationEngine().audioExecutionState().effects;
                    return !application.busy() && std::none_of(effects.begin(), effects.end(),
                        [](const auto& effect) { return effect.pending(); });
                })) return true;
            }
            QThread::msleep(4);
        } while (timer.elapsed() < 4500);
        return false;
    };

    require(awaitTrack(first), "Native track observation, rather than authored selection, supplies Current");
    const auto switchResult = callControl(control, [&](SessionApplication& application) {
        return application.useBgmTrack(permit, target, second);
    });
    if (switchResult.status != BgmRuntimeCommandStatus::Admitted || !switchResult.audioEffect)
        throw std::runtime_error("The native switch was not admitted, status "
            + std::to_string(static_cast<int>(switchResult.status)) + ", reason "
            + std::to_string(static_cast<int>(switchResult.reason)));
    require(awaitTrack(second), "The admitted native switch reaches the requested prepared Current");

    const auto matchingTerminal = [&](const QJsonObject& admission) {
        const auto operation = admission.value("operation");
        require(operation.isString() && !operation.toString().isEmpty(), "Audio admission supplies an operation identity");
        QElapsedTimer deadline;
        deadline.start();
        do
        {
            const auto facts = callControl(control, [](SessionApplication& application) {
                return application.audioEffectsSnapshot();
            });
            const auto effects = facts.value("effects").toArray();
            for (const auto& entry : effects)
            {
                const auto receipt = entry.toObject();
                if (receipt.value("operation") != operation) continue;
                for (const char* field : {"documentIncarnation", "bindingToken", "execution", "sequence",
                         "scope", "playlistId", "itemId"})
                    require(receipt.value(field) == admission.value(field), "Native audio operation identity remains fixed");
                const auto phase = receipt.value("phase").toString();
                if (phase != "pending" && phase != "preparing") return receipt;
            }
            QThread::msleep(4);
        } while (deadline.elapsed() < 3000);
        throw std::runtime_error("The exact audio operation has no native terminal fact");
    };

    const auto failedSwitch = callControl(control, [&](SessionApplication& application) {
        return application.useBgmTrack(permit, target, badTrack);
    });
    require(failedSwitch.status == BgmRuntimeCommandStatus::Admitted && failedSwitch.audioEffect,
        "The real decoder failure follows native admission");
    const auto failedReceipt = matchingTerminal(*failedSwitch.audioEffect);
    require(failedReceipt.value("phase") == QJsonValue{"failed"}
        && failedReceipt.value("error") == QJsonValue{"audio-preparation-failed"},
        "The exact admitted switch reports its real audio preparation failure");
    // List Loop recovers from the final malformed track to the first owned
    // track. The separate Engine successor may still hold structural capacity
    // after the failed Use receipt is terminal.
    require(awaitTrack(first), "List Loop finishes native recovery to the first track after decode failure");

    const auto gainEdit = callControl(control, [&](SessionApplication& application) {
        auto& model = Access::model(application);
        BgmAuthoredRequest edit;
        edit.action = BgmAuthoredAction::SetQueueVolume;
        edit.target = target;
        edit.permit = permit;
        edit.value = 0.25;
        const auto previous = model.authoredRevision();
        const auto applied = application.applyBgmAuthoredOperation(edit);
        if (applied.status != AuthoredStatus::Applied)
            throw std::runtime_error("Native gain admission after BGM recovery returned authored status "
                + std::to_string(static_cast<int>(applied.status)));
        require(applied.authoredRevision == previous + 1, "Queue gain advances the native authored revision once");
        require(applied.bgmReadback && applied.bgmReadback->queueVolume == edit.value,
            "Queue gain returns its canonical authored readback");
        require(applied.audioEffect.has_value(), "Active queue gain carries its own native audio receipt");
        return applied;
    });
    const auto gainReceipt = matchingTerminal(*gainEdit.audioEffect);
    require(gainReceipt.value("phase") == QJsonValue{"applied"}
        && gainReceipt.value("scope") == QJsonValue{"playlist-bgm-gain"}
        && gainReceipt.value("operation") != failedReceipt.value("operation"),
        "A new queue gain can settle normally after the earlier operation failed");

    callControl(control, [&](SessionApplication& application) {
        auto& model = Access::model(application);
        BgmAuthoredRequest revoked;
        revoked.action = BgmAuthoredAction::SetQueueVolume;
        revoked.target = target;
        revoked.permit = permit;
        revoked.value = 0.5;
        application.revokeBgmEditPermit(permit);
        require(application.applyBgmAuthoredOperation(revoked).status == AuthoredStatus::RevokedPermit
            && application.useBgmTrack(permit, target, first).reason == BgmRuntimeRejectReason::RevokedPermit
            && model.authoredRevision() == gainEdit.authoredRevision,
            "Revoking the native peer permit protects both authored and runtime authority");
    });
    control.stop();
    std::cout << "Current Session BGM authority, actual runtime Current, real decoder failure receipt, "
        "subsequent gain receipt, and permit revocation: PASS\n";
}

}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir directory;
    try
    {
        require(directory.isValid(), "Dedicated safe test directory exists");
        const auto image = directory.filePath("safe-cp3-image.png");
        QImage pixels(32, 24, QImage::Format_RGB32);
        pixels.fill(QColor{40, 90, 150});
        require(pixels.save(image), "Generated harmless image fixture");

        const auto testCase = app.arguments().value(1);
        if (testCase == QStringLiteral("--cp3-control-stall-completion"))
            verifyAutonomousCompletionWhileGuiBlocked(image);
        else if (testCase == QStringLiteral("--cp3-reserved-close"))
            verifyCloseAdmissionAtCommandCapacity();
        else if (testCase == QStringLiteral("--cp3-authored-rebuild"))
            verifyAutonomousAuthoredRebuild(image, directory.filePath("owned-active-removed.png"));
        else if (testCase == QStringLiteral("--bgm-authority"))
            verifyAutonomousBgmAuthority(image, directory.filePath("owned-silent.wav"),
                directory.filePath("owned-malformed.wav"));
        else
            throw std::runtime_error("Unknown Session runtime contract case");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Session runtime contract FAIL: " << error.what() << '\n';
        return 1;
    }
}
