#include "MainWindow.h"
#include "ApplicationCoordinator.h"
#include "LauncherWindow.h"
#include "BackgroundMusicWindow.h"
#include "MainWindowTestDisplay.h"
#include "CanvasChromeFrame.h"
#include "Infrastructure/Persistence/ProjectFile.h"
#include "Platform/Windows/DesktopPresentationHostTestAccess.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/Renderer.h"

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <QElapsedTimer>
#include <QThread>
#include <QPixmap>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolButton>
#include <QSettings>
#include <QtEndian>
#include <QProcess>
#include <QAbstractNativeEventFilter>
#include <QMessageBox>
#include <QFileDialog>
#include <QAbstractButton>
#include <Windows.h>
#include <dxgi.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <vector>

class ApplicationCoordinatorTestAccess
{
public:
    static QMenu& menu(DesktopSceneEngine::Player::ApplicationCoordinator& c) { return c.trayMenu_; }
    static QLocalSocket& control(DesktopSceneEngine::Player::SessionCoordinatorJoin& j) { return j.socket_; }
    static QUuid epoch(DesktopSceneEngine::Player::SessionCoordinatorJoin& j) { return j.coordinatorEpoch_; }
    static quint64 revision(DesktopSceneEngine::Player::SessionCoordinatorJoin& j) { return j.deploymentRevision_; }
    static void click(DesktopSceneEngine::Player::ApplicationCoordinator& c)
    { QMetaObject::invokeMethod(&c.tray_, "activated", Qt::DirectConnection, Q_ARG(QSystemTrayIcon::ActivationReason, QSystemTrayIcon::Trigger)); }
    static bool allDone(DesktopSceneEngine::Player::ApplicationCoordinator& c) { return c.showAllSnapshot_.empty(); }
    static void icon(DesktopSceneEngine::Player::ApplicationCoordinator& c, QIcon icon)
    { c.tray_.setIcon(std::move(icon)); c.updateTray(); }
    static void observe(DesktopSceneEngine::Player::ApplicationCoordinator& c, QByteArray& frames)
    {
        QObject::connect(&c.server_, &QLocalServer::newConnection, &c, [&c, &frames] {
            for (auto* socket : c.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
                if (!socket->property("s5Observed").toBool())
                {
                    socket->setProperty("s5Observed", true);
                    QObject::connect(socket, &QLocalSocket::readyRead, &c, [socket, &frames] {
                        if (socket->property("sessionControl").toBool()) frames += socket->peek(socket->bytesAvailable());
                    }); // Registered before the real control consumer; never consumes or manufactures a reply.
                }
        });
    }
};

class MainWindowDesktopPresentationTestAccess
{
public:
    static DesktopSceneEngine::Engine& engine(MainWindow& window) { return window.engine_; }
    static auto& host(MainWindow& window) { return window.desktopHost_; }
    static auto preview(const MainWindow& window) { return window.previewPresentationTargetId_; }
    static auto desktop(const MainWindow& window) { return window.desktopPresentationTargetId_; }
    static auto canvasOutsideMode(const MainWindow& window) { return window.currentCanvasOutsideMode_; }
    static auto canvasOutsideFrame(const MainWindow& window) { return window.canvasOutsideInstructionForFrame(); }
    static const auto& bound(const MainWindow& window) { return window.displaySelection_.boundDisplayId(); }
    static auto& creator(MainWindow& window) { return window.desktopHostCreatorForTesting_; }
    static auto& reconciler(MainWindow& window) { return window.desktopHostReconcilerForTesting_; }
    static auto& enumerator(MainWindow& window) { return window.displayEnumeratorForTesting_; }
    static bool desired(const MainWindow& window) { return window.desktopDeploymentDesired_; }
    static bool button(MainWindow& window, const char* action)
    {
        const auto* button = window.findChild<QToolButton*>(QStringLiteral("deployDisplayButton"));
        return button && button->text() == QCoreApplication::translate("MainWindow", action);
    }
    static bool hasManualRefreshButton(MainWindow& window)
    {
        const QString refresh = QCoreApplication::translate("MainWindow", "Refresh");
        const auto buttons = window.findChildren<QToolButton*>();
        return std::any_of(buttons.cbegin(), buttons.cend(), [&refresh](const QToolButton* button)
        {
            return button != nullptr && button->text() == refresh;
        });
    }
    static bool clickDeploymentButton(MainWindow& window)
    {
        auto* button = window.findChild<QToolButton*>(QStringLiteral("deployDisplayButton"));
        if (!button || !button->isEnabled()) return false;
        button->click(); // Real existing UI action, never the private deployment helper.
        return true;
    }
    static void stopTimer(MainWindow& window) { window.tickTimer_.stop(); }
    static bool timerStopped(const MainWindow& window) { return !window.tickTimer_.isActive(); }
    static void tick(MainWindow& window) { window.onTick(); }
    static bool reconcile(MainWindow& window, bool allowContinuation)
    { return window.reconcileDesktopPresentation(allowContinuation); }
    static bool reconciliationQueued(const MainWindow& window)
    { return window.desktopEndpointReconciliationQueued_; }
    static bool continuationArmed(const MainWindow& window)
    { return window.desktopReconciliationContinuationTimer_.isActive(); }
    static bool fireContinuation(MainWindow& window)
    {
        if (!window.desktopReconciliationContinuationTimer_.isActive()) return false;
        window.runDesktopPresentationContinuation();
        return true;
    }
    static void invokeContinuation(MainWindow& window)
    { window.runDesktopPresentationContinuation(); }
    static void shutdown(MainWindow& window) { window.shutdownEngine(); }
    static void language(MainWindow& window, DesktopSceneEngine::Player::UiLanguage language)
    { window.applyCommittedLanguage(language); }
    static void pause(MainWindow& window) { window.togglePlaybackPause(); }
    static void next(MainWindow& window) { window.navigateScene(false); }
    static auto& model(MainWindow& window) { return window.workspaceModel_; }
    static const auto& document(const MainWindow& window) { return window.documentState_; }
    static void cleanDocument(MainWindow& window)
    { window.documentState_.markClean(window.workspaceModel_.authoredRevision()); window.updateDocumentTitle(); }
    static void documentPath(MainWindow& window, const QString& path)
    { window.documentState_.currentPath = std::filesystem::path(path.toStdWString()); window.updateDocumentTitle(); }
    static auto* bgm(MainWindow& window, DesktopSceneEngine::Player::PlaylistId id)
    { return window.openBackgroundMusicWindow(id); }
    static void newDocument(MainWindow& window) { window.createNewDocument(); }
    static bool load(MainWindow& window, const std::filesystem::path& path)
    {
        auto candidate = window.loadProjectCandidate(path);
        if (!candidate) return false;
        window.commitOpenProject(std::move(*candidate));
        return true;
    }
    static void enumeration(MainWindow& window, const DesktopSceneEngine::Platform::DisplayEnumerationResult& result)
    { window.applyDisplayEnumerationResult(result); }
    static void enumerator(MainWindow& window, DesktopSceneEngine::Platform::DisplayEnumerationResult result)
    { window.displayEnumeratorForTesting_ = [result = std::move(result)] { return result; }; }
    static bool canvasUi(MainWindow& window, DesktopSceneEngine::Domain::AspectRatio aspect)
    {
        const auto* label = window.findChild<QLabel*>(QStringLiteral("currentCanvasAspectLabel"));
        window.previewViewportHost_->resize(1600, 1000);
        QCoreApplication::processEvents();
        const auto size = window.canvasFrame_->canvasRect().size();
        return label && label->text().endsWith(QStringLiteral("%1:%2").arg(aspect.numerator).arg(aspect.denominator))
            && !size.isEmpty() && std::llabs(static_cast<long long>(size.width()) * aspect.denominator
                - static_cast<long long>(size.height()) * aspect.numerator) <= 16;
    }
    static const void* projection(const MainWindow& window)
    { return window.activePlaybackProjection_ ? &window.activePlaybackProjection_->project : nullptr; }
    static bool start(MainWindow& window, DesktopSceneEngine::Player::PlaylistId playlist,
        DesktopSceneEngine::Player::ItemId item)
    { return window.startPlaylistPlayback(playlist, item); }
    static bool inspect(MainWindow& window, DesktopSceneEngine::Player::ItemId item)
    {
        if (!window.workspaceModel_.selectItem(item)) return false;
        window.refreshInspector();
        return true;
    }
    static void seek(MainWindow& window, int value)
    {
        window.beginTimelineDrag();
        window.positionSlider_->setValue(value);
        window.commitTimelineDrag();
    }
    static void sendNative(MainWindow& window, bool taskbar)
    {
        const UINT message = taskbar ? window.taskbarCreatedMessage_ : WM_DISPLAYCHANGE;
        SendMessageW(reinterpret_cast<HWND>(window.winId()), message, 0, 0);
    }
    static void native(MainWindow& window, bool taskbar)
    {
        sendNative(window, taskbar);
        QCoreApplication::processEvents();
    }
};

class LauncherWindowTestAccess
{
public:
    static void settings(DesktopSceneEngine::Player::LauncherWindow& window) { window.openSettings(); }
};

namespace DesktopSceneEngine::Rendering
{
class CanvasTargetFitTestAccess
{
public:
    static GraphicsBackend& backend(Engine& engine) { return engine.renderer_->backend_; }
    static const void* scene(Engine& engine) { return engine.sceneRuntime_.get(); }
    static const void* playback(Engine& engine) { return engine.playbackRuntime_.get(); }
    static const void* media(Engine& engine) { return engine.mediaResource_.get(); }
    static const void* audioMixer(Engine& engine) { return engine.audioMixer_.get(); }
    static const void* audioOutput(Engine& engine) { return engine.audioOutput_.get(); }
    static const void* backgroundMusicRuntime(Engine& engine)
    { return engine.backgroundMusicRuntime_.get(); }
    static const void* backgroundMusicExecutor(Engine& engine)
    { return engine.backgroundMusicExecutor_.get(); }
    // Deterministic Engine detach-rejection test control; the real backend and
    // its borrowed native HWND remain alive. This is not device-loss simulation.
    static bool& initialized(Engine& engine) { return engine.initialized_; }
};
}

namespace
{
using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Platform;
using namespace DesktopSceneEngine::Platform::Windows;
using Access = MainWindowDesktopPresentationTestAccess;
using Gpu = Rendering::CanvasTargetFitTestAccess;

bool require(bool condition, const char* label)
{ if (!condition) std::cerr << "Test failed: " << label << '\n'; return condition; }
Rendering::GraphicsBackend& backend(MainWindow& window) { return Gpu::backend(Access::engine(window)); }

bool writeRecoveryBmp(const QString& path)
{
    constexpr std::array<unsigned char, 58> bytes{
        0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0B,
        0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0xDB,
        0x0D, 0xFF};
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly))
    {
        std::cerr << "BMP open failed: " << path.toStdString() << " "
            << output.errorString().toStdString() << '\n';
        return false;
    }
    const auto written = output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<qint64>(bytes.size()));
    if (written != static_cast<qint64>(bytes.size()))
        std::cerr << "BMP write failed: " << written << " "
            << output.errorString().toStdString() << '\n';
    return written == static_cast<qint64>(bytes.size());
}

// Native fixtures own every window/class they create; none belong to Explorer.
struct ShellFixture
{
    ShellFixture()
    {
        for (const auto* name : {L"Progman", L"SHELLDLL_DefView", L"SysListView32", L"WorkerW"})
        {
            WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW;
            type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = name;
            const auto atom = RegisterClassW(&type);
            if (atom) classes.push_back(name);
            valid = valid && atom != 0;
        }
        if (!valid) return;
        valid = createGeneration();
        display.id = Player::TestSupport::mainWindowTestDisplayId();
        display.desktopX = 140; display.desktopY = 170;
        display.pixelWidth = 377; display.pixelHeight = 211;
    }
    bool createGeneration()
    {
        if (root) return false;
        if (previousRoot)
            recycleBlocker = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                L"WorkerW", L"DSE topology generation separator", WS_POPUP,
                0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        root = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"Progman", L"Slice 4 test-owned shell",
            WS_POPUP, 100, 100, 600, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        worker = child(L"WorkerW", root, 0);
        defView = child(L"SHELLDLL_DefView", root, WS_EX_LAYERED);
        icons = child(L"SysListView32", defView, 0);
        const bool created = root && worker && defView && icons
            && SetLayeredWindowAttributes(defView, 0, 255, LWA_ALPHA)
            && SetWindowPos(defView, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(root, SW_SHOWNOACTIVATE);
        return created;
    }
    bool destroyGeneration()
    {
        if (!root) return true;
        previousRoot = root;
        const bool destroyed = DestroyWindow(root) != FALSE;
        root = nullptr; worker = nullptr; defView = nullptr; icons = nullptr;
        return destroyed;
    }
    ~ShellFixture()
    {
        if (root) DestroyWindow(root);
        if (recycleBlocker) DestroyWindow(recycleBlocker);
        for (const auto* name : classes) UnregisterClassW(name, GetModuleHandleW(nullptr));
    }
    bool intact() const { return IsWindow(root) && IsWindow(worker) && IsWindow(defView) && IsWindow(icons); }
    static HWND child(const wchar_t* name, HWND parent, DWORD exStyle)
    { return CreateWindowExW(exStyle, name, L"", WS_CHILD | WS_VISIBLE, 0, 0, 600, 400,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr); }
    std::vector<const wchar_t*> classes;
    HWND root = nullptr, worker = nullptr, defView = nullptr, icons = nullptr;
    HWND previousRoot = nullptr, recycleBlocker = nullptr;
    DisplayDescriptor display;
    bool valid = true;
};

// Observe destruction of a TEST-owned DSE HWND, forwarding its original owner
// procedure unchanged. This would fail if MainWindow destroys before detach.
struct DestroyObservation
{
    DestroyObservation(MainWindow& owner, bool stopped) : window(owner), expectStopped(stopped)
    {
        handle = static_cast<HWND>(Access::host(window).nativeHandle());
        installed = handle && SetPropW(handle, property, this);
        if (installed)
            original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(handle, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&procedure)));
        installed = installed && original;
    }
    ~DestroyObservation()
    {
        if (installed && IsWindow(handle) && GetPropW(handle, property) == this)
        {
            SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
            RemovePropW(handle, property);
        }
    }
    static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* observation = static_cast<DestroyObservation*>(GetPropW(handle, property));
        if (!observation) return DefWindowProcW(handle, message, wParam, lParam);
        const auto original = observation->original;
        if (message == WM_DESTROY)
        {
            auto& window = observation->window;
            observation->destroyed = true;
            observation->detachedFirst = !Access::desktop(window).isValid()
                && backend(window).presentationTargetCountForTesting() == 1
                && backend(window).pacingPresentationTargetForTesting() == Access::preview(window);
            observation->stoppedFirst = !observation->expectStopped || Access::timerStopped(window);
            Access::sendNative(window, true); // Cannot recursively replace or recreate during shutdown.
        }
        if (message == WM_NCDESTROY) RemovePropW(handle, property);
        return CallWindowProcW(original, handle, message, wParam, lParam);
    }
    static constexpr const wchar_t* property = L"DSESlice4TestDestroyObservation";
    MainWindow& window;
    HWND handle = nullptr;
    WNDPROC original = nullptr;
    bool installed = false, destroyed = false, detachedFirst = false, stoppedFirst = false;
    bool expectStopped;
};

struct CreationControl
{
    void install(MainWindow& window)
    {
        Access::enumerator(window) = [this]
        {
            return DisplayEnumerationResult{DisplayEnumerationStatus::Success,
                missing
                    ? (exposeUnrelatedDisplayWhenMissing
                        ? DisplaySnapshot{{unrelatedDisplay}} : DisplaySnapshot{})
                    : DisplaySnapshot{{shell.display}}};
        };
        Access::enumeration(window, {DisplayEnumerationStatus::Success, {{shell.display}}});
        Access::reconciler(window) = [this](DesktopPresentationHost& host, const DisplayId& bound)
        {
            ++reconciliations;
            exactBinding = exactBinding && bound == shell.display.id;
            if (reenterReconciliation && !reenteredReconciliation)
            {
                reenteredReconciliation = true;
                Access::sendNative(*installedWindow, true);
            }
            if (forcedReconciliationStatus)
                return *forcedReconciliationStatus;
            if (missing) return DesktopPresentationHostAttachmentStatus::TransientlyUnavailable;
            return DesktopPresentationHostTestAccess::reconcile(host, shell.display, shell.root);
        };
        Access::creator(window) = [this, &window](DesktopPresentationHost& host, const DisplayId& bound)
        {
            ++attempts;
            exactBinding = exactBinding && bound == shell.display.id;
            if (reenter) Access::sendNative(window, false);
            if (missing) return DesktopPresentationHostStatus::DisplayUnavailable;
            if (forcedStatus != DesktopPresentationHostStatus::Ready) return forcedStatus;
            DesktopHostCreationTrace trace;
            const auto status = DesktopPresentationHostTestAccess::create(host, shell.display,
                shell.root, DesktopHostFailurePoint::None, trace);
            lastWindow = trace.createdWindow;
            if (status == DesktopPresentationHostStatus::Ready
                && failure != Rendering::Detail::CompositionFailurePoint::None)
            {
                observation = std::make_unique<DestroyObservation>(window, false);
                backend(window).failNextCompositionOperationForTesting(failure);
            }
            return status;
        };
        installedWindow = &window;
    }
    ShellFixture shell;
    int attempts = 0;
    int reconciliations = 0;
    bool exactBinding = true, missing = false, reenter = false;
    bool exposeUnrelatedDisplayWhenMissing = false;
    bool reenterReconciliation = false, reenteredReconciliation = false;
    DesktopPresentationHostStatus forcedStatus = DesktopPresentationHostStatus::Ready;
    std::optional<DesktopPresentationHostAttachmentStatus> forcedReconciliationStatus{};
    Rendering::Detail::CompositionFailurePoint failure = Rendering::Detail::CompositionFailurePoint::None;
    HWND lastWindow = nullptr;
    DisplayDescriptor unrelatedDisplay{};
    MainWindow* installedWindow = nullptr;
    std::unique_ptr<DestroyObservation> observation;
};

bool initialize(MainWindow& window)
{
    window.show();
    QCoreApplication::processEvents();
    const bool result = window.initializeEngine();
    Access::stopTimer(window); // Deterministic direct invocation of the ONE production tick.
    return result;
}

bool initializeDeployed(MainWindow& window)
{ return initialize(window) && Access::clickDeploymentButton(window); }

bool previewHealthy(MainWindow& window)
{
    auto& gpu = backend(window);
    const auto preview = Access::preview(window);
    const auto before = gpu.presentationTargetDiagnosticsForTesting(preview).presentAttemptCount;
    if (!gpu.forceNextDxgiPresentResultForTesting(preview, static_cast<std::int32_t>(S_OK)))
        return false;
    if (Access::desktop(window).isValid()
        && !gpu.forceNextDxgiPresentResultForTesting(
            Access::desktop(window), static_cast<std::int32_t>(S_OK)))
        return false;
    Access::tick(window);
    return Access::engine(window).isInitialized() && preview.isValid()
        && !gpu.presentationTargetInfo(preview).failed
        && gpu.presentationTargetDiagnosticsForTesting(preview).presentAttemptCount == before + 1;
}

bool verifyTrayDeployment(Player::ApplicationTranslationState& translation)
{
    using namespace Player;
    CreationControl control;
    const auto scope = *currentInteractiveInstanceScope();
    const auto launcherSlot = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, scope);
    const auto sessionSlot = *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{control.shell.display.id}, scope);
    InstanceAdmissionLease launcherLease, sessionLease;
    if (launcherLease.acquire(launcherSlot) != InstanceAdmissionOutcome::Acquired
        || sessionLease.acquire(sessionSlot) != InstanceAdmissionOutcome::Acquired) return false;
    InstanceActivationEndpoint launcherActivation(launcherSlot), sessionActivation(sessionSlot);
    if (!launcherActivation.listen() || !sessionActivation.listen()) return false;
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    ApplicationCoordinator coordinator(launcherSlot, launcherActivation, true, [] {});
    if (!coordinator.listen()) return false;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    QTemporaryDir mediaFiles;
    QImage image(64, 40, QImage::Format_RGBA8888); image.fill(Qt::green);
    const auto mediaPath = mediaFiles.filePath("tray-deployed.png");
    auto& model = Access::model(window);
    const auto playlist = model.addPlaylist();
    if (!image.save(mediaPath)) return false;
    const auto item = model.addItem(playlist, WorkspaceMediaType::Image, mediaPath);
    if (!item || !model.setImageDisplayDuration(*item, 30) || !Access::start(window, playlist, *item)) return false;
    Access::pause(window);
    if (!Access::engine(window).seek(7)) return false;
    sessionActivation.bindReadyRole(window, [&] { (void)window.trayPresentation().restore(); });
    SessionCoordinatorJoin join(sessionSlot, launcherSlot);
    join.enableControl(window.trayPresentation()); join.start(window, false);
    const auto wait = [](const std::function<bool()>& condition) {
        QElapsedTimer time; time.start();
        while (!condition() && time.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(5); }
        return condition();
    };
    if (!wait([&] { return join.qualified(); })) return false;
    auto& engine = Access::engine(window);
    const auto preview = Access::preview(window), desktop = Access::desktop(window);
    const auto host = Access::host(window).nativeHandle();
    const auto revision = Access::model(window).authoredRevision();
    const auto canvas = Access::model(window).desktopCanvas().aspectRatio;
    const auto scene = Gpu::scene(engine), playback = Gpu::playback(engine), media = Gpu::media(engine);
    const auto renderer = &backend(window);
    const auto position = engine.playbackTime();
    const bool desired = Access::desired(window), continuation = Access::continuationArmed(window);
    auto* button = window.findChild<QToolButton*>("sessionHideToTray");
    if (!button || !button->isEnabled()) return false;
    button->click();
    if (!wait([&] { return !window.isVisible(); })) return false;
    Access::native(window, false); // Existing desktop reconciliation while Hidden; no Explorer restart.
    if (!require(!window.isVisible() && previewHealthy(window) && Access::preview(window) == preview
        && Access::desktop(window) == desktop && Access::host(window).nativeHandle() == host
        && Access::desired(window) == desired && Access::continuationArmed(window) == continuation
        && Access::model(window).authoredRevision() == revision && Access::model(window).desktopCanvas().aspectRatio == canvas
        && Gpu::scene(engine) == scene && Gpu::playback(engine) == playback && Gpu::media(engine) == media
        && &backend(window) == renderer && backend(window).presentationTargetCountForTesting() == 2,
        "S2 Hidden retains both actual GPU targets/host/intent/recovery/Canvas/runtime; Preview remains healthy")) return false;
    coordinator.showSession(join.incarnation());
    if (!wait([&] { return window.isVisible(); })) return false;
    if (!require(Access::desktop(window) == desktop && Access::host(window).nativeHandle() == host
        && Access::preview(window) == preview && Access::desired(window) && control.attempts == 1
        && engine.isPaused() && engine.playbackTime() == position && model.activeItemId() == item
        && previewHealthy(window), "S2 Show restores the same deployed endpoint without reconstruction")) return false;
    join.stop(); sessionActivation.beginShutdown(); Access::shutdown(window);
    return require(!Access::engine(window).isInitialized() && !Access::host(window).isValid()
        && control.shell.intact(), "Test-owned desktop endpoint tears down safely; real Explorer untouched");
}

bool verifyTrayDeploymentControl(Player::ApplicationTranslationState& translation, const QString& scenario)
{
    using namespace Player;
    CreationControl control;
    const auto scope = *currentInteractiveInstanceScope();
    const auto launcherSlot = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, scope);
    const auto sessionSlot = *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{control.shell.display.id}, scope);
    InstanceAdmissionLease launcherLease, sessionLease;
    if (!require(launcherLease.acquire(launcherSlot) == InstanceAdmissionOutcome::Acquired
            && sessionLease.acquire(sessionSlot) == InstanceAdmissionOutcome::Acquired, "S4 native admission prerequisite")) return false;
    InstanceActivationEndpoint launcherActivation(launcherSlot), sessionActivation(sessionSlot);
    if (!launcherActivation.listen() || !sessionActivation.listen()) return false;
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    QTemporaryDir files;
    QSettings settings(files.filePath("preferences.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore preferences(settings);
    ApplicationCoordinator coordinator(launcherSlot, launcherActivation, false, [] {});
    if (!coordinator.listen()) return false;
    coordinator.setLauncherFactory([&] {
        auto* launcher = new LauncherWindow(translation, preferences);
        launcher->setAcceptedCloseCallback([&, launcher] { coordinator.retireLauncher(*launcher); }); return launcher;
    });
    if (!coordinator.showLauncher()) return false;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(control.shell.valid && control.shell.intact() && initialize(window), "S4 test-owned shell/Engine ready")) return false;
    sessionActivation.bindReadyRole(window, [&] { (void)window.trayPresentation().restore(); });
    SessionCoordinatorJoin join(sessionSlot, launcherSlot);
    join.enableControl(window.trayPresentation());
    join.enableDesktopDeployment([&] { return window.desktopDeploymentProjection(); },
        [&](DesktopDeploymentIntent intent) { return window.requestDesktopDeployment(intent); });
    join.start(window, false);
    const auto wait = [](const std::function<bool()>& condition) {
        QElapsedTimer time; time.start();
        while (!condition() && time.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(5); }
        return condition();
    };
    if (!require(wait([&] { return join.qualified(); }) && coordinator.sessionCount() == 1, "S4 actual native control qualified")) return false;
    const auto action = [&]() -> QAction* {
        for (auto* entry : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (entry->data().toUuid() == join.incarnation() && entry->menu()->actions().size() > 1)
                return entry->menu()->actions()[1];
        return nullptr;
    };
    if (!require(wait([&] { return action() && action()->isEnabled(); }),
        "S4 RED A: actual Session submenu must expose deployment capability action")) return false;
    const auto hwnd = window.winId(); const auto incarnation = join.incarnation();
    QTemporaryDir content;
    QImage image(80, 50, QImage::Format_RGBA8888); image.fill(Qt::green);
    const auto file = content.filePath("s4.png");
    auto& model = Access::model(window); auto& engine = Access::engine(window);
    const auto playlist = model.addPlaylist();
    if (!image.save(file)) return false;
    const auto item = model.addItem(playlist, WorkspaceMediaType::Image, file);
    if (!item || !model.setImageDisplayDuration(*item, 30) || !Access::start(window, playlist, *item)) return false;
    Access::pause(window); if (!engine.seek(7)) return false;
    const auto authoredRevision = model.authoredRevision(); const auto preview = Access::preview(window);
    const auto canvas = model.desktopCanvas().aspectRatio;
    const bool dirty = Access::document(window).isDirty(model.authoredRevision());
    const auto projectPath = Access::document(window).currentPath;
    const auto scene = Gpu::scene(engine), clock = Gpu::playback(engine), media = Gpu::media(engine);
    const auto statePreserved = [&] {
        return window.winId() == hwnd && join.incarnation() == incarnation && model.authoredRevision() == authoredRevision
            && Access::document(window).isDirty(model.authoredRevision()) == dirty && Access::document(window).currentPath == projectPath
            && model.desktopCanvas().aspectRatio == canvas && model.activeItemId() == item
            && Access::bound(window) == control.shell.display.id && Access::preview(window) == preview
            && Gpu::scene(engine) == scene && Gpu::playback(engine) == clock && Gpu::media(engine) == media
            && engine.isPaused() && engine.playbackTime() == 7;
    };
    QPointer<QAction> staleDeploy = action();
    if (scenario == "hidden" || scenario == "failure" || scenario == "disconnect-flight" || scenario == "loss")
    {
        window.findChild<QToolButton*>("sessionHideToTray")->click();
        if (!require(wait([&] { return !window.isVisible(); }), "S4 real Hidden prerequisite")) return false;
    }
    if (scenario == "minimized") window.showMinimized();
    bool hiddenBefore = !window.isVisible(); const bool minimizedBefore = window.isMinimized();
    if (scenario == "stale" || scenario == "disconnected")
    {
        if (!require(Access::clickDeploymentButton(window) && Access::desktop(window).isValid(), "S4 real local deployment prerequisite")) return false;
        if (scenario == "disconnected")
        {
            control.missing = true; Access::native(window, false);
            if (!require(Access::desired(window) && !Access::desktop(window).isValid() && Access::button(window, "Undeploy"), "S4 unresolved retained-intent prerequisite")) return false;
        }
    }
    const char* red = scenario == "menu" ? "S4 RED A: actual Session submenu must expose deployment capability action"
        : scenario == "visible" ? "S4 RED B: actual tray Deploy must attach the existing Session desktop endpoint"
        : scenario == "hidden" ? "S4 RED C: real Hidden Session requires tray Deploy/Undeploy without Show"
        : scenario == "stale" ? "S4 RED D: stale menu must dispatch explicit Deploy, never reverse current intent"
        : "S4 RED E: disconnected retained intent must project actionable Undeploy";
    if (!require((scenario == "stale" ? staleDeploy.data() : action()) != nullptr, red)) return false;
    const auto finished = [&](DesktopDeploymentResult expected) {
        return wait([&] { return coordinator.sessionDeploymentResult(incarnation) == expected; });
    };
    const auto publish = [&] { join.publishDesktopDeployment(); return wait([&] {
        return action() && action()->text() == QCoreApplication::translate("ApplicationCoordinator", Access::desired(window)
            || Access::desktop(window).isValid() || Access::host(window).nativeHandle() ? "Undeploy" : "Deploy"); }); };
    if (scenario == "menu")
    {
        QList<QAction*> entries;
        for (auto* entry : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (entry->menu()) entries.push_back(entry); // S6 adds top-level actions; S4 still verifies role topology.
        if (!require(entries.size() == 2 && entries[0]->menu()->actions().size() == 3
            && entries[0]->menu()->actions()[1]->isSeparator()
            && action()->text() == QCoreApplication::translate("ApplicationCoordinator", "Deploy"), "S4 deployment remains Session-only within S5 Show/separator/Close menus")) return false;
    }
    if (scenario == "stale")
    {
        staleDeploy->trigger();
        if (!require(finished(DesktopDeploymentResult::AlreadySatisfied) && control.attempts == 1
            && Access::desktop(window).isValid() && Access::desired(window), "S4 stale Deploy is AlreadySatisfied, never Toggle")) return false;
    }
    else if (scenario == "disconnected")
    {
        if (!publish() || !require(action()->isEnabled(), "S4 retained disconnected intent can Undeploy")) return false;
        action()->trigger();
        if (!require(finished(DesktopDeploymentResult::Completed) && !Access::desired(window)
            && !Access::continuationArmed(window), "S4 Undeploy cancels real unresolved recovery intent")) return false;
        if (!publish() || !require(!action()->isEnabled(), "S4 unresolved Deploy disabled by local capability")) return false;
        coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Deploy);
        if (!require(finished(DesktopDeploymentResult::Rejected) && control.attempts == 2, "S4 fresh local capability rejects unresolved Deploy")) return false;
    }
    else if (scenario == "failure")
    {
        control.forcedStatus = DesktopPresentationHostStatus::NativeFailure;
        action()->trigger();
        if (!require(finished(DesktopDeploymentResult::Failure) && control.attempts == 1 && !Access::desired(window)
            && !Access::desktop(window).isValid() && !window.isVisible(), "S4 host failure is terminal, no Show or retry")) return false;
        Access::native(window, true); QCoreApplication::processEvents();
        if (!require(control.attempts == 1, "S4 failed initial Deploy has no retained retry authority")) return false;
    }
    else if (scenario == "showall")
    {
        action()->trigger(); ApplicationCoordinatorTestAccess::click(coordinator);
        if (!require(finished(DesktopDeploymentResult::Completed)
            && wait([&] { return ApplicationCoordinatorTestAccess::allDone(coordinator); })
            && Access::desktop(window).isValid() && control.attempts == 1, "S4 independent Show All and Deploy do not toggle or duplicate endpoint")) return false;
        coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Undeploy);
        if (!finished(DesktopDeploymentResult::Completed)) return false;
    }
    else if (scenario == "rapid")
    {
        action()->trigger();
        coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Undeploy);
        if (!require(finished(DesktopDeploymentResult::Completed) && control.attempts == 1 && !Access::desired(window)
            && !Access::desktop(window).isValid() && !Access::host(window).nativeHandle(), "S4 fresh Deploy immediately followed by explicit Undeploy executes each desired intent once")) return false;
    }
    else if (scenario == "loss")
    {
        action()->trigger(); join.stop();
        if (!require(wait([&] { return window.isVisible() && !action()->isEnabled(); })
            && control.attempts <= 1 && engine.isInitialized(), "S4 control loss safely surfaces through eligibility, not deployment, without retry")) return false;
        hiddenBefore = false;
    }
    else if (scenario == "unavailable" || scenario == "unavailable-flight")
    {
        QLocalSocket* nativePeer = nullptr;
        std::unique_ptr<QSignalBlocker> blockedReply;
        if (scenario == "unavailable-flight")
        {
            for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
                if (socket->property("sessionControl").toBool()) nativePeer = socket;
            if (!require(nativePeer, "S4 actual authenticated socket for held deployment reply")) return false;
            blockedReply = std::make_unique<QSignalBlocker>(nativePeer);
        }
        action()->trigger();
        if (blockedReply)
        {
            if (!require(wait([&] {
                const auto bytes = nativePeer->peek(nativePeer->bytesAvailable());
                for (qsizetype offset = 0; offset + 42 <= bytes.size(); offset += 42)
                    if (bytes[offset] == 'F' && bytes[offset + 41] == static_cast<char>(DesktopDeploymentResult::Completed)) return true;
                return false;
            }), "S4 hold a genuine target Completed frame, not a fabricated success ACK")) return false;
        }
        else if (!require(finished(DesktopDeploymentResult::Completed), "S4 real previous Deploy completed")) return false;
        const auto target = Access::desktop(window); const auto host = Access::host(window).nativeHandle();
        if (!require(target.isValid() && host && control.attempts == 1, "S4 real endpoint exists before recovery becomes unavailable")) return false;
        ApplicationCoordinatorTestAccess::icon(coordinator, {}); // Actual tray capability loss, not a mocked deployment result.
        coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Undeploy);
        if (!require(coordinator.sessionDeploymentResult(incarnation) == DesktopDeploymentResult::Unavailable,
            "S4 P2 RED: new unavailable intent cannot retain the previous request's result")) return false;
        if (blockedReply)
        {
            blockedReply.reset();
            QMetaObject::invokeMethod(nativePeer, "readyRead", Qt::DirectConnection); // Consume genuine buffered target frames.
            if (!require(coordinator.sessionDeploymentResult(incarnation) == DesktopDeploymentResult::Unavailable,
                "S4 older real Completed must not overwrite the newer unavailable request")) return false;
        }
        QCoreApplication::processEvents();
        if (!require(Access::desktop(window) == target && Access::host(window).nativeHandle() == host
            && Access::desired(window) && control.attempts == 1 && !action()->isEnabled()
            && action()->text() == QCoreApplication::translate("ApplicationCoordinator", "Undeploy"),
            "S4 unavailable request sends nothing, preserves retained endpoint/projection, and does not retry")) return false;
        ApplicationCoordinatorTestAccess::icon(coordinator, QApplication::windowIcon());
        coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Undeploy);
        if (!require(finished(DesktopDeploymentResult::Completed) && !Access::desktop(window).isValid()
            && !Access::host(window).nativeHandle() && !Access::desired(window), "S4 subsequent available explicit Undeploy remains operational")) return false;
    }
    else
    {
        if (scenario == "disconnect-flight") control.missing = true; // Actual target changes after menu but before delivery.
        action()->trigger();
        if (scenario == "disconnect-flight")
        {
            if (!require(finished(DesktopDeploymentResult::Rejected) && control.attempts == 0
                && !window.isVisible(), "S4 in-flight disconnect locally rejects stale Deploy without Show")) return false;
        }
        else
        {
            if (scenario == "busy")
            {
                const auto creator = Access::creator(window);
                Access::creator(window) = [&, creator](DesktopPresentationHost& host, const DisplayId& id) {
                    if (!require(window.requestDesktopDeployment(DesktopDeploymentIntent::Undeploy) == DesktopDeploymentResult::Rejected,
                        "S4 existing replacement guard rejects reentrant opposite intent without queue")) return DesktopPresentationHostStatus::NativeFailure;
                    return creator(host, id);
                };
            }
            if (!require(finished(DesktopDeploymentResult::Completed) && Access::desktop(window).isValid()
                && Access::desired(window) && control.attempts == 1, "S4 RED B/C: real tray Deploy creates one actual endpoint")) return false;
            const auto target = Access::desktop(window); const auto host = Access::host(window).nativeHandle();
            coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Deploy);
            if (!require(finished(DesktopDeploymentResult::AlreadySatisfied) && Access::desktop(window) == target
                && Access::host(window).nativeHandle() == host && control.attempts == 1, "S4 repeated explicit Deploy keeps the same endpoint")) return false;
            if (!publish()) return false;
            if (scenario == "translation")
            {
                Access::language(window, UiLanguage::SimplifiedChinese);
                if (!publish() || !require(action()->text() == QString::fromUtf8("取消部署"), "S4 actual translated Undeploy menu")) return false;
            }
            if (scenario == "revision")
            {
                QByteArray frame(1, 'T'); frame += ApplicationCoordinatorTestAccess::epoch(join).toRfc4122(); frame += incarnation.toRfc4122();
                const auto offset = frame.size(); frame.resize(offset + 8); qToBigEndian<quint64>(1, frame.data() + offset); frame += char{6};
                if (!require(ApplicationCoordinatorTestAccess::revision(join) > 1, "S4 actual projection revision advanced")) return false;
                QLocalSocket* nativePeer = nullptr;
                for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
                    if (socket->property("sessionControl").toBool()) nativePeer = socket;
                if (!require(nativePeer, "S4 real authenticated server-side socket")) return false;
                bool consumed = false;
                const auto observed = QObject::connect(nativePeer, &QLocalSocket::readyRead, &coordinator, [&] { consumed = true; });
                ApplicationCoordinatorTestAccess::control(join).write(frame); ApplicationCoordinatorTestAccess::control(join).flush();
                if (!require(wait([&] { return consumed; }), "S4 stale frame consumed by real server handler before assertion")) return false;
                QObject::disconnect(observed);
                if (!require(action()->text() == QCoreApplication::translate("ApplicationCoordinator", "Undeploy"), "S4 stale revision cannot rewind retained projection")) return false;
            }
            if (scenario == "detach")
            {
                QScopedValueRollback<bool> rejection(Gpu::initialized(engine), false);
                action()->trigger();
                if (!require(finished(DesktopDeploymentResult::Failure) && !Access::desired(window)
                    && Access::desktop(window) == target && Access::host(window).nativeHandle() == host, "S4 detach failure retains borrowed HWND/target, not false completion")) return false;
            }
            if (!publish()) return false;
            if (scenario == "sequential")
            {
                coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Deploy);
                coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Undeploy);
            }
            else action()->trigger();
            if (!require(finished(DesktopDeploymentResult::Completed) && !Access::desired(window)
                && !Access::desktop(window).isValid() && !Access::host(window).nativeHandle(), "S4 actual tray Undeploy removes endpoint without Show")) return false;
            coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Undeploy);
            if (!require(finished(DesktopDeploymentResult::AlreadySatisfied) && control.attempts == 1, "S4 repeated explicit Undeploy is no-op")) return false;
        }
    }
    if (!require(statePreserved() && (!window.isVisible()) == hiddenBefore && window.isMinimized() == minimizedBefore
        && previewHealthy(window), "S4 Project/dirty/Canvas/playback/runtime/DisplayId/presentation preserved")) return false;
    join.stop(); sessionActivation.beginShutdown(); Access::shutdown(window);
    if (scenario == "translation") Access::language(window, UiLanguage::English);
    return require(control.shell.intact(), "S4 test-owned shell stays healthy; Explorer untouched");
}

class S5WindowObservation final : public QObject
{
public:
    int closes = 0, shows = 0;
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Close) ++closes;
        if (event->type() == QEvent::Show) ++shows;
        return false;
    }
};

bool verifyTrayClose(Player::ApplicationTranslationState& translation, const QString& scenario)
{
    using namespace Player;
    CreationControl control;
    const auto scope = *currentInteractiveInstanceScope();
    const auto launcherSlot = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, scope);
    const auto sessionSlot = *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{control.shell.display.id}, scope);
    InstanceAdmissionLease launcherLease, sessionLease;
    if (launcherLease.acquire(launcherSlot) != InstanceAdmissionOutcome::Acquired
        || sessionLease.acquire(sessionSlot) != InstanceAdmissionOutcome::Acquired) return false;
    InstanceActivationEndpoint launcherActivation(launcherSlot), sessionActivation(sessionSlot);
    if (!launcherActivation.listen() || !sessionActivation.listen()) return false;
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    QTemporaryDir files;
    QSettings settings(files.filePath("preferences.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore preferences(settings);
    QByteArray observedFrames;
    ApplicationCoordinator coordinator(launcherSlot, launcherActivation, false, [] {});
    if (!coordinator.listen()) return false;
    ApplicationCoordinatorTestAccess::observe(coordinator, observedFrames);
    coordinator.setLauncherFactory([&] {
        auto* launcher = new LauncherWindow(translation, preferences);
        launcher->setAcceptedCloseCallback([&, launcher] { coordinator.retireLauncher(*launcher); }); return launcher;
    });
    if (!coordinator.showLauncher()) return false;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(control.shell.valid && control.shell.intact() && initialize(window), "S5 real test-owned shell/Engine prerequisite")) return false;
    sessionActivation.bindReadyRole(window, [&] { (void)window.trayPresentation().restore(); });
    SessionCoordinatorJoin join(sessionSlot, launcherSlot);
    join.enableControl(window.trayPresentation());
    join.enableDesktopDeployment([&] { return window.desktopDeploymentProjection(); },
        [&](DesktopDeploymentIntent intent) { return window.requestDesktopDeployment(intent); });
    int accepted = 0; bool activationStoppedBeforeEngine = false;
    window.setAcceptedCloseCallback([&] {
        ++accepted; sessionActivation.beginShutdown();
        activationStoppedBeforeEngine = Access::engine(window).isInitialized();
    });
    join.start(window, false);
    const auto wait = [](const std::function<bool()>& condition) {
        QElapsedTimer time; time.start();
        while (!condition() && time.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(5); }
        return condition();
    };
    if (!require(wait([&] { return join.qualified(); }) && coordinator.sessionCount() == 1, "S5 real native control qualified")) return false;
    const auto closeAction = [&](const QUuid& role) -> QAction* {
        for (auto* entry : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (entry->data().toUuid() == role)
                for (auto* action : entry->menu()->actions())
                    if (action->text() == QCoreApplication::translate("ApplicationCoordinator", "Close")) return action;
        return nullptr;
    };
    const auto hasOutcome = [&](quint8 expected) {
        for (qsizetype offset = 0; offset + 42 <= observedFrames.size(); offset += 42)
            if (observedFrames[offset] == 'O' && static_cast<quint8>(observedFrames[offset + 41]) == expected) return true;
        return false;
    };
    if (scenario == "query" || scenario == "query-old")
    {
        QLocalSocket* nativePeer = nullptr;
        for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
            if (socket->property("sessionControl").toBool()) nativePeer = socket;
        if (!require(nativePeer, "S5 authenticated native QueryState channel")) return false;
        if (scenario == "query-old")
        {
            window.findChild<QToolButton*>("sessionHideToTray")->click();
            if (!require(wait([&] { return window.trayPresentation().hiddenToTray() && !window.isVisible(); }),
                "S5 old-key observation starts from genuinely qualified hidden role")) return false;
            const RoleCloseKey oldKey{QUuid::createUuid(), 17};
            observedFrames.clear();
            coordinator.reconcileRoleClose(join.incarnation(), oldKey);
            const auto postQueryState = [&]() -> std::optional<QUuid> {
                bool resultSeen = false;
                for (qsizetype offset = 0; offset + 42 <= observedFrames.size(); offset += 42)
                {
                    if (observedFrames[offset] == 'O' && QUuid::fromRfc4122(observedFrames.mid(offset + 1, 16)) == oldKey.generation
                        && qFromBigEndian<quint64>(observedFrames.constData() + offset + 33) == oldKey.request) resultSeen = true;
                    if (resultSeen && observedFrames[offset] == 'V') return QUuid::fromRfc4122(observedFrames.mid(offset + 1, 16));
                }
                return std::nullopt;
            };
            const bool oldQueryDelivered = wait([&] { return postQueryState().has_value() || !join.qualified(); });
            if (!require(oldQueryDelivered
                && postQueryState() == ApplicationCoordinatorTestAccess::epoch(join) && join.qualified()
                && window.trayPresentation().hiddenToTray() && !window.isVisible() && Access::engine(window).isInitialized()
                && !window.trayPresentation().closeOperation()
                && coordinator.roleCloseOperation(join.incarnation())->key == oldKey
                && coordinator.roleCloseOperation(join.incarnation())->result == RoleCloseResult::Uncertain,
                "S5 prior-key query retains original Uncertain without poisoning current epoch/control or executing Close")) return false;
            join.stop(); sessionActivation.beginShutdown(); Access::shutdown(window); return true;
        }
        QByteArray query(1, 'K'); query += ApplicationCoordinatorTestAccess::epoch(join).toRfc4122(); query += join.incarnation().toRfc4122();
        const auto offset = query.size(); query.resize(offset + 8); qToBigEndian<quint64>(17, query.data() + offset); query += char{0};
        nativePeer->write(query); nativePeer->flush();
        if (!require(wait([&] { return hasOutcome(5) || !join.qualified(); }) && hasOutcome(5),
            "S5 RED G: actual same-request QueryState must return Uncertain without executing unknown Close")) return false;
        join.stop(); sessionActivation.beginShutdown(); Access::shutdown(window);
        return true;
    }
    auto& model = Access::model(window); auto& engine = Access::engine(window);
    const auto playlist = model.addPlaylist();
    QImage image(80, 50, QImage::Format_RGBA8888); image.fill(Qt::green);
    const auto mediaPath = files.filePath("s5.png");
    if (!image.save(mediaPath)) return false;
    const auto item = model.addItem(playlist, WorkspaceMediaType::Image, mediaPath);
    if (!item || !model.setImageDisplayDuration(*item, 30) || !Access::start(window, playlist, *item)) return false;
    Access::pause(window); if (!engine.seek(7)) return false;
    const bool launcherCase = scenario == "menu" || scenario.startsWith("launcher-");
    const bool clean = launcherCase || scenario == "clean" || scenario == "minimized-clean" || scenario == "hidden-clean";
    const bool saving = scenario == "visible-save" || scenario == "hidden-save";
    const bool discarding = scenario == "visible-discard" || scenario == "hidden-discard";
    const bool accepting = clean || saving || discarding;
    if (clean) Access::cleanDocument(window);
    if (!clean && !require(Access::document(window).isDirty(model.authoredRevision()), "S5 genuinely dirty document prerequisite")) return false;
    if (scenario == "save-failure") Access::documentPath(window, files.path()); // Existing directory: real save must fail.
    if (saving) Access::documentPath(window, files.filePath("saved.json"));
    const bool hidden = scenario.startsWith("hidden-") || scenario == "save-as-cancel" || scenario == "save-failure"
        || scenario == "control-loss" || scenario == "show-pending" || scenario == "showall-pending"
        || scenario == "duplicate" || scenario == "pending-wait" || scenario == "uncertain" || scenario == "deploy-race";
    if (hidden)
    {
        if (!clean && !Access::clickDeploymentButton(window)) return false;
        window.findChild<QToolButton*>("sessionHideToTray")->click();
        if (!require(wait([&] { return !window.isVisible(); }), "S5 real HiddenToTray prerequisite")) return false;
    }
    if (scenario == "minimized-clean") window.showMinimized();
    const auto hwnd = window.winId(); const auto incarnation = join.incarnation();
    const auto authored = model.authoredRevision(); const auto preview = Access::preview(window);
    const auto desktop = Access::desktop(window); const auto host = Access::host(window).nativeHandle();
    const auto scene = Gpu::scene(engine), clock = Gpu::playback(engine), media = Gpu::media(engine);
    const auto path = Access::document(window).currentPath; const auto canvas = model.desktopCanvas().aspectRatio;
    const char* red = scenario == "menu" ? "S5 RED A: real primary-role submenu must expose ordinary Close"
        : scenario == "clean" ? "S5 RED B: real tray clean Close must enter existing accepted teardown"
        : scenario == "hidden-cancel" ? "S5 RED C: hidden dirty tray Close must run ordinary Cancel and restore hidden"
        : scenario == "save-as-cancel" || scenario == "save-failure" ? "S5 RED D: hidden tray Close must use ordinary Save As / error abort"
        : scenario == "modal" ? "S5 RED E: actual Close must have fresh modal admission with no late execution"
        : "S5 RED F: real hidden clean tray Close must accept without Show";
    if (!require(closeAction(incarnation), red)) return false;
    if (!require(wait([&] { return closeAction(incarnation) && closeAction(incarnation)->isEnabled(); }),
        "S5 initial target capability must reach the actual menu before triggering Close")) return false;
    if (launcherCase)
    {
        QPointer<QWidget> launcher = coordinator.launcherWindow();
        if (!require(closeAction(coordinator.launcherIncarnation()), "S5 Launcher Close action exists")) return false;
        QList<QAction*> entries;
        for (auto* entry : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (entry->menu()) entries.push_back(entry); // Keep S5's ordinary per-role Close contract.
        if (!require(entries.size() == 2 && entries[0]->menu()->actions().size() == 3
            && entries[0]->menu()->actions()[1]->isSeparator() && entries[1]->menu()->actions().size() == 4
            && entries[1]->menu()->actions()[2]->isSeparator(), "S5 exact per-role separators/actions alongside S6 top-level All Close")) return false;
        if (scenario == "launcher-minimized") launcher->showMinimized();
        if (scenario == "launcher-hidden") launcher->findChild<QToolButton*>("launcherHideToTray")->click();
        if (scenario == "launcher-settings")
        {
            bool observed = false, rejected = false;
            QTimer probe; probe.setInterval(10);
            QObject::connect(&probe, &QTimer::timeout, launcher, [&] {
                auto* settingsDialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!settingsDialog || settingsDialog->parentWidget() != launcher) return;
                observed = true;
                (void)coordinator.requestRoleClose(coordinator.launcherIncarnation());
                const auto operation = coordinator.roleCloseOperation(coordinator.launcherIncarnation());
                rejected = operation && operation->result == RoleCloseResult::Rejected;
                settingsDialog->reject();
            });
            probe.start(); LauncherWindowTestAccess::settings(*static_cast<LauncherWindow*>(launcher.data())); probe.stop();
            if (!require(observed && rejected && launcher,
                "S5 Launcher modal admission rejects without ordinary destruction")) return false;
            QCoreApplication::processEvents();
            if (!require(launcher && coordinator.launcherWindow() == launcher, "S5 rejected Launcher Close does not execute later")) return false;
        }
        if (!require(wait([&] { return closeAction(coordinator.launcherIncarnation())->isEnabled(); }),
            "S5 new Launcher Close awaits current enabled projection after modal resolves")) return false;
        S5WindowObservation launcherObservation; launcher->installEventFilter(&launcherObservation);
        const auto oldIncarnation = coordinator.launcherIncarnation();
        closeAction(coordinator.launcherIncarnation())->trigger();
        if (!require(wait([&] { return launcher.isNull(); }) && coordinator.sessionCount() == 1
            && engine.isInitialized() && launcherObservation.closes == 1
            && (scenario != "launcher-hidden" || launcherObservation.shows == 0), "S5 Launcher ordinary destruction preserves Session/coordinator without hidden flash")) return false;
        const auto operation = coordinator.roleCloseOperation(oldIncarnation);
        if (!require(operation && operation->result == RoleCloseResult::CloseAccepted, "S5 Launcher Accepted retained for exact retired incarnation")) return false;
        join.stop(); sessionActivation.beginShutdown(); Access::shutdown(window);
        return true;
    }
    S5WindowObservation observation; window.installEventFilter(&observation);
    if (scenario == "modal" || scenario == "bgm-modal" || scenario == "native-modal" || scenario == "bgm-native-modal")
    {
        auto* stale = closeAction(incarnation);
        QWidget* owner = &window;
        if (scenario.startsWith("bgm-"))
        {
            owner = Access::bgm(window, playlist);
            if (!require(owner && !window.isAncestorOf(owner), "S5 parentless actual BGM editor prerequisite")) return false;
            owner->show();
        }
        if (scenario.endsWith("native-modal"))
        {
            QFileDialog modal(owner, "S5 real native modal"); modal.setFileMode(QFileDialog::ExistingFile);
            QTimer probe; probe.setInterval(10); bool nativeSeen = false, submitted = false;
            QObject::connect(&probe, &QTimer::timeout, &modal, [&] {
                const auto popup = GetLastActivePopup(reinterpret_cast<HWND>(owner->winId()));
                wchar_t name[32]{}; if (popup) GetClassNameW(popup, name, 32);
                if (wcscmp(name, L"#32770") != 0 || !IsWindowVisible(popup) || IsWindowEnabled(reinterpret_cast<HWND>(owner->winId()))) return;
                nativeSeen = true;
                if (!submitted) { submitted = true; (void)coordinator.requestRoleClose(incarnation); }
                const auto operation = coordinator.roleCloseOperation(incarnation);
                if (operation && operation->result == RoleCloseResult::Rejected) modal.reject();
            });
            probe.start(); QTimer::singleShot(5000, &modal, &QFileDialog::reject); modal.exec(); probe.stop();
            if (!require(nativeSeen && hasOutcome(4), "S5 actual Windows native modal rejects Close through nested control delivery")) return false;
        }
        else
        {
            QDialog modal(owner); modal.setWindowModality(Qt::ApplicationModal); modal.show();
            stale->trigger();
            if (!require(wait([&] { return hasOutcome(4); }), "S5 fresh process-aware modal admission rejection")) return false;
            modal.hide();
        }
        QCoreApplication::processEvents();
        if (!require(engine.isInitialized() && accepted == 0 && observation.closes == 0,
            "S5 rejected Close never executes after modal resolves")) return false;
    }
    else
    {
        bool dirtyPrompt = false, saveAs = false, error = false, pendingProof = clean;
        bool auxiliarySubmitted = false, pendingDeadlineProof = scenario != "pending-wait", fixtureTimedOut = false;
        QElapsedTimer pendingTime;
        QLocalSocket* nativePeer = nullptr;
        std::unique_ptr<QSignalBlocker> blockedReply;
        if (scenario == "uncertain")
        {
            for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
                if (socket->property("sessionControl").toBool()) nativePeer = socket;
            if (!require(nativePeer, "S5 real delivery barrier before actual Close")) return false;
            blockedReply = std::make_unique<QSignalBlocker>(nativePeer);
        }
        QTimer dialogs; dialogs.setInterval(10);
        QObject::connect(&dialogs, &QTimer::timeout, &window, [&] {
            for (auto* top : QApplication::topLevelWidgets())
            {
                if (auto* fileDialog = qobject_cast<QFileDialog*>(top); fileDialog && fileDialog->isVisible())
                { saveAs = true; fileDialog->reject(); return; }
                if (auto* message = qobject_cast<QMessageBox*>(top); message && message->isVisible())
                {
                    if (message->standardButtons().testFlag(QMessageBox::Save))
                    {
                        dirtyPrompt = true;
                        const auto operation = coordinator.roleCloseOperation(incarnation);
                        if (!operation) return;
                        if (scenario != "uncertain" && operation->result != RoleCloseResult::Pending) return;
                        pendingProof = window.trayPresentation().closePending() && engine.isInitialized() && accepted == 0;
                        if (!pendingTime.isValid()) pendingTime.start();
                        if (scenario == "pending-wait" && pendingTime.elapsed() < SessionQualificationLifetimeMs + 150) return;
                        if (scenario == "pending-wait") pendingDeadlineProof = operation->result == RoleCloseResult::Pending;
                        if (!auxiliarySubmitted)
                        {
                            auxiliarySubmitted = true;
                            if (scenario == "control-loss") join.stop();
                            if (scenario == "show-pending") coordinator.showSession(incarnation);
                            if (scenario == "showall-pending") ApplicationCoordinatorTestAccess::click(coordinator);
                            if (scenario == "deploy-race") coordinator.requestSessionDeployment(incarnation, DesktopDeploymentIntent::Undeploy);
                            if (scenario == "duplicate")
                            {
                                coordinator.reconcileRoleClose(incarnation, operation->key);
                                if (!require(coordinator.roleCloseOperation(incarnation)->result == RoleCloseResult::Pending,
                                    "S5 explicit same-key query must preserve known Pending until new evidence"))
                                { fixtureTimedOut = true; message->button(QMessageBox::Cancel)->click(); return; }
                                pendingProof = pendingProof && !coordinator.requestRoleClose(incarnation)
                                    && window.trayPresentation().requestClose(operation->key) == RoleCloseResult::Pending
                                    && window.trayPresentation().requestClose({operation->key.generation, operation->key.request + 100}) == RoleCloseResult::Rejected
                                    && coordinator.roleCloseOperation(incarnation)->key == operation->key;
                                (void)window.close(); // Ordinary x collision uses QWidget's real in-progress Close guard.
                                pendingProof = pendingProof && observation.closes == 1 && window.trayPresentation().closePending();
                                for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
                                    if (socket->property("sessionControl").toBool())
                                    {
                                        QByteArray replay(1, 'C'); replay += operation->key.generation.toRfc4122(); replay += incarnation.toRfc4122();
                                        const auto offset = replay.size(); replay.resize(offset + 8);
                                        qToBigEndian<quint64>(operation->key.request, replay.data() + offset); replay += char{0};
                                        socket->write(replay); socket->flush();
                                    }
                            }
                        }
                        if (scenario == "show-pending")
                        {
                            bool showCompleted = false;
                            for (qsizetype offset = 0; offset + 42 <= observedFrames.size(); offset += 42)
                                if (observedFrames[offset] == 'D' && observedFrames[offset + 41] == 1) showCompleted = true;
                            if (!showCompleted) return;
                        }
                        if (scenario == "showall-pending" && !ApplicationCoordinatorTestAccess::allDone(coordinator)) return;
                        if (scenario == "deploy-race" && coordinator.sessionDeploymentResult(incarnation) != DesktopDeploymentResult::Rejected) return;
                        if (scenario == "duplicate")
                        {
                            int receipts = 0, pendingResults = 0;
                            for (qsizetype offset = 0; offset + 42 <= observedFrames.size(); offset += 42)
                            {
                                if (observedFrames[offset] == 'J') ++receipts;
                                if (observedFrames[offset] == 'O' && observedFrames[offset + 41] == static_cast<char>(RoleCloseResult::Pending)) ++pendingResults;
                            }
                            if (receipts < 2 || pendingResults < 3) return; // Genuine replay + same-key query were consumed inside the real dialog loop.
                        }
                        const auto choice = saving || scenario == "save-as-cancel" || scenario == "save-failure" ? QMessageBox::Save
                            : discarding ? QMessageBox::Discard : QMessageBox::Cancel;
                        message->button(choice)->click(); return;
                    }
                    else { error = true; message->button(QMessageBox::Ok)->click(); return; }
                }
            }
        });
        QTimer safety; safety.setSingleShot(true);
        QObject::connect(&safety, &QTimer::timeout, &window, [&] {
            fixtureTimedOut = true;
            for (auto* top : QApplication::topLevelWidgets())
            {
                if (auto* file = qobject_cast<QFileDialog*>(top); file && file->isVisible()) { file->reject(); return; }
                if (auto* message = qobject_cast<QMessageBox*>(top); message && message->isVisible())
                {
                    if (auto* cancel = message->button(QMessageBox::Cancel)) cancel->click();
                    else if (auto* ok = message->button(QMessageBox::Ok)) ok->click();
                    return;
                }
            }
        }); // Test safety only; a timeout is a failure, never a production decision.
        safety.start(8000); dialogs.start(); closeAction(incarnation)->trigger();
        const auto submitted = coordinator.roleCloseOperation(incarnation);
        if (!require(submitted.has_value(), "S5 coordinator records exact request before target executes")) return false;
        const bool completed = wait([&] { return accepting ? accepted == 1
            : scenario == "control-loss" || scenario == "uncertain"
                ? window.trayPresentation().queryClose(submitted->key) == RoleCloseResult::CloseAborted : hasOutcome(3); });
        dialogs.stop(); safety.stop();
        if (blockedReply)
        {
            const auto realAbortedFrames = [&] {
                const auto frames = nativePeer->peek(nativePeer->bytesAvailable());
                int count = 0;
                for (qsizetype offset = 0; offset + 42 <= frames.size(); offset += 42)
                    if (frames[offset] == 'O' && frames[offset + 41] == static_cast<char>(RoleCloseResult::CloseAborted)) ++count;
                return count;
            };
            if (!require(wait([&] { return realAbortedFrames() == 2; }), "S5 both genuine completion reports held before QueryState")) return false;
            auto* deadline = nativePeer->findChild<QTimer*>("roleCloseDeliveryDeadline");
            if (!require(deadline && completed, "S5 actual Close completed locally while result delivery held")) return false;
            QMetaObject::invokeMethod(deadline, "timeout", Qt::DirectConnection);
            const auto operation = coordinator.roleCloseOperation(incarnation);
            if (!require(operation && operation->key == submitted->key && operation->result == RoleCloseResult::Uncertain
                && !coordinator.requestRoleClose(incarnation), "S5 delivery uncertainty is not Cancel/Accepted/Rejected and cannot resend Close")) return false;
            if (!require(wait([&] { return realAbortedFrames() >= 3; }), "S5 actual target consumed same-request query and produced a genuine retained result")) return false;
            blockedReply.reset(); QMetaObject::invokeMethod(nativePeer, "readyRead", Qt::DirectConnection);
            if (!require(wait([&] { return coordinator.roleCloseOperation(incarnation)->result == RoleCloseResult::CloseAborted; })
                && coordinator.roleCloseOperation(incarnation)->key == submitted->key, "S5 same-request reconciliation observes real abort without executing twice")) return false;
        }
        if (!require(completed && pendingProof && pendingDeadlineProof && !fixtureTimedOut && observation.closes == 1,
            "S5 one actual ordinary closeEvent, genuine Pending and no user decision timeout")) return false;
        if (accepting)
        {
            if (!require(!engine.isInitialized() && accepted == 1 && activationStoppedBeforeEngine
                && (!clean || !dirtyPrompt) && (!clean || !hidden || observation.shows == 0), "S5 accepted teardown once; hidden clean has no Show")) return false;
            if (!require(wait([&] { return hasOutcome(2); }), "S5 CloseAccepted is reported only after real Engine/BGM teardown")) return false;
            const auto acceptedOperation = coordinator.roleCloseOperation(incarnation);
            if (!require(acceptedOperation && !coordinator.requestRoleClose(incarnation)
                && coordinator.roleCloseOperation(incarnation)->key == acceptedOperation->key
                && coordinator.roleCloseOperation(incarnation)->result == RoleCloseResult::CloseAccepted,
                "S5 accepted role cannot admit another Close or overwrite its terminal receipt before retirement")) return false;
            if (saving && !require(QFile::exists(files.filePath("saved.json")) && !Access::document(window).isDirty(authored), "S5 actual successful save uses existing document lifecycle")) return false;
        }
        else
        {
            if (!require(dirtyPrompt && accepted == 0 && engine.isInitialized() && window.winId() == hwnd
                && join.incarnation() == incarnation && model.authoredRevision() == authored
                && Access::document(window).currentPath == path && Access::document(window).isDirty(authored)
                && model.desktopCanvas().aspectRatio == canvas && model.activeItemId() == item
                && Access::bound(window) == control.shell.display.id && Access::preview(window) == preview
                && Access::desktop(window) == desktop && Access::host(window).nativeHandle() == host
                && Gpu::scene(engine) == scene && Gpu::playback(engine) == clock && Gpu::media(engine) == media
                && engine.isPaused() && engine.playbackTime() == 7
                && (!window.isVisible()) == (hidden && scenario != "control-loss"),
                "S5 actual abort preserves dirty/Project/Canvas/runtime/clock/deployment/DisplayId and pre-Close presentation")) return false;
            if (scenario == "save-as-cancel" && !require(saveAs, "S5 actual Save As interaction reached")) return false;
            if (scenario == "save-failure" && !require(error, "S5 actual save error feedback reached")) return false;
            if (scenario == "control-loss" && !require(wait([&] { return coordinator.roleCloseOperation(incarnation)->result == RoleCloseResult::Uncertain; }),
                "S5 disconnected coordinator cannot invent the locally aborted result")) return false;
        }
    }
    window.removeEventFilter(&observation);
    join.stop(); sessionActivation.beginShutdown(); Access::shutdown(window);
    return require(control.shell.intact(), "S5 test-owned shell healthy; no Explorer mutation");
}

constexpr UINT S4PeerClose = WM_APP + 174;
class S4PeerHarness final : public QAbstractNativeEventFilter
{
public:
    MainWindow* window = nullptr;
    HWND hwnd = nullptr;
    bool nativeEventFilter(const QByteArray&, void* native, qintptr* result) override
    {
        const auto* message = static_cast<MSG*>(native);
        // Never call winId() for unrelated native teardown messages: it can
        // recreate a dying QWidget HWND and recursively enter the native filter.
        if (!window || message->hwnd != hwnd || message->message != S4PeerClose) return false;
        QTimer::singleShot(0, window, [] {
            if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if (auto* discard = prompt->button(QMessageBox::Discard))
                    discard->click();
        });
        window->close();
        if (result) *result = 0; return true;
    }
};

int runS4DeploymentPeer(Player::ApplicationTranslationState& translation, bool cleanClose = false)
{
    using namespace Player;
    CreationControl control;
    const auto scope = *currentInteractiveInstanceScope();
    const auto launcherSlot = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, scope);
    const auto sessionSlot = *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{control.shell.display.id}, scope);
    InstanceAdmissionLease admission;
    if (admission.acquire(sessionSlot) != InstanceAdmissionOutcome::Acquired) return 1;
    InstanceActivationEndpoint activation(sessionSlot);
    if (!activation.listen()) return 1;
    MainWindow window(control.shell.display.id, translation); control.install(window);
    if (!initialize(window)) return 1;
    QTemporaryDir files;
    QImage image(64, 40, QImage::Format_RGBA8888); image.fill(Qt::green);
    const auto file = files.filePath("peer.png"); auto& model = Access::model(window); auto& engine = Access::engine(window);
    const auto playlist = model.addPlaylist(); if (!image.save(file)) return 1;
    const auto item = model.addItem(playlist, WorkspaceMediaType::Image, file);
    if (!item || !model.setImageDisplayDuration(*item, 30) || !Access::start(window, playlist, *item)) return 1;
    Access::pause(window); if (!engine.seek(7)) return 1;
    if (cleanClose) Access::cleanDocument(window);
    activation.bindReadyRole(window, [&] { (void)window.trayPresentation().restore(); });
    SessionCoordinatorJoin join(sessionSlot, launcherSlot);
    join.enableControl(window.trayPresentation());
    join.enableDesktopDeployment([&] { return window.desktopDeploymentProjection(); }, [&](DesktopDeploymentIntent intent) {
        const auto result = window.requestDesktopDeployment(intent);
        std::cout << "S4 PEER ENDPOINT " << Access::desktop(window).isValid() << " attempts=" << control.attempts << std::endl;
        return result;
    });
    int acceptedCloses = 0;
    window.setAcceptedCloseCallback([&] {
        ++acceptedCloses;
        join.beginRoleTeardown(); activation.beginShutdown();
        QTimer::singleShot(0, &join, [&] { join.drainAndStop([] { QCoreApplication::quit(); }); });
    });
    const auto hwnd = window.winId(); const auto revision = model.authoredRevision();
    const auto canvas = model.desktopCanvas().aspectRatio; const auto preview = Access::preview(window);
    const bool dirty = Access::document(window).isDirty(model.authoredRevision());
    const auto projectPath = Access::document(window).currentPath;
    const auto scene = Gpu::scene(engine), clock = Gpu::playback(engine), media = Gpu::media(engine);
    join.start(window, false);
    bool ready = false;
    S5WindowObservation closeObservation; window.installEventFilter(&closeObservation);
    QTimer check; check.setInterval(10);
    QObject::connect(&check, &QTimer::timeout, &window, [&] {
        if (!engine.isInitialized()) return;
        if (window.winId() != hwnd || model.authoredRevision() != revision || model.activeItemId() != item
            || Access::document(window).isDirty(model.authoredRevision()) != dirty || Access::document(window).currentPath != projectPath
            || model.desktopCanvas().aspectRatio != canvas || Access::preview(window) != preview
            || Gpu::scene(engine) != scene || Gpu::playback(engine) != clock || Gpu::media(engine) != media
            || !engine.isPaused() || engine.playbackTime() != 7 || Access::bound(window) != control.shell.display.id)
        { std::cerr << "S4 PEER STATE LOST" << std::endl; QCoreApplication::exit(2); }
        if (!ready && join.qualified())
        {
            window.findChild<QToolButton*>("sessionHideToTray")->click();
            if (!window.isVisible())
            {
                ready = true;
                closeObservation.shows = 0;
                std::cout << "S4 PEER READY " << static_cast<quintptr>(hwnd) << " " << join.incarnation().toString().toStdString() << std::endl;
            }
        }
    });
    S4PeerHarness harness; harness.window = &window; harness.hwnd = reinterpret_cast<HWND>(hwnd);
    QApplication::instance()->installNativeEventFilter(&harness);
    check.start(); QTimer::singleShot(30000, &window, [] { QCoreApplication::exit(3); });
    const auto result = QApplication::exec();
    QApplication::instance()->removeNativeEventFilter(&harness);
    window.removeEventFilter(&closeObservation);
    if (cleanClose && !require(result == 0 && acceptedCloses == 1 && closeObservation.closes == 1
        && closeObservation.shows == 0 && !engine.isInitialized(), "S5 peer hidden clean ordinary Close once, no Show, explicit process exit")) return 4;
    return result;
}

bool verifyS5ClosePeer(Player::ApplicationTranslationState& translation, const QString& scenario)
{
    using namespace Player;
    const auto slot = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope());
    InstanceAdmissionLease admission;
    if (admission.acquire(slot) != InstanceAdmissionOutcome::Acquired) return false;
    InstanceActivationEndpoint activation(slot); if (!activation.listen()) return false;
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    QTemporaryDir files; QSettings settings(files.filePath("preferences.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore preferences(settings);
    int drained = 0;
    ApplicationCoordinator coordinator(slot, activation, true, [&] { ++drained; }); if (!coordinator.listen()) return false;
    const bool last = scenario == "peer-last" || scenario == "peer-pending-death";
    if (!last)
    {
        coordinator.setLauncherFactory([&] {
            auto* w = new LauncherWindow(translation, preferences);
            w->setAcceptedCloseCallback([&, w] { coordinator.retireLauncher(*w); }); return w;
        });
        if (!coordinator.showLauncher()) return false;
    }
    const auto wait = [](const std::function<bool()>& condition) {
        QElapsedTimer time; time.start();
        while (!condition() && time.elapsed() < 6000) { QCoreApplication::processEvents(); QThread::msleep(5); }
        return condition();
    };
    QProcess child; QByteArray output; HWND hwnd = nullptr; QUuid incarnation;
    const auto read = [&] { output += child.readAllStandardOutput(); output += child.readAllStandardError(); };
    const auto start = [&] {
        output.clear(); child.start(QCoreApplication::applicationFilePath(), {scenario == "peer-pending-death" ? "--s4-deployment-peer" : "--s5-close-peer"});
        if (!child.waitForStarted(5000) || !wait([&] { read(); return output.contains("S4 PEER READY ") || child.state() == QProcess::NotRunning; })) return false;
        const auto parts = output.mid(output.indexOf("S4 PEER READY ") + 14).split('\n').first().trimmed().split(' ');
        if (parts.size() != 2 || child.state() != QProcess::Running) return false;
        hwnd = reinterpret_cast<HWND>(parts[0].toULongLong()); incarnation = QUuid(QString::fromUtf8(parts[1]));
        DWORD pid = 0;
        return IsWindow(hwnd) && !IsWindowVisible(hwnd) && GetWindowThreadProcessId(hwnd, &pid)
            && pid == child.processId() && !incarnation.isNull() && output.count("Desktop Scene Engine initialized.") == 1;
    };
    const auto action = [&]() -> QAction* {
        for (auto* entry : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (entry->data().toUuid() == incarnation && entry->menu()->actions().size() == 4) return entry->menu()->actions().back();
        return nullptr;
    };
    const auto close = [&] {
        if (!wait([&] { return action() && action()->isEnabled(); })) return false;
        action()->trigger();
        return wait([&] { return child.state() == QProcess::NotRunning && coordinator.sessionCount() == 0; })
            && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
    };
    bool ok = start();
    if (ok && scenario == "peer-pending-death")
    {
        ok = wait([&] { return action() && action()->isEnabled(); });
        if (ok) action()->trigger();
        ok = ok && wait([&] { const auto op = coordinator.roleCloseOperation(incarnation); return op && op->result == RoleCloseResult::Pending; });
        if (ok)
        {
            child.kill(); child.waitForFinished(5000); // Intentional crash of this admitted test child; NOT Close implementation or success cleanup.
            ok = wait([&] { const auto op = coordinator.roleCloseOperation(incarnation); return coordinator.sessionCount() == 0
                && op && op->result == RoleCloseResult::RoleGone; }) && wait([&] { return drained == 1; });
        }
    }
    else if (ok && scenario == "peer-late")
    {
        QLocalSocket* peer = nullptr;
        for (auto* socket : coordinator.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
            if (socket->property("sessionControl").toBool()) peer = socket;
        if (!peer) ok = false;
        else
        {
            QSignalBlocker hold(peer);
            const auto old = incarnation;
            ok = close(); // Verified process death proceeds independently of held real outcome frames.
            bool realAccepted = false;
            const auto frames = peer->peek(peer->bytesAvailable());
            for (qsizetype offset = 0; offset + 42 <= frames.size(); offset += 42)
                if (frames[offset] == 'O' && frames[offset + 41] == static_cast<char>(RoleCloseResult::CloseAccepted)) realAccepted = true;
            ok = ok && realAccepted && start() && incarnation != old;
            hold.unblock(); hold.dismiss(); QMetaObject::invokeMethod(peer, "readyRead", Qt::DirectConnection);
            ok = ok && wait([&] { return action() && action()->isEnabled(); })
                && !coordinator.roleCloseOperation(incarnation) && child.state() == QProcess::Running && !IsWindowVisible(hwnd)
                && close();
        }
    }
    else if (ok)
    {
        QObject retiredActionOwner; QPointer<QAction> oldAction;
        ok = wait([&] { return action() && action()->isEnabled(); });
        if (ok) { oldAction = action(); oldAction->setParent(&retiredActionOwner); }
        const auto old = incarnation;
        ok = ok && close();
        if (last) ok = ok && wait([&] { return drained == 1; });
        else if (ok)
        {
            ok = start() && incarnation != old;
            if (ok)
            {
                oldAction->setEnabled(true); oldAction->trigger(); // Genuine retained stale-enabled QAction, same exact old incarnation.
                ok = wait([&] { return action() && action()->isEnabled(); }) && !coordinator.roleCloseOperation(incarnation)
                    && child.state() == QProcess::Running && !IsWindowVisible(hwnd) && close();
            }
        }
    }
    if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(5000); } // Test-owned failure cleanup, never success proof.
    if (!ok) { read(); std::cerr << output.constData(); }
    return require(ok, "S5 actual independent hidden Session ordinary Close/result/drain/replacement/death safety");
}

bool verifyS4DeploymentPeer(Player::ApplicationTranslationState& translation)
{
    using namespace Player;
    const auto slot = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope());
    InstanceAdmissionLease admission;
    if (admission.acquire(slot) != InstanceAdmissionOutcome::Acquired) return false;
    InstanceActivationEndpoint activation(slot); if (!activation.listen()) return false;
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    QTemporaryDir files; QSettings settings(files.filePath("preferences.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore preferences(settings);
    ApplicationCoordinator coordinator(slot, activation, false, [] {}); if (!coordinator.listen()) return false;
    coordinator.setLauncherFactory([&] {
        auto* w = new LauncherWindow(translation, preferences);
        w->setAcceptedCloseCallback([&, w] { coordinator.retireLauncher(*w); }); return w;
    });
    if (!coordinator.showLauncher()) return false;
    const auto wait = [](const std::function<bool()>& condition) {
        QElapsedTimer time; time.start();
        while (!condition() && time.elapsed() < 6000) { QCoreApplication::processEvents(); QThread::msleep(5); }
        return condition();
    };
    QProcess child; QByteArray output;
    HWND hwnd = nullptr; QUuid incarnation;
    const auto read = [&] { output += child.readAllStandardOutput(); output += child.readAllStandardError(); };
    const auto start = [&] {
        output.clear(); child.start(QCoreApplication::applicationFilePath(), {"--s4-deployment-peer"});
        if (!child.waitForStarted(5000) || !wait([&] { read(); return output.contains("S4 PEER READY ") || child.state() == QProcess::NotRunning; })) return false;
        const auto parts = output.mid(output.indexOf("S4 PEER READY ") + 14).split('\n').first().trimmed().split(' ');
        if (parts.size() != 2 || child.state() != QProcess::Running) { std::cerr << output.constData(); return false; }
        hwnd = reinterpret_cast<HWND>(parts[0].toULongLong()); incarnation = QUuid(QString::fromUtf8(parts[1]));
        DWORD owner = 0; GetWindowThreadProcessId(hwnd, &owner);
        return IsWindow(hwnd) && !IsWindowVisible(hwnd) && owner == child.processId() && coordinator.sessionCount() == 1;
    };
    const auto close = [&] {
        if (!PostMessageW(hwnd, S4PeerClose, 0, 0) || !wait([&] { return child.state() == QProcess::NotRunning; })) return false;
        read(); return child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0
            && output.count("Desktop Scene Engine initialized.") == 1
            && !output.contains("S4 PEER STATE LOST") && wait([&] { return coordinator.sessionCount() == 0; });
    };
    const auto deploymentAction = [&]() -> QAction* {
        for (auto* a : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (a->data().toUuid() == incarnation && a->menu()->actions().size() == 4) return a->menu()->actions()[1];
        return nullptr;
    };
    const auto exercise = [&] {
        if (!wait([&] { return deploymentAction() && deploymentAction()->isEnabled(); })) return false;
        deploymentAction()->trigger();
        if (!wait([&] { read(); return coordinator.sessionDeploymentResult(incarnation) == DesktopDeploymentResult::Completed
            && output.contains("S4 PEER ENDPOINT 1 attempts=1"); })) return false;
        if (IsWindowVisible(hwnd) || !IsWindow(hwnd)) return false;
        deploymentAction()->trigger();
        if (!wait([&] { read(); return coordinator.sessionDeploymentResult(incarnation) == DesktopDeploymentResult::Completed
            && output.contains("S4 PEER ENDPOINT 0 attempts=1"); })) return false;
        return !IsWindowVisible(hwnd) && IsWindow(hwnd);
    };
    bool ok = start();
    QObject retiredActionOwner;
    QPointer<QAction> oldAction;
    QUuid oldIncarnation;
    if (ok)
    {
        ok = wait([&] { return deploymentAction() && deploymentAction()->isEnabled(); });
        if (ok) { oldAction = deploymentAction(); oldAction->setParent(&retiredActionOwner); oldIncarnation = incarnation; }
        ok = ok && exercise() && close() && start();
    }
    if (ok)
    {
        oldAction->trigger();
        if (!wait([&] { return deploymentAction() && deploymentAction()->isEnabled(); })) ok = false;
        read(); ok = ok && oldIncarnation != incarnation && !output.contains("[Tray] Deployment result=") && !IsWindowVisible(hwnd);
        ok = ok && exercise() && close();
    }
    if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(5000); } // Test-owned failure cleanup only.
    if (!ok) { read(); std::cerr << output.constData(); }
    return require(ok, "S4 real independent child native tray Deploy/Undeploy and stale replacement action preserve one Engine/session/hidden HWND");
}

bool verifyCanvasOutsideSessionLifecycle(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    control.shell.display.pixelWidth = control.shell.display.pixelHeight = 320;
    QTemporaryDir directory;
    QSettings settings{directory.filePath("canvas-outside.ini"), QSettings::IniFormat};
    Player::ApplicationPreferencesStore store{settings};
    if (!require(control.shell.valid && directory.isValid()
            && store.saveCanvasOutsideMode(control.shell.display.id, CanvasOutsideMode::Transparent)
                == QSettings::NoError, "session policy fixture initializes only its own route preference")) return false;
    MainWindow window{control.shell.display.id, translation, store, store.load()};
    control.install(window);
    auto* opaque = window.findChild<QAction*>(QStringLiteral("canvasOutsideOpaqueAction"));
    auto* transparent = window.findChild<QAction*>(QStringLiteral("canvasOutsideTransparentAction"));
    if (!require(opaque && transparent && opaque->isEnabled() && transparent->isEnabled()
            && transparent->isChecked() && !opaque->isChecked()
            && Access::canvasOutsideMode(window) == CanvasOutsideMode::Transparent
            && !Access::canvasOutsideFrame(window) && control.attempts == 0,
        "per-route restore and available actions precede native initialization, with no target instruction")) return false;
    auto canvas = Access::model(window).desktopCanvas();
    canvas.aspectRatio = {16, 9};
    Access::model(window).setDesktopCanvas(canvas);
    if (!initialize(window)) return false;
    auto& gpu = backend(window);
    auto& engine = Access::engine(window);
    const auto preview = Access::preview(window);
    opaque->trigger();
    transparent->trigger();
    if (!require(previewHealthy(window) && control.attempts == 0
            && !Access::canvasOutsideFrame(window) && gpu.presentationTargetCountForTesting() == 1U,
        "mode changes before Deploy use normal Preview ticks without creating a Desktop endpoint")) return false;
    if (!Access::clickDeploymentButton(window)) return false;
    const auto firstDesktop = Access::desktop(window);
    const auto sample = [&](int y, std::uint32_t expectedDesktop, std::uint32_t expectedPreview)
    {
        const auto desktop = Access::desktop(window);
        const auto snapshot = Access::canvasOutsideFrame(window);
        if (!require(snapshot && snapshot->targetId == desktop
                && snapshot->mode == Access::canvasOutsideMode(window),
                "MainWindow derives a fresh exact-ID/mode frame instruction")
            || !gpu.captureNextPresentationPixelForTesting(desktop, 160, y)
            || !gpu.captureNextPresentationPixelForTesting(preview)
            || !gpu.forceNextDxgiPresentResultForTesting(preview, static_cast<std::int32_t>(S_OK))
            || !gpu.forceNextDxgiPresentResultForTesting(desktop, static_cast<std::int32_t>(S_OK))) return false;
        Access::tick(window); // The existing production tick, including paused/static rendering.
        const auto pd = gpu.presentationTargetDiagnosticsForTesting(preview);
        const auto dd = gpu.presentationTargetDiagnosticsForTesting(desktop);
        return require(pd.capturedPixelValid && pd.capturedPixelRgba == expectedPreview
                && dd.capturedPixelValid && dd.capturedPixelRgba == expectedDesktop
                && pd.lastPresentSyncInterval == 0U
                && pd.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT && dd.lastPresentSyncInterval == 1U
                && dd.lastPresentFlags == 0U && pd.lastPresentOrder < dd.lastPresentOrder,
            "normal onTick changes Desktop only, retaining opaque Preview and existing paced Present order");
    };
    if (!sample(5, 0U, 0xFF191919U) || !sample(160, 0U, 0xFF191919U)) return false;
    const auto native = gpu.presentationTargetDiagnosticsForTesting(firstDesktop);
    const auto composition = gpu.compositionDiagnosticsForTesting();
    const auto host = Access::host(window).nativeHandle();
    const auto targetInfo = gpu.presentationTargetInfo(firstDesktop);
    opaque->trigger();
    if (!sample(5, 0xFF191919U, 0xFF191919U)) return false;
    transparent->trigger();
    if (!sample(5, 0U, 0xFF191919U)) return false;
    const auto switched = gpu.presentationTargetDiagnosticsForTesting(firstDesktop);
    if (!require(Access::host(window).nativeHandle() == host && control.attempts == 1
            && Access::desktop(window) == firstDesktop
            && switched.deviceIdentity == native.deviceIdentity
            && switched.swapChainIdentity == native.swapChainIdentity
            && switched.compositionTargetIdentity == native.compositionTargetIdentity
            && switched.compositionVisualIdentity == native.compositionVisualIdentity
            && gpu.presentationTargetInfo(firstDesktop).targetGeneration == targetInfo.targetGeneration
            && gpu.compositionDiagnosticsForTesting().commitCount == composition.commitCount
            && gpu.compositionDiagnosticsForTesting().waitCount == composition.waitCount,
        "static mode switching creates no host, swap chain, DComp binding or transaction")) return false;

    const QString mediaPath = directory.filePath("canvas-outside.bmp");
    auto& model = Access::model(window);
    const auto playlist = model.addPlaylist();
    if (!writeRecoveryBmp(mediaPath)) return false;
    const auto item = model.addItem(playlist, Player::WorkspaceMediaType::Image, mediaPath);
    if (!require(item && model.setImageDisplayDuration(*item, 30)
            && Access::start(window, playlist, *item), "production image session starts for paused-mode checks")) return false;
    Access::pause(window);
    Access::seek(window, 400);
    const auto time = engine.playbackTime();
    const auto revision = model.authoredRevision();
    const bool dirty = window.isWindowModified();
    const void* scene = Gpu::scene(engine);
    const void* playback = Gpu::playback(engine);
    const void* media = Gpu::media(engine);
    const void* project = Access::projection(window);
    const auto uploads = gpu.mediaUploadCountForTesting();
    const auto statePreserved = [&]
    {
        return engine.isPaused() && engine.playbackTime() == time && time > 0.0F
            && model.authoredRevision() == revision && window.isWindowModified() == dirty
            && model.desktopCanvas().aspectRatio == Domain::AspectRatio{16, 9}
            && model.activeItemId() == item && Access::bound(window) == control.shell.display.id
            && Gpu::scene(engine) == scene && Gpu::playback(engine) == playback && Gpu::media(engine) == media
            && Access::projection(window) == project && engine.isInitialized();
    };
    if (!sample(5, 0U, 0xFF0DDB0DU)) return false;
    opaque->trigger();
    if (!sample(5, 0xFF191919U, 0xFF0DDB0DU)) return false;
    transparent->trigger();
    if (!sample(5, 0U, 0xFF0DDB0DU)
        || !require(statePreserved() && gpu.mediaUploadCountForTesting() == uploads,
            "paused mode switches keep authored state, playback/seek, active media and the single upload")) return false;
    if (!Access::clickDeploymentButton(window)
        || !require(!Access::desktop(window).isValid() && !Access::canvasOutsideFrame(window)
                && Access::canvasOutsideMode(window) == CanvasOutsideMode::Transparent
                && statePreserved() && previewHealthy(window),
            "Undeploy removes only the Desktop instruction/endpoint, not MainWindow mode or runtime")) return false;
    if (!Access::clickDeploymentButton(window)
        || !require(Access::desktop(window) != firstDesktop && gpu.presentationTargetCountForTesting() == 2U,
            "Redeploy creates exactly one fresh target")
        || !sample(5, 0U, 0xFF0DDB0DU) || !statePreserved()) return false;

    // Storage is initialization-only: a contradictory external value must not
    // replace the live choice during an endpoint-generation reconciliation.
    if (store.saveCanvasOutsideMode(control.shell.display.id, CanvasOutsideMode::Opaque) != QSettings::NoError)
        return false;
    const auto oldEndpoint = Access::desktop(window);
    if (!control.shell.destroyGeneration() || !control.shell.createGeneration()) return false;
    Access::native(window, true); // TEST-owned shell generation, never a real Explorer restart.
    if (!require(Access::desktop(window).isValid() && Access::desktop(window) != oldEndpoint
            && Access::canvasOutsideMode(window) == CanvasOutsideMode::Transparent && statePreserved()
            && store.loadCanvasOutsideMode(control.shell.display.id).value == CanvasOutsideMode::Opaque,
        "fresh Shell endpoint snapshots current live mode without storage reload/repair or runtime reset")
        || !sample(5, 0U, 0xFF0DDB0DU)) return false;
    control.missing = true;
    Access::native(window, false);
    if (!require(!Access::canvasOutsideFrame(window) && !Access::desktop(window).isValid()
            && Access::desired(window) && previewHealthy(window),
        "temporarily unavailable bound display leaves a healthy Preview and no stale frame instruction")) return false;
    if (Access::continuationArmed(window)) (void)Access::fireContinuation(window);
    opaque->trigger();
    transparent->trigger();
    control.missing = false;
    Access::native(window, false);
    if (!sample(5, 0U, 0xFF0DDB0DU) || !statePreserved()) return false;

    const auto desktop = Access::desktop(window);
    const auto oldDeviceGeneration = gpu.presentationTargetInfo(preview).deviceGeneration;
    if (!gpu.forceNextDxgiPresentResultForTesting(preview, static_cast<std::int32_t>(S_OK))
        || !gpu.forceNextDxgiPresentResultForTesting(desktop, static_cast<std::int32_t>(E_FAIL))) return false;
    Access::tick(window);
    if (!require(gpu.pendingTargetLocalRecoveryForTesting(desktop) && gpu.presentationTargetInfo(preview).ready,
        "healthy DComp Present failure arms only the existing one-shot target recovery")) return false;
    Access::tick(window);
    if (!sample(5, 0U, 0xFF0DDB0DU)
        || !require(gpu.presentationTargetInfo(preview).deviceGeneration == oldDeviceGeneration && statePreserved(),
            "7A recovery keeps live policy and runtime without shared D3D replacement")) return false;
    const auto uploadsBeforeLoss = gpu.mediaUploadCountForTesting();
    if (!gpu.forceNextDxgiPresentResultForTesting(preview, static_cast<std::int32_t>(E_FAIL),
            static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))) return false;
    Access::tick(window);
    if (!require(gpu.pendingGraphicsRecoveryForTesting() && statePreserved(),
        "D3D11 device-removal tick preserves MainWindow mode/runtime while retiring shared graphics")) return false;
    Access::tick(window);
    if (!sample(5, 0U, 0xFF0DDB0DU) || !sample(160, 0xFF0DDB0DU, 0xFF0DDB0DU)
        || !require(gpu.presentationTargetInfo(preview).deviceGeneration > oldDeviceGeneration
                && gpu.graphicsRecoveryAttemptCountForTesting() == 1U
                && gpu.mediaUploadCountForTesting() == uploadsBeforeLoss + 1U && statePreserved(),
            "Slice-5 recovery rebuilds one shared media generation and applies the next current frame")) return false;
    DestroyObservation close(window, true);
    Access::shutdown(window);
    return require(close.destroyed && close.detachedFirst && close.stoppedFirst && control.shell.intact()
            && !engine.isInitialized() && !Access::canvasOutsideFrame(window),
        "normal existing shutdown detaches Desktop before destroying its test-owned host");
}

bool verifyLiveDeployedMediaEdit(Player::ApplicationTranslationState& translation, bool scaleEdit)
{
    CreationControl control;
    control.shell.display.pixelWidth = 320;
    control.shell.display.pixelHeight = 200;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    Access::newDocument(window); // Use normal display-derived Canvas; never override its ratio.
    QTemporaryDir directory;
    QImage image(64, 40, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, x < 32 ? QColor{219, 13, 13} : QColor{13, 219, 13});
    const auto path = directory.filePath("live-coverage.png");
    if (!require(control.shell.valid && directory.isValid(), "live-edit test-owned shell and temporary directory")
        || !require(image.save(path), "live-edit matching-aspect opaque PNG writes successfully")
        || !require(initialize(window), "live-edit normal Preview initialization")) return false;
    auto& model = Access::model(window);
    const auto playlist = model.addPlaylist();
    const auto item = model.addItem(playlist, Player::WorkspaceMediaType::Image, path);
    if (!require(item && model.setImageDisplayDuration(*item, 30)
            && Access::start(window, playlist, *item) && Access::inspect(window, *item),
            "live-edit fixture starts the active production item and its normal inspector")) return false;
    Access::pause(window);
    Access::seek(window, 400);
    auto* scale = window.findChild<QDoubleSpinBox*>(QStringLiteral("visualScaleSpinBox"));
    auto* x = window.findChild<QDoubleSpinBox*>(QStringLiteral("visualPositionXSpinBox"));
    auto* y = window.findChild<QDoubleSpinBox*>(QStringLiteral("visualPositionYSpinBox"));
    auto* fit = window.findChild<QComboBox*>(QStringLiteral("visualFitAspectCombo"));
    auto* opaque = window.findChild<QAction*>(QStringLiteral("canvasOutsideOpaqueAction"));
    auto* reveal = window.findChild<QAction*>(QStringLiteral("canvasOutsideTransparentAction"));
    const auto canvas = model.desktopCanvas().aspectRatio;
    if (!require(scale && x && y && fit && opaque && reveal
            && canvas == *Domain::AspectRatio::from(320, 200),
            "existing production controls and normal display Canvas")) return false;
    const auto selectFit = [&](Domain::MediaPresentationMode mode) {
        const int index = fit->findData(static_cast<int>(mode));
        if (index < 0) return false;
        fit->setCurrentIndex(index);
        // The production control commits user activation, not programmatic selection.
        return QMetaObject::invokeMethod(fit, "activated", Qt::DirectConnection, Q_ARG(int, index));
    };
    scale->setValue(1.0); x->setValue(0.0); y->setValue(0.0);
    if (!require(selectFit(Domain::MediaPresentationMode::Native),
            "select Native through the production fit activation")) return false;
    if (!Access::clickDeploymentButton(window)) return false;
    reveal->trigger();
    auto& engine = Access::engine(window);
    auto& gpu = backend(window);
    const auto desktop = Access::desktop(window), preview = Access::preview(window);
    const auto target = gpu.presentationTargetInfo(desktop);
    const auto native = gpu.presentationTargetDiagnosticsForTesting(desktop);
    const auto composition = gpu.compositionDiagnosticsForTesting();
    const auto host = Access::host(window).nativeHandle();
    const auto time = engine.playbackTime();
    const void* runtime = Gpu::scene(engine), *clock = Gpu::playback(engine);
    const auto identityPreserved = [&]
    {
        const auto d = gpu.presentationTargetDiagnosticsForTesting(desktop);
        const auto info = gpu.presentationTargetInfo(desktop);
        const auto shared = gpu.compositionDiagnosticsForTesting();
        return require(Access::desktop(window) == desktop && Access::preview(window) == preview
                && Access::host(window).nativeHandle() == host && control.attempts == 1
                && gpu.presentationTargetCountForTesting() == 2U
                && d.deviceIdentity == native.deviceIdentity && d.swapChainIdentity == native.swapChainIdentity
                && d.compositionTargetIdentity == native.compositionTargetIdentity
                && d.compositionVisualIdentity == native.compositionVisualIdentity
                && info.deviceGeneration == target.deviceGeneration && info.targetGeneration == target.targetGeneration
                && shared.commitCount == composition.commitCount && shared.waitCount == composition.waitCount
                && model.desktopCanvas().aspectRatio == canvas
                && Access::bound(window) == control.shell.display.id && model.activeItemId() == item
                && Gpu::scene(engine) == runtime && Gpu::playback(engine) == clock
                && engine.isPaused() && engine.playbackTime() == time,
            "live edits retain Desktop HWND/target/native bindings, shared runtime/clock and playback position");
    };
    const auto matches = [](std::uint32_t pixel, std::uint32_t expected)
    {
        if (expected != 0xFF191919U) return pixel == expected;
        return (pixel >> 24) == 255U && (pixel & 255U) >= 25U && (pixel & 255U) <= 26U
            && ((pixel >> 8) & 255U) >= 25U && ((pixel >> 8) & 255U) <= 26U
            && ((pixel >> 16) & 255U) >= 25U && ((pixel >> 16) & 255U) <= 26U;
    };
    const auto sample = [&](double u, double v, std::uint32_t expectedDesktop,
        std::uint32_t expectedPreview, const char* label)
    {
        for (const auto id : {preview, desktop})
        {
            const auto size = gpu.presentationTargetInfo(id);
            if (!gpu.forceNextDxgiPresentResultForTesting(id, static_cast<std::int32_t>(S_OK))
                || !gpu.captureNextPresentationPixelForTesting(id,
                    static_cast<int>(size.clientWidth * u), static_cast<int>(size.viewportHeight * v))) return false;
        }
        Access::tick(window); // Actual onTick -> Engine shared geometry -> both native targets.
        const auto d = gpu.presentationTargetDiagnosticsForTesting(desktop);
        const auto p = gpu.presentationTargetDiagnosticsForTesting(preview);
        std::cout << label << " Desktop=0x" << std::hex << d.capturedPixelRgba
            << " Preview=0x" << p.capturedPixelRgba << std::dec << '\n';
        return require(d.capturedPixelValid && p.capturedPixelValid
                && matches(d.capturedPixelRgba, expectedDesktop) && matches(p.capturedPixelRgba, expectedPreview)
                && p.lastPresentSyncInterval == 0U && p.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                && d.lastPresentSyncInterval == 1U && d.lastPresentFlags == 0U
                && p.lastPresentOrder < d.lastPresentOrder, label) && identityPreserved();
    };
    constexpr std::uint32_t red = 0xFF0D0DDBU, green = 0xFF0DDB0DU, background = 0xFF191919U;
    if (!sample(0.25, 0.5, red, red, "live baseline scale 1 covers Canvas")
        || !sample(0.75, 0.5, green, green, "live baseline right source UV")) return false;
    const auto uploads = gpu.mediaUploadCountForTesting();
    if (scaleEdit) scale->setValue(0.9);
    else { x->setValue(0.2); y->setValue(0.15); }
    if (!identityPreserved()
        || !sample(scaleEdit ? 0.25 : 0.65, 0.4, red, red,
            "live geometry/UV updates before testing uncovered alpha")
        || !sample(0.8, 0.4, green, green, "live moved/scaled media remains opaque")) return false;
    // Each existing projection rebind can upload once, never once per target.
    const auto afterEditUploads = gpu.mediaUploadCountForTesting();
    if (!require(afterEditUploads == uploads + (scaleEdit ? 1U : 2U),
            "existing live projection edits upload media once per rebind, shared by both targets")) return false;
    opaque->trigger();
    if (!sample(0.025, 0.5, background, background, "live Opaque internal gap")) return false;
    reveal->trigger();
    bool ok = sample(0.025, 0.5, 0U, background,
        scaleEdit ? "live scale 0.9 internal border must reveal wallpaper" : "live position internal gap must reveal wallpaper");
    if (scaleEdit)
    {
        for (const auto point : {std::array{0.975, 0.5}, std::array{0.5, 0.025}, std::array{0.5, 0.975}})
            ok = sample(point[0], point[1], 0U, background, "live scale 0.9 remaining internal borders") && ok;
    }
    else ok = sample(0.65, 0.95, 0U, background, "live positive Y exposes bottom gap") && ok;
    if (!require(gpu.mediaUploadCountForTesting() == afterEditUploads,
            "background policy switches never reload media or change content geometry")) return false;
    // Fit edits use the same existing control/rebind path while still deployed.
    // Custom's default 16:9 baseline exposes more vertical gap than this 16:10 Canvas.
    const double fitGapY = scaleEdit ? 0.075 : 0.825;
    for (const auto mode : {Domain::MediaPresentationMode::AutoFill,
            Domain::MediaPresentationMode::Custom, Domain::MediaPresentationMode::Native})
    {
        if (!require(selectFit(mode)
                && model.item(*item)->presentation.mediaPresentation.mode == mode,
                "live fit edit commits to the one authored item")) return false;
        const bool customGap = mode == Domain::MediaPresentationMode::Custom;
        ok = sample(0.8, fitGapY, customGap ? 0U : green, customGap ? background : green,
            "live fit changes coverage on the next tick without redeployment") && ok;
    }
    return ok;
}

bool verifyLiveDeployedScaleUpdate(Player::ApplicationTranslationState& translation)
{ return verifyLiveDeployedMediaEdit(translation, true); }
bool verifyLiveDeployedPositionUpdate(Player::ApplicationTranslationState& translation)
{ return verifyLiveDeployedMediaEdit(translation, false); }

bool verifyDefaultCanvas(Player::ApplicationTranslationState& translation)
{
    for (const auto dimensions : {std::array{2560, 1600}, std::array{1920, 1080}})
    {
        CreationControl control;
        control.shell.display.pixelWidth = dimensions[0];
        control.shell.display.pixelHeight = dimensions[1];
        MainWindow window{control.shell.display.id, translation};
        control.install(window);
        Access::newDocument(window); // Actual new-document reset and fresh exact identity enumeration.
        const auto aspect = Domain::AspectRatio::from(dimensions[0], dimensions[1]);
        if (!require(aspect && Access::model(window).desktopCanvas().aspectRatio == *aspect
            && Access::canvasUi(window, *aspect) && !window.isWindowModified(),
            "new exact-bound native Canvas/model/Preview must match dimensions, not fixed 16:9")) return false;
        if (!initializeDeployed(window)) return false;
        auto& gpu = backend(window);
        const unsigned char green[]{13, 219, 13, 255};
        if (gpu.updateImage(green, 1, 1, 4, 4, sizeof(green))
            != DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated) return false;
        const auto sample = [&](bool gutter)
        {
            const auto preview = Access::preview(window), desktop = Access::desktop(window);
            const auto previewInfo = gpu.presentationTargetInfo(preview);
            const auto desktopInfo = gpu.presentationTargetInfo(desktop);
            if (!gpu.captureNextPresentationPixelForTesting(preview, previewInfo.clientWidth / 2,
                    previewInfo.clientHeight / 2)
                || !gpu.captureNextPresentationPixelForTesting(desktop, desktopInfo.clientWidth / 2, 5)) return false;
            if (!gpu.forceNextDxgiPresentResultForTesting(preview, static_cast<std::int32_t>(S_OK))
                || !gpu.forceNextDxgiPresentResultForTesting(desktop, static_cast<std::int32_t>(S_OK)))
                return false;
            Access::tick(window); // Real onTick -> Engine::render(model Canvas), not a direct test render.
            const auto a = gpu.presentationTargetDiagnosticsForTesting(preview);
            const auto b = gpu.presentationTargetDiagnosticsForTesting(desktop);
            const auto pixel = b.capturedPixelRgba;
            const bool expectedDesktop = gutter
                ? ((pixel & 255U) >= 25 && (pixel & 255U) <= 26
                    && ((pixel >> 8) & 255U) >= 25 && ((pixel >> 8) & 255U) <= 26
                    && ((pixel >> 16) & 255U) >= 25 && ((pixel >> 16) & 255U) <= 26
                    && (pixel >> 24) == 255)
                : pixel == 0xFF0DDB0D;
            return require(a.capturedPixelValid && a.capturedPixelRgba == 0xFF0DDB0D
                && b.capturedPixelValid && expectedDesktop,
                "MainWindow Canvas reaches Engine: Preview content, Desktop edge full/default or authored gutter");
        };
        if (!sample(false)) return false;
        if (dimensions[1] == 1600)
        {
            QTemporaryDir projectDirectory;
            const auto path = std::filesystem::path{projectDirectory.filePath("explicit-16-9.json").toStdWString()};
            auto project = Access::model(window).authoredProject();
            project.desktopCanvas.aspectRatio = {16, 9};
            Infrastructure::Persistence::saveAuthoredProjectFile(project, path);
            if (!require(Access::load(window, path)
                && Access::model(window).desktopCanvas().aspectRatio == Domain::AspectRatio{16, 9}
                && Access::canvasUi(window, {16, 9}), "real project load overrides 16:10 default with authored 16:9")) return false;
            const auto revision = Access::model(window).authoredRevision();
            control.shell.display.pixelWidth = 1920; control.shell.display.pixelHeight = 1080;
            Access::native(window, false);
            control.shell.display.pixelWidth = 2560; control.shell.display.pixelHeight = 1600;
            Access::native(window, false);
            if (!require(Access::model(window).desktopCanvas().aspectRatio == Domain::AspectRatio{16, 9}
                && Access::model(window).authoredRevision() == revision && Access::canvasUi(window, {16, 9}),
                "display invalidation/recovery never overwrites loaded authored Canvas")) return false;
            if (gpu.updateImage(green, 1, 1, 4, 4, sizeof(green))
                    != DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated
                || !sample(true)) return false;
        }
        std::cout << "Canvas " << dimensions[0] << 'x' << dimensions[1] << " default="
            << aspect->numerator << ':' << aspect->denominator << " UI/Preview/Engine GPU PASS\n";
    }
    MainWindow unresolved{Player::TestSupport::mainWindowTestDisplayId(), translation};
    DisplayDescriptor other;
    other.id = DisplayId::fromTargetDevicePath("\\\\?\\DISPLAY#OTHER_NOT_BOUND");
    other.pixelWidth = 2560; other.pixelHeight = 1600; other.primary = true;
    Access::enumerator(unresolved, {DisplayEnumerationStatus::Success, {{other}}});
    Access::newDocument(unresolved);
    return require(Access::model(unresolved).desktopCanvas().aspectRatio == Domain::AspectRatio{16, 9}
        && Access::canvasUi(unresolved, {16, 9}), "unresolved bound identity retains safe default, never another/primary display");
}

bool verifyMainWindowRefreshOwnership(Player::ApplicationTranslationState& translation)
{
    MainWindow window{Player::TestSupport::mainWindowTestDisplayId(), translation};
    return require(!Access::hasManualRefreshButton(window),
        "MainWindow must not expose manual display Refresh; Launcher owns manual discovery");
}

bool verifyDeploymentSemantics(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(initialize(window) && Access::button(window, "Deploy")
        && control.attempts == 0 && !Access::desktop(window).isValid()
        && !Access::host(window).isValid() && backend(window).presentationTargetCountForTesting() == 1
        && backend(window).pacingPresentationTargetForTesting() == Access::preview(window)
        && previewHealthy(window), "MainWindow startup must be Preview-only, with NO Desktop creation attempt")) return false;
    Access::native(window, true);
    Access::native(window, false);
    if (!require(control.attempts == 0 && !Access::host(window).isValid() && !Access::desired(window),
        "TaskbarCreated/WM_DISPLAYCHANGE before Deploy must not create Desktop")) return false;
    Access::shutdown(window);
    Access::native(window, true);
    if (!require(control.attempts == 0 && !Access::engine(window).isInitialized()
        && !Access::desktop(window).isValid(), "Preview-only shutdown never creates/cleans a Desktop endpoint")) return false;
    if (!require(initialize(window) && !Access::desired(window) && Access::clickDeploymentButton(window)
        && control.attempts == 1 && Access::desired(window) && Access::host(window).isValid()
        && Access::desktop(window).isValid() && Access::button(window, "Undeploy")
        && backend(window).presentationTargetCountForTesting() == 2
        && backend(window).pacingPresentationTargetForTesting() == Access::desktop(window)
        && previewHealthy(window), "actual existing Deploy button creates Desktop, including empty clear Canvas")) return false;
    DestroyObservation close(window, true);
    Access::shutdown(window);
    return require(close.destroyed && close.detachedFirst && close.stoppedFirst && !Access::desired(window),
        "explicitly deployed shutdown clears intent and detaches before host destruction");
}

bool verifyReversibleDeployment(Player::ApplicationTranslationState& translation, bool checkLabel)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(initialize(window) && Access::button(window, "Deploy") && Access::clickDeploymentButton(window),
        "existing Deploy button starts one Desktop endpoint")) return false;
    if (checkLabel && !require(Access::button(window, "Undeploy"),
        "successful realization must change the existing button to Undeploy")) return false;
    const auto first = Access::desktop(window);
    DestroyObservation cancel(window, false);
    Access::sendNative(window, true); // Queued BEFORE cancellation; must see cleared intent later.
    if (!require(Access::clickDeploymentButton(window) && !Access::desired(window) && !Access::desktop(window).isValid()
        && !Access::host(window).isValid() && cancel.destroyed && cancel.detachedFirst
        && Access::button(window, "Deploy") && backend(window).presentationTargetCountForTesting() == 1
        && backend(window).pacingPresentationTargetForTesting() == Access::preview(window)
        && previewHealthy(window), "second click must Undeploy safely, not replace/redeploy Desktop")) return false;
    Access::native(window, true);
    Access::native(window, false);
    if (!require(control.attempts == 1 && !Access::desktop(window).isValid(),
        "native events after user Undeploy must not recreate Desktop")) return false;
    return require(Access::clickDeploymentButton(window) && control.attempts == 2 && Access::desktop(window).isValid()
        && Access::desktop(window) != first && Access::host(window).isValid()
        && Access::button(window, "Undeploy") && backend(window).presentationTargetCountForTesting() == 2,
        "Deploy-Undeploy-Deploy creates a fresh nonduplicated endpoint");
}

bool verifyFailedDeployIntent(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    control.forcedStatus = DesktopPresentationHostStatus::NativeFailure;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(initialize(window) && Access::clickDeploymentButton(window) && !Access::desired(window)
        && Access::button(window, "Deploy") && !Access::host(window).isValid()
        && !Access::desktop(window).isValid() && !Access::continuationArmed(window)
        && previewHealthy(window),
        "failed initial Deploy must restore intent false and keep Deploy label")) return false;
    Access::native(window, true);
    Access::native(window, false);
    return require(control.attempts == 1, "failed initial Deploy cannot be retried by native events");
}

bool verifyDetachRejection(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    const auto target = Access::desktop(window);
    const auto handle = Access::host(window).nativeHandle();
    // Cancellation must remain available even if the bound display snapshot is
    // unresolved but graphics still borrows the retained Desktop HWND.
    Access::enumeration(window, {DisplayEnumerationStatus::Success, {}});
    bool retained = false;
    {
        QScopedValueRollback<bool> rejection(Gpu::initialized(Access::engine(window)), false);
        retained = Access::clickDeploymentButton(window) && !Access::desired(window)
            && Access::desktop(window) == target && Access::host(window).nativeHandle() == handle
            && Access::host(window).isValid() && Access::button(window, "Undeploy")
            && backend(window).presentationTargetCountForTesting() == 2;
        Access::native(window, true);
        Access::native(window, false);
        retained = retained && control.attempts == 1 && Access::desktop(window) == target
            && Access::host(window).nativeHandle() == handle
            && !Access::continuationArmed(window);
    }
    if (!require(retained && previewHealthy(window),
        "persistent detach rejection stays fail-closed, retains target/HWND, and never arms retry")) return false;
    DestroyObservation retry(window, false);
    return require(Access::clickDeploymentButton(window) && retry.destroyed && retry.detachedFirst
        && !Access::desktop(window).isValid() && Access::host(window).nativeHandle() == nullptr
        && !Access::desired(window) && Access::button(window, "Deploy") && previewHealthy(window),
        "retrying Undeploy releases graphics before retained HWND; never redeploys");
}

bool verifyDeploymentTranslation(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    const auto target = Access::desktop(window);
    const auto text = [&window]
    { return window.findChild<QToolButton*>(QStringLiteral("deployDisplayButton"))->text(); };
    Access::language(window, Player::UiLanguage::SimplifiedChinese);
    if (!require(translation.activeLanguage() == Player::UiLanguage::SimplifiedChinese
        && text() == QStringLiteral("取消部署") && Access::desktop(window) == target
        && control.attempts == 1, "language change translates Undeploy without changing deployment")) return false;
    if (!require(Access::clickDeploymentButton(window) && text() == QStringLiteral("部署")
        && !Access::desktop(window).isValid() && previewHealthy(window), "Chinese Undeploy returns to 部署")) return false;
    if (!require(Access::clickDeploymentButton(window) && text() == QStringLiteral("取消部署")
        && Access::desktop(window).isValid() && Access::desktop(window) != target,
        "Chinese Deploy establishes fresh endpoint")) return false;
    Access::language(window, Player::UiLanguage::English);
    return require(text() == QStringLiteral("Undeploy") && Access::desired(window) && control.attempts == 2,
        "English retranslation preserves derived Undeploy state");
}

bool verifyHostFailures(Player::ApplicationTranslationState& translation)
{
    {
        MainWindow missing{Player::TestSupport::mainWindowTestDisplayId(), translation};
        if (!require(initialize(missing) && backend(missing).presentationTargetCountForTesting() == 1
            && !Access::desktop(missing).isValid() && !Access::host(missing).isValid()
            && previewHealthy(missing), "real unresolved canonical identity is nonfatal, no monitor fallback")) return false;
        DisplayDescriptor previouslyAvailable;
        previouslyAvailable.id = Access::bound(missing); previouslyAvailable.pixelWidth = 2560;
        previouslyAvailable.pixelHeight = 1600;
        Access::enumeration(missing, {DisplayEnumerationStatus::Success, {{previouslyAvailable}}});
        if (!require(Access::clickDeploymentButton(missing) && !Access::desired(missing)
            && Access::button(missing, "Deploy") && !Access::host(missing).isValid()
            && !Access::desktop(missing).isValid() && previewHealthy(missing),
            "Deploy re-resolves stale exact identity, fails nonfatally without monitor fallback")) return false;
        Access::shutdown(missing);
        if (!require(!Access::engine(missing).isInitialized(), "unavailable Desktop shutdown")) return false;
    }
    for (const auto status : {DesktopPresentationHostStatus::DisplayUnavailable,
            DesktopPresentationHostStatus::UnsupportedShellTopology, DesktopPresentationHostStatus::NativeFailure})
    {
        CreationControl control;
        control.forcedStatus = status;
        MainWindow window{control.shell.display.id, translation};
        control.install(window);
        if (!require(initializeDeployed(window) && control.attempts == 1 && control.exactBinding && !Access::desired(window)
            && Access::button(window, "Deploy")
            && !Access::desktop(window).isValid() && !Access::host(window).isValid()
            && !Access::continuationArmed(window)
            && backend(window).presentationTargetCountForTesting() == 1 && previewHealthy(window),
            "every Desktop-local host failure preserves Preview and fixed session")) return false;
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        if (!require(control.attempts == 1, "failed explicit Deploy has no continuous retry")) return false;
        control.forcedStatus = DesktopPresentationHostStatus::Ready;
        Access::native(window, true);
        Access::native(window, false);
        if (!require(control.attempts == 1 && !Access::desired(window),
            "native events cannot retry a failed initial host creation")) return false;
        if (!require(Access::clickDeploymentButton(window) && control.attempts == 2
            && Access::desired(window) && Access::host(window).isValid()
            && Access::desktop(window).isValid() && Access::button(window, "Undeploy"),
            "another explicit Deploy can succeed after initial host failure")) return false;
    }
    return true;
}

bool verifyAttachFailures(Player::ApplicationTranslationState& translation)
{
    for (const auto point : {Rendering::Detail::CompositionFailurePoint::SwapChainCreation,
            Rendering::Detail::CompositionFailurePoint::RenderTargetViewCreation,
            Rendering::Detail::CompositionFailurePoint::AttachCommit})
    {
        CreationControl control;
        control.failure = point;
        MainWindow window{control.shell.display.id, translation};
        control.install(window);
        if (!require(control.shell.valid && initializeDeployed(window) && control.lastWindow
            && !IsWindow(control.lastWindow) && !Access::host(window).isValid()
            && !Access::desktop(window).isValid() && !Access::desired(window) && Access::button(window, "Deploy")
            && !Access::continuationArmed(window)
            && backend(window).presentationTargetCountForTesting() == 1
            && backend(window).pacingPresentationTargetForTesting() == Access::preview(window)
            && control.observation && control.observation->installed && control.observation->destroyed
            && control.observation->detachedFirst && control.shell.intact() && previewHealthy(window),
            "real D3D/DComp attach failure cleans successful host and preserves Preview/pacing")) return false;
        control.failure = Rendering::Detail::CompositionFailurePoint::None;
        Access::native(window, true);
        Access::native(window, false);
        if (!require(control.attempts == 1 && !Access::desired(window),
            "native events cannot retry failed initial graphics attachment")) return false;
        if (!require(Access::clickDeploymentButton(window) && control.attempts == 2 && Access::desktop(window).isValid()
            && Access::desired(window) && Access::button(window, "Undeploy")
            && backend(window).presentationTargetCountForTesting() == 2
            && backend(window).pacingPresentationTargetForTesting() == Access::desktop(window),
            "next explicit Deploy succeeds after attach failure")) return false;
    }
    return true;
}

bool samePixelsFromOneTick(MainWindow& window, std::uint32_t expected)
{
    auto& gpu = backend(window);
    const auto preview = Access::preview(window), desktop = Access::desktop(window);
    const auto beforePreview = gpu.presentationTargetDiagnosticsForTesting(preview).drawCount;
    const auto beforeDesktop = gpu.presentationTargetDiagnosticsForTesting(desktop).drawCount;
    const auto uploads = gpu.mediaUploadCountForTesting();
    if (!gpu.captureNextPresentationPixelForTesting(preview)
        || !gpu.captureNextPresentationPixelForTesting(desktop)
        || !gpu.forceNextDxgiPresentResultForTesting(preview, static_cast<std::int32_t>(S_OK))
        || !gpu.forceNextDxgiPresentResultForTesting(desktop, static_cast<std::int32_t>(S_OK))) return false;
    Access::tick(window);
    const auto first = gpu.presentationTargetDiagnosticsForTesting(preview);
    const auto second = gpu.presentationTargetDiagnosticsForTesting(desktop);
    return require(first.drawCount == beforePreview + 1 && second.drawCount == beforeDesktop + 1
        && first.capturedPixelValid && second.capturedPixelValid
        && first.capturedPixelRgba == expected && second.capturedPixelRgba == expected
        && gpu.mediaUploadCountForTesting() == uploads && first.lastPresentSyncInterval == 0
        && second.lastPresentSyncInterval == 1 && second.lastPresentOrder == first.lastPresentOrder + 1,
        "one MainWindow tick draws once per target from same media upload, Desktop paces last");
}

bool verifyEngineGraphicsRecoveryCase(
    Player::ApplicationTranslationState& translation,
    bool recoveryMediaFailure)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(control.shell.valid && initializeDeployed(window),
            "Engine recovery fixture deploys Preview and Desktop"))
        return false;

    QTemporaryDir mediaDirectory{
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/graphics-recovery-XXXXXX")};
    const auto path = mediaDirectory.filePath("graphics-recovery.bmp");
    auto& model = Access::model(window);
    const auto playlist = model.addPlaylist();
    if (!require(mediaDirectory.isValid(),
            "Engine recovery temporary directory")
        || !require(writeRecoveryBmp(path),
            "Engine recovery image fixture")
        || !require([&]
            {
                const auto item = model.addItem(
                    playlist, Player::WorkspaceMediaType::Image, path);
                return item && model.setImageDisplayDuration(*item, 30)
                    && Access::start(window, playlist, *item);
            }(), "Engine recovery starts production image playback"))
        return false;

    Access::pause(window);
    Access::seek(window, 400);
    auto& engine = Access::engine(window);
    auto& gpu = backend(window);
    const auto preview = Access::preview(window);
    const auto desktop = Access::desktop(window);
    const auto oldGeneration = gpu.presentationTargetInfo(preview).deviceGeneration;
    const auto oldUploads = gpu.mediaUploadCountForTesting();
    const float playbackTime = engine.playbackTime();
    const void* const playbackIdentity = Gpu::playback(engine);
    const void* const mediaIdentity = Gpu::media(engine);
    const void* const sceneIdentity = Gpu::scene(engine);
    const void* const audioMixerIdentity = Gpu::audioMixer(engine);
    const void* const audioOutputIdentity = Gpu::audioOutput(engine);
    const void* const backgroundMusicRuntimeIdentity =
        Gpu::backgroundMusicRuntime(engine);
    const void* const backgroundMusicExecutorIdentity =
        Gpu::backgroundMusicExecutor(engine);
    const void* const projectIdentity = Access::projection(window);
    const auto canvas = model.desktopCanvas().aspectRatio;
    const auto displayId = Access::bound(window);
    engine.setPlaybackSpeed(1.25F);
    engine.setVolume(0.37F);
    if (!engine.isMuted())
        engine.toggleMute();
    if (!require(engine.isPaused() && playbackTime > 0.0F
            && gpu.forceNextDxgiPresentResultForTesting(
                preview, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
            && gpu.forceNextDxgiPresentResultForTesting(
                desktop, static_cast<std::int32_t>(S_OK)),
            "Engine recovery loss preconditions"))
        return false;

    Access::tick(window);
    if (!require(!gpu.hasCurrentGraphicsGenerationForTesting()
            && gpu.pendingGraphicsRecoveryForTesting()
            && gpu.graphicsRecoveryAttemptCountForTesting() == 0U
            && gpu.presentationTargetCountForTesting() == 2U
            && gpu.preferredPacingTargetForTesting() == desktop
            && engine.isPaused() && engine.playbackTime() == playbackTime,
            "detecting tick records loss without reentrant recovery or playback change"))
        return false;

    if (recoveryMediaFailure)
    {
        gpu.forceNextMediaUpdateResultForTesting(
            static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK));
    }
    Access::tick(window);
    const auto replacementGeneration = gpu.presentationTargetInfo(preview).deviceGeneration;
    if (!require(gpu.hasCurrentGraphicsGenerationForTesting()
            && !gpu.pendingGraphicsRecoveryForTesting()
            && gpu.graphicsRecoveryAttemptCountForTesting() == 1U
            && gpu.graphicsGenerationCreationCountForTesting() == 2U
            && replacementGeneration > oldGeneration
            && gpu.presentationTargetInfo(desktop).deviceGeneration == replacementGeneration
            && gpu.hasCurrentMediaRealizationForTesting() == !recoveryMediaFailure
            && gpu.mediaUploadCountForTesting()
                == oldUploads + (recoveryMediaFailure ? 0U : 1U)
            && Gpu::playback(engine) == playbackIdentity
            && Gpu::media(engine) == mediaIdentity
            && Gpu::scene(engine) == sceneIdentity
            && Gpu::audioMixer(engine) == audioMixerIdentity
            && Gpu::audioOutput(engine) == audioOutputIdentity
            && Gpu::backgroundMusicRuntime(engine)
                == backgroundMusicRuntimeIdentity
            && Gpu::backgroundMusicExecutor(engine)
                == backgroundMusicExecutorIdentity
            && Access::projection(window) == projectIdentity
            && model.desktopCanvas().aspectRatio == canvas
            && Access::bound(window) == displayId
            && engine.isPaused() && engine.playbackTime() == playbackTime
            && engine.playbackSpeed() == 1.25F
            && engine.volume() == 0.37F && engine.isMuted()
            && Access::desired(window),
            recoveryMediaFailure
                ? "recovery-time ResourceFailure publishes without media or playback rollback"
                : "next safe Engine render performs one media-aware generation replacement"))
        return false;

    Access::tick(window);
    const bool stable = require(gpu.graphicsRecoveryAttemptCountForTesting() == 1U
            && gpu.graphicsGenerationCreationCountForTesting() == 2U
            && engine.playbackTime() == playbackTime,
            "later Engine ticks do not repeat successful recovery");
    Access::shutdown(window);
    return stable;
}

bool verifyEngineGraphicsRecoveryOrchestration(
    Player::ApplicationTranslationState& translation)
{
    return verifyEngineGraphicsRecoveryCase(translation, false)
        && verifyEngineGraphicsRecoveryCase(translation, true);
}

bool verifyEngineGraphicsUnavailableSteadyState(
    Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(control.shell.valid && initializeDeployed(window),
            "GraphicsUnavailable fixture deploys Preview and Desktop"))
        return false;

    QTemporaryDir mediaDirectory{
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/graphics-unavailable-XXXXXX")};
    const auto path = mediaDirectory.filePath("graphics-unavailable.bmp");
    auto& model = Access::model(window);
    const auto playlist = model.addPlaylist();
    if (!require(mediaDirectory.isValid()
            && writeRecoveryBmp(path),
            "GraphicsUnavailable image fixture")
        || !require([&]
            {
                const auto item = model.addItem(
                    playlist, Player::WorkspaceMediaType::Image, path);
                return item && model.setImageDisplayDuration(*item, 30)
                    && Access::start(window, playlist, *item);
            }(), "GraphicsUnavailable starts production image playback"))
        return false;

    Access::pause(window);
    Access::seek(window, 400);
    auto& engine = Access::engine(window);
    auto& gpu = backend(window);
    const auto preview = Access::preview(window);
    const auto originalDesktop = Access::desktop(window);
    const float pausedTime = engine.playbackTime();
    const void* const playbackIdentity = Gpu::playback(engine);
    const void* const mediaIdentity = Gpu::media(engine);
    const void* const sceneIdentity = Gpu::scene(engine);
    if (!require(gpu.forceNextDxgiPresentResultForTesting(
                preview, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
            && gpu.forceNextDxgiPresentResultForTesting(
                originalDesktop, static_cast<std::int32_t>(S_OK)),
            "GraphicsUnavailable production loss preconditions"))
        return false;
    Access::tick(window);
    gpu.failNextGraphicsGenerationCreationForTesting();
    Access::tick(window);
    if (!require(!gpu.hasCurrentGraphicsGenerationForTesting()
            && !gpu.pendingGraphicsRecoveryForTesting()
            && gpu.graphicsRecoveryAttemptCountForTesting() == 1U
            && gpu.graphicsGenerationCreationCountForTesting() == 1U
            && gpu.presentationTargetCountForTesting() == 2U
            && Access::desired(window)
            && engine.isPaused() && engine.playbackTime() == pausedTime,
            "failed sole recovery attempt preserves the logical deployed session"))
        return false;

    Access::pause(window);
    for (int index = 0; index < 3; ++index)
    {
        engine.update(0.05F);
        engine.render(model.desktopCanvas().aspectRatio);
    }
    if (!require(!engine.isPaused() && engine.playbackTime() > pausedTime
            && Gpu::playback(engine) == playbackIdentity
            && Gpu::media(engine) == mediaIdentity
            && Gpu::scene(engine) == sceneIdentity
            && gpu.graphicsRecoveryAttemptCountForTesting() == 1U
            && gpu.graphicsGenerationCreationCountForTesting() == 1U
            && !gpu.hasCurrentGraphicsGenerationForTesting()
            && !gpu.pendingGraphicsRecoveryForTesting(),
            "logical playback advances while repeated render opportunities do not retry"))
        return false;

    if (!require(Access::clickDeploymentButton(window)
            && !Access::desired(window) && !Access::desktop(window).isValid()
            && gpu.presentationTargetCountForTesting() == 1U,
            "Undeploy removes only the logical Desktop endpoint while unavailable"))
        return false;
    if (!require(Access::clickDeploymentButton(window),
            "Deploy remains a valid product action while unavailable"))
        return false;
    const auto replacementDesktop = Access::desktop(window);
    if (!require(Access::desired(window) && replacementDesktop.isValid()
            && replacementDesktop != originalDesktop
            && gpu.presentationTargetCountForTesting() == 2U
            && gpu.preferredPacingTargetForTesting() == replacementDesktop
            && gpu.graphicsRecoveryAttemptCountForTesting() == 1U
            && gpu.graphicsGenerationCreationCountForTesting() == 1U
            && !gpu.hasCurrentGraphicsGenerationForTesting()
            && !gpu.pendingGraphicsRecoveryForTesting(),
            "Deploy stores a fresh stable logical Desktop ID without authorizing recovery"))
        return false;

    const bool clean = require(Access::clickDeploymentButton(window)
            && !Access::desired(window) && !Access::desktop(window).isValid()
            && gpu.presentationTargetCountForTesting() == 1U,
        "final unavailable Undeploy detaches before destroying the host");
    Access::shutdown(window);
    return clean;
}

bool verifyDeviceLossLifecycleBoundaries(
    Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(initialize(window),
            "device-loss lifecycle initializes Preview-only"))
        return false;

    auto& gpu = backend(window);
    const auto preview = Access::preview(window);
    const auto expectedDesktop = gpu.nextPresentationTargetIdForTesting();
    if (!require(expectedDesktop.isValid()
            && gpu.forceNextTargetRealizationResultForTesting(
                expectedDesktop, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED)),
            "control DeviceLost at the next stable Desktop realization ID"))
        return false;
    if (!require(Access::clickDeploymentButton(window)
            && Access::desired(window)
            && Access::desktop(window) == expectedDesktop
            && Access::host(window).isValid()
            && gpu.presentationTargetCountForTesting() == 2U
            && gpu.preferredPacingTargetForTesting() == expectedDesktop
            && !gpu.hasCurrentGraphicsGenerationForTesting()
            && gpu.pendingGraphicsRecoveryForTesting()
            && gpu.graphicsRecoveryAttemptCountForTesting() == 0U,
            "DeviceLost during Deploy preserves host, intent, descriptor, ID, and pending edge"))
        return false;

    Access::tick(window);
    const auto recoveredGeneration = gpu.presentationTargetInfo(preview).deviceGeneration;
    if (!require(gpu.hasCurrentGraphicsGenerationForTesting()
            && !gpu.pendingGraphicsRecoveryForTesting()
            && gpu.graphicsRecoveryAttemptCountForTesting() == 1U
            && gpu.graphicsGenerationCreationCountForTesting() == 2U
            && recoveredGeneration != 0U
            && gpu.presentationTargetInfo(expectedDesktop).deviceGeneration
                == recoveredGeneration
            && !gpu.presentationTargetInfo(preview).failed
            && !gpu.presentationTargetInfo(expectedDesktop).failed,
            "next safe tick reconstructs Preview and Desktop in one shared generation"))
        return false;

    if (!require(gpu.forceDeviceLostOnTargetDetachForTesting(expectedDesktop)
            && Access::clickDeploymentButton(window)
            && !Access::desired(window)
            && !Access::desktop(window).isValid()
            && !Access::host(window).isValid()
            && !gpu.hasCurrentGraphicsGenerationForTesting()
            && gpu.presentationTargetCountForTesting() == 1U
            && gpu.pendingGraphicsRecoveryForTesting(),
            "DeviceLost during Undeploy detaches Desktop before host destruction and records one edge"))
        return false;

    Access::tick(window);
    if (!require(gpu.hasCurrentGraphicsGenerationForTesting()
            && !gpu.pendingGraphicsRecoveryForTesting()
            && gpu.graphicsRecoveryAttemptCountForTesting() == 2U
            && gpu.graphicsGenerationCreationCountForTesting() == 3U
            && gpu.presentationTargetCountForTesting() == 1U,
            "post-Undeploy recovery reconstructs only the remaining Preview descriptor"))
        return false;

    if (!require(gpu.forceNextDxgiPresentResultForTesting(
                preview, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED)),
            "control DeviceLost immediately before shutdown"))
        return false;
    Access::tick(window);
    const auto attemptsBeforeShutdown =
        gpu.graphicsRecoveryAttemptCountForTesting();
    if (!require(!gpu.hasCurrentGraphicsGenerationForTesting()
            && gpu.pendingGraphicsRecoveryForTesting(),
            "near-shutdown loss is pending before the structural shutdown fence"))
        return false;
    Access::shutdown(window);
    return require(!Access::engine(window).isInitialized()
            && !gpu.hasCurrentGraphicsGenerationForTesting()
            && !gpu.pendingGraphicsRecoveryForTesting()
            && gpu.presentationTargetCountForTesting() == 0U
            && gpu.graphicsRecoveryAttemptCountForTesting()
                == attemptsBeforeShutdown,
        "shutdown clears the edge and descriptors without resurrecting graphics");
}

bool verifyReplacementAndState(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    control.reenter = true;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!require(control.shell.valid && initializeDeployed(window) && control.attempts == 1 && control.exactBinding
        && Access::desktop(window).isValid() && backend(window).presentationTargetCountForTesting() == 2,
        "explicit Deploy real native host + Engine attachment; recursive native event bounded")) return false;
    auto& engine = Access::engine(window);
    auto& gpu = backend(window);
    const auto* engineIdentity = &engine;
    QTemporaryDir mediaDirectory;
    QImage image(64, 40, QImage::Format_RGBA8888);
    image.fill(QColor{13, 219, 13});
    const auto greenPath = mediaDirectory.filePath("green.png");
    if (!require(mediaDirectory.isValid() && image.save(greenPath), "temporary authored image fixture")) return false;
    image.fill(QColor{13, 219, 219});
    const auto cyanPath = mediaDirectory.filePath("cyan.png");
    if (!require(image.save(cyanPath), "second authored image fixture")) return false;
    auto& model = Access::model(window);
    const auto playlist = model.addPlaylist();
    const auto green = model.addItem(playlist, Player::WorkspaceMediaType::Image, greenPath);
    const auto cyan = model.addItem(playlist, Player::WorkspaceMediaType::Image, cyanPath);
    if (!require(green && cyan && model.setImageDisplayDuration(*green, 30)
        && Access::start(window, playlist, *green), "production workspace/Engine image playback")) return false;
    Access::pause(window);
    Access::seek(window, 400);
    if (!require(engine.isPaused() && engine.playbackTime() > 0 && samePixelsFromOneTick(window, 0xFF0DDB0D),
        "production pause/seek shared by both outputs")) return false;
    const auto* sceneIdentity = Gpu::scene(engine);
    const auto* playbackIdentity = Gpu::playback(engine);
    const auto* mediaIdentity = Gpu::media(engine);
    const auto* projectIdentity = Access::projection(window);
    const auto revision = model.authoredRevision();
    const auto time = engine.playbackTime();
    const auto generation = gpu.presentationTargetInfo(Access::preview(window)).deviceGeneration;
    const auto uploads = gpu.mediaUploadCountForTesting();
    const auto canvas = model.desktopCanvas().aspectRatio;
    const auto firstTarget = Access::desktop(window);
    const auto statePreserved = [&]
    {
        return &Access::engine(window) == engineIdentity && Gpu::scene(engine) == sceneIdentity
            && Gpu::playback(engine) == playbackIdentity && Gpu::media(engine) == mediaIdentity
            && Access::projection(window) == projectIdentity && model.authoredRevision() == revision
            && model.desktopCanvas().aspectRatio == canvas && model.activeItemId() == green
            && Access::bound(window) == control.shell.display.id && engine.isPaused()
            && engine.playbackTime() == time && gpu.mediaUploadCountForTesting() == uploads
            && gpu.presentationTargetInfo(Access::preview(window)).deviceGeneration == generation;
    };
    {
        DestroyObservation cancellation(window, false);
        if (!require(Access::clickDeploymentButton(window) && cancellation.destroyed && cancellation.detachedFirst
            && !Access::desired(window) && !Access::desktop(window).isValid()
            && Access::host(window).nativeHandle() == nullptr && Access::button(window, "Deploy")
            && statePreserved() && gpu.presentationTargetCountForTesting() == 1
            && gpu.pacingPresentationTargetForTesting() == Access::preview(window)
            && gpu.captureNextPresentationPixelForTesting(Access::preview(window)),
            "Undeploy removes only presentation endpoint, preserves paused project/Canvas/Runtime/media/device")) return false;
        Access::tick(window);
        const auto preview = gpu.presentationTargetDiagnosticsForTesting(Access::preview(window));
        if (!require(preview.capturedPixelValid && preview.capturedPixelRgba == 0xFF0DDB0D
            && preview.lastPresentSyncInterval == 1 && statePreserved(),
            "same paused media remains visible in healthy Preview with pacing fallback after Undeploy")) return false;
    }
    Access::native(window, true);
    Access::native(window, false);
    if (!require(control.attempts == 1 && Access::clickDeploymentButton(window)
        && control.attempts == 2 && Access::desktop(window).isValid()
        && Access::desktop(window) != firstTarget && Access::button(window, "Undeploy")
        && gpu.presentationTargetCountForTesting() == 2 && statePreserved()
        && samePixelsFromOneTick(window, 0xFF0DDB0D),
        "redeployment restores fresh Desktop target without playback reset or duplicate layer")) return false;
    const auto oldTarget = Access::desktop(window);
    const auto oldWindow = static_cast<HWND>(Access::host(window).nativeHandle());
    const auto attemptsBeforeFalsePositive = control.attempts;
    Access::native(window, true);
    if (!require(control.attempts == attemptsBeforeFalsePositive && control.exactBinding
        && Access::desktop(window) == oldTarget && Access::desktop(window).isValid()
        && Access::host(window).nativeHandle() == oldWindow && IsWindow(oldWindow)
        && Access::host(window).isValid() && &Access::engine(window) == engineIdentity
        && Gpu::scene(engine) == sceneIdentity && Gpu::playback(engine) == playbackIdentity
        && Gpu::media(engine) == mediaIdentity && Access::projection(window) == projectIdentity
        && model.authoredRevision() == revision && engine.isPaused() && engine.playbackTime() == time
        && gpu.presentationTargetInfo(Access::preview(window)).deviceGeneration == generation
        && gpu.mediaUploadCountForTesting() == uploads && samePixelsFromOneTick(window, 0xFF0DDB0D),
        "false-positive TaskbarCreated preserves endpoint and paused Runtime/project/media/device")) return false;

    control.reenter = false;
    const auto beforeMissing = control.attempts;
    control.missing = true;
    control.exposeUnrelatedDisplayWhenMissing = true;
    control.unrelatedDisplay.id = DisplayId::fromTargetDevicePath("unrelated-primary-display");
    control.unrelatedDisplay.pixelWidth = 1920;
    control.unrelatedDisplay.pixelHeight = 1080;
    control.unrelatedDisplay.primary = true;
    Access::native(window, false);
    if (!require(control.attempts == beforeMissing + 1 && !Access::desktop(window).isValid()
        && !Access::host(window).isValid() && Access::desired(window) && Access::button(window, "Undeploy")
        && Access::bound(window) == control.shell.display.id
        && gpu.presentationTargetCountForTesting() == 1
        && gpu.pacingPresentationTargetForTesting() == Access::preview(window)
        && Access::continuationArmed(window) && previewHealthy(window),
        "display disappearance removes Desktop, no rebind/fallback, Preview becomes pacer")) return false;
    if (!require(Access::fireContinuation(window) && control.attempts == beforeMissing + 2
            && !Access::continuationArmed(window),
        "display disappearance consumes its only finite continuation")) return false;
    control.missing = false;
    Access::native(window, false);
    if (!require(control.attempts == beforeMissing + 3 && control.exactBinding
        && Access::desktop(window).isValid() && Access::desktop(window) != oldTarget
        && samePixelsFromOneTick(window, 0xFF0DDB0D), "later display event restores same bound identity/static state")) return false;
    // External owned-HWND destruction directly authorizes queued reconciliation.
    const auto externallyGone = Access::desktop(window);
    DestroyWindow(static_cast<HWND>(Access::host(window).nativeHandle()));
    if (!require(!Access::host(window).isValid() && gpu.presentationTargetCountForTesting() == 2,
        "external HWND destruction does not magically detach graphics")) return false;
    QCoreApplication::processEvents();
    if (!require(Access::desktop(window).isValid() && Access::desktop(window) != externallyGone
        && gpu.presentationTargetCountForTesting() == 2 && samePixelsFromOneTick(window, 0xFF0DDB0D),
        "event removes stale target and replaces already-invalid host")) return false;
    Access::pause(window);
    if (!require(!engine.isPaused() && samePixelsFromOneTick(window, 0xFF0DDB0D), "production resume drives same outputs")) return false;
    Access::next(window);
    if (!require(model.activeItemId() == cyan && samePixelsFromOneTick(window, 0xFFDBDB0D),
        "production scene/media navigation changes both outputs")) return false;

    DestroyObservation shutdown(window, true);
    const auto attempts = control.attempts;
    Access::sendNative(window, true); // A queued pre-shutdown event must also be ignored later.
    Access::shutdown(window);
    Access::native(window, false);
    Access::native(window, true);
    return require(shutdown.installed && shutdown.destroyed && shutdown.detachedFirst && shutdown.stoppedFirst
        && control.attempts == attempts && !engine.isInitialized() && !Access::desktop(window).isValid()
        && !Access::host(window).isValid() && control.shell.intact(), "shutdown stops submission/detaches before destroy; events ignored");
}

bool verifyInvalidShutdownAndDestructor(Player::ApplicationTranslationState& translation)
{
    {
        CreationControl control;
        MainWindow window{control.shell.display.id, translation};
        control.install(window);
        if (!initializeDeployed(window)) return false;
        DestroyWindow(static_cast<HWND>(Access::host(window).nativeHandle()));
        const auto attempts = control.attempts;
        Access::shutdown(window);
        Access::shutdown(window);
        Access::native(window, true);
        if (!require(control.attempts == attempts && !Access::host(window).isValid()
            && !Access::desktop(window).isValid() && control.shell.intact(), "already-invalid host shutdown idempotent")) return false;
    }
    CreationControl control;
    auto window = std::make_unique<MainWindow>(control.shell.display.id, translation);
    control.install(*window);
    if (!initializeDeployed(*window)) return false;
    DestroyObservation destructor(*window, true);
    window.reset();
    return require(destructor.destroyed && destructor.detachedFirst && destructor.stoppedFirst
        && control.shell.intact(), "actual MainWindow destructor obeys native/graphics ownership order");
}

bool verifyDeferredNativeEntry(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    const auto target = Access::desktop(window);
    const auto host = Access::host(window).nativeHandle();
    Access::sendNative(window, true);
    Access::sendNative(window, true);
    Access::sendNative(window, false);
    if (!require(control.attempts == 1 && Access::desktop(window) == target,
        "native notification must defer graphics mutation until the Qt control queue")) return false;
    QCoreApplication::processEvents();
    return require(control.attempts == 1 && Access::desktop(window) == target
        && Access::host(window).nativeHandle() == host,
        "coalesced false-positive native events preserve the valid Desktop endpoint");
}

bool verifyExternalHostDestructionSignal(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    const auto target = Access::desktop(window);
    const auto attempts = control.attempts;
    const auto preview = Access::preview(window);
    const auto previewPresents = backend(window)
        .presentationTargetDiagnosticsForTesting(preview).presentAttemptCount;
    if (!require(DestroyWindow(static_cast<HWND>(Access::host(window).nativeHandle())) != FALSE,
        "external host destruction fixture")) return false;
    Access::tick(window);
    if (!require(backend(window).presentationTargetDiagnosticsForTesting(preview).presentAttemptCount
            == previewPresents && backend(window).presentationTargetCountForTesting() == 2,
        "known stale Desktop HWND fences shared render before queued detach")) return false;
    QCoreApplication::processEvents();
    return require(control.attempts == attempts + 1 && Access::desktop(window).isValid()
        && Access::desktop(window) != target && Access::host(window).isValid(),
        "external WM_NCDESTROY signal reconciles a fresh Desktop endpoint without polling");
}

bool verifyExternalHostDetachFailure(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    const auto target = Access::desktop(window);
    const auto preview = Access::preview(window);
    if (!require(DestroyWindow(static_cast<HWND>(Access::host(window).nativeHandle())) != FALSE,
        "external destruction before deterministic detach rejection")) return false;
    const auto presents = backend(window)
        .presentationTargetDiagnosticsForTesting(preview).presentAttemptCount;
    {
        QScopedValueRollback<bool> rejection(Gpu::initialized(Access::engine(window)), false);
        QCoreApplication::processEvents();
        Access::tick(window);
        if (!require(Access::desktop(window) == target && !Access::host(window).isValid()
                && backend(window).presentationTargetCountForTesting() == 2
                && backend(window).presentationTargetDiagnosticsForTesting(preview).presentAttemptCount
                    == presents
                && control.attempts == 1 && !Access::continuationArmed(window),
            "detach failure after host loss retains stale ID, fences render, and publishes no replacement"))
            return false;
    }
    Access::native(window, true);
    return require(Access::desktop(window).isValid() && Access::desktop(window) != target
            && Access::host(window).isValid() && control.attempts == 2,
        "later external authorization can clean retained stale state and recover once");
}

bool verifyBoundedContinuation(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    const auto firstTarget = Access::desktop(window);

    control.missing = true;
    Access::native(window, false);
    if (!require(control.attempts == 2 && Access::desired(window)
            && !Access::desktop(window).isValid() && !Access::host(window).isValid()
            && Access::continuationArmed(window) && Access::button(window, "Undeploy")
            && Access::bound(window) == control.shell.display.id,
        "transient display loss preserves intent and arms one continuation")) return false;
    if (!require(Access::fireContinuation(window) && control.attempts == 3
            && !Access::continuationArmed(window),
        "one transient continuation is consumed and cannot rearm itself")) return false;
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    if (!require(control.attempts == 3 && !Access::continuationArmed(window),
        "continuation exhaustion leaves no hidden retry")) return false;

    control.missing = false;
    Access::native(window, false);
    if (!require(control.attempts == 4 && Access::desktop(window).isValid()
            && Access::desktop(window) != firstTarget && !Access::continuationArmed(window),
        "new display event restores the exact bound display with fresh target identity")) return false;

    control.missing = true;
    Access::native(window, false);
    if (!require(control.attempts == 5 && Access::continuationArmed(window),
        "second external cycle may independently arm one continuation")) return false;
    Access::sendNative(window, true);
    Access::sendNative(window, false);
    const auto attemptsBeforeCollision = control.attempts;
    Access::invokeContinuation(window);
    if (!require(!Access::continuationArmed(window) && Access::reconciliationQueued(window),
        "newer native notification cancels the older continuation and coalesces")
        || !require(control.attempts == attemptsBeforeCollision,
        "queued timer callback colliding with an external pass creates no duplicate work")) return false;
    control.missing = false;
    const auto attemptsBeforeSupersedingPass = control.attempts;
    QCoreApplication::processEvents();
    if (!require(control.attempts == attemptsBeforeSupersedingPass + 1
            && Access::desktop(window).isValid() && !Access::continuationArmed(window)
            && !Access::fireContinuation(window),
        "superseding external pass performs one attempt; canceled timer cannot duplicate it")) return false;

    control.missing = true;
    control.reenterReconciliation = true;
    const auto attemptsBeforeReentrantPass = control.attempts;
    if (!require(!Access::reconcile(window, true)
            && Access::reconciliationQueued(window) && !Access::continuationArmed(window)
            && control.attempts == attemptsBeforeReentrantPass + 1,
        "current transient pass cannot arm a timer when a newer native pass is pending")) return false;
    QCoreApplication::processEvents();
    if (!require(control.attempts == attemptsBeforeReentrantPass + 2
            && Access::continuationArmed(window),
        "notification during reconciliation authorizes at most one later external pass")) return false;
    return require(Access::fireContinuation(window)
            && control.attempts == attemptsBeforeReentrantPass + 3
            && !Access::continuationArmed(window),
        "later pass owns one finite continuation and exhaustion remains bounded");
}

bool verifyTerminalRecoveryOutcomes(Player::ApplicationTranslationState& translation)
{
    for (const auto outcome : {
            DesktopPresentationHostAttachmentStatus::StructurallyUnsupported,
            DesktopPresentationHostAttachmentStatus::NativeFailure})
    {
        CreationControl control;
        MainWindow window{control.shell.display.id, translation};
        control.install(window);
        if (!initializeDeployed(window)) return false;
        control.forcedReconciliationStatus = outcome;
        control.forcedStatus = outcome == DesktopPresentationHostAttachmentStatus::StructurallyUnsupported
            ? DesktopPresentationHostStatus::UnsupportedShellTopology
            : DesktopPresentationHostStatus::NativeFailure;
        Access::native(window, true);
        if (!require(control.attempts == 2 && Access::desired(window)
                && !Access::desktop(window).isValid() && !Access::host(window).isValid()
                && !Access::continuationArmed(window),
            "unsupported/local-native recovery outcome publishes nothing and cannot self-retry")) return false;
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        if (!require(control.attempts == 2,
            "terminal recovery outcome has no hidden retry")) return false;
    }
    return true;
}

bool verifyGeometryAndTopologyGeneration(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    auto& gpu = backend(window);
    const auto originalTarget = Access::desktop(window);
    const auto originalHost = Access::host(window).nativeHandle();
    const auto generation = gpu.presentationTargetInfo(originalTarget).deviceGeneration;
    control.shell.display.desktopX += 31;
    control.shell.display.desktopY += 17;
    control.shell.display.pixelWidth += 43;
    control.shell.display.pixelHeight += 29;
    Access::native(window, false);
    RECT repaired{};
    GetWindowRect(static_cast<HWND>(originalHost), &repaired);
    if (!require(control.attempts == 1 && Access::desktop(window) == originalTarget
            && Access::host(window).nativeHandle() == originalHost
            && gpu.presentationTargetInfo(originalTarget).deviceGeneration == generation
            && repaired.left == control.shell.display.desktopX
            && repaired.top == control.shell.display.desktopY
            && repaired.right - repaired.left == control.shell.display.pixelWidth
            && repaired.bottom - repaired.top == control.shell.display.pixelHeight,
        "display geometry repair preserves host, target ID, and GraphicsGeneration")) return false;

    const HWND generationA = control.shell.root;
    const auto attempts = control.attempts;
    if (!require(control.shell.destroyGeneration() && !Access::host(window).isValid()
            && Access::desktop(window) == originalTarget,
        "generation A parent destruction destroys the real published host")) return false;
    Access::tick(window);
    if (!require(control.shell.createGeneration() && control.shell.root != generationA,
        "generation B is a distinct test-owned Shell topology")) return false;
    QCoreApplication::processEvents();
    return require(control.attempts == attempts + 1 && Access::desktop(window).isValid()
            && Access::desktop(window) != originalTarget
            && Access::host(window).nativeHandle() != originalHost
            && gpu.presentationTargetInfo(Access::desktop(window)).deviceGeneration == generation
            && gpu.presentationTargetCountForTesting() == 2,
        "generation A to B detaches stale logical target and publishes one fresh identity");
}

bool verifyCancellationAndShutdownAuthority(Player::ApplicationTranslationState& translation)
{
    CreationControl control;
    MainWindow window{control.shell.display.id, translation};
    control.install(window);
    if (!initializeDeployed(window)) return false;
    control.missing = true;
    Access::native(window, false);
    if (!require(Access::continuationArmed(window) && Access::desired(window)
            && Access::button(window, "Undeploy"),
        "unrealized desired deployment remains explicitly cancelable")) return false;
    const auto attempts = control.attempts;
    if (!require(Access::clickDeploymentButton(window) && !Access::desired(window)
            && !Access::continuationArmed(window) && !Access::fireContinuation(window),
        "Undeploy revokes timer and all creation authority")) return false;
    Access::native(window, true);
    QCoreApplication::processEvents();
    if (!require(control.attempts == attempts && !Access::desktop(window).isValid(),
        "queued/native reconciliation with intent false performs no creation")) return false;

    control.missing = false;
    Access::native(window, false);
    if (!require(Access::clickDeploymentButton(window) && Access::desktop(window).isValid(),
        "fresh explicit Deploy remains available after cancellation")) return false;
    const auto attemptsBeforeShutdown = control.attempts;
    Access::sendNative(window, true);
    Access::shutdown(window);
    QCoreApplication::processEvents();
    return require(!Access::engine(window).isInitialized() && !Access::desired(window)
            && !Access::desktop(window).isValid() && !Access::host(window).isValid()
            && !Access::continuationArmed(window)
            && control.attempts == attemptsBeforeShutdown,
        "shutdown cancels queued work and host callbacks cannot resurrect deployment");
}

bool verifyRealStartup(Player::ApplicationTranslationState& translation)
{
    const auto current = DesktopSceneEngine::Platform::enumerateActiveDisplays();
    if (current.status != DesktopSceneEngine::Platform::DisplayEnumerationStatus::Success
        || current.snapshot.displays.empty()) return false;
    for (const auto& display : current.snapshot.displays)
    {
        MainWindow window{display.id, translation};
        const auto aspect = Domain::AspectRatio::from(display.pixelWidth, display.pixelHeight);
        if (!require(aspect && Access::model(window).desktopCanvas().aspectRatio == *aspect
            && Access::canvasUi(window, *aspect) && initialize(window)
            && backend(window).presentationTargetCountForTesting() == 1
            && !Access::desktop(window).isValid() && !Access::host(window).isValid()
            && backend(window).pacingPresentationTargetForTesting() == Access::preview(window),
            "real fixed-session startup seeds exact native Canvas and remains Preview-only")) return false;
        std::cout << "Real startup " << display.pixelWidth << 'x' << display.pixelHeight
            << " Canvas=" << aspect->numerator << ':' << aspect->denominator << " Preview-only PASS\n";
    }
    return true;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    Player::ApplicationTranslationState translation;
    if (argc == 2 && std::string_view(argv[1]) == "--s5-close-peer")
    { application.setQuitOnLastWindowClosed(false); return runS4DeploymentPeer(translation, true); }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]).startsWith("--tray-close-peer-"))
    { application.setQuitOnLastWindowClosed(false); return verifyS5ClosePeer(translation, QString::fromLocal8Bit(argv[1]).mid(13)) ? 0 : 1; }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]).startsWith("--tray-close-"))
    {
        application.setQuitOnLastWindowClosed(false);
        return verifyTrayClose(translation, QString::fromLocal8Bit(argv[1]).mid(13)) ? 0 : 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--s4-deployment-peer")
    { application.setQuitOnLastWindowClosed(false); return runS4DeploymentPeer(translation); }
    if (argc == 2 && std::string_view(argv[1]) == "--tray-deployment-peer")
    { application.setQuitOnLastWindowClosed(false); return verifyS4DeploymentPeer(translation) ? 0 : 1; }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]).startsWith("--tray-deployment-"))
    {
        application.setQuitOnLastWindowClosed(false);
        return verifyTrayDeploymentControl(translation, QString::fromLocal8Bit(argv[1]).mid(18)) ? 0 : 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--tray-deployed")
    {
        application.setQuitOnLastWindowClosed(false);
        return verifyTrayDeployment(translation) ? 0 : 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--live-deployed-scale")
        return verifyLiveDeployedScaleUpdate(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--live-deployed-position")
        return verifyLiveDeployedPositionUpdate(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--deploy-label")
        return verifyReversibleDeployment(translation, true) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--undeploy-lifecycle")
        return verifyReversibleDeployment(translation, false) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--failed-deploy-intent")
        return verifyFailedDeployIntent(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--detach-rejection")
        return verifyDetachRejection(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--deployment-translation")
        return verifyDeploymentTranslation(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--canvas-default")
        return verifyDefaultCanvas(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--deployment-semantics")
        return verifyDeploymentSemantics(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--graphics-recovery")
        return verifyEngineGraphicsRecoveryOrchestration(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--graphics-unavailable")
        return verifyEngineGraphicsUnavailableSteadyState(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--graphics-lifecycle")
        return verifyDeviceLossLifecycleBoundaries(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--real-startup")
        return verifyRealStartup(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--refresh-ownership")
        return verifyMainWindowRefreshOwnership(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--shell-reconciliation")
        return verifyDeferredNativeEntry(translation)
            && verifyExternalHostDestructionSignal(translation)
            && verifyExternalHostDetachFailure(translation)
            && verifyBoundedContinuation(translation)
            && verifyTerminalRecoveryOutcomes(translation)
            && verifyGeometryAndTopologyGeneration(translation)
            && verifyCancellationAndShutdownAuthority(translation) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--replacement-state")
        return verifyReplacementAndState(translation) ? 0 : 1;
    const bool passed = verifyLiveDeployedScaleUpdate(translation)
        && verifyLiveDeployedPositionUpdate(translation)
        && verifyCanvasOutsideSessionLifecycle(translation)
        && verifyMainWindowRefreshOwnership(translation)
        && verifyDefaultCanvas(translation) && verifyDeploymentSemantics(translation)
        && verifyEngineGraphicsRecoveryOrchestration(translation)
        && verifyEngineGraphicsUnavailableSteadyState(translation)
        && verifyDeviceLossLifecycleBoundaries(translation)
        && verifyReversibleDeployment(translation, true) && verifyFailedDeployIntent(translation)
        && verifyDetachRejection(translation) && verifyDeploymentTranslation(translation)
        && verifyDeferredNativeEntry(translation) && verifyExternalHostDestructionSignal(translation)
        && verifyExternalHostDetachFailure(translation)
        && verifyBoundedContinuation(translation) && verifyTerminalRecoveryOutcomes(translation)
        && verifyGeometryAndTopologyGeneration(translation)
        && verifyCancellationAndShutdownAuthority(translation)
        && verifyHostFailures(translation) && verifyAttachFailures(translation)
        && verifyReplacementAndState(translation) && verifyInvalidShutdownAndDestructor(translation);
    if (passed) std::cout << "MainWindow Desktop lifecycle PASS: native Canvas defaults, project override/GPU, Preview-only startup, reversible Deploy/Undeploy/intent, detach rejection, translation, failures, one tick/pixels, pacing, preserved playback, replacement, fixed identity, shutdown.\n";
    return passed ? 0 : 1;
}
