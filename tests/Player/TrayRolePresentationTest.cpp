#include "ApplicationCoordinator.h"
#include "ApplicationPreferences.h"
#include "LauncherWindow.h"
#include "MainWindow.h"
#include "BackgroundMusicWindow.h"

#include <QAbstractNativeEventFilter>
#include <QAccessible>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QCloseEvent>
#include <QMessageBox>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QWindowStateChangeEvent>
#include "DisplayPresentation.h"
#include <windows.h>

#include <iostream>
#include <stdexcept>
#include <sstream>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

class ApplicationCoordinatorTestAccess
{
public:
    static QMenu& menu(ApplicationCoordinator& c) { return c.trayMenu_; }
    static void click(ApplicationCoordinator& c, QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Trigger)
    { QMetaObject::invokeMethod(&c.tray_, "activated", Qt::DirectConnection, Q_ARG(QSystemTrayIcon::ActivationReason, reason)); }
    static void displays(ApplicationCoordinator& c, Platform::DisplayEnumerationResult result)
    { c.enumerateDisplays_ = [result] { return result; }; c.updateTray(); }
    static void icon(ApplicationCoordinator& c, const QIcon& icon) { c.tray_.setIcon(icon); c.updateTray(); }
    static bool allDone(const ApplicationCoordinator& c) { return c.showAllSnapshot_.empty() && c.showAllAwaiting_.isNull(); }
    static QUuid awaiting(const ApplicationCoordinator& c) { return c.showAllAwaiting_; }
    static QUuid terminal(const ApplicationCoordinator& c) { return c.showAllTerminal_; }
    static std::vector<QUuid> snapshot(const ApplicationCoordinator& c) { return c.showAllSnapshot_; }
    static QLocalSocket& control(SessionCoordinatorJoin& j) { return j.socket_; }
    static quint64 lastShow(const SessionCoordinatorJoin& j) { return j.lastShowRequest_; }
    static bool drainPending(const ApplicationCoordinator& c) { return c.drain_.isActive(); }
    static bool iconVisible(const ApplicationCoordinator& c) { return c.tray_.isVisible(); }
};

class LauncherWindowTestAccess
{
public:
    static void translate(LauncherWindow& w) { w.retranslateUi(); }
    static void settings(LauncherWindow& w) { w.openSettings(); }
};

class MainWindowDocumentLifecycleTestAccess
{
public:
    static auto& model(MainWindow& w) { return w.workspaceModel_; }
    static auto& engine(MainWindow& w) { return w.engine_; }
    static auto preview(MainWindow& w) { return w.previewPresentationTargetId_; }
    static bool start(MainWindow& w, PlaylistId p, ItemId i) { return w.startPlaylistPlayback(p, i); }
    static void pause(MainWindow& w) { w.togglePlaybackPause(); w.tickTimer_.stop(); }
    static auto* bgm(MainWindow& w, PlaylistId p) { return w.openBackgroundMusicWindow(p); }
};

namespace
{
constexpr UINT HarnessMessage = WM_APP + 173; // This executable's test-owned HWNDs only.
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
bool waitUntil(const std::function<bool()>& condition, int budget = 6000)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < budget) { QApplication::processEvents(); QThread::msleep(5); }
    return condition();
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
        // Posted windows_dispatcher_MSG supplies no native result slot.
        invoke(static_cast<int>(msg->wParam)); if (result) *result = 0; return true;
    }
};

class RestoreObserver final : public QObject
{
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::WindowStateChange)
        {
            auto* w = qobject_cast<QWidget*>(object);
            const auto* state = static_cast<QWindowStateChangeEvent*>(event);
            if (w && (state->oldState() & Qt::WindowMinimized) && !w->isMinimized() && w->isVisible())
                std::cout << "RESTORE WHILE VISIBLE" << std::endl;
        }
        return false;
    }
};

int peer(const QString& name, bool staged = false)
{
    QTemporaryDir directory;
    ApplicationTranslationState translation;
    const auto slot = sessionSlot(name);
    InstanceAdmissionLease admission;
    require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "S3 peer admission failed");
    InstanceActivationEndpoint activation(slot);
    require(activation.listen(), "S3 peer ACTIVATE failed");
    const auto id = DisplayId::fromTargetDevicePath(name.toStdString());
    MainWindow window(id, translation);
    SessionCoordinatorJoin join(slot, launcherSlot());
    window.setAcceptedCloseCallback([&] { join.stop(); activation.beginShutdown(); QCoreApplication::quit(); });
    window.show();
    require(window.initializeEngine(), "S3 peer real Engine failed");
    window.setWindowTitle(name);
    activation.bindReadyRole(window, [&] { (void)window.trayPresentation().restore(); });
    join.enableControl(window.trayPresentation());
    if (!staged)
    {
        join.start(window, false);
        require(waitUntil([&] { return join.qualified(); }), "S3 peer must qualify its real native control");
    }
    using Access = MainWindowDocumentLifecycleTestAccess;
    auto& model = Access::model(window); auto& engine = Access::engine(window);
    QImage image(80, 50, QImage::Format_RGBA8888); image.fill(Qt::green);
    const auto file = directory.filePath("opaque.png"); require(image.save(file), "S3 image fixture failed");
    const auto playlist = model.addPlaylist(); const auto item = model.addItem(playlist, WorkspaceMediaType::Image, file);
    require(item && model.setImageDisplayDuration(*item, 30) && Access::start(window, playlist, *item), "S3 real playback fixture failed");
    Access::pause(window); require(engine.seek(7), "S3 paused seek failed");
    const auto revision = model.authoredRevision(); const auto preview = Access::preview(window);
    const auto canvas = model.authoredProject().desktopCanvas.aspectRatio;
    const auto incarnation = join.incarnation(); const auto hwnd = reinterpret_cast<HWND>(window.winId());
    NativeHarness harness; harness.hwnd = hwnd;
    RestoreObserver restoreObserver; window.installEventFilter(&restoreObserver);
    std::unique_ptr<QSignalBlocker> heldInput;
    harness.invoke = [&](int action) {
        if (action == 1) window.findChild<QToolButton*>("sessionHideToTray")->click();
        else if (action == 2) window.showMinimized();
        else if (action == 3)
        {
            QTimer::singleShot(0, &window, [] {
                if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    if (auto* discard = prompt->button(QMessageBox::Discard)) discard->click();
            });
            window.close(); // Real QWidget Close, including its closing guard and document prompt.
        }
        else if (action == 4)
        {
            const auto* bgm = Access::bgm(window, playlist);
            if (bgm && bgm->isVisible() && !bgm->parentWidget()) std::cout << "BGM READY" << std::endl;
        }
        else if (action == 5) join.stop();
        else if (action == 6)
        {
            heldInput = std::make_unique<QSignalBlocker>(ApplicationCoordinatorTestAccess::control(join));
            std::cout << "INPUT HELD" << std::endl;
        }
        else if (action == 7)
        {
            heldInput.reset();
            QMetaObject::invokeMethod(&ApplicationCoordinatorTestAccess::control(join), "readyRead", Qt::DirectConnection);
        }
        else if (action == 8 || action == 9)
        {
            auto* bgm = Access::bgm(window, playlist);
            require(bgm && !bgm->parentWidget(), "Actual BGM-owned modal root");
            const auto beforeShow = ApplicationCoordinatorTestAccess::lastShow(join);
            QDialog modal(bgm);
            QFileDialog file(bgm, "S3 test-owned native file dialog");
            file.setFileMode(QFileDialog::ExistingFile);
            auto& dialog = action == 8 ? static_cast<QDialog&>(file) : modal;
            dialog.setWindowModality(Qt::ApplicationModal);
            QElapsedTimer elapsed; elapsed.start();
            QTimer probe; probe.setInterval(20);
            bool ready = false;
            QObject::connect(&probe, &QTimer::timeout, &dialog, [&] {
                const auto root = GetAncestor(reinterpret_cast<HWND>(bgm->winId()), GA_ROOTOWNER);
                const auto popup = GetLastActivePopup(root);
                wchar_t type[32]{}; if (popup) GetClassNameW(popup, type, 32);
                const bool observed = action == 8 ? (popup != root && wcscmp(type, L"#32770") == 0 && IsWindowVisible(popup))
                    : QApplication::activeModalWidget() == &modal;
                if (observed && !ready) { ready = true; std::cout << "MODAL READY" << std::endl; }
                if (observed && ApplicationCoordinatorTestAccess::lastShow(join) > beforeShow)
                {
                    const bool healthy = action == 8 ? (file.testAttribute(Qt::WA_DontShowOnScreen) && !IsWindowEnabled(root) && IsWindowEnabled(popup))
                        : modal.isVisible() && !window.trayPresentation().eligible();
                    if (healthy) std::cout << "MODAL SHOW PASS" << std::endl;
                    probe.stop();
                    if (action == 8) PostMessageW(popup, WM_CLOSE, 0, 0); else modal.reject();
                }
                else if (elapsed.elapsed() >= 5000) { probe.stop(); dialog.reject(); }
            });
            probe.start(); dialog.exec();
        }
        else if (action == 10) join.start(window, false);
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    QTimer check;
    QObject::connect(&check, &QTimer::timeout, &window, [&] {
        if (!engine.isInitialized()) return;
        if (window.winId() != reinterpret_cast<WId>(hwnd) || join.incarnation() != incarnation
            || model.authoredRevision() != revision || Access::preview(window) != preview
            || model.authoredProject().desktopCanvas.aspectRatio != canvas || model.activeItemId() != item
            || !engine.isPaused() || engine.playbackTime() != 7)
        { std::cerr << "S3 runtime identity/state changed" << std::endl; QCoreApplication::exit(2); }
    });
    check.start(10);
    std::cout << "READY " << reinterpret_cast<quintptr>(hwnd) << " " << incarnation.toString().toStdString() << std::endl;
    QTimer::singleShot(55000, &window, [] { QCoreApplication::exit(3); });
    const int result = QApplication::exec();
    QApplication::instance()->removeNativeEventFilter(&harness);
    std::cout << "PEER DONE " << result << std::endl;
    return result;
}

struct Child
{
    QProcess process;
    QByteArray output;
    HWND hwnd{};
    QUuid incarnation;
    DisplayId id;
    void read() { output += process.readAllStandardOutput(); output += process.readAllStandardError(); }
    explicit Child(const QString& name, bool staged = false) : id(DisplayId::fromTargetDevicePath(name.toStdString()))
    {
        process.start(QCoreApplication::applicationFilePath(), {staged ? "peer-staged" : "peer", name});
        require(process.waitForStarted(5000), "S3 owned peer failed to start");
        require(waitUntil([&] { read(); return output.contains("READY ") || process.state() == QProcess::NotRunning; }), "S3 real peer readiness timed out");
        const auto line = output.mid(output.indexOf("READY ")).split('\n').first().trimmed().split(' ');
        require(line.size() == 3 && process.state() == QProcess::Running, "S3 peer did not enroll/initialize");
        hwnd = reinterpret_cast<HWND>(line[1].toULongLong()); incarnation = QUuid(QString::fromUtf8(line[2]));
        require(IsWindow(hwnd) && !incarnation.isNull(), "S3 native peer identity invalid");
    }
    void command(int action) { if (!PostMessageW(hwnd, HarnessMessage, action, 0)) { read(); std::cerr << output.constData(); throw std::runtime_error("S3 owned HWND command failed"); } }
    void close()
    {
        command(3);
        if (!waitUntil([&] { return process.state() == QProcess::NotRunning; })) { read(); std::cerr << output.constData(); throw std::runtime_error("S3 peer normal Close did not finish"); }
        read(); if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || output.count("Desktop Scene Engine initialized.") != 1)
            std::cerr << "EXIT " << process.exitCode() << " status=" << process.exitStatus() << "\n" << output.constData();
        require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && output.count("Desktop Scene Engine initialized.") == 1,
            "S3 normal peer exit / single Engine proof failed");
    }
    ~Child()
    {
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(5000); }
    }
};

struct Fixture
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("preferences.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore preferences{settings};
    ApplicationTranslationState translation;
    InstanceSlot slot = launcherSlot();
    InstanceAdmissionLease admission;
    InstanceActivationEndpoint activation{slot};
    int drains = 0;
    ApplicationCoordinator coordinator{slot, activation, false, [&] { ++drains; }};
    Fixture()
    {
        require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "Existing coordinator: refuse interference");
        require(activation.listen() && coordinator.listen(), "S3 real coordinator failed");
        coordinator.setLauncherFactory([&] {
            auto* w = new LauncherWindow(translation, preferences);
            w->setAcceptedCloseCallback([&, w] { coordinator.retireLauncher(*w); }); return w;
        });
        require(coordinator.showLauncher(), "S3 real Launcher failed");
    }
    auto roles() { QList<QAction*> result; for (auto* a : ApplicationCoordinatorTestAccess::menu(coordinator).actions()) if (a->menu()) result.push_back(a); return result; }
    QAction* show(const QUuid& incarnation)
    {
        for (auto* a : roles()) if (a->data().toUuid() == incarnation) return a->menu()->actions().front();
        return nullptr;
    }
};

Platform::DisplayEnumerationResult displays(std::initializer_list<DisplayId> ids)
{
    Platform::DisplayEnumerationResult result{Platform::DisplayEnumerationStatus::Success};
    int number = 0;
    for (const auto& id : ids)
    {
        Platform::DisplayDescriptor d; d.id = id; d.friendlyName = "Same friendly display";
        d.displayNumber = ++number; d.pixelWidth = 1920; d.pixelHeight = 1080;
        result.snapshot.displays.push_back(d);
    }
    return result;
}

struct CoutCapture
{
    std::ostringstream stream;
    std::streambuf* previous = std::cout.rdbuf(stream.rdbuf());
    ~CoutCapture() { std::cout.rdbuf(previous); }
};

void hold(Child& child)
{
    child.command(6);
    require(waitUntil([&] { child.read(); return child.output.contains("INPUT HELD"); }), "Must hold actual native control delivery before snapshot");
}

void hidden(Child& child)
{
    child.command(1);
    require(waitUntil([&] { return IsWindow(child.hwnd) && !IsWindowVisible(child.hwnd); }), "Must establish real live HiddenToTray peer");
}

void run(const QString& name)
{
    Fixture f;
    const auto nonce = "S3-" + QUuid::createUuid().toString();
    Child a(nonce + "-A"), b(nonce + "-B");
    const auto mixed = [&] {
        const bool ready = waitUntil([&] { return IsIconic(a.hwnd) && !IsWindowVisible(b.hwnd); });
        if (!ready) { a.read(); b.read(); std::cerr << "A:\n" << a.output.constData() << "B:\n" << b.output.constData(); }
        return ready;
    };
    require(f.coordinator.sessionCount() == 2, "S3 requires two actually joined independent Session processes");
    QPointer<QWidget> launcher = f.coordinator.launcherWindow();
    const auto incarnation = f.coordinator.launcherIncarnation(); const auto hwnd = launcher->winId();
    if (name == "menu")
    {
        require(f.roles().size() == 3, "S3 RED A: real Launcher plus two joined Sessions must project three primary-role submenus");
        a.command(2); b.command(1);
        require(mixed(), "S3 mixed visibility fixture failed");
        require(f.roles().size() == 3, "Visible/minimized/hidden primary roles must all remain in menu");
        b.command(4); require(waitUntil([&] { b.read(); return b.output.contains("BGM READY"); }), "S3 real modeless BGM readiness failed");
        require(f.roles().size() == 3, "BGM/dialogs are not primary roles");
    }
    else if (name == "launcher")
    {
        auto* button = launcher->findChild<QToolButton*>("launcherHideToTray");
        if (button) button->click();
        require(waitUntil([&] { return !launcher->isVisible(); }, 500), "S3 RED B: real Launcher client Hide must hide, not Close, the same incarnation");
        require(launcher && launcher->winId() == hwnd && f.coordinator.launcherIncarnation() == incarnation
            && IsWindowVisible(a.hwnd) && IsWindowVisible(b.hwnd), "Launcher Hide must preserve object/incarnation and Sessions");
        require(f.roles().size() == 3, "Hidden Launcher must remain a role");
        auto* accessible = QAccessible::queryAccessibleInterface(button);
        require(accessible && accessible->text(QAccessible::Name) == QCoreApplication::translate("LauncherWindow", "Hide to System Tray")
            && button->shortcut().isEmpty() && button->parentWidget() == static_cast<LauncherWindow*>(launcher.data())->centralWidget(), "Launcher accessible client control contract");
        f.roles().front()->menu()->actions().front()->trigger();
        require(waitUntil([&] { return launcher->isVisible(); }), "Actual Launcher Show action must restore same UI");
        launcher->showMaximized(); QApplication::processEvents();
        const auto normalGeometry = launcher->normalGeometry();
        button->click(); require(!launcher->isVisible(), "Maximized Launcher Hide");
        f.show(incarnation)->trigger();
        require(launcher->isVisible() && launcher->isMaximized() && launcher->normalGeometry() == normalGeometry
            && IsZoomed(reinterpret_cast<HWND>(hwnd))
            && launcher->winId() == hwnd, "Same maximized Launcher restoration");
        launcher->showMinimized(); f.show(incarnation)->trigger();
        require(!launcher->isMinimized() && launcher->isMaximized() && IsZoomed(reinterpret_cast<HWND>(hwnd)), "Minimized-maximized Launcher Show must not force normal state");
    }
    else if (name == "all" || name == "nonactivation")
    {
        a.command(2); b.command(1);
        require(mixed(), "S3 mixed state must be real before left-click");
        CoutCapture capture;
        ApplicationCoordinatorTestAccess::click(f.coordinator);
        require(waitUntil([&] { return !IsIconic(a.hwnd) && IsWindowVisible(b.hwnd); }, 1500),
            "S3 RED C: real tray Trigger must restore all snapshot Session HWNDs, not only one Session");
        require(waitUntil([&] { return ApplicationCoordinatorTestAccess::allDone(f.coordinator); }), "Actual Show completion must settle snapshot");
        a.read(); b.read();
        if (name == "nonactivation") require(!a.output.contains("RESTORE WHILE VISIBLE"),
            "Nonterminal Show must avoid Qt Windows visible-minimized setWindowState path, which requests native activation despite WA_ShowWithoutActivating");
        require(a.output.count("activation=none") == 1 && b.output.count("activation=none") == 1
            && !a.output.contains("activation=request") && !b.output.contains("activation=request")
            && QString::fromStdString(capture.stream.str()).count("Launcher Show activation=request") == 1,
            "Show All must issue two non-activating Session Shows and one terminal Launcher activation intent");
    }
    else if (name == "lifecycle")
    {
        QObject retiredActionOwner;
        QPointer<QAction> oldLauncherAction = f.show(incarnation);
        require(oldLauncherAction, "Actual old Launcher action"); oldLauncherAction->setParent(&retiredActionOwner);
        QPointer<QAction> oldSessionAction = f.show(a.incarnation);
        require(oldSessionAction, "Actual old Session action"); oldSessionAction->setParent(&retiredActionOwner);
        require(launcher->close() && waitUntil([&] { return !launcher; }), "S3 ordinary Launcher Close must destroy UI first");
        require(f.roles().size() == 2, "S3 RED D: actual Launcher role teardown must leave exactly both living Session submenus");
        launcher = f.coordinator.showLauncher();
        require(launcher && f.coordinator.launcherIncarnation() != incarnation && f.roles().size() == 3,
            "Launcher recreation must produce a new role/action incarnation");
        auto* hide = launcher->findChild<QToolButton*>("launcherHideToTray"); hide->click();
        require(!launcher->isVisible() && oldLauncherAction, "Replacement Launcher hidden / stale action still valid");
        oldLauncherAction->trigger();
        require(!launcher->isVisible(), "Old Launcher QAction must not control replacement incarnation");
        f.show(f.coordinator.launcherIncarnation())->trigger();
        a.close();
        require(waitUntil([&] { return f.coordinator.sessionCount() == 1 && f.roles().size() == 2; }), "Actual Session normal process exit must remove its primary role");
        Child replacement(nonce + "-A");
        require(f.roles().size() == 3 && replacement.incarnation != a.incarnation, "Replacement must enroll as a fresh role");
        hidden(replacement);
        require(oldSessionAction && oldSessionAction != f.show(replacement.incarnation), "Replacement uses a distinct real action");
        oldSessionAction->trigger(); f.show(replacement.incarnation)->trigger();
        require(waitUntil([&] { replacement.read(); return IsWindowVisible(replacement.hwnd) && replacement.output.contains("[Tray] Show completed=1"); }), "Replacement's own Show action must work");
        require(replacement.output.count("[Tray] Show completed=1") == 1, "Old Session QAction must dispatch no replacement command");
        replacement.close();
    }
    else if (name == "ordering")
    {
        auto ordered = displays({b.id, a.id});
        ApplicationCoordinatorTestAccess::displays(f.coordinator, ordered);
        require(f.roles()[0]->data().toUuid() == incarnation && f.roles()[1]->data().toUuid() == b.incarnation
            && f.roles()[2]->data().toUuid() == a.incarnation && f.roles()[1]->text() != f.roles()[2]->text(),
            "Actual registry menu must follow injected Platform snapshot order and disambiguate collisions only in presentation");
        const auto lastLabel = displayLabel(ordered.snapshot.displays[1]);
        ApplicationCoordinatorTestAccess::displays(f.coordinator, displays({b.id}));
        require(f.roles()[1]->data().toUuid() == b.incarnation && f.roles()[2]->text().startsWith(lastLabel)
            && f.roles()[2]->text().endsWith(" — Disconnected"), "Unresolved Session keeps last-known friendly label");
        ApplicationCoordinatorTestAccess::displays(f.coordinator, displays({}));
        require(f.roles()[1]->data().toUuid() == a.incarnation && f.roles()[2]->data().toUuid() == b.incarnation,
            "Unresolved stable exact-ID order independent of Launcher UI rows");
        require(f.translation.setLanguage(UiLanguage::SimplifiedChinese) == UiLanguage::SimplifiedChinese, "Actual Chinese catalog");
        LauncherWindowTestAccess::translate(*static_cast<LauncherWindow*>(launcher.data()));
        ApplicationCoordinatorTestAccess::displays(f.coordinator, displays({}));
        require(f.roles()[0]->text() == QString::fromUtf8("启动器") && f.show(a.incarnation)->text() == QString::fromUtf8("显示")
            && f.roles()[1]->text().endsWith(QString::fromUtf8(" — 已断开连接"))
            && launcher->findChild<QToolButton*>("launcherHideToTray")->accessibleName() == QString::fromUtf8("隐藏到系统托盘"),
            "Menu/last-known label and Launcher Hide retranslate through existing catalog");
    }
    else if (name == "modal")
    {
        auto& w = *static_cast<LauncherWindow*>(launcher.data());
        bool observed = false;
        QTimer::singleShot(0, &w, [&] {
            auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            require(modal && modal->parentWidget() == &w, "Actual Launcher-owned Settings top-level modal");
            w.trayPresentation().refreshEligibility();
            auto* button = w.findChild<QToolButton*>("launcherHideToTray");
            require(!button->isEnabled() && !w.trayPresentation().eligible(), "Relevant Settings modal rejects Launcher Hide");
            w.trayPresentation().requestHide(); require(w.isVisible(), "Fresh modal guard must keep Launcher visible");
            f.show(incarnation)->trigger(); require(modal->isVisible(), "Launcher Show must surface blocker, not destroy it");
            observed = true; modal->reject();
        });
        LauncherWindowTestAccess::settings(w);
        require(observed && w.isVisible() && IsWindowVisible(a.hwnd) && IsWindowVisible(b.hwnd), "Modal gating preserves independent Sessions");
    }
    else if (name == "qualification")
    {
        launcher->findChild<QToolButton*>("launcherHideToTray")->click(); require(!launcher->isVisible(), "Launcher legitimately hidden first");
        ApplicationCoordinatorTestAccess::icon(f.coordinator, {});
        require(launcher->isVisible() && launcher->winId() == hwnd && f.coordinator.launcherIncarnation() == incarnation
            && !launcher->findChild<QToolButton*>("launcherHideToTray")->isEnabled(), "Lost actual tray capability must fail visible without UI replacement");
        ApplicationCoordinatorTestAccess::icon(f.coordinator, QApplication::windowIcon());
    }
    else if (name == "native-modal-all" || name == "application-modal-all")
    {
        a.command(name == "native-modal-all" ? 8 : 9);
        require(waitUntil([&] { a.read(); return a.output.contains("MODAL READY"); }), "Actual BGM-owned blocking interaction must be ready before Show All");
        CoutCapture capture;
        ApplicationCoordinatorTestAccess::click(f.coordinator);
        require(waitUntil([&] { a.read(); return a.output.contains("MODAL SHOW PASS") && ApplicationCoordinatorTestAccess::allDone(f.coordinator); }),
            "Real Show All must complete through BGM-owned native/application-modal loop while surfacing its live blocker");
        b.read();
        require(a.output.count("activation=none") == 1 && b.output.count("activation=none") == 1
            && !a.output.contains("activation=request") && f.roles().size() == 3
            && QString::fromStdString(capture.stream.str()).count("Launcher Show activation=request") == 1,
            "BGM/modal not a role; only frozen terminal requests activation");
    }
    else if (name == "drain-join")
    {
        Child staged(nonce + "-staged", true);
        require(f.coordinator.sessionCount() == 2, "Ready admitted staged peer is not falsely projected before Join");
        a.close(); b.close();
        require(waitUntil([&] { return f.coordinator.sessionCount() == 0; }), "Old native Session lifetimes genuinely retired");
        require(launcher->close() && waitUntil([&] { return !launcher && ApplicationCoordinatorTestAccess::drainPending(f.coordinator); }),
            "Actual zero-role bounded drain must be pending before valid Join");
        require(f.roles().isEmpty() && !ApplicationCoordinatorTestAccess::iconVisible(f.coordinator), "Zero roles have no phantom menu/icon");
        staged.command(10);
        require(waitUntil([&] { staged.read(); return f.coordinator.sessionCount() == 1 && staged.output.contains("Coordinator Session joined."); }),
            "Already-ready real native peer must Join during drain and retain same coordinator");
        require(f.drains == 0 && f.roles().size() == 1 && ApplicationCoordinatorTestAccess::iconVisible(f.coordinator), "Valid Join cancels drain without fictitious Launcher");
        staged.close();
        require(waitUntil([&] { return f.drains == 1 && f.coordinator.sessionCount() == 0; }), "Last real role still drains exactly once");
        return;
    }
    else if (name == "snapshot" || name == "terminal-loss" || name == "replacement")
    {
        hidden(a); hidden(b); hold(a);
        CoutCapture capture;
        ApplicationCoordinatorTestAccess::click(f.coordinator);
        const auto snapshot = ApplicationCoordinatorTestAccess::snapshot(f.coordinator);
        require(snapshot.size() == 3 && ApplicationCoordinatorTestAccess::awaiting(f.coordinator) == a.incarnation
            && ApplicationCoordinatorTestAccess::terminal(f.coordinator) == incarnation, "Actual Show All snapshot and nonterminal in flight");
        if (name == "terminal-loss")
        {
            require(launcher->close() && waitUntil([&] { return !launcher; }), "Old terminal role genuinely removed");
            launcher = f.coordinator.showLauncher(); launcher->findChild<QToolButton*>("launcherHideToTray")->click();
            require(!launcher->isVisible() && f.coordinator.launcherIncarnation() != incarnation, "New terminal replacement hidden");
        }
        std::unique_ptr<Child> later;
        if (name == "replacement")
        {
            hold(b); a.close();
            require(waitUntil([&] { return f.coordinator.sessionCount() == 1; }), "Old in-flight incarnation removed");
            later = std::make_unique<Child>(nonce + "-A"); hidden(*later); b.command(7);
        }
        else
        {
            later = std::make_unique<Child>(nonce + "-C"); hidden(*later); a.command(7);
        }
        require(waitUntil([&] { return ApplicationCoordinatorTestAccess::allDone(f.coordinator); }), "Snapshot must finish without including new/replacement roles");
        later->read(); require(!IsWindowVisible(later->hwnd) && !later->output.contains("[Tray] Show completed="), "New/replacement incarnation excluded from frozen snapshot");
        require(IsWindowVisible(b.hwnd), "Other reachable snapshot role still restored");
        if (name == "terminal-loss") require(!launcher->isVisible() && capture.stream.str().find("activation=request") == std::string::npos,
            "Disappeared terminal gets no fallback activation of replacement");
        later->close();
    }
    else if (name == "overlap" || name == "unavailable")
    {
        require(launcher->close() && waitUntil([&] { return !launcher; }), "No Launcher terminal fixture");
        hidden(a); hidden(b);
        if (name == "unavailable")
        {
            b.command(5); require(waitUntil([&] { return IsWindowVisible(b.hwnd) && !f.show(b.incarnation)->isEnabled(); }), "Real stopped control is visible but unavailable, not role death");
        }
        hold(b); // canonical first A is dispatched last, with the sole terminal activation.
        ApplicationCoordinatorTestAccess::click(f.coordinator);
        ApplicationCoordinatorTestAccess::click(f.coordinator, QSystemTrayIcon::DoubleClick);
        ApplicationCoordinatorTestAccess::click(f.coordinator);
        ApplicationCoordinatorTestAccess::click(f.coordinator, QSystemTrayIcon::MiddleClick);
        if (name == "overlap") b.command(7);
        require(waitUntil([&] { return ApplicationCoordinatorTestAccess::allDone(f.coordinator) && IsWindowVisible(a.hwnd); }), "Coalesced snapshot restores reachable roles");
        a.read(); b.read(); require(a.output.count("activation=request") == 1 && b.output.count("activation=request") == 0
            && (name == "unavailable" || b.output.count("activation=none") == 1), "First canonical Session is the sole terminal; overlapping/double/middle signals do not create duplicate work");
        launcher = f.coordinator.showLauncher();
    }
    else throw std::runtime_error("Unknown S3 case");
    if (a.process.state() != QProcess::NotRunning) a.close();
    b.close();
    require(waitUntil([&] { return f.coordinator.sessionCount() == 0; }), "S3 native termination bookkeeping did not settle");
    require(launcher && launcher->close() && waitUntil([&] { return !launcher && f.drains == 1; }), "S3 zero-role bounded drain failed");
    require(f.roles().isEmpty() && !ApplicationCoordinatorTestAccess::iconVisible(f.coordinator), "No zero-role menu/icon survives normal teardown");
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv); application.setQuitOnLastWindowClosed(false);
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    try
    {
        if (application.arguments().value(1).startsWith("peer")) return peer(application.arguments().value(2), application.arguments().value(1) == "peer-staged");
        run(application.arguments().value(1));
        std::cout << "S3 " << application.arguments().value(1).toStdString() << " PASS" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
