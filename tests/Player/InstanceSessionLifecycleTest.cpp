#include "InstanceAdmission.h"
#include "InstanceActivation.h"
#include "LauncherWindow.h"
#include "MainWindow.h"

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
    static bool close(MainWindow& w, bool cancel)
    {
        QCloseEvent event;
        w.closeAfterDecision(&event, w.resolveUnsavedChanges(cancel
            ? MainWindow::UnsavedChangesChoice::Cancel : MainWindow::UnsavedChangesChoice::Discard));
        return event.isAccepted();
    }
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
    MainWindow window{id, translation};
    int acceptedCloses = 0;
    window.setAcceptedCloseCallback([&] {
        ++acceptedCloses;
        if (!Access::engine(window).isInitialized()) std::terminate(); // Stop accepting BEFORE Engine teardown.
        endpoint.beginShutdown();
    });
    window.show();
    if (!window.initializeEngine()) return false;
    endpoint.bindReadyRole(window);
    auto& model = Access::model(window);
    auto& engine = Access::engine(window);
    QImage image(64, 40, QImage::Format_RGBA8888); image.fill(Qt::green);
    const auto path = files.filePath("active.png");
    if (!image.save(path)) return false;
    const auto playlist = model.addPlaylist();
    const auto item = model.addItem(playlist, WorkspaceMediaType::Image, path);
    if (!item || !model.setImageDisplayDuration(*item, 30) || !Access::start(window, playlist, *item)) return false;
    Access::pause(window);
    if (!engine.seek(7)) return false;
    const auto revision = model.authoredRevision();
    const auto* project = Access::project(window);
    const float position = engine.playbackTime();
    const auto preview = Access::preview(window);
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
