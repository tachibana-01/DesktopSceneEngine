#include "InstanceAdmission.h"
#include "InstanceActivation.h"
#include "ApplicationCoordinator.h"
#include "LauncherWindow.h"
#include "MainWindow.h"
#include "BackgroundMusicWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QImage>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QPixmap>
#include <QDialog>
#include <QFileDialog>
#include <QAccessible>
#include <QSignalBlocker>
#include <QtEndian>
#include <windows.h>
#include <QUuid>

#include <iostream>
#include <thread>

// Existing document-close seam, same production decision and teardown paths; no alternate Session model.
class MainWindowDocumentLifecycleTestAccess
{
public:
    static auto& model(MainWindow& w) { return w.workspaceModel_; }
    static auto& engine(MainWindow& w) { return w.engine_; }
    static const void* project(MainWindow& w) { return w.activePlaybackProjection_ ? &w.activePlaybackProjection_->project : nullptr; }
    static auto preview(MainWindow& w) { return w.previewPresentationTargetId_; }
    static auto& document(MainWindow& w) { return w.documentState_; }
    static const auto& bound(MainWindow& w) { return w.displaySelection_.boundDisplayId(); }
    static bool start(MainWindow& w, DesktopSceneEngine::Player::PlaylistId p, DesktopSceneEngine::Player::ItemId i)
    { return w.startPlaylistPlayback(p, i); }
    static void pause(MainWindow& w) { w.togglePlaybackPause(); w.tickTimer_.stop(); }
    static void resume(MainWindow& w) { w.togglePlaybackPause(); w.previousTick_ = std::chrono::steady_clock::now(); w.tickTimer_.start(16); }
    static bool saveAndReload(MainWindow& w, const std::filesystem::path& path)
    {
        if (w.saveProjectToPath(path, true) != MainWindow::SaveResult::Saved) return false;
        auto candidate = w.loadProjectCandidate(path);
        if (!candidate) return false;
        w.commitOpenProject(std::move(*candidate));
        return true;
    }
    static auto* bgm(MainWindow& w, DesktopSceneEngine::Player::PlaylistId p) { return w.openBackgroundMusicWindow(p); }
    static bool close(MainWindow& w, bool cancel)
    {
        QCloseEvent event;
        w.closeAfterDecision(&event, w.resolveUnsavedChanges(cancel
            ? MainWindow::UnsavedChangesChoice::Cancel : MainWindow::UnsavedChangesChoice::Discard));
        return event.isAccepted();
    }
};

class ApplicationCoordinatorTestAccess
{
public:
    static void dropControl(DesktopSceneEngine::Player::ApplicationCoordinator& coordinator)
    {
        for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
            if (socket->property("sessionControl").toBool()) socket->abort();
    }
    static bool permitPending(const DesktopSceneEngine::Player::SessionCoordinatorJoin& join) { return join.hideRequest_ != 0; }
    static void expire(DesktopSceneEngine::Player::SessionCoordinatorJoin& join) { join.expiry_.start(0); }
    static void invalidateAge(DesktopSceneEngine::Player::SessionCoordinatorJoin& join) { join.qualificationAge_.invalidate(); }
    static void corruptShow(DesktopSceneEngine::Player::ApplicationCoordinator& coordinator, const QUuid& incarnation, bool wrongEpoch)
    {
        QByteArray frame(1, 'S');
        frame += (wrongEpoch ? QUuid::createUuid() : coordinator.epoch_).toRfc4122();
        frame += (wrongEpoch ? incarnation : QUuid::createUuid()).toRfc4122();
        const auto offset = frame.size(); frame.resize(offset + 8);
        qToBigEndian<quint64>(1, frame.data() + offset); frame += char{1};
        for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
            if (socket->property("sessionControl").toBool()) socket->write(frame);
    }
    static void actualTrayShow(DesktopSceneEngine::Player::ApplicationCoordinator& coordinator)
    { coordinator.trayMenu_.actions().front()->menu()->actions().front()->trigger(); }
    static bool trayShowReady(const DesktopSceneEngine::Player::ApplicationCoordinator& coordinator)
    { return !coordinator.trayMenu_.actions().empty() && coordinator.trayMenu_.actions().front()->menu()->actions().front()->isEnabled(); }
    static bool trayPresent(const DesktopSceneEngine::Player::ApplicationCoordinator& coordinator) { return coordinator.tray_.isVisible(); }
    static QLocalSocket& socket(DesktopSceneEngine::Player::SessionCoordinatorJoin& join) { return join.socket_; }
    static void requestRenew(DesktopSceneEngine::Player::SessionCoordinatorJoin& join) { join.renewQualification(); }
    static void ageRequests(DesktopSceneEngine::Player::SessionCoordinatorJoin& join, bool renewal)
    {
        const auto overdue = join.requestClock_.elapsed() - DesktopSceneEngine::Player::SessionQualificationLifetimeMs - 1;
        join.hideSentAt_ = overdue;
        if (renewal) { join.renewalSentAt_ = overdue; join.qualificationAge_.invalidate(); }
    }
    static QByteArray renewFrame(DesktopSceneEngine::Player::SessionCoordinatorJoin& join)
    {
        QByteArray frame(1, 'R');
        frame += join.coordinatorEpoch_.toRfc4122(); frame += join.incarnation_.toRfc4122();
        const auto offset = frame.size(); frame.resize(offset + 8);
        qToBigEndian<quint64>(++join.sequence_, frame.data() + offset); frame += char{1};
        return frame;
    }
    static QPointer<QLocalSocket> coordinatorControl(DesktopSceneEngine::Player::ApplicationCoordinator& coordinator)
    {
        for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
            if (socket->property("sessionControl").toBool()) return socket;
        return {};
    }
    static void holdLocalTimers(DesktopSceneEngine::Player::SessionCoordinatorJoin& join)
    { join.renewal_.stop(); join.expiry_.stop(); }
};

namespace
{
using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;
using Access = MainWindowDocumentLifecycleTestAccess;
bool require(bool value, const char* message)
{
    if (!value) std::cerr << message << std::endl;
    return value;
}
bool waitUntil(const std::function<bool()>& condition, int deadline = 5000)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < deadline) { QApplication::processEvents(); QThread::msleep(5); }
    return condition();
}
bool repeat(const InstanceSlot& slot)
{
    QProcess child;
    auto env = QProcessEnvironment::systemEnvironment();
    const auto path = env.value("Path", env.value("PATH"));
    env.remove("Path"); env.remove("PATH"); env.insert("Path", path);
    child.setProcessEnvironment(env);
    child.start(QString::fromUtf8(P0_PLAYER_EXECUTABLE), {"--display-session", QString::fromUtf8(slot.canonicalIdentity())});
    if (!child.waitForStarted(5000)) return false;
    QElapsedTimer time; time.start();
    while (child.state() != QProcess::NotRunning && time.elapsed() < 10000)
    {
        QApplication::processEvents(); QThread::msleep(5);
    }
    return require(child.state() == QProcess::NotRunning && child.exitStatus() == QProcess::NormalExit
        && child.exitCode() == 0 && !child.readAllStandardOutput().contains("Desktop Scene Engine initialized."),
        "Real Player repeat must forward without initializing a second Engine");
}
bool occupied(const InstanceSlot& slot)
{
    InstanceAdmissionOutcome result = InstanceAdmissionOutcome::Error;
    std::thread child([&] { InstanceAdmissionLease lease; result = lease.acquire(slot); }); child.join();
    return result == InstanceAdmissionOutcome::Occupied;
}
bool lifecycle()
{
    const auto id = DisplayId::fromTargetDevicePath(("P0-lifecycle-" + QUuid::createUuid().toString()).toStdString());
    const auto slot = *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{id}, *currentInteractiveInstanceScope());
    QTemporaryDir files;
    ApplicationTranslationState translation;
    InstanceAdmissionLease lease;
    if (lease.acquire(slot) != InstanceAdmissionOutcome::Acquired) return false;
    InstanceActivationEndpoint endpoint(slot);
    if (!endpoint.listen()) return false;
    const bool trayCase = QCoreApplication::arguments().last().startsWith("tray-")
        && !QCoreApplication::arguments().contains("tray-red");
    const auto launcherSlot = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope());
    InstanceAdmissionLease launcherLease;
    InstanceActivationEndpoint launcherActivation(launcherSlot);
    std::unique_ptr<ApplicationCoordinator> coordinator;
    if (trayCase)
    {
        QPixmap icon(16, 16); icon.fill(Qt::green);
        QApplication::setWindowIcon(QCoreApplication::arguments().contains("tray-unavailable") ? QIcon{} : QIcon(icon));
        if (launcherLease.acquire(launcherSlot) != InstanceAdmissionOutcome::Acquired || !launcherActivation.listen()) return false;
        coordinator = std::make_unique<ApplicationCoordinator>(launcherSlot, launcherActivation, true, [] {});
        if (!coordinator->listen()) return false;
    }
    MainWindow window{id, translation};
    std::unique_ptr<SessionCoordinatorJoin> join;
    int acceptedCloses = 0;
    window.setAcceptedCloseCallback([&] {
        ++acceptedCloses;
        if (!Access::engine(window).isInitialized()) std::terminate(); // Stop accepting BEFORE Engine teardown.
        endpoint.beginShutdown();
        if (join) join->stop();
    });
    window.show();
    if (!window.initializeEngine()) return false;
    endpoint.bindReadyRole(window, [&window] { (void)window.trayPresentation().restore(); });
    if (trayCase)
    {
        join = std::make_unique<SessionCoordinatorJoin>(slot, launcherSlot);
        join->enableControl(window.trayPresentation());
        join->start(window, false);
        if (QCoreApplication::arguments().contains("tray-unavailable"))
        {
            if (!waitUntil([&] { return join->joined(); })) return false;
            window.trayPresentation().requestHide(); QApplication::processEvents();
            auto* button = window.findChild<QToolButton*>("sessionHideToTray");
            return require(window.isVisible() && !join->qualified() && button && !button->isEnabled()
                && coordinator->sessionCount() == 1 && Access::close(window, false),
                "An actual icon-unavailable coordinator channel must leave Session visible with Hide unavailable");
        }
        if (!require(waitUntil([&] { return join->qualified(); }), "Real coordinator/tray control channel must qualify")) return false;
    }
    auto& model = Access::model(window);
    auto& engine = Access::engine(window);
    QImage image(64, 40, QImage::Format_RGBA8888); image.fill(Qt::green);
    const auto path = files.filePath("active.png");
    if (!image.save(path)) return false;
    const auto playlist = model.addPlaylist();
    const auto item = model.addItem(playlist, WorkspaceMediaType::Image, path);
    if (!item || !model.setImageDisplayDuration(*item, 30) || !Access::start(window, playlist, *item)) return false;
    if (QCoreApplication::arguments().contains("tray-state"))
    {
        if (!Access::saveAndReload(window, std::filesystem::path{files.filePath("loaded-v3.json").toStdWString()})
            || !model.renamePlaylist(playlist, "Loaded then dirty") || !Access::start(window, playlist, *item)) return false;
    }
    Access::pause(window);
    if (!engine.seek(7)) return false;
    const auto revision = model.authoredRevision();
    const auto* project = Access::project(window);
    const float position = engine.playbackTime();
    const auto preview = Access::preview(window);
    const auto canvas = model.authoredProject().desktopCanvas.aspectRatio;
    const auto activeItem = model.activeItemId();
    if (QCoreApplication::arguments().contains("tray-red"))
    {
        auto* hide = window.findChild<QToolButton*>("sessionHideToTray");
        if (hide) hide->click();
        QApplication::processEvents();
        return require(!window.isVisible() && window.winId() != 0 && engine.isInitialized()
            && model.authoredRevision() == revision && Access::project(window) == project
            && engine.isPaused() && engine.playbackTime() == position && Access::preview(window) == preview,
            "S2 RED A/B: real client Hide must hide the live Session and preserve its dirty paused Project/Engine; current Session stays visible");
    }
    if (trayCase)
    {
        const auto caseName = QCoreApplication::arguments().last();
        const auto documentPath = Access::document(window).currentPath;
        QPointer<MainWindow> sameObject(&window);
        const auto hwnd = reinterpret_cast<HWND>(window.winId());
        const auto incarnation = join->incarnation();
        const auto* sameEngine = &engine;
        const auto geometry = window.geometry();
        auto* button = window.findChild<QToolButton*>("sessionHideToTray");
        if (!require(button && button->isEnabled() && button->shortcut().isEmpty(), "Actual accessible client Hide control must be usable without a shortcut")) return false;
        auto* accessible = QAccessible::queryAccessibleInterface(button);
        if (!require(accessible && accessible->text(QAccessible::Name) == window.tr("Hide to System Tray")
            && button->toolTip() == window.tr("Hide to System Tray") && button->parentWidget() == window.centralWidget()
            && ApplicationCoordinatorTestAccess::trayPresent(*coordinator), "Client accessibility/name/layout and one real coordinator tray adapter")) return false;
        if (caseName == "tray-modal")
        {
            auto* bgm = Access::bgm(window, playlist);
            if (!require(bgm && bgm->isVisible() && !bgm->parentWidget(), "Real parentless modeless BGM fixture")) return false;
            for (QWidget* owner : {static_cast<QWidget*>(&window), static_cast<QWidget*>(bgm)})
            {
                QDialog modal(owner); modal.setWindowModality(Qt::ApplicationModal); modal.show();
                if (!waitUntil([&] { return QApplication::activeModalWidget() == &modal; })) return false;
                window.trayPresentation().refreshEligibility();
                window.trayPresentation().requestHide();
                QApplication::processEvents();
                if (!require(!button->isEnabled() && window.isVisible() && !ApplicationCoordinatorTestAccess::permitPending(*join),
                    "Main/BGM application-modal owner rejects Hide before IPC")) return false;
                const auto beforeShow = window.trayPresentation().showGeneration();
                coordinator->showSession(incarnation);
                if (!waitUntil([&] { return window.trayPresentation().showGeneration() > beforeShow; })) return false;
                modal.close();
            }
            window.trayPresentation().refreshEligibility();
            if (!button->isEnabled()) return false;
            button->click();
            if (!require(waitUntil([&] { return !window.isVisible(); }) && bgm->isVisible(), "Hide only Main; modeless BGM stays independently visible")) return false;
            coordinator->showSession(incarnation);
            if (!waitUntil([&] { return window.isVisible(); })) return false;
        }
        if (caseName == "tray-native-modal")
        {
            auto* bgm = Access::bgm(window, playlist);
            if (!bgm) return false;
            QFileDialog dialog(bgm, "S2 BGM-owned native modal");
            dialog.setFileMode(QFileDialog::ExistingFile);
            QTimer probe; probe.setInterval(20);
            QElapsedTimer deadline; deadline.start();
            bool observed = false, safe = false;
            QObject::connect(&probe, &QTimer::timeout, &dialog, [&] {
                const auto root = GetAncestor(reinterpret_cast<HWND>(bgm->winId()), GA_ROOTOWNER);
                const auto popup = GetLastActivePopup(root);
                wchar_t type[32]{};
                if (popup && popup != root && IsWindowVisible(popup)) GetClassNameW(popup, type, 32);
                if (wcscmp(type, L"#32770") == 0)
                {
                    probe.stop(); observed = true;
                    window.trayPresentation().refreshEligibility();
                    window.trayPresentation().requestHide();
                    surfaceAdmittedRole(window);
                    safe = dialog.testAttribute(Qt::WA_DontShowOnScreen) && !button->isEnabled()
                        && !ApplicationCoordinatorTestAccess::permitPending(*join) && window.isVisible()
                        && !IsWindowEnabled(root) && IsWindowVisible(popup) && IsWindowEnabled(popup);
                    PostMessageW(popup, WM_CLOSE, 0, 0);
                }
                else if (deadline.elapsed() > 5000) { probe.stop(); dialog.reject(); }
            });
            probe.start(); dialog.exec(); probe.stop();
            if (!require(observed && safe, "Actual BGM native dialog/hidden Qt wrapper blocks Hide; Show surfaces the correct local blocker root")) return false;
            window.trayPresentation().refreshEligibility();
        }
        if (caseName == "tray-maximized") window.showMaximized();
        if (caseName == "tray-backpressure")
        {
            auto peer = ApplicationCoordinatorTestAccess::coordinatorControl(*coordinator);
            auto& socket = ApplicationCoordinatorTestAccess::socket(*join);
            if (!peer) return false;
            ApplicationCoordinatorTestAccess::holdLocalTimers(*join);
            QSignalBlocker holdConsumption(socket);
            socket.setReadBufferSize(1); // Real native receive backpressure, not a mocked write result.
            qint64 maximum = 0;
            int reads = 0;
            QObject observer;
            QObject::connect(peer, &QLocalSocket::readyRead, &observer, [&] { ++reads; });
            for (int i = 0; i < 5000 && peer && peer->state() == QLocalSocket::ConnectedState; ++i)
            {
                const auto before = reads;
                socket.write(ApplicationCoordinatorTestAccess::renewFrame(*join)); socket.flush();
                if (!waitUntil([&] { return reads > before || !peer || peer->state() != QLocalSocket::ConnectedState; }, 1000)) return false;
                if (peer) maximum = (std::max)(maximum, peer->bytesToWrite());
                if (maximum > 42 * 16) break;
            }
            std::cout << "Actual coordinator reply backlog=" << maximum << " bytes" << std::endl;
            return require(maximum <= 42 * 16 && (!peer || peer->state() != QLocalSocket::ConnectedState)
                && window.isVisible() && Access::close(window, false),
                "S2 backpressure RED: authenticated valid small Renew frames must not grow the coordinator output past its bounded frame budget");
        }
        if (caseName == "tray-delayed-batch" || caseName == "tray-expired-permit")
        {
            auto& socket = ApplicationCoordinatorTestAccess::socket(*join);
            {
                QSignalBlocker holdDelivery(socket);
                if (caseName == "tray-delayed-batch") ApplicationCoordinatorTestAccess::requestRenew(*join);
                button->click();
                if (!waitUntil([&] { return socket.bytesAvailable() >= (caseName == "tray-delayed-batch" ? 84 : 42); })) return false;
                ApplicationCoordinatorTestAccess::ageRequests(*join, caseName == "tray-delayed-batch");
            }
            // Release real buffered native replies, not a manufactured successful permit.
            QMetaObject::invokeMethod(&socket, "readyRead", Qt::QueuedConnection);
            if (!waitUntil([&] { return !ApplicationCoordinatorTestAccess::permitPending(*join); })) return false;
            return require(window.isVisible() && (caseName != "tray-delayed-batch" || !join->qualified())
                && Access::close(window, false), "S2 stale-reply RED: overdue real Qualified/Permit cannot renew eligibility at receipt or Hide");
        }
        if (caseName == "tray-unqualified")
        {
            ApplicationCoordinatorTestAccess::invalidateAge(*join);
            button->click();
            QApplication::processEvents();
            return require(window.isVisible() && !ApplicationCoordinatorTestAccess::permitPending(*join)
                && Access::close(window, false), "Fresh expired qualification rejects a stale enabled-button Hide");
        }
        if (caseName == "tray-pending" || caseName == "tray-ending")
        {
            button->click();
            if (!require(ApplicationCoordinatorTestAccess::permitPending(*join), "Real Hide request is in flight, not a fixture-only permit")) return false;
            if (caseName == "tray-ending")
            {
                if (!Access::close(window, false)) return false;
                QApplication::processEvents();
                return require(!engine.isInitialized() && !button->isEnabled() && !join->qualified()
                    && !window.trayPresentation().restore(), "Late Hide/Show cannot resurrect accepted Session teardown");
            }
            // Same local Show restoration used by ACTIVATE; arrived before the pending permit.
            (void)window.trayPresentation().restore();
            if (!require(waitUntil([&] { return !ApplicationCoordinatorTestAccess::permitPending(*join); })
                && window.isVisible() && join->qualified(), "A newer Show wins over the real delayed Hide permit")) return false;
        }
        if (caseName == "tray-playing") Access::resume(window);
        button->click();
        if (!require(waitUntil([&] { return !window.isVisible(); }) && !IsWindowVisible(hwnd)
            && IsWindow(hwnd) && sameObject && occupied(slot) && coordinator->sessionCount() == 1
            && incarnation == join->incarnation() && &Access::engine(window) == sameEngine && acceptedCloses == 0,
            "S2 Hide retains native HWND/QObject/process/admission/incarnation/Engine and is not Close")) return false;
        if (caseName == "tray-playing")
        {
            if (!require(waitUntil([&] { return engine.playbackTime() > position + 0.1F; })
                && !engine.isPaused() && !window.isVisible(), "The same Engine clock continues playing while Main is hidden")) return false;
            Access::pause(window);
        }
        if (caseName == "tray-loss" || caseName == "tray-expiry" || caseName == "tray-epoch" || caseName == "tray-stale" || caseName == "tray-retire-control")
        {
            if (caseName == "tray-loss") ApplicationCoordinatorTestAccess::dropControl(*coordinator);
            else if (caseName == "tray-retire-control") join->stop();
            else if (caseName == "tray-expiry") ApplicationCoordinatorTestAccess::expire(*join);
            else ApplicationCoordinatorTestAccess::corruptShow(*coordinator, incarnation, caseName == "tray-epoch");
            if (!require(waitUntil([&] { return window.isVisible(); }, 1000) && IsWindowVisible(hwnd)
                && !join->qualified() && coordinator->sessionCount() == 1 && engine.isInitialized(),
                "S2 RED C: actual qualified control disconnect must automatically fail visible, without role death or Engine teardown")) return false;
        }
        else
        {
            coordinator->showSession(QUuid::createUuid());
            QApplication::processEvents();
            if (!require(!window.isVisible() && join->qualified(), "An absent/stale coordinator incarnation cannot Show the current Session")) return false;
            if (!ApplicationCoordinatorTestAccess::trayShowReady(*coordinator))
                std::cout << "Fixture: native Hide completed before coordinator State/Show projection; awaiting actual enabled tray action" << std::endl;
            if (!require(waitUntil([&] { return ApplicationCoordinatorTestAccess::trayShowReady(*coordinator); }),
                "Coordinator must receive the real Hidden State before the tray QAction is invoked")) return false;
            ApplicationCoordinatorTestAccess::actualTrayShow(*coordinator);
            if (!require(waitUntil([&] { return window.isVisible(); }) && IsWindowVisible(hwnd)
                && (caseName == "tray-maximized" ? window.isMaximized() : window.geometry() == geometry),
                "Real explicit coordinator Show restores the same HWND and normal geometry")) return false;
            for (int i = 0; i < 3; ++i)
            {
                window.trayPresentation().requestHide();
                if (!waitUntil([&] { return !window.isVisible(); })) return false;
                window.trayPresentation().requestHide();
                if (!waitUntil([&] { return !ApplicationCoordinatorTestAccess::permitPending(*join); }) || window.isVisible()) return false;
                coordinator->showSession(incarnation); coordinator->showSession(incarnation);
                if (!waitUntil([&] { return window.isVisible(); })) return false;
            }
            button->click();
            if (!waitUntil([&] { return !window.isVisible(); }) || !repeat(slot)) return false;
            if (!require(window.isVisible() && !window.trayPresentation().hiddenToTray(), "Existing duplicate ACTIVATE independently restores the same local presentation")) return false;
        }
        if (!require(window.isWindowModified() && model.authoredRevision() == revision && Access::project(window) == project
            && engine.isPaused() && (caseName == "tray-playing" ? engine.playbackTime() > position : engine.playbackTime() == position)
            && Access::preview(window) == preview && model.authoredProject().desktopCanvas.aspectRatio == canvas && model.activeItemId() == activeItem
            && Access::bound(window) == id && Access::document(window).currentPath == documentPath
            && incarnation == join->incarnation() && sameObject && window.winId() == reinterpret_cast<WId>(hwnd),
            "S2 state preservation: dirty authored Project/projection/paused seek/Preview/bound DisplayId remain identical")) return false;
        return Access::close(window, false) && !engine.isInitialized() && acceptedCloses == 1;
    }
    window.showMaximized(); window.showMinimized();
    if (!repeat(slot) || window.isMinimized() || !window.isMaximized()) return false;
    if (!require(!Access::close(window, true) && acceptedCloses == 0 && occupied(slot)
        && engine.isInitialized(), "Cancelled dirty close must retain lease, endpoint and Engine")) return false;
    window.showMinimized();
    if (!repeat(slot)) return false;
    if (!require(window.isWindowModified() && model.authoredRevision() == revision && Access::project(window) == project
        && engine.isPaused() && engine.playbackTime() == position && Access::preview(window) == preview
        && Access::bound(window) == id && !Access::document(window).currentPath,
        "ACTIVATE before/after Cancel preserves dirty Project, projection, playback, Preview and exact bound DisplayId")) return false;
    if (!require(Access::close(window, false) && acceptedCloses == 1 && !engine.isInitialized() && occupied(slot),
        "Accepted close stops endpoint before Engine, and holds the lease through remaining resource destruction")) return false;
    QLocalSocket late; late.connectToServer(slot.endpointName());
    return require(!late.waitForConnected(100), "No ACTIVATE endpoint resurrection after accepted shutdown");
}
bool launcherClose()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore store(settings);
    ApplicationTranslationState translation;
    LauncherWindow window(translation, store);
    int calls = 0; window.setAcceptedCloseCallback([&] { ++calls; });
    window.show();
    return require(window.close() && calls == 1, "Launcher accepted close invokes transport stop before role destruction");
}
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    const bool ok = lifecycle() && launcherClose();
    if (ok) std::cout << "Real Session Engine + ACTIVATE + dirty Cancel/accepted close PASS" << std::endl;
    return ok ? 0 : 1;
}
