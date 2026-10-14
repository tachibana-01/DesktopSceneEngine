#include "ApplicationCoordinator.h"
#include "ApplicationPreferences.h"
#include "LauncherWindow.h"
#include "MainWindow.h"
#include "BackgroundMusicWindow.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QAbstractButton>
#include <QCloseEvent>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QImage>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QtEndian>
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

class ApplicationCoordinatorTestAccess
{
public:
    static QMenu& menu(ApplicationCoordinator& c) { return c.trayMenu_; }
    static bool closing(const ApplicationCoordinator& c) { return c.closing_; }
    static bool icon(const ApplicationCoordinator& c) { return c.tray_.isVisible(); }
    static void displays(ApplicationCoordinator& c, Platform::DisplayEnumerationResult result)
    { c.enumerateDisplays_ = [result] { return result; }; c.updateTray(); }
    static QLocalSocket& control(SessionCoordinatorJoin& j) { return j.socket_; }
    static quint64 showRequest(const SessionCoordinatorJoin& j) { return j.lastShowRequest_; }
    static void refresh(ApplicationCoordinator& c) { c.updateTray(); }
    static QTimer& drain(ApplicationCoordinator& c) { return c.drain_; }
    static bool closeProgressSettled(const ApplicationCoordinator& c) { return !c.closeProgressQueued_; }
};

class MainWindowDesktopPresentationTestAccess
{
public:
    static auto& model(MainWindow& w) { return w.workspaceModel_; }
    static auto& engine(MainWindow& w) { return w.engine_; }
    static auto& document(MainWindow& w) { return w.documentState_; }
    static auto preview(MainWindow& w) { return w.previewPresentationTargetId_; }
    static auto bound(MainWindow& w) { return w.displaySelection_.boundDisplayId(); }
    static bool start(MainWindow& w, PlaylistId p, ItemId i) { return w.startPlaylistPlayback(p, i); }
    static void pause(MainWindow& w) { w.togglePlaybackPause(); w.tickTimer_.stop(); }
    static auto* bgm(MainWindow& w, PlaylistId p) { return w.openBackgroundMusicWindow(p); }
};

namespace
{
using Access = MainWindowDesktopPresentationTestAccess;
constexpr UINT HarnessMessage = WM_APP + 176; // Only this executable's owned HWNDs.
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
bool waitUntil(const std::function<bool()>& predicate, int budget = 7000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!predicate() && elapsed.elapsed() < budget) { QApplication::processEvents(); QThread::msleep(5); }
    return predicate();
}
InstanceSlot launcherSlot()
{ return *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope()); }
InstanceSlot sessionSlot(const QString& name)
{ return *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{DisplayId::fromTargetDevicePath(name.toStdString())}, *currentInteractiveInstanceScope()); }

class NativeHarness final : public QAbstractNativeEventFilter
{
public:
    HWND hwnd{};
    std::function<void(int)> invoke;
    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override
    {
        const auto* msg = static_cast<MSG*>(message);
        if (msg->hwnd != hwnd || msg->message != HarnessMessage) return false;
        invoke(static_cast<int>(msg->wParam)); if (result) *result = 0; return true;
    }
};

class CloseObservation final : public QObject
{
public:
    int closes = 0;
    bool eventFilter(QObject*, QEvent* e) override
    {
        if (e->type() == QEvent::Close) { ++closes; std::cout << "ORDINARY CLOSE " << closes << std::endl; }
        return false;
    }
};

int peer(const QString& name, const QString& mode)
{
    QTemporaryDir files;
    QSettings settings(files.filePath("preferences.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore preferences(settings);
    ApplicationTranslationState translation;
    const auto slot = sessionSlot(name);
    InstanceAdmissionLease admission;
    require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "S6 peer admission");
    InstanceActivationEndpoint activation(slot); require(activation.listen(), "S6 peer ACTIVATE");
    const auto id = DisplayId::fromTargetDevicePath(name.toStdString());
    MainWindow window(id, translation, preferences, ApplicationPreferences{});
    SessionCoordinatorJoin join(slot, launcherSlot());
    window.show(); require(window.initializeEngine(), "S6 actual Engine/Preview prerequisite");
    activation.bindReadyRole(window, [&] { (void)window.trayPresentation().restore(); });
    join.enableControl(window.trayPresentation());
    join.enableDesktopDeployment([&] { return window.desktopDeploymentProjection(); },
        [&](DesktopDeploymentIntent intent) { return window.requestDesktopDeployment(intent); });
    window.setAcceptedCloseCallback([&] {
        join.beginRoleTeardown(); activation.beginShutdown();
        QTimer::singleShot(0, &join, [&] { join.drainAndStop([] { QCoreApplication::quit(); }); });
    });
    auto& model = Access::model(window); auto& engine = Access::engine(window);
    QImage image(80, 50, QImage::Format_RGBA8888); image.fill(Qt::green);
    const auto file = files.filePath("opaque.png"); require(image.save(file), "S6 image fixture");
    const auto playlist = model.addPlaylist(); const auto item = model.addItem(playlist, WorkspaceMediaType::Image, file);
    require(item && model.setImageDisplayDuration(*item, 30) && Access::start(window, playlist, *item), "S6 real scene fixture");
    Access::pause(window); require(engine.seek(7), "S6 paused seek");
    if (mode == "clean") Access::document(window).markClean(model.authoredRevision());
    if (mode == "save-ok") Access::document(window).currentPath = std::filesystem::path(files.filePath("saved.json").toStdWString());
    if (mode == "save-failure") Access::document(window).currentPath = std::filesystem::path(files.path().toStdWString());
    const auto revision = model.authoredRevision(); const auto path = Access::document(window).currentPath;
    const auto canvas = model.desktopCanvas().aspectRatio; const auto preview = Access::preview(window);
    const bool dirty = Access::document(window).isDirty(revision);
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    CloseObservation observation; window.installEventFilter(&observation);
    auto& control = ApplicationCoordinatorTestAccess::control(join);
    QObject::connect(&control, &QLocalSocket::readyRead, &window, [&] {
        if (!join.qualified()) return;
        const auto bytes = control.peek(control.bytesAvailable());
        for (qsizetype i = 0; i + 42 <= bytes.size(); i += 42)
            if (bytes[i] == 'C' || bytes[i] == 'K')
                std::cout << (bytes[i] == 'C' ? "WIRE CLOSE " : "WIRE QUERY ")
                    << qFromBigEndian<quint64>(bytes.constData() + i + 33) << std::endl;
    }); // Observe genuine frames; never consume, replace or fabricate protocol results.
    join.start(window, false);
    require(waitUntil([&] { return join.qualified(); }), "S6 real native control qualification");
    std::unique_ptr<QDialog> blocker;
    if (mode == "busy" || mode == "bgm-busy")
    {
        QWidget* owner = mode == "bgm-busy" ? static_cast<QWidget*>(Access::bgm(window, playlist)) : &window;
        require(owner && (mode != "bgm-busy" || !owner->parentWidget()), "S6 actual parentless BGM root");
        blocker = std::make_unique<QDialog>(owner);
        blocker->setWindowModality(Qt::ApplicationModal); blocker->show();
        require(waitUntil([&] { return QApplication::activeModalWidget() == blocker.get(); }), "S6 pre-existing modal prerequisite");
    }
    NativeHarness harness; harness.hwnd = hwnd;
    bool cleanup = false;
    harness.invoke = [&](int command) {
        if (command >= 1 && command <= 3)
        {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            require(box, "S6 decision must target actual ordinary Close prompt");
            const auto button = command == 1 ? QMessageBox::Cancel : command == 2 ? QMessageBox::Discard : QMessageBox::Save;
            require(box->button(button), "S6 actual decision button"); box->button(button)->click();
        }
        else if (command == 4)
        {
            if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) dialog->reject();
            else
            {
                const auto root = GetAncestor(hwnd, GA_ROOTOWNER), popup = GetLastActivePopup(root);
                wchar_t type[32]{}; if (popup) GetClassNameW(popup, type, 32);
                require(popup != root && wcscmp(type, L"#32770") == 0, "S6 actual native Save As");
                PostMessageW(popup, WM_CLOSE, 0, 0);
            }
        }
        else if (command == 5)
        { auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); require(box && box->button(QMessageBox::Ok), "S6 actual save error"); box->button(QMessageBox::Ok)->click(); }
        else if (command == 6) { require(blocker != nullptr, "S6 owned blocker"); blocker->reject(); blocker.reset(); std::cout << "BLOCKER GONE" << std::endl; }
        else if (command == 7) window.close(); // Independent ordinary user x, not a coordinator command.
        else if (command == 8) { join.stop(); std::cout << "CONTROL STOPPED" << std::endl; }
        else if (command == 9)
        {
            cleanup = true;
            if (blocker) { blocker->reject(); blocker.reset(); }
            QTimer::singleShot(0, &window, [&] {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    if (auto* discard = box->button(QMessageBox::Discard)) discard->click();
            });
            window.close();
        }
        else if (command == 10) window.findChild<QToolButton*>("sessionHideToTray")->click();
        else if (command == 11)
        {
            const bool preserved = window.winId() == reinterpret_cast<WId>(hwnd) && model.authoredRevision() == revision
                && model.activeItemId() == item && model.desktopCanvas().aspectRatio == canvas && Access::preview(window) == preview
                && Access::document(window).currentPath == path && Access::document(window).isDirty(revision) == dirty
                && Access::bound(window) == id && engine.isInitialized() && engine.isPaused() && engine.playbackTime() == 7;
            require(preserved, "S6 survivor Project/path/dirty/Canvas/media/playback/Engine/Preview/DisplayId preservation");
            std::cout << "PRESERVED " << observation.closes << std::endl;
        }
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    int previousPrompt = 0; bool fileReady = false, errorReady = false;
    quint64 previousShow = 0;
    QTimer check; check.setInterval(10);
    QObject::connect(&check, &QTimer::timeout, &window, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            if (box->button(QMessageBox::Cancel) && observation.closes != previousPrompt)
            { previousPrompt = observation.closes; std::cout << "PROMPT " << previousPrompt << std::endl; }
            if (box->button(QMessageBox::Ok) && !errorReady) { errorReady = true; std::cout << "SAVE ERROR" << std::endl; }
        }
        if (!fileReady)
        {
            const auto root = GetAncestor(hwnd, GA_ROOTOWNER), popup = GetLastActivePopup(root);
            wchar_t type[32]{}; if (popup) GetClassNameW(popup, type, 32);
            if (qobject_cast<QFileDialog*>(QApplication::activeModalWidget())
                || (popup != root && wcscmp(type, L"#32770") == 0 && IsWindowVisible(popup)))
            { fileReady = true; std::cout << "SAVE AS" << std::endl; }
        }
        const auto show = ApplicationCoordinatorTestAccess::showRequest(join);
        if (show > previousShow) { previousShow = show; std::cout << "SHOW " << show << std::endl; }
    });
    check.start();
    std::cout << "READY " << reinterpret_cast<quintptr>(hwnd) << " " << join.incarnation().toString().toStdString()
        << " dirty=" << dirty << " modal=" << bool(blocker) << std::endl;
    QTimer::singleShot(55000, &window, [] { QCoreApplication::exit(3); });
    const int result = QApplication::exec();
    QApplication::instance()->removeNativeEventFilter(&harness);
    require(result == 0 && !engine.isInitialized(), "S6 peer explicit normal teardown");
    std::cout << "PEER DONE closes=" << observation.closes << " cleanup=" << cleanup << std::endl;
    return result;
}

struct Child
{
    QProcess process; QByteArray output; HWND hwnd{}; QUuid incarnation; DisplayId id;
    explicit Child(const QString& name, const QString& mode = "dirty") : id(DisplayId::fromTargetDevicePath(name.toStdString()))
    {
        process.start(QCoreApplication::applicationFilePath(), {"peer", name, mode});
        require(process.waitForStarted(5000), "S6 owned peer startup");
        if (!waitUntil([&] { read(); return output.contains("READY ") || process.state() == QProcess::NotRunning; }))
        { std::cerr << output.constData(); throw std::runtime_error("S6 peer readiness timeout"); }
        const auto line = output.mid(output.indexOf("READY ")).split('\n').first().trimmed().split(' ');
        if (line.size() != 5 || process.state() != QProcess::Running) { std::cerr << output.constData(); throw std::runtime_error("S6 peer prerequisite failed"); }
        hwnd = reinterpret_cast<HWND>(line[1].toULongLong()); incarnation = QUuid(QString::fromUtf8(line[2]));
        DWORD pid = 0;
        require(IsWindow(hwnd) && GetWindowThreadProcessId(hwnd, &pid) && pid == process.processId() && !incarnation.isNull(), "S6 native process/HWND/incarnation proof");
        require(output.count("Desktop Scene Engine initialized.") == 1 && line[3] == (mode == "clean" ? "dirty=0" : "dirty=1"), "S6 one real Engine / document fixture");
    }
    void read() { output += process.readAllStandardOutput(); output += process.readAllStandardError(); }
    bool has(const char* text) { read(); return output.contains(text); }
    void await(const char* text) { if (!waitUntil([&] { return has(text); })) { std::cerr << output.constData(); throw std::runtime_error(text); } }
    void command(int n) { require(PostMessageW(hwnd, HarnessMessage, n, 0), "S6 owned HWND command"); }
    void exited()
    {
        require(waitUntil([&] { return process.state() == QProcess::NotRunning; }), "S6 expected ordinary Close process exit"); read();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode()) std::cerr << output.constData();
        require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "S6 normal process exit");
    }
    void preserved(int closes)
    { command(11); const auto marker = QByteArray("PRESERVED ") + QByteArray::number(closes); require(waitUntil([&] { read(); return output.contains(marker); }), "S6 unchanged survivor state and ordinary Close count"); }
    void cleanup() { if (process.state() != QProcess::NotRunning) { command(9); exited(); } }
    void crash() { process.kill(); require(process.waitForFinished(5000), "S6 deterministic owned process-death injection"); }
    ~Child() { if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(5000); } }
};

Platform::DisplayEnumerationResult displays(std::initializer_list<DisplayId> ids)
{
    Platform::DisplayEnumerationResult result{Platform::DisplayEnumerationStatus::Success};
    int n = 0;
    for (const auto& id : ids)
    { Platform::DisplayDescriptor d; d.id = id; d.friendlyName = "Display"; d.displayNumber = ++n; d.pixelWidth = 1920; d.pixelHeight = 1080; result.snapshot.displays.push_back(d); }
    return result;
}

struct Fixture
{
    QTemporaryDir files; QSettings settings{files.filePath("preferences.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore preferences{settings}; ApplicationTranslationState translation;
    InstanceSlot slot = launcherSlot(); InstanceAdmissionLease admission; InstanceActivationEndpoint activation{slot};
    int drains = 0; ApplicationCoordinator coordinator{slot, activation, false, [&] { ++drains; }};
    Fixture()
    {
        require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "Refuse interference with an existing coordinator");
        require(activation.listen() && coordinator.listen(), "S6 actual coordinator/admission/control");
        coordinator.setLauncherFactory([&] {
            auto* w = new LauncherWindow(translation, preferences);
            w->setAcceptedCloseCallback([&, w] { coordinator.retireLauncher(*w); }); return w;
        });
        require(coordinator.showLauncher(), "S6 real Launcher");
    }
    QAction* all()
    {
        for (auto* a : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (!a->menu() && a->text() == QCoreApplication::translate("ApplicationCoordinator", "Close All")) return a;
        return nullptr;
    }
    QAction* role(const QUuid& id)
    { for (auto* a : ApplicationCoordinatorTestAccess::menu(coordinator).actions()) if (a->menu() && a->data().toUuid() == id) return a; return nullptr; }
    void start()
    {
        ApplicationCoordinatorTestAccess::refresh(coordinator);
        require(all() && all()->isEnabled(), "S6 RED: missing enabled top-level Close All after verified real role readiness"); all()->trigger();
    }
    void done()
    { require(waitUntil([&] { return ApplicationCoordinatorTestAccess::closing(coordinator) || (all() && all()->isEnabled()); }), "S6 batch must finish/retain survivors or drain normally"); }
    RoleCloseOperation outcome(Child& c, RoleCloseResult expected)
    {
        const bool observed = waitUntil([&] { const auto op = coordinator.roleCloseOperation(c.incarnation); return op && op->result == expected; });
        if (!observed)
        { c.read(); const auto op = coordinator.roleCloseOperation(c.incarnation); std::cerr << "EXPECTED " << int(expected) << " ACTUAL " << (op ? int(op->result) : -1) << "\n" << c.output.constData(); }
        require(observed, "S6 required real S5 semantic result");
        return *coordinator.roleCloseOperation(c.incarnation);
    }
};

void run(const QString& scenario)
{
    Fixture f;
    const auto nonce = "S6-" + QUuid::createUuid().toString();
    if (scenario == "launcher-only")
    {
        const auto incarnation = f.coordinator.launcherIncarnation(); f.start();
        require(waitUntil([&] { return f.drains == 1; }), "S6 Launcher-only accepted normal drain");
        require(f.coordinator.roleCloseOperation(incarnation)->result == RoleCloseResult::CloseAccepted
            && !ApplicationCoordinatorTestAccess::icon(f.coordinator), "S6 one-role ordinary Close / zero-role tray removal"); return;
    }
    const bool busy = scenario == "busy" || scenario == "bgm-busy";
    const bool accepted = scenario == "full-accept" || scenario == "late-drain" || scenario == "discard" || scenario == "save";
    std::unique_ptr<Child> bOwned;
    if (scenario == "order") bOwned = std::make_unique<Child>(nonce + "-B"); // Registration order B,A differs from Platform order A,B.
    const QString mode = busy ? scenario : scenario == "save-failure" ? "save-failure" : scenario == "save" ? "save-ok"
        : scenario == "full-accept" || scenario == "late-drain" ? "clean" : "dirty";
    Child a(nonce + "-A", mode);
    QPointer<QLocalSocket> heldPeer;
    for (auto* socket : f.coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
        if (socket->property("sessionControl").toBool()) heldPeer = socket;
    const bool bothDirty = scenario == "order" || scenario == "all-abort" || scenario == "old-result" || scenario == "pending-death";
    if (!bOwned) bOwned = std::make_unique<Child>(nonce + "-B", bothDirty ? "dirty" : "clean");
    auto& b = *bOwned;
    ApplicationCoordinatorTestAccess::displays(f.coordinator, displays({a.id, b.id}));
    require(f.coordinator.sessionCount() == 2 && f.role(a.incarnation) && f.role(b.incarnation), "S6 two independent live role projections prerequisite");
    const auto launcherId = f.coordinator.launcherIncarnation();
    std::unique_ptr<Child> later, replacement, u, z;
    if (scenario == "order")
    {
        z = std::make_unique<Child>(nonce + "-Z"); u = std::make_unique<Child>(nonce + "-U");
        require(f.coordinator.sessionCount() == 4, "S6 unresolved tail readiness");
    }
    if (scenario == "preexisting-close") { a.command(7); a.await("PROMPT 1"); }
    if (scenario == "preexisting-request")
    { require(f.coordinator.requestRoleClose(a.incarnation), "S6 pre-existing S5 admission"); a.await("PROMPT 1"); f.outcome(a, RoleCloseResult::Pending); }
    if (scenario == "hidden") { a.command(10); require(waitUntil([&] { return !IsWindowVisible(a.hwnd); }), "S6 actual HiddenToTray fixture"); }
    if (scenario == "unavailable") { a.command(8); a.await("CONTROL STOPPED"); require(waitUntil([&] { return !f.role(a.incarnation)->menu()->actions().back()->isEnabled(); }), "S6 unavailable projection prerequisite"); }
    std::unique_ptr<QSignalBlocker> held;
    if (scenario == "uncertain" || scenario == "old-result")
    {
        // Captured while A was the only Session; native Join has already verified it.
        a.command(11); a.await("PRESERVED 0");
        require(heldPeer, "S6 actual A endpoint captured before B joined");
        if (scenario == "uncertain") held = std::make_unique<QSignalBlocker>(heldPeer.data());
    }
    std::unique_ptr<QDialog> launcherBlocker;
    if (scenario == "all-abort")
    { launcherBlocker = std::make_unique<QDialog>(f.coordinator.launcherWindow()); launcherBlocker->setWindowModality(Qt::ApplicationModal); launcherBlocker->show(); require(QApplication::activeModalWidget() == launcherBlocker.get(), "S6 pre-existing Launcher modal prerequisite"); }
    std::unique_ptr<QSignalBlocker> drainHold;
    if (scenario == "late-drain") drainHold = std::make_unique<QSignalBlocker>(ApplicationCoordinatorTestAccess::drain(f.coordinator)); // Hold only the test-owned timer notification.
    if (scenario == "translation")
    { require(f.translation.setLanguage(UiLanguage::SimplifiedChinese) == UiLanguage::SimplifiedChinese, "S6 actual Chinese catalog loaded"); ApplicationCoordinatorTestAccess::refresh(f.coordinator); require(f.all() && f.all()->text() == QString::fromUtf8("全部关闭"), "S6 production translated top-level action"); }
    f.start();
    if (scenario != "all-abort") require(waitUntil([&] { return f.coordinator.launcherWindow() == nullptr; }), "S6 Launcher ordinary Close first");
    else require(waitUntil([&] { return f.coordinator.roleCloseOperation(launcherId).has_value(); }), "S6 Launcher Busy admission executed first");
    const auto launcherOp = f.coordinator.roleCloseOperation(launcherId);
    require(launcherOp && launcherOp->result == (scenario == "all-abort" ? RoleCloseResult::Rejected : RoleCloseResult::CloseAccepted), "S6 Launcher first Accepted/Busy receipt, no rollback");
    if (accepted)
    {
        if (scenario == "discard" || scenario == "save")
        { a.await("PROMPT 1"); f.outcome(a, RoleCloseResult::Pending); require(!f.coordinator.roleCloseOperation(b.incarnation), "S6 accepted dirty decision remains sequential"); a.command(scenario == "save" ? 3 : 2); }
        a.exited(); b.exited();
        require(waitUntil([&] { return f.coordinator.sessionCount() == 0; }), "S6 all accepted verified native removal");
        for (auto* child : {&a, &b})
        { const auto op = f.coordinator.roleCloseOperation(child->incarnation); require(op && op->result == RoleCloseResult::CloseAccepted, "S6 genuine S5 Accepted outcome"); child->read(); require(child->output.count("ORDINARY CLOSE ") == 1, "S6 ordinary Close exactly once"); }
        if (scenario == "late-drain")
        {
            auto& drain = ApplicationCoordinatorTestAccess::drain(f.coordinator); require(f.drains == 0, "S6 zero-role drain held at deterministic notification barrier"); drain.stop();
            later = std::make_unique<Child>(nonce + "-L", "clean");
            drainHold.reset();
            QMetaObject::invokeMethod(&drain, "timeout", Qt::DirectConnection);
            require(f.drains == 0 && f.coordinator.sessionCount() == 1 && !f.coordinator.roleCloseOperation(later->incarnation), "S6 late join is excluded and prevents stale zero-role drain");
            later->cleanup();
        }
        require(waitUntil([&] { return f.drains == 1; }) && !ApplicationCoordinatorTestAccess::icon(f.coordinator), "S6 full accepted last-role bounded drain and tray removal"); return;
    }
    if (busy || scenario == "preexisting-close" || scenario == "preexisting-request" || scenario == "unavailable")
    {
        f.outcome(a, scenario == "preexisting-request" ? RoleCloseResult::Pending : RoleCloseResult::Rejected); b.exited(); f.done();
        require(waitUntil([&] { return f.coordinator.sessionCount() == 1; }) && f.drains == 0
            && ApplicationCoordinatorTestAccess::icon(f.coordinator), "S6 Busy/unavailable survivor coordinator/tray lifetime after verified death notification");
        if (busy) { a.preserved(0); a.command(6); a.await("BLOCKER GONE"); }
        if (scenario == "preexisting-close" || scenario == "preexisting-request") { a.command(1); a.preserved(1); }
        if (scenario == "unavailable") a.preserved(0);
        a.preserved(scenario == "preexisting-close" || scenario == "preexisting-request" ? 1 : 0); // No late queued Close after modal release.
    }
    else
    {
        a.await("PROMPT 1"); const auto pending = f.outcome(a, scenario == "uncertain" ? RoleCloseResult::Uncertain : RoleCloseResult::Pending);
        if (scenario == "translation" || scenario == "uncertain")
        {
            const auto prefix = QCoreApplication::translate("ApplicationCoordinator", scenario == "uncertain" ? "Close outcome not confirmed: %1" : "Closing: %1").split("%1").first();
            bool status = false;
            for (auto* action : ApplicationCoordinatorTestAccess::menu(f.coordinator).actions())
                if (action->isVisible() && !action->isEnabled() && action->text().startsWith(prefix)) status = true;
            require(status, "S6 minimal translated Pending/Uncertain status row");
        }
        if (scenario == "pending")
        {
            auto* deadline = heldPeer->findChild<QTimer*>("roleCloseDeliveryDeadline");
            require(deadline && !deadline->isActive(), "S6 Pending cancels transport deadline, no human deadline");
            for (int i = 0; i < 20; ++i) QMetaObject::invokeMethod(deadline, "timeout", Qt::DirectConnection);
            require(f.coordinator.roleCloseOperation(a.incarnation)->result == RoleCloseResult::Pending, "S6 arbitrary expiry cannot synthesize human Cancel/advance");
        }
        require(!f.coordinator.roleCloseOperation(b.incarnation) && f.all() && !f.all()->isEnabled(), "S6 own interaction waits; second batch disabled");
        f.all()->trigger(); // Disabled real action must not create/queue a second batch.
        f.all()->setEnabled(true); f.all()->trigger(); // Retained stale-enabled invocation is also fenced at execution.
        ApplicationCoordinatorTestAccess::refresh(f.coordinator);
        require(!f.all()->isEnabled() && f.coordinator.roleCloseOperation(a.incarnation)->key == pending.key, "S6 reentrant invocation neither replaces nor queues snapshot");
        require(!f.coordinator.requestRoleClose(b.incarnation), "S6 conflicting coordinator Close refused while batch active");
        const auto deployment = f.coordinator.sessionDeploymentResult(b.incarnation);
        f.coordinator.requestSessionDeployment(b.incarnation, DesktopDeploymentIntent::Deploy);
        require(f.coordinator.sessionDeploymentResult(b.incarnation) == deployment, "S6 coordinator Deploy does not overlap current batch");
        auto* show = f.role(a.incarnation)->menu()->actions().front(); require(show->isEnabled(), "S6 Show remains available during Pending");
        if (scenario != "uncertain") { show->trigger(); a.await("SHOW "); } // Don't overflow the bounded channel by holding unrelated Show frames too.
        require(f.coordinator.roleCloseOperation(a.incarnation)->key == pending.key && !f.coordinator.roleCloseOperation(b.incarnation), "S6 Show surfaces only current interaction, never advances");
        if (scenario == "order") ApplicationCoordinatorTestAccess::displays(f.coordinator, displays({b.id, a.id}));
        if (scenario == "later" || scenario == "launcher-recreate")
        { later = std::make_unique<Child>(nonce + "-L", "clean"); if (scenario == "launcher-recreate") require(f.coordinator.showLauncher(), "S6 recreate Launcher after snapshot"); }
        if (scenario == "replacement" || scenario == "pending-death")
        {
            auto& old = scenario == "pending-death" ? a : b;
            old.crash(); require(waitUntil([&] { return !f.role(old.incarnation); }), "S6 verified native death before replacement");
            replacement = std::make_unique<Child>(nonce + (scenario == "pending-death" ? "-A" : "-B"), "clean");
            require(replacement->incarnation != old.incarnation && replacement->id == old.id, "S6 same DisplayId, fresh exact incarnation");
        }
        if (scenario == "ordinary-overlap") { b.command(7); b.exited(); }
        if (scenario == "uncertain" || scenario == "old-result")
        {
            require(heldPeer && heldPeer->state() == QLocalSocket::ConnectedState, "S6 actual live outcome transport");
            if (!held) held = std::make_unique<QSignalBlocker>(heldPeer.data());
        }
        if (scenario != "pending-death")
        {
            if (scenario == "save-as" || scenario == "save-failure")
            {
                a.command(3);
                if (scenario == "save-as") { a.await("SAVE AS"); a.command(4); }
                else { a.await("SAVE ERROR"); a.command(5); }
            }
            else a.command(1);
        }
        if (scenario == "uncertain" || scenario == "old-result")
        {
            a.preserved(1);
            require(waitUntil([&] { return heldPeer && heldPeer->bytesAvailable() >= 42; }), "S6 genuine terminal result buffered");
            require(!f.coordinator.roleCloseOperation(b.incarnation), "S6 unresolved current result stalls next target");
            // Disconnect is not process death. Delivery expiry is not a human deadline.
            if (scenario == "uncertain")
            {
                auto* deadline = heldPeer->findChild<QTimer*>("roleCloseDeliveryDeadline");
                require(deadline && deadline->isActive(), "S6 genuine delivery deadline is active, not a human deadline");
                deadline->stop(); QMetaObject::invokeMethod(deadline, "timeout", Qt::DirectConnection);
                require(f.coordinator.roleCloseOperation(a.incarnation)->result == RoleCloseResult::Uncertain, "S6 timeout is neither Cancel nor Rejected nor RoleGone");
            }
            else f.coordinator.reconcileRoleClose(a.incarnation, pending.key);
            a.await("WIRE QUERY ");
            require(f.coordinator.roleCloseOperation(a.incarnation)->key == pending.key && !f.coordinator.roleCloseOperation(b.incarnation), "S6 same-key reconciliation without new Close");
            if (scenario == "uncertain")
            {
                require(waitUntil([&] {
                    const auto bytes = heldPeer->peek(heldPeer->bytesAvailable());
                    for (qsizetype i = 0; i + 42 <= bytes.size(); i += 42)
                        if (bytes[i] == 'O' && bytes[i + 41] == char(RoleCloseResult::CloseAborted)) return true;
                    return false;
                }), "S6 genuine Aborted reply must reach retained transport before release");
            }
            require(heldPeer->bytesAvailable() <= 42 * 16, "S6 held genuine frames remain inside the unchanged S5 transport bound");
            held.reset(); QMetaObject::invokeMethod(heldPeer, "readyRead", Qt::DirectConnection);
        }
        if (scenario != "pending-death") { f.outcome(a, RoleCloseResult::CloseAborted); a.preserved(1); }
        else f.outcome(a, RoleCloseResult::RoleGone);
        if (bothDirty)
        {
            b.await("PROMPT 1");
            if (scenario == "old-result")
            {
                const auto bPending = f.outcome(b, RoleCloseResult::Pending);
                a.read(); const auto queries = a.output.count("WIRE QUERY ");
                require(heldPeer && heldPeer->state() == QLocalSocket::ConnectedState, "S6 retained genuine old-target endpoint");
                QSignalBlocker holdOldReply(heldPeer.data());
                f.coordinator.reconcileRoleClose(a.incarnation, pending.key);
                require(waitUntil([&] { a.read(); return a.output.count("WIRE QUERY ") > queries; }), "S6 new same-key query actually reaches old target after advancement");
                require(waitUntil([&] {
                    const auto bytes = heldPeer->peek(heldPeer->bytesAvailable());
                    for (qsizetype i = 0; i + 42 <= bytes.size(); i += 42)
                        if (bytes[i] == 'O' && QUuid::fromRfc4122(bytes.mid(i + 1, 16)) == pending.key.generation
                            && QUuid::fromRfc4122(bytes.mid(i + 17, 16)) == a.incarnation
                            && qFromBigEndian<quint64>(bytes.constData() + i + 33) == pending.key.request
                            && bytes[i + 41] == char(RoleCloseResult::CloseAborted)) return true;
                    return false;
                }), "S6 genuine exact old-key Aborted reply buffered while B is current");
                require(heldPeer->bytesAvailable() <= 42 * 16, "S6 old reply fixture respects unchanged transport bound");
                const auto afterConsumer = std::make_shared<bool>(false);
                const auto observation = QObject::connect(heldPeer, &QLocalSocket::readyRead, &f.coordinator, [&, afterConsumer] {
                    // Registered after the real consumer. Do not infer consumption from a child-side log.
                    QTimer::singleShot(0, &f.coordinator, [afterConsumer] { *afterConsumer = true; });
                });
                holdOldReply.unblock(); QMetaObject::invokeMethod(heldPeer, "readyRead", Qt::DirectConnection);
                require(waitUntil([&] { return *afterConsumer && ApplicationCoordinatorTestAccess::closeProgressSettled(f.coordinator); }), "S6 old reply consumed and queued batch progress evaluated");
                QObject::disconnect(observation);
                const auto current = f.coordinator.roleCloseOperation(b.incarnation);
                require(current && current->key == bPending.key && current->result == RoleCloseResult::Pending
                    && !f.all()->isEnabled(), "S6 consumed old result neither completes nor advances current exact target");
            }
            b.command(1); f.outcome(b, RoleCloseResult::CloseAborted); b.preserved(1);
        }
        else if (scenario != "replacement" && scenario != "ordinary-overlap") b.exited();
        if (scenario == "order")
        {
            require(!f.coordinator.roleCloseOperation(z->incarnation), "S6 stable unresolved U before Z");
            u->await("PROMPT 1"); u->command(1); f.outcome(*u, RoleCloseResult::CloseAborted);
            z->await("PROMPT 1"); z->command(1); f.outcome(*z, RoleCloseResult::CloseAborted);
        }
        f.done();
        if (scenario == "hidden") require(!IsWindowVisible(a.hwnd), "S6 cancelled batch restores safely qualified HiddenToTray");
        for (auto* excluded : {later.get(), replacement.get()}) if (excluded)
        { require(!f.coordinator.roleCloseOperation(excluded->incarnation), "S6 later/replacement exact incarnation excluded"); excluded->preserved(0); }
        if (scenario == "launcher-recreate") require(f.coordinator.launcherWindow() && f.coordinator.launcherIncarnation() != launcherId, "S6 recreated Launcher excluded");
        require(f.drains == 0 && ApplicationCoordinatorTestAccess::icon(f.coordinator), "S6 partial/all-abort survivor lifetime; no rollback");
        a.read(); if (scenario != "pending-death") require(a.output.count("WIRE CLOSE ") == 1, "S6 once-only ordinary Close request, no blind retry");
    }
    if (scenario == "all-abort") { launcherBlocker->reject(); launcherBlocker.reset(); }
    a.cleanup(); b.cleanup(); if (later) later->cleanup(); if (replacement) replacement->cleanup(); if (u) u->cleanup(); if (z) z->cleanup();
    if (f.coordinator.launcherWindow()) f.coordinator.launcherWindow()->close();
    require(waitUntil([&] { return f.drains == 1 && f.coordinator.sessionCount() == 0; }), "S6 last-role normal bounded drain");
    require(!ApplicationCoordinatorTestAccess::icon(f.coordinator), "S6 zero-role tray removed");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv); QApplication::setQuitOnLastWindowClosed(false);
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    try
    {
        if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "peer") return peer(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
        require(argc == 2, "S6 requires a named case"); run(QString::fromLocal8Bit(argv[1]));
        std::cout << "S6 " << argv[1] << " PASS" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "S6 FAIL: " << e.what() << std::endl; return 1; }
}
