#include "ApplicationCoordinator.h"
#include "ApplicationPreferences.h"
#include "LauncherWindow.h"
#include "MainWindow.h"
#include "CanvasChromeFrame.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/Renderer.h"

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QAbstractButton>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QtEndian>
#include <windows.h>
#include <tlhelp32.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

class ApplicationCoordinatorTestAccess
{
public:
    static QUuid epoch(const ApplicationCoordinator& c) { return c.epoch_; }
    static QUuid epoch(const SessionCoordinatorJoin& j) { return j.coordinatorEpoch_; }
    static QMenu& menu(ApplicationCoordinator& c) { c.updateTray(); return c.trayMenu_; }
    static void disconnect(SessionCoordinatorJoin& j) { j.socket_.abort(); }
    static void noBootstrap(SessionCoordinatorJoin& j) { j.bootstrapCoordinator_ = [] {
        std::cout << "BOOTSTRAP FAILURE" << std::endl; return false;
    }; }
    static bool retrying(const SessionCoordinatorJoin& j) { return j.retry_.isActive() || j.deadline_.isActive(); }
    static bool renewing(const SessionCoordinatorJoin& j) { return j.renewalRequest_ != 0; }
    static void renew(SessionCoordinatorJoin& j) { j.renewQualification(); }
    static bool deadHandleRetired(const SessionCoordinatorJoin& j) { return !j.coordinatorLifetime_; }
    static void stale(SessionCoordinatorJoin& j, const QUuid& old)
    {
        for (const char kind : QByteArray("QPS EUCKVFD").replace(" ", ""))
        {
            QByteArray frame(1, kind); frame += old.toRfc4122(); frame += j.incarnation().toRfc4122();
            const auto n = frame.size(); frame.resize(n + 8); qToBigEndian<quint64>(91, frame.data() + n);
            frame += char{0}; j.reply_ += frame; j.consumeControl();
        } // Deterministic delayed-frame injection only; actual replacement pipe/peer is independently verified.
        for (const char kind : QByteArray("VTFDBOJARH"))
        {
            QByteArray frame(1, kind); frame += old.toRfc4122(); frame += j.incarnation().toRfc4122();
            const auto n = frame.size(); frame.resize(n + 8); qToBigEndian<quint64>(91, frame.data() + n);
            frame += char{0}; j.socket_.write(frame);
        }
        j.renewQualification(); j.socket_.flush(); // A genuine new-generation round trip proves old reports did not poison the channel.
    }
    static void closeAll(ApplicationCoordinator& c) { c.closeAll(); }
    static bool batch(const ApplicationCoordinator& c) { return c.closeBatch_.has_value(); }
    static void trayIcon(ApplicationCoordinator& c, QIcon icon) { c.tray_.setIcon(std::move(icon)); c.updateTray(); }
    static void observe(ApplicationCoordinator& c, QByteArray& frames)
    {
        // Same non-consuming native-control observation as the S5 fixture.
        QObject::connect(&c.server_, &QLocalServer::newConnection, &c, [&c, &frames] {
            for (auto* socket : c.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
                if (!socket->property("s7PriorObserved").toBool()) {
                    socket->setProperty("s7PriorObserved", true);
                    QObject::connect(socket, &QLocalSocket::readyRead, &c, [socket, &frames] {
                        if (socket->property("sessionControl").toBool()) frames += socket->peek(socket->bytesAvailable());
                    }); // Before beginSessionControl installs its consumer; never manufactures a reply.
                }
        });
    }
    static void raw(ApplicationCoordinator& c, const QByteArray& frame)
    {
        for (auto* socket : c.findChildren<QLocalSocket*>())
            if (socket->property("sessionControl").toBool() && socket->state() == QLocalSocket::ConnectedState)
            { if (socket->write(frame) != frame.size()) throw std::runtime_error("S7 prior native frame delivery"); socket->flush(); return; }
        throw std::runtime_error("S7 prior current native control absent");
    }
    static HANDLE retainPipe(SessionCoordinatorJoin& j)
    {
        HANDLE copy{};
        if (!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(j.socket_.socketDescriptor()),
            GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) throw std::runtime_error("S7 retain original native pipe");
        return copy;
    }
    static void unqualify(SessionCoordinatorJoin& j) { j.revokeQualification(); }
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
    // Stable playback time must not disable production update/render/presentation.
    static void pause(MainWindow& w) { w.togglePlaybackPause(); }
    static bool timerActive(MainWindow& w) { return w.tickTimer_.isActive(); }
    static void observeTicks(MainWindow& w, quint64& ticks) {
        QObject::connect(&w.tickTimer_, &QTimer::timeout, &w, [&ticks] { ++ticks; });
    }
    static QWidget& surface(MainWindow& w) { return *w.canvasFrame_->canvasSurface(); }
    static QSplitter& splitter(MainWindow& w) { return *w.workspaceSplitter_; }
    static void playPause(MainWindow& w) { w.playPauseButton_->click(); }
};

namespace DesktopSceneEngine::Rendering
{
// Reuse the existing test-only Engine/Renderer access; no production API or instrumentation.
class CanvasTargetFitTestAccess
{
public:
    static GraphicsBackend& backend(Engine& engine) { return engine.renderer_->backend_; }
};
}

namespace
{
constexpr UINT HarnessMessage = WM_APP + 177;
void require(bool v, const char* why) { if (!v) throw std::runtime_error(why); }
bool wait(const std::function<bool()>& predicate, int ms = 11000)
{
    QElapsedTimer clock; clock.start();
    bool ready = predicate();
    while (!ready && clock.elapsed() < ms)
    { QApplication::processEvents(); QThread::msleep(5); ready = predicate(); }
    return ready; // Do not invoke an already-successful UIA action again after it hides its provider.
}
InstanceSlot launcherSlot() { return *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope()); }
DisplayId identity(const QString& name)
{ return DisplayId::parseCanonicalRepresentation(name.toStdString()).value_or(DisplayId::fromTargetDevicePath(name.toStdString())); }
InstanceSlot sessionSlot(const QString& name)
{ return *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{identity(name)}, *currentInteractiveInstanceScope()); }
void output(const QJsonObject& o) { std::cout << "STATE " << QJsonDocument(o).toJson(QJsonDocument::Compact).constData() << std::endl; }
QJsonObject closeState(const std::optional<RoleCloseOperation>& operation)
{
    return operation ? QJsonObject{{"generation", operation->key.generation.toString()},
        {"request", QString::number(operation->key.request)}, {"result", int(operation->result)}} : QJsonObject{};
}
QByteArray priorFrame(char kind, const RoleCloseKey& key, const QUuid& incarnation, char value = 0)
{
    QByteArray frame(1, kind); frame += key.generation.toRfc4122(); frame += incarnation.toRfc4122();
    const auto offset = frame.size(); frame.resize(offset + 8); qToBigEndian<quint64>(key.request, frame.data() + offset);
    frame += value; return frame;
}
COLORREF screenPixelAt(POINT point)
{
    COLORREF pixel = CLR_INVALID;
    if (HDC dc = GetDC(nullptr)) { pixel = GetPixel(dc, point.x, point.y); ReleaseDC(nullptr, dc); }
    return pixel;
}
class Harness final : public QAbstractNativeEventFilter
{
public:
    HWND hwnd{}; std::function<void(int)> command;
    bool nativeEventFilter(const QByteArray&, void* raw, qintptr* result) override
    {
        const auto& m = *static_cast<MSG*>(raw);
        if (m.hwnd != hwnd || m.message != HarnessMessage) return false;
        try { command(static_cast<int>(m.wParam)); }
        catch (const std::exception& e) { std::cerr << "HARNESS FAIL: " << e.what() << std::endl;
            SetPropW(hwnd, L"S7HarnessFailed", reinterpret_cast<HANDLE>(quintptr(1)));
            if (!qEnvironmentVariableIsSet("DSE_S7_HUMAN")) QCoreApplication::exit(1);
            else std::cerr << "Human-mode child diagnostic failure: scene retained.\n"; }
        if (result) *result = 0; return true;
    }
};
QString ownerTitle() { return "S7 owner " + qEnvironmentVariable("DSE_S7_NONCE"); }
HWND ownerWindow()
{
    struct Query { QString title; HWND found{}; } q{ownerTitle()};
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto& q = *reinterpret_cast<Query*>(p); wchar_t text[256]{}; GetWindowTextW(h, text, 256);
        if (QString::fromWCharArray(text) == q.title) { q.found = h; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&q)); return q.found;
}
int trayWindows(DWORD pid, bool inject = false)
{
    struct Query { DWORD pid; int count = 0; bool inject; } q{pid, 0, inject};
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto& q = *reinterpret_cast<Query*>(p); DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        wchar_t title[128]{}; GetWindowTextW(h, title, 128);
        if (pid == q.pid && wcscmp(title, L"QTrayIconMessageWindow") == 0)
        { ++q.count; if (q.inject) SendMessageW(h, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0); }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&q)); return q.count;
}
int ownerWindows()
{
    struct Query { QString title; int count = 0; } q{ownerTitle()};
    EnumWindows([](HWND h, LPARAM p) -> BOOL { auto& q = *reinterpret_cast<Query*>(p);
        wchar_t text[256]{}; GetWindowTextW(h, text, 256); if (QString::fromWCharArray(text) == q.title) ++q.count; return TRUE;
    }, reinterpret_cast<LPARAM>(&q)); return q.count;
}
HWND productionWindow(DWORD process)
{
    struct Query { DWORD pid; HWND hwnd{}; } q{process};
    EnumWindows([](HWND h, LPARAM p) -> BOOL { auto& q = *reinterpret_cast<Query*>(p); DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid); wchar_t title[256]{}; GetWindowTextW(h, title, 256);
        if (pid == q.pid && wcsstr(title, L"DesktopSceneEngine") && !GetWindow(h, GW_OWNER)) q.hwnd = h; return TRUE;
    }, reinterpret_cast<LPARAM>(&q)); return q.hwnd;
}
bool productionHide(HWND hwnd)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IUIAutomation> automation; ComPtr<IUIAutomationElement> root, button;
    ComPtr<IUIAutomationCondition> english, chinese, either;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))
        || FAILED(automation->ElementFromHandle(hwnd, &root))) return false;
    const auto condition = [&](const wchar_t* name, IUIAutomationCondition** out) {
        VARIANT value; VariantInit(&value); value.vt = VT_BSTR; value.bstrVal = SysAllocString(name);
        const auto hr = automation->CreatePropertyCondition(UIA_NamePropertyId, value, out); VariantClear(&value); return SUCCEEDED(hr);
    };
    if (!condition(L"Hide to System Tray", &english) || !condition(L"隐藏到系统托盘", &chinese)
        || FAILED(automation->CreateOrCondition(english.Get(), chinese.Get(), &either))
        || FAILED(root->FindFirst(TreeScope_Subtree, either.Get(), &button)) || !button) return false;
    BOOL enabled = FALSE; ComPtr<IUIAutomationInvokePattern> invoke;
    return SUCCEEDED(button->get_CurrentIsEnabled(&enabled)) && enabled
        && SUCCEEDED(button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && SUCCEEDED(invoke->Invoke());
}

void productionRecovery()
{
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "S7 production UIA COM prerequisite");
    struct ComLifetime { ~ComLifetime() { CoUninitialize(); } } com;
    const auto exe = QCoreApplication::applicationDirPath() + "/DesktopSceneEngine_QtPlayer.exe";
    struct OwnedProcess {
        QProcess process; QByteArray output; DWORD pid{};
        void read() { output += process.readAllStandardOutput(); output += process.readAllStandardError(); }
        ~OwnedProcess() { if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(5000); } }
    } owner, session, relaunch;
    const auto launch = [&](OwnedProcess& p, const QStringList& args) {
        p.process.start(exe, args); require(p.process.waitForStarted(5000), "S7 real production launch"); p.pid = DWORD(p.process.processId());
    };
    launch(owner, {});
    require(wait([&] { return productionWindow(owner.pid) && trayWindows(owner.pid) == 1; }), "S7 production owner/Launcher prerequisite");
    const auto id = DisplayId::fromTargetDevicePath(("S7-production-" + QUuid::createUuid().toString()).toStdString());
    launch(session, {"--display-session", QString::fromStdString(*id.canonicalRepresentation())});
    require(wait([&] { session.read(); return session.output.contains("Coordinator Session joined. PID=" + QByteArray::number(owner.pid)); }), "S7 actual QtPlayer native Join");
    const auto hwnd = productionWindow(session.pid); require(hwnd && session.output.count("Desktop Scene Engine initialized.") == 1, "S7 actual sole Engine/native primary");
    require(wait([&] { return productionHide(hwnd); }) && wait([&] { return !IsWindowVisible(hwnd); }), "S7 production UIA genuinely HiddenToTray");
    const auto before = session.output.size(); owner.process.kill(); require(owner.process.waitForFinished(5000), "S7 owned production owner death");
    require(wait([&] { session.read(); return IsWindowVisible(hwnd) && session.output.mid(before).contains("Coordinator Session joined. PID="); }),
        "S7 actual executable recovery/rejoin");
    const auto joined = session.output.mid(session.output.lastIndexOf("Coordinator Session joined. PID=")).split('\n').first().trimmed();
    const auto pid = joined.mid(QByteArray("Coordinator Session joined. PID=").size()).toUInt();
    require(pid && pid != owner.pid && trayWindows(pid) == 1 && trayWindows(session.pid) == 0, "S7 actual unique replacement logical tray");
    HANDLE replacement = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    require(replacement && WaitForSingleObject(replacement, 0) == WAIT_TIMEOUT, "S7 production verified replacement lifetime");
    struct NativeLifetime { HANDLE h; ~NativeLifetime() {
        // This lifetime was authenticated by the exact owned Session's new Join,
        // never by an arbitrary process-name scan. Failure cleanup cannot leak it.
        if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT) TerminateProcess(h, 3);
        CloseHandle(h);
    } } native{replacement};
    require(wait([&] { return productionHide(hwnd); }) && wait([&] { return !IsWindowVisible(hwnd); }), "S7 fresh qualified production Hide after recovery");
    launch(relaunch, {}); // REAL ordinary executable invocation: admission -> ACTIVATE -> existing owner/provider.
    require(wait([&] { return relaunch.process.state() == QProcess::NotRunning; }) && relaunch.process.exitCode() == 0
        && wait([&] { return IsWindowVisible(hwnd) && productionWindow(pid); }), "S7 ordinary production relaunch restores Launcher and unresolved Session");
    session.read(); require(session.output.count("Desktop Scene Engine initialized.") == 1 && productionWindow(session.pid) == hwnd,
        "S7 ordinary relaunch no duplicate Session/native object/Engine rebuild");
    require(PostMessageW(hwnd, WM_CLOSE, 0, 0) && wait([&] { return session.process.state() == QProcess::NotRunning; })
        && session.process.exitStatus() == QProcess::NormalExit && session.process.exitCode() == 0, "S7 actual clean Session ordinary Close");
    const auto launcher = productionWindow(pid);
    wchar_t closingTitle[256]{}; if (launcher) GetWindowTextW(launcher, closingTitle, 256);
    std::cout << "PRODUCTION closing HWND=" << quintptr(launcher) << " title=" << QString::fromWCharArray(closingTitle).toStdString() << std::endl;
    require(launcher && PostMessageW(launcher, WM_CLOSE, 0, 0), "S7 replacement Launcher ordinary Close");
    require(wait([&] { return WaitForSingleObject(replacement, 0) == WAIT_OBJECT_0; }), "S7 actual final zero-role drain");
}
int owner(bool bootstrap)
{
    InstanceAdmissionLease admission; const auto slot = launcherSlot();
    const auto outcome = admission.acquire(slot);
    if (outcome == InstanceAdmissionOutcome::Occupied) return 0; // Production's bounded singleton bootstrap loser.
    require(outcome != InstanceAdmissionOutcome::Error, "S7 owner lease");
    InstanceActivationEndpoint activation(slot); require(activation.listen(), "S7 owner ACTIVATE");
    QTemporaryDir files; QSettings settings(files.filePath("prefs.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore preferences(settings); ApplicationTranslationState translation;
    ApplicationCoordinator coordinator(slot, activation, bootstrap);
    QByteArray priorFrames;
    ApplicationCoordinatorTestAccess::observe(coordinator, priorFrames);
    coordinator.setLauncherFactory([&] { auto* w = new LauncherWindow(translation, preferences);
        w->setAcceptedCloseCallback([&, w] { coordinator.retireLauncher(*w); }); return w; });
    require(coordinator.listen(), "S7 owner Join");
    if (!bootstrap) require(coordinator.showLauncher(), "S7 actual Launcher");
    QWidget endpoint; endpoint.setWindowTitle(ownerTitle()); const auto hwnd = reinterpret_cast<HWND>(endpoint.winId());
    Harness harness; harness.hwnd = hwnd; quintptr priorInspections = 0;
    harness.command = [&](int n) {
        if (n == 1 || n == 10)
        {
            auto& menu = ApplicationCoordinatorTestAccess::menu(coordinator);
            QJsonObject o{{"count", int(coordinator.sessionCount())}, {"epoch", ApplicationCoordinatorTestAccess::epoch(coordinator).toString()},
                {"launcher", coordinator.launcherIncarnation().toString()}, {"batch", ApplicationCoordinatorTestAccess::batch(coordinator)},
                {"pid", int(GetCurrentProcessId())}, {"tray", trayWindows(GetCurrentProcessId())}};
            o["launcherHwnd"] = coordinator.launcherWindow() ? QString::number(coordinator.launcherWindow()->winId()) : QString{};
            int roles = 0, available = 0;
            QJsonArray labels;
            for (auto* a : menu.actions()) if (a->menu()) {
                ++roles; if (a->menu()->actions().front()->isEnabled()) ++available;
                QJsonArray commands;
                for (auto* command : a->menu()->actions()) if (!command->isSeparator())
                    commands.append(QJsonObject{{"text", command->text()}, {"enabled", command->isEnabled()}});
                labels.append(QJsonObject{{"label", a->text()}, {"incarnation", a->data().toUuid().toString()}, {"commands", commands}});
            }
            o["menu"] = labels;
            o["roles"] = roles; o["available"] = available;
            const QUuid priorIncarnation(qEnvironmentVariable("DSE_S7_PRIOR_INCARNATION"));
            o["priorClose"] = closeState(coordinator.roleCloseOperation(priorIncarnation));
            for (auto* role : menu.actions()) if (role->menu() && role->data().toUuid() != coordinator.launcherIncarnation())
            { o["currentIncarnation"] = role->data().toUuid().toString(); o["currentClose"] = closeState(coordinator.roleCloseOperation(role->data().toUuid())); break; }
            QJsonArray replies;
            for (qsizetype p = 0; p + 42 <= priorFrames.size(); p += 42)
                if (priorFrames[p] == 'O') replies.append(QJsonObject{
                    {"generation", QUuid::fromRfc4122(priorFrames.mid(p + 1, 16)).toString()},
                    {"incarnation", QUuid::fromRfc4122(priorFrames.mid(p + 17, 16)).toString()},
                    {"request", QString::number(qFromBigEndian<quint64>(priorFrames.constData() + p + 33))},
                    {"result", int(static_cast<quint8>(priorFrames[p + 41]))}});
            o["closeReplies"] = replies;
            const auto originalKey = RoleCloseKey{QUuid(qEnvironmentVariable("DSE_S7_PRIOR_EPOCH")),
                qEnvironmentVariable("DSE_S7_PRIOR_REQUEST").toULongLong()};
            int knownReplies = 0, unknownReplies = 0, knownResult = -1, unknownResult = -1, currentPriorReplies = 0, currentPriorResult = -1;
            for (const auto& reply : replies) {
                const auto r = reply.toObject();
                if (r["generation"].toString() != originalKey.generation.toString()) continue;
                if (r["incarnation"] == o["currentIncarnation"] && r["incarnation"].toString() != priorIncarnation.toString()
                    && r["request"].toString().toULongLong() == originalKey.request) { ++currentPriorReplies; currentPriorResult = r["result"].toInt(); }
                if (r["incarnation"].toString() != priorIncarnation.toString()) continue;
                if (r["request"].toString().toULongLong() == originalKey.request) { ++knownReplies; knownResult = r["result"].toInt(); }
                if (r["request"].toString().toULongLong() == originalKey.request + 1000000) { ++unknownReplies; unknownResult = r["result"].toInt(); }
            }
            const auto receipt = coordinator.roleCloseOperation(priorIncarnation);
            SetPropW(hwnd, L"S7PriorResult", reinterpret_cast<HANDLE>(quintptr(receipt ? int(receipt->result) + 1 : 0)));
            SetPropW(hwnd, L"S7PriorKeyMatchesOriginal", reinterpret_cast<HANDLE>(quintptr(receipt && receipt->key == originalKey ? 2 : 1)));
            SetPropW(hwnd, L"S7PriorReplies", reinterpret_cast<HANDLE>(quintptr(knownReplies + 1)));
            SetPropW(hwnd, L"S7PriorWireResult", reinterpret_cast<HANDLE>(quintptr(knownResult + 1)));
            SetPropW(hwnd, L"S7UnknownReplies", reinterpret_cast<HANDLE>(quintptr(unknownReplies + 1)));
            SetPropW(hwnd, L"S7UnknownWireResult", reinterpret_cast<HANDLE>(quintptr(unknownResult + 1)));
            const auto currentResult = o["currentClose"].toObject();
            SetPropW(hwnd, L"S7CurrentResult", reinterpret_cast<HANDLE>(quintptr(currentResult.isEmpty() ? 0 : currentResult["result"].toInt() + 1)));
            SetPropW(hwnd, L"S7CurrentPriorReplies", reinterpret_cast<HANDLE>(quintptr(currentPriorReplies + 1)));
            SetPropW(hwnd, L"S7CurrentPriorWireResult", reinterpret_cast<HANDLE>(quintptr(currentPriorResult + 1)));
            int currentStateReplies = 0; bool afterPriorReply = false;
            for (qsizetype p = 0; p + 42 <= priorFrames.size(); p += 42) {
                if (priorFrames[p] == 'O' && QUuid::fromRfc4122(priorFrames.mid(p + 1, 16)) == originalKey.generation
                    && QUuid::fromRfc4122(priorFrames.mid(p + 17, 16)) == priorIncarnation) afterPriorReply = true;
                else if (afterPriorReply && priorFrames[p] == 'V') {
                    if (QUuid::fromRfc4122(priorFrames.mid(p + 1, 16)) == ApplicationCoordinatorTestAccess::epoch(coordinator)
                        && QUuid::fromRfc4122(priorFrames.mid(p + 17, 16)) == priorIncarnation) ++currentStateReplies;
                    afterPriorReply = false;
                }
            }
            SetPropW(hwnd, L"S7PriorCurrentStateReplies", reinterpret_cast<HANDLE>(quintptr(currentStateReplies + 1)));
            SetPropW(hwnd, L"S7PriorInspection", reinterpret_cast<HANDLE>(++priorInspections));
            SetPropW(hwnd, L"S7Count", reinterpret_cast<HANDLE>(quintptr(coordinator.sessionCount() + 1)));
            SetPropW(hwnd, L"S7Roles", reinterpret_cast<HANDLE>(quintptr(roles + 1)));
            SetPropW(hwnd, L"S7Batch", reinterpret_cast<HANDLE>(quintptr(ApplicationCoordinatorTestAccess::batch(coordinator) ? 2 : 1)));
            if (n == 1) output(o);
        }
        else if (n == 2) { for (auto* s : coordinator.findChildren<QLocalSocket*>()) if (s->property("sessionControl").toBool()) s->abort(); }
        else if (n == 3)
        {
            std::cout << "SHELL BEGIN" << std::endl;
            require(trayWindows(GetCurrentProcessId(), true) == 1, "One Qt tray backend before shell event");
            std::cout << "QT SHELL EVENT RETURNED" << std::endl;
            int adapters = 0;
            for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == "coordinatorShellEvents")
            { ++adapters; SendMessageW(reinterpret_cast<HWND>(w->winId()), RegisterWindowMessageW(L"TaskbarCreated"), 0, 0); }
            require(adapters == 1, "S7 RED F: coordinator shell projection adapter absent");
            std::cout << "SHELL RECOVERED" << std::endl;
        }
        else if (n == 4) { if (coordinator.launcherWindow()) coordinator.launcherWindow()->close(); }
        else if (n == 5) ApplicationCoordinatorTestAccess::closeAll(coordinator);
        else if (n == 6) { for (auto* a : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (a->menu() && a->data().toUuid() != coordinator.launcherIncarnation())
            { require(coordinator.requestRoleClose(a->data().toUuid()), "S7 current remote Close admission"); break; } }
        else if (n == 9) QCoreApplication::quit(); // Test-owned owner cleanup only, after peers closed.
        else if (n == 11) ApplicationCoordinatorTestAccess::trayIcon(coordinator, {});
        else if (n == 12) ApplicationCoordinatorTestAccess::trayIcon(coordinator, QApplication::windowIcon());
        else if (n == 13) { for (auto* a : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
            if (a->menu() && a->data().toUuid() != coordinator.launcherIncarnation())
            { auto* show = a->menu()->actions().front(); require(show->isEnabled(), "S7 current tray Show enabled"); show->trigger(); break; } }
        else if (n >= 14 && n <= 20) {
            const QUuid original(qEnvironmentVariable("DSE_S7_PRIOR_INCARNATION"));
            RoleCloseKey key{QUuid(qEnvironmentVariable("DSE_S7_PRIOR_EPOCH")), qEnvironmentVariable("DSE_S7_PRIOR_REQUEST").toULongLong()};
            require(!original.isNull() && !key.generation.isNull() && key.request, "S7 prior key comes from real Session Close ledger");
            if (n == 15) key.request += 1000000; // Unknown request in the ACTUAL retired epoch, not a random UUID.
            if (n == 18) { for (auto* role : ApplicationCoordinatorTestAccess::menu(coordinator).actions())
                if (role->menu() && role->data().toUuid() != coordinator.launcherIncarnation())
                { coordinator.reconcileRoleClose(role->data().toUuid(), key); break; } }
            else if (n == 17) {
                for (const char kind : QByteArray("QPS EUCVFD").replace(" ", ""))
                    ApplicationCoordinatorTestAccess::raw(coordinator, priorFrame(kind, key, original));
            }
            else if (n == 19) ApplicationCoordinatorTestAccess::raw(coordinator, priorFrame('K', {key.generation, 0}, original));
            else if (n == 20) ApplicationCoordinatorTestAccess::raw(coordinator, priorFrame('K', key, coordinator.launcherIncarnation()));
            else { coordinator.reconcileRoleClose(original, key);
                if (n == 16) coordinator.reconcileRoleClose(original, key); } // Two outstanding same-key observations, no Close.
        }
        else if (n == 21) require(coordinator.showLauncher(), "S7 test-owned fresh Launcher holds same owner during exact Session replacement");
        else if (n == 22) {
            require(coordinator.sessionCount() == 0 && coordinator.launcherWindow(), "S7 prior zero-Session ordinary Launcher drain prerequisite");
            coordinator.launcherWindow()->close();
            coordinator.reconcileRoleClose(QUuid(qEnvironmentVariable("DSE_S7_PRIOR_INCARNATION")),
                RoleCloseKey{QUuid(qEnvironmentVariable("DSE_S7_PRIOR_EPOCH")), qEnvironmentVariable("DSE_S7_PRIOR_REQUEST").toULongLong()});
        } // Query overlaps the actual last-role close/drain, never manufactures membership.
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    std::cout << "OWNER READY " << quintptr(hwnd) << std::endl;
    if (!qEnvironmentVariableIsSet("DSE_S7_HUMAN")) QTimer::singleShot(55000, &coordinator, [] { QCoreApplication::exit(3); });
    const auto result = QApplication::exec(); QApplication::instance()->removeNativeEventFilter(&harness); return result;
}

int peer(const QString& name)
{
    using Access = MainWindowDesktopPresentationTestAccess;
    QTemporaryDir files; QSettings settings(files.filePath("prefs.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore preferences(settings); ApplicationTranslationState translation;
    const auto slot = sessionSlot(name); InstanceAdmissionLease admission;
    require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "S7 peer admission");
    InstanceActivationEndpoint activation(slot); require(activation.listen(), "S7 peer ACTIVATE");
    quint64 ticks = 0;
    const auto id = identity(name); MainWindow window(id, translation, preferences, {});
    Access::observeTicks(window, ticks);
    window.show(); require(window.initializeEngine(), "S7 real Engine");
    activation.bindReadyRole(window, [&] { (void)window.trayPresentation().restore(); });
    SessionCoordinatorJoin join(slot, launcherSlot()); join.enableControl(window.trayPresentation());
    join.enableDesktopDeployment([&] { return window.desktopDeploymentProjection(); }, [&](auto intent) { return window.requestDesktopDeployment(intent); });
    window.setAcceptedCloseCallback([&] { join.beginRoleTeardown(); activation.beginShutdown();
        QTimer::singleShot(0, &join, [&] { join.drainAndStop([] { QCoreApplication::quit(); }); }); });
    auto& model = Access::model(window); auto& engine = Access::engine(window);
    QImage image(80, 50, QImage::Format_RGBA8888); image.fill(Qt::green); const auto file = files.filePath("image.png");
    require(image.save(file), "S7 image"); const auto playlist = model.addPlaylist(); const auto item = model.addItem(playlist, WorkspaceMediaType::Image, file);
    require(item && model.setImageDisplayDuration(*item, 30) && Access::start(window, playlist, *item), "S7 authored scene");
    Access::pause(window); require(engine.seek(7), "S7 paused playback");
    const auto revision = model.authoredRevision(); const auto canvas = model.desktopCanvas().aspectRatio;
    const auto preview = Access::preview(window); const auto deployment = window.desktopDeploymentProjection();
    const auto hwnd = reinterpret_cast<HWND>(window.winId()); const auto engineAddress = quintptr(&engine);
    const auto previewWindow = reinterpret_cast<HWND>(Access::surface(window).winId());
    const auto originalSize = window.size(); const auto originalSplit = Access::splitter(window).sizes();
    const auto nativeExtendedStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const auto qtFlags = window.windowFlags();
    join.start(window); require(wait([&] { return join.qualified(); }), "S7 current control prerequisite");
    int closes = 0, failVisibleShows = 0;
    class CloseObserver final : public QObject {
    public: int& count; int& safetyShows; SessionCoordinatorJoin& join;
        CloseObserver(int& n, int& s, SessionCoordinatorJoin& j) : count(n), safetyShows(s), join(j) {}
        bool eventFilter(QObject*, QEvent* event) override {
            if (event->type() == QEvent::Close) ++count;
            if (event->type() == QEvent::Show && !join.qualified()) ++safetyShows;
            return false;
        }
    } observer(closes, failVisibleShows, join);
    window.installEventFilter(&observer);
    RECT previousSurface{};
    std::vector<std::pair<POINT, COLORREF>> previousPixels;
    const auto rememberSurface = [&] {
        require(GetWindowRect(previewWindow, &previousSurface), "S7 pre-resize native surface rectangle");
        previousPixels.clear();
        for (const LONG x : {previousSurface.left + 8, (previousSurface.left + previousSurface.right) / 2, previousSurface.right - 8})
            for (const LONG y : {previousSurface.top + 8, (previousSurface.top + previousSurface.bottom) / 2, previousSurface.bottom - 8}) {
                const POINT point{x, y};
                if (WindowFromPoint(point) == previewWindow) previousPixels.emplace_back(point, screenPixelAt(point));
            }
    };
    const auto frameState = [&] {
        auto& gpu = Rendering::CanvasTargetFitTestAccess::backend(engine);
        const auto target = gpu.presentationTargetInfo(Access::preview(window));
        const auto diagnostic = gpu.presentationTargetDiagnosticsForTesting(Access::preview(window));
        RECT client{}; const auto child = reinterpret_cast<HWND>(Access::surface(window).winId());
        const bool nativeValid = IsWindow(child) && GetClientRect(child, &client);
        DXGI_SWAP_CHAIN_DESC descriptor{}; HRESULT descriptorResult = E_FAIL, deviceReason = E_FAIL;
        if (auto* chain = reinterpret_cast<IDXGISwapChain*>(diagnostic.swapChainIdentity)) {
            descriptorResult = chain->GetDesc(&descriptor);
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&device)))) deviceReason = device->GetDeviceRemovedReason();
        }
        POINT center{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
        const bool mapped = nativeValid && ClientToScreen(child, &center);
        const auto hit = mapped ? WindowFromPoint(center) : nullptr;
        const bool exposed = hit == child;
        DWORD hitPid = 0; if (hit) GetWindowThreadProcessId(hit, &hitPid);
        const COLORREF screenPixel = exposed ? screenPixelAt(center) : CLR_INVALID;
        RECT surface{}, host{}; GetWindowRect(child, &surface);
        const auto hostWindow = reinterpret_cast<HWND>(Access::surface(window).parentWidget()->parentWidget()->winId());
        GetWindowRect(hostWindow, &host);
        COLORREF reclaimed = CLR_INVALID, expanded = CLR_INVALID;
        for (const auto& [point, prior] : previousPixels)
            if (prior == RGB(0, 255, 0) && PtInRect(&host, point) && WindowFromPoint(point) == hostWindow) {
                reclaimed = screenPixelAt(point); break; // Former actual media pixel now belongs to Qt viewport background.
            }
        if (!IsRectEmpty(&previousSurface))
            for (const LONG x : {surface.left + 8, (surface.left + surface.right) / 2, surface.right - 8})
                for (const LONG y : {surface.top + 8, (surface.top + surface.bottom) / 2, surface.bottom - 8}) {
                    const POINT point{x, y};
                    if (!PtInRect(&previousSurface, point) && WindowFromPoint(point) == child) expanded = screenPixelAt(point);
                }
        return QJsonObject{{"pid", int(GetCurrentProcessId())}, {"object", QString::number(quintptr(&window))},
            {"engine", QString::number(engineAddress)}, {"incarnation", join.incarnation().toString()},
            {"hwnd", QString::number(quintptr(hwnd))}, {"childHwnd", QString::number(quintptr(child))},
            {"childSame", child == previewWindow}, {"targetSame", Access::preview(window) == preview},
            {"targetCount", int(gpu.presentationTargetCountForTesting())}, {"timerActive", Access::timerActive(window)},
            {"ticks", QString::number(ticks)}, {"paused", engine.isPaused()}, {"position", engine.playbackTime()},
            {"qualified", join.qualified()}, {"hidden", window.trayPresentation().hiddenToTray()},
            {"nativeValid", nativeValid}, {"nativeWidth", int(client.right - client.left)}, {"nativeHeight", int(client.bottom - client.top)},
            {"clientWidth", target.clientWidth}, {"clientHeight", target.clientHeight},
            {"realizedWidth", target.viewportWidth}, {"realizedHeight", target.viewportHeight},
            {"ready", target.ready}, {"occluded", target.occluded}, {"failed", target.failed},
            {"deviceGeneration", QString::number(target.deviceGeneration)}, {"targetGeneration", QString::number(target.targetGeneration)},
            {"device", QString::number(diagnostic.deviceIdentity)}, {"swapChain", QString::number(diagnostic.swapChainIdentity)},
            {"swapChainWindow", QString::number(quintptr(descriptor.OutputWindow))}, {"descriptorResult", int(descriptorResult)},
            {"deviceRemovedReason", int(deviceReason)}, {"draws", QString::number(diagnostic.drawCount)},
            {"presents", QString::number(diagnostic.presentAttemptCount)}, {"resizes", QString::number(diagnostic.successfulResizeCount)},
            {"syncInterval", int(diagnostic.lastPresentSyncInterval)}, {"presentFlags", int(diagnostic.lastPresentFlags)},
            {"pixelValid", diagnostic.capturedPixelValid}, {"pixelRgba", QString::number(diagnostic.capturedPixelRgba)},
            {"screenExposed", exposed}, {"screenPixelRgb", QString::number(screenPixel)},
            {"screenX", int(center.x)}, {"screenY", int(center.y)}, {"hitHwnd", QString::number(quintptr(hit))},
            {"hitPid", int(hitPid)}, {"foregroundHwnd", QString::number(quintptr(GetForegroundWindow()))},
            {"childVisible", bool(IsWindowVisible(child))}, {"childEnabled", bool(IsWindowEnabled(child))},
            {"reclaimedPixelRgb", QString::number(reclaimed)}, {"expandedPixelRgb", QString::number(expanded)}};
    };
    struct PriorPipe { HANDLE handle{}; ~PriorPipe() { if (handle) CloseHandle(handle); } } priorPipe;
    Harness harness; harness.hwnd = hwnd; QUuid oldEpoch = ApplicationCoordinatorTestAccess::epoch(join);
    harness.command = [&](int n) {
        if (n == 1)
        {
            require(model.authoredRevision() == revision && model.activeItemId() == item && model.desktopCanvas().aspectRatio == canvas
                && Access::preview(window) == preview && Access::bound(window) == id && Access::document(window).isDirty(revision)
                && engine.isInitialized() && engine.isPaused() && engine.playbackTime() == 7 && quintptr(&engine) == engineAddress
                && window.desktopDeploymentProjection().retained == deployment.retained && window.winId() == reinterpret_cast<WId>(hwnd),
                "S7 exact Project/dirty/Canvas/media/playback/Engine/Preview/DisplayId/deployment/HWND preservation");
            require(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) == nativeExtendedStyle && window.windowFlags() == qtFlags,
                "S7 exact native extended style and Qt window flags after restore");
            QJsonObject state{{"pid", int(GetCurrentProcessId())}, {"owner", int(join.coordinatorPid())},
                {"incarnation", join.incarnation().toString()}, {"epoch", ApplicationCoordinatorTestAccess::epoch(join).toString()},
                {"qualified", join.qualified()}, {"hidden", window.trayPresentation().hiddenToTray()},
                {"retrying", ApplicationCoordinatorTestAccess::retrying(join)}, {"retired", ApplicationCoordinatorTestAccess::deadHandleRetired(join)},
                {"renewal", ApplicationCoordinatorTestAccess::renewing(join)},
                {"engine", QString::number(engineAddress)}, {"close", bool(window.trayPresentation().closeOperation())}, {"tray", trayWindows(GetCurrentProcessId())},
                {"hwnd", QString::number(quintptr(hwnd))}, {"revision", QString::number(revision)}, {"item", item->value.toString()},
                {"canvas", QString::number(canvas.numerator) + ":" + QString::number(canvas.denominator)},
                {"displayId", QString::fromStdString(*id.canonicalRepresentation())}, {"media", file},
                {"paused", engine.isPaused()}, {"position", engine.playbackTime()}, {"dirty", Access::document(window).isDirty(revision)},
                {"deployed", window.desktopDeploymentProjection().retained}, {"failVisibleShows", failVisibleShows}};
            state["closeOperation"] = closeState(window.trayPresentation().closeOperation());
            state["closeCount"] = closes;
            if (qEnvironmentVariableIsSet("DSE_S7_HUMAN")) state["frame"] = frameState();
            output(state);
            SetPropW(hwnd, L"S7Closes", reinterpret_cast<HANDLE>(quintptr(closes + 1)));
        }
        else if (n == 2) { oldEpoch = ApplicationCoordinatorTestAccess::epoch(join); window.findChild<QToolButton*>("sessionHideToTray")->click(); }
        else if (n == 3) ApplicationCoordinatorTestAccess::disconnect(join);
        else if (n == 4) ApplicationCoordinatorTestAccess::noBootstrap(join);
        else if (n == 5) ApplicationCoordinatorTestAccess::stale(join, oldEpoch);
        else if (n == 6) { Access::document(window).markClean(model.authoredRevision()); window.close(); }
        else if (n == 7) { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Cancel)->click(); }
        else if (n == 8) window.close();
        else if (n == 12) window.showMaximized();
        else if (n == 13) window.showMinimized();
        else if (n == 14) ApplicationCoordinatorTestAccess::renew(join);
        else if (n == 15) (void)window.trayPresentation().restore();
        else if (n == 16) throw std::runtime_error("Injected test-local child diagnostic exception");
        else if (n == 17) output(frameState());
        else if (n == 18) Access::playPause(window); // Real production transport button; no direct tick/render.
        else if (n == 19) { rememberSurface(); window.resize(1350, 850); Access::splitter(window).setSizes({230, 540, 380}); }
        else if (n == 20) { rememberSurface(); window.resize(1500, 950); Access::splitter(window).setSizes({230, 860, 300}); }
        else if (n == 21) require(Rendering::CanvasTargetFitTestAccess::backend(engine).captureNextPresentationPixelForTesting(preview), "S7 GPU readback armed");
        else if (n == 22) { require(engine.isPaused() && engine.seek(7), "S7 original paused position restored");
            window.resize(originalSize); Access::splitter(window).setSizes(originalSplit); previousPixels.clear(); previousSurface = {};
            require(SetWindowPos(hwnd, (nativeExtendedStyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), "S7 restore original native z-order class after pixel qualification"); }
        else if (n == 23) {
            // A background tool session cannot override Windows foreground policy.
            // Expose ONLY this test-owned window for real screen-pixel proof; restore
            // its original class before the formal Gate snapshot. No activation.
            require(SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), "S7 test-owned pixel visibility qualification");
        }
        else if (n == 24) {
            const auto operation = window.trayPresentation().closeOperation();
            require(operation && operation->key.generation == ApplicationCoordinatorTestAccess::epoch(join), "S7 export genuine current Close key before owner death");
            qputenv("DSE_S7_PRIOR_EPOCH", operation->key.generation.toString().toUtf8());
            qputenv("DSE_S7_PRIOR_REQUEST", QByteArray::number(operation->key.request));
            qputenv("DSE_S7_PRIOR_INCARNATION", join.incarnation().toString().toUtf8());
            require(!priorPipe.handle, "S7 retain original pipe once"); priorPipe.handle = ApplicationCoordinatorTestAccess::retainPipe(join);
        }
        else if (n == 25) ApplicationCoordinatorTestAccess::unqualify(join);
        else if (n == 26) {
            require(priorPipe.handle && window.trayPresentation().closeOperation(), "S7 retired native pipe/key retained");
            const auto frame = priorFrame('K', window.trayPresentation().closeOperation()->key, join.incarnation());
            OVERLAPPED io{}; io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); require(io.hEvent, "S7 old pipe event");
            DWORD bytes = 0; const bool sent = WriteFile(priorPipe.handle, frame.constData(), DWORD(frame.size()), &bytes, &io);
            auto error = sent ? ERROR_SUCCESS : GetLastError();
            if (error == ERROR_IO_PENDING) {
                if (WaitForSingleObject(io.hEvent, 1000) == WAIT_OBJECT_0) {
                    const bool completed = GetOverlappedResult(priorPipe.handle, &io, &bytes, FALSE);
                    error = completed ? ERROR_SUCCESS : GetLastError();
                } else { CancelIoEx(priorPipe.handle, &io); GetOverlappedResult(priorPipe.handle, &io, &bytes, TRUE); error = ERROR_TIMEOUT; }
            }
            CloseHandle(io.hEvent);
            require(error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA || error == ERROR_PIPE_NOT_CONNECTED,
                "S7 stale original connection cannot deliver prior Query or gain current authority");
        }
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    std::cout << "PEER READY " << quintptr(hwnd) << " " << join.incarnation().toString().toStdString() << std::endl;
    if (!qEnvironmentVariableIsSet("DSE_S7_HUMAN")) QTimer::singleShot(50000, &window, [] { QCoreApplication::exit(3); });
    const auto result = QApplication::exec(); QApplication::instance()->removeNativeEventFilter(&harness); return result;
}

struct Child
{
    QProcess process; QByteArray bytes; HWND hwnd{}; DWORD pid = 0; QJsonObject state;
    Child(const QStringList& args, const char* ready)
    {
        process.start(QCoreApplication::applicationFilePath(), args); require(process.waitForStarted(5000), "S7 child startup"); pid = DWORD(process.processId());
        if (!wait([&] { read(); return bytes.contains(ready) || process.state() == QProcess::NotRunning; }))
        { std::cerr << bytes.constData(); throw std::runtime_error("S7 readiness timeout"); }
        const auto line = bytes.mid(bytes.indexOf(ready)).split('\n').first().trimmed().split(' ');
        if (process.state() != QProcess::Running || line.size() < 3) { std::cerr << bytes.constData(); throw std::runtime_error("S7 child prerequisite"); }
        hwnd = reinterpret_cast<HWND>(line[2].toULongLong()); DWORD ownerPid = 0;
        require(IsWindow(hwnd) && GetWindowThreadProcessId(hwnd, &ownerPid) && ownerPid == pid, "S7 native HWND/process identity");
    }
    void read() { bytes += process.readAllStandardOutput(); bytes += process.readAllStandardError(); }
    void send(int n) { require(PostMessageW(hwnd, HarnessMessage, n, 0), "S7 owned native harness"); }
    QJsonObject query(int command = 1)
    {
        read(); const auto offset = bytes.size(); send(command);
        if (!wait([&] { read(); const auto tail = bytes.mid(offset); const auto start = tail.indexOf("STATE ");
            return start >= 0 && tail.indexOf('\n', start) >= 0; })) { std::cerr << bytes.constData(); throw std::runtime_error("S7 state response"); }
        const auto line = bytes.mid(offset).split('\n');
        for (const auto& l : line) if (l.startsWith("STATE ")) { state = QJsonDocument::fromJson(l.mid(6)).object(); return state; }
        throw std::runtime_error("S7 invalid response");
    }
    void hide(int queryCommand = 1) { send(2); require(wait([&] { return !IsWindowVisible(hwnd); }), "S7 genuinely hidden primary"); require(query(queryCommand)["qualified"].toBool() && state["hidden"].toBool(), "S7 healthy hidden prerequisite"); }
    void crash() { process.kill(); require(process.waitForFinished(5000), "S7 owned exact process death"); }
    ~Child() { if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(5000); } }
};
struct Replacement
{
    HWND hwnd{}; HANDLE process{}; DWORD pid{};
    void capture(DWORD oldPid)
    {
        require(wait([&] { hwnd = ownerWindow(); DWORD p = 0; if (hwnd) GetWindowThreadProcessId(hwnd, &p); pid = p; return p && p != oldPid; }),
            "S7 RED B/C: bounded unique replacement coordinator did not appear");
        process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
        require(process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT, "S7 verified owned replacement lifetime");
    }
    void inspect(int count)
    {
        PostMessageW(hwnd, HarnessMessage, 10, 0);
        require(wait([&] { return quintptr(GetPropW(hwnd, L"S7Count")) == quintptr(count + 1); }), "S7 fresh owner exact living registry count");
        require(ownerWindows() == 1 && quintptr(GetPropW(hwnd, L"S7Roles")) == quintptr(count + 1)
            && quintptr(GetPropW(hwnd, L"S7Batch")) == 1, "S7 no inherited Launcher incarnation, stale menus or Close All batch");
    }
    ~Replacement() { if (process) { if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) { PostMessageW(hwnd, HarnessMessage, 9, 0);
            if (WaitForSingleObject(process, 3000) == WAIT_TIMEOUT) TerminateProcess(process, 3); } CloseHandle(process); } }
};

void frameBaseline(Child& owner, Child& session)
{
    const auto count = [](const QJsonObject& s, const char* key) { return s[key].toString().toULongLong(); };
    const auto initial = session.query(17);
    std::cout << "BASELINE initial " << QJsonDocument(initial).toJson(QJsonDocument::Compact).constData() << std::endl;
    require(initial["timerActive"].toBool(), "S7 frame-driving RED: Human Gate fixture stopped production tick timer before any Explorer restart");
    const auto observe = [&](const char* phase) {
        const auto state = session.query(17);
        std::cout << "BASELINE " << phase << " " << QJsonDocument(state).toJson(QJsonDocument::Compact).constData() << std::endl;
        return state;
    };
    const auto identityAndTimer = [&](const QJsonObject& s) {
        for (const char* key : {"pid", "object", "engine", "hwnd", "childHwnd", "incarnation", "device", "swapChain", "deviceGeneration"})
            require(s[key] == initial[key], "S7 baseline native/object/Engine/device/swapchain identity preserved");
        require(s["timerActive"].toBool() && s["childSame"].toBool() && s["targetSame"].toBool()
            && s["nativeValid"].toBool() && s["targetCount"].toInt() == 1 && s["qualified"].toBool(),
            "S7 baseline same Preview HWND/target and genuine active production timer");
        require(s["descriptorResult"].toInt() == S_OK && s["deviceRemovedReason"].toInt() == S_OK
            && s["swapChainWindow"] == s["childHwnd"], "S7 real swapchain OutputWindow and healthy D3D11 device");
    };
    const auto drawable = [&](const QJsonObject& s) {
        return s["ready"].toBool() && !s["failed"].toBool() && !s["occluded"].toBool()
            && s["clientWidth"] == s["nativeWidth"] && s["clientHeight"] == s["nativeHeight"]
            && s["realizedWidth"] == s["nativeWidth"] && s["realizedHeight"] == s["nativeHeight"]
            && s["syncInterval"].toInt() == 1 && s["presentFlags"].toInt() == 0;
    };
    const auto framesAfter = [&](const QJsonObject& before, const char* phase) {
        require(wait([&] { const auto s = session.query(17); return drawable(s)
            && count(s, "ticks") >= count(before, "ticks") + 3 && count(s, "draws") >= count(before, "draws") + 3
            && count(s, "presents") >= count(before, "presents") + 3; }, 4000), "S7 real timer-driven draw/Present progress and native size realization");
        const auto s = observe(phase); identityAndTimer(s); return s;
    };
    const auto elapsed = [](int milliseconds) { QElapsedTimer clock; clock.start();
        require(wait([&] { return clock.elapsed() >= milliseconds; }, milliseconds + 1000), "S7 bounded real-time observation"); };
    const auto pixels = [&] {
        session.send(21);
        const bool passed = wait([&] { const auto s = session.query(17); return s["pixelValid"].toBool()
            && count(s, "pixelRgba") == 0xFF00FF00ULL && s["screenExposed"].toBool()
            && count(s, "screenPixelRgb") == RGB(0, 255, 0); }, 4000);
        if (!passed) observe("pixel-failure");
        require(passed, "S7 actual green GPU framebuffer AND exposed native screen pixel; no forced DXGI result");
    };
    session.send(23); // Test-owned temporary z-order qualification, never activation or a frame/render invocation.
    auto state = framesAfter(initial, "initial-paused"); pixels();
    session.send(18); elapsed(350); auto playing = framesAfter(state, "playing");
    require(!playing["paused"].toBool() && playing["position"].toDouble() > state["position"].toDouble() + 0.15,
        "S7 actual Play advances playback position");
    session.send(18); const auto paused = session.query(17); elapsed(350); state = framesAfter(paused, "paused-rendering");
    require(state["paused"].toBool() && state["position"] == paused["position"], "S7 Pause freezes clock, not rendering");
    session.hide(17); const auto hiddenPaused = session.query(17); elapsed(250); auto hidden = observe("hidden-paused"); identityAndTimer(hidden);
    require(hidden["position"] == hiddenPaused["position"] && count(hidden, "ticks") > count(hiddenPaused, "ticks"), "S7 hidden paused clock stable, production tick continues");
    owner.send(13); require(wait([&] { return IsWindowVisible(session.hwnd); }), "S7 actual current-incarnation tray Show restores same native window");
    identityAndTimer(session.query(17)); session.send(23); state = framesAfter(hidden, "shown-paused"); pixels();
    require(state["position"] == paused["position"], "S7 paused Hide/Show preserves position");
    session.send(18); elapsed(250); playing = observe("before-playing-hide");
    session.hide(17); elapsed(350); hidden = observe("hidden-playing"); identityAndTimer(hidden);
    require(!hidden["paused"].toBool() && hidden["position"].toDouble() > playing["position"].toDouble() + 0.15, "S7 hidden playback actually advances");
    owner.send(13); require(wait([&] { return IsWindowVisible(session.hwnd); }), "S7 playing tray Show");
    identityAndTimer(session.query(17)); session.send(23); state = framesAfter(hidden, "shown-playing"); pixels();
    require(!state["paused"].toBool() && state["position"].toDouble() > hidden["position"].toDouble(), "S7 Show retains playing state and advancing clock");
    session.send(18); state = session.query(17);
    for (const int command : {19, 20}) {
        const auto before = state; session.send(command); state = framesAfter(before, command == 19 ? "resize-shrink" : "resize-grow"); pixels();
        require((state["nativeWidth"] != before["nativeWidth"] || state["nativeHeight"] != before["nativeHeight"])
            && count(state, "resizes") > count(before, "resizes") && state["position"] == before["position"], "S7 real Preview resize realized while paused clock stays stable");
        require(wait([&] { const auto s = session.query(17); return command == 19
            ? count(s, "reclaimedPixelRgb") == RGB(17, 22, 29) : count(s, "expandedPixelRgb") == RGB(0, 255, 0); }, 4000),
            "S7 native repaint: formerly green exposed area cleared to viewport background / newly enlarged surface drawn green");
        state = observe(command == 19 ? "reclaimed-screen-pixel" : "expanded-screen-pixel");
    }
    session.send(22); state = framesAfter(state, "original-state-restored");
    require(state["paused"].toBool() && state["position"].toDouble() == 7, "S7 fixed paused snapshot restored without stopping timer");
    (void)session.query(); // Existing full authored/runtime/flags preservation assertion remains unchanged.
    std::cout << "BASELINE PASS: production timer/play/pause/Hide/Show/resize/native pixels; NO Explorer restart" << std::endl;
}

// Not a CTest and never restarts Explorer. This explicitly selected production-like
// gate uses real Launcher/MainWindow/Engine/control sources plus test-only identity
// snapshots and temporary media/preferences. The operator alone controls Shell restart.
int humanGate(bool baselineOnly = false)
{
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    require(input, "S7 interactive input desktop available");
    wchar_t desktopName[128]{}; DWORD bytes = 0;
    const bool normal = GetUserObjectInformationW(input, UOI_NAME, desktopName, sizeof(desktopName), &bytes)
        && wcscmp(desktopName, L"Default") == 0;
    CloseDesktop(input); require(normal, "S7 unlocked normal input desktop");
    SYSTEM_POWER_STATUS power{}; require(GetSystemPowerStatus(&power), "S7 power-state read");
    const auto displays = Platform::enumerateActiveDisplays();
    require(displays.status == Platform::DisplayEnumerationStatus::Success, "S7 real display enumeration");
    const auto primary = std::find_if(displays.snapshot.displays.begin(), displays.snapshot.displays.end(), [](const auto& d) { return d.primary; });
    require(primary != displays.snapshot.displays.end() && primary->id.canonicalRepresentation(), "S7 exact persistent primary display route");
    const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qputenv("DSE_S7_NONCE", nonce.toUtf8()); qputenv("DSE_S7_HUMAN", "1");
    Child c({"owner"}, "OWNER READY");
    Child a({"peer", QString::fromStdString(*primary->id.canonicalRepresentation())}, "PEER READY");
    if (baselineOnly) {
        frameBaseline(c, a); // Automation only; a real Human Gate does not temporarily change its window z-order.
        a.send(6); require(wait([&] { return a.process.state() == QProcess::NotRunning; }) && a.process.exitCode() == 0,
            "S7 baseline ordinary temporary Session close");
        c.send(4); require(wait([&] { return c.process.state() == QProcess::NotRunning; }) && c.process.exitCode() == 0,
            "S7 baseline ordinary Launcher/owner drain");
        std::cout << "BASELINE NORMAL CLEANUP PASS" << std::endl; return 0;
    }
    a.hide(); const auto before = a.query(); const auto ownerBefore = c.query();
    require(before["frame"].toObject()["timerActive"].toBool(), "S7 Human Gate retains production frame-driving");
    require(before["qualified"].toBool() && before["hidden"].toBool() && ownerBefore["roles"].toInt() == 2
        && trayWindows(c.pid) == 1 && trayWindows(a.pid) == 0, "S7 human gate initial exact roles/healthy Hidden/logical sole tray");
    std::cout << "GATE NONCE " << nonce.toStdString() << " parent=" << GetCurrentProcessId()
        << " AC=" << int(power.ACLineStatus) << " battery=" << int(power.BatteryLifePercent)
        << " display=" << primary->displayNumber << " " << primary->pixelWidth << "x" << primary->pixelHeight << std::endl;
    std::cout << "GATE BEFORE OWNER " << QJsonDocument(ownerBefore).toJson(QJsonDocument::Compact).constData() << std::endl;
    std::cout << "GATE BEFORE SESSION " << QJsonDocument(before).toJson(QJsonDocument::Compact).constData() << std::endl;
    c.read(); const auto shellOffset = c.bytes.size();
    QWidget endpoint; endpoint.setWindowTitle("S7 gate " + nonce);
    Harness harness; harness.hwnd = reinterpret_cast<HWND>(endpoint.winId()); int checks = 0, diagnosticAttempts = 0;
    harness.command = [&](int command) { try {
        if (command == 1) {
            RemovePropW(harness.hwnd, L"S7GateFailed");
            require(wait([&] { c.read(); return c.bytes.mid(shellOffset).contains("[Tray] Shell projection refreshed epoch=" + ownerBefore["epoch"].toString().toUtf8()); }, 3000),
                "S7 genuine Shell recreation reached production coordinator adapter");
            a.send(14); require(wait([&] { const auto s = a.query(); return s["qualified"].toBool() && !s["renewal"].toBool(); }), "S7 current exact-incarnation control round trip");
            const auto after = a.query(); const auto ownerAfter = c.query();
            const auto frameBefore = before["frame"].toObject(), frameAfter = after["frame"].toObject();
            require(frameAfter["timerActive"].toBool() && frameAfter["childSame"].toBool() && frameAfter["targetSame"].toBool()
                && frameAfter["swapChainWindow"] == frameAfter["childHwnd"]
                && frameAfter["ticks"].toString().toULongLong() > frameBefore["ticks"].toString().toULongLong(),
                "S7 post-Shell Preview native binding and production frame-driving preserved");
            for (const auto* key : {"pid", "owner", "incarnation", "epoch", "engine", "hwnd", "revision", "item", "canvas", "displayId", "media", "paused", "position", "dirty", "deployed"})
                require(after[key] == before[key], "S7 post-Shell exact Session/runtime/authored/deployment identity");
            for (const auto* key : {"pid", "epoch", "launcher", "launcherHwnd", "count", "roles", "available", "menu", "batch"})
                require(ownerAfter[key] == ownerBefore[key], "S7 post-Shell same coordinator/Launcher/fresh menu");
            const bool safetySurfaced = after["failVisibleShows"].toInt() > before["failVisibleShows"].toInt();
            require((safetySurfaced ? !after["hidden"].toBool() && IsWindowVisible(a.hwnd) : after["hidden"].toBool() && !IsWindowVisible(a.hwnd))
                && trayWindows(c.pid) == 1 && trayWindows(a.pid) == 0
                && ownerWindows() == 1, "S7 healthy hidden role retained; logical single owner, not a physical icon claim");
            std::cout << "GATE SAFETY PATH " << (safetySurfaced ? "qualification-loss fail-visible; no automatic re-hide" : "continuously qualified HiddenToTray") << std::endl;
            std::cout << "GATE AFTER OWNER " << QJsonDocument(ownerAfter).toJson(QJsonDocument::Compact).constData() << std::endl;
            std::cout << "GATE AFTER SESSION " << QJsonDocument(after).toJson(QJsonDocument::Compact).constData() << std::endl;
            std::cout << "GATE AUTOMATED POST-RESTART PASS; physical icon/user Show proof REQUIRED" << std::endl;
            SetPropW(harness.hwnd, L"S7Checked", reinterpret_cast<HANDLE>(quintptr(++checks)));
        }
        else if (command == 2) {
            a.send(6); require(wait([&] { return a.process.state() == QProcess::NotRunning; }) && a.process.exitCode() == 0, "S7 gate normal temporary Session teardown");
            c.send(4); require(wait([&] { return c.process.state() == QProcess::NotRunning; }) && c.process.exitCode() == 0, "S7 gate normal final Launcher/owner drain");
            std::cout << "GATE NORMAL CLEANUP PASS" << std::endl; QCoreApplication::quit();
        }
    } catch (const std::exception& e) {
        // A real Gate failure is evidence, not permission to kill the scene.
        std::cerr << "GATE AUTOMATED FAIL (scene retained): " << e.what() << std::endl;
        SetPropW(harness.hwnd, L"S7GateFailed", reinterpret_cast<HANDLE>(quintptr(1)));
    } if (command == 1) SetPropW(harness.hwnd, L"S7DiagnosticAttempts", reinterpret_cast<HANDLE>(quintptr(++diagnosticAttempts))); };
    QApplication::instance()->installNativeEventFilter(&harness);
    const int result = QApplication::exec(); QApplication::instance()->removeNativeEventFilter(&harness); return result;
}

int gateCommand(const QString& nonce, int command)
{
    const auto title = ("S7 gate " + nonce).toStdWString(); const auto hwnd = FindWindowW(nullptr, title.c_str());
    require(hwnd && IsWindow(hwnd), "S7 exact owned gate endpoint");
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    require(process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT, "S7 gate retained process identity");
    wchar_t path[32768]{}; DWORD chars = DWORD(std::size(path));
    const bool ownImage = QueryFullProcessImageNameW(process, 0, path, &chars)
        && QString::fromWCharArray(path).replace('\\', '/').compare(QCoreApplication::applicationFilePath(), Qt::CaseInsensitive) == 0;
    CloseHandle(process); require(ownImage, "S7 exact test-owned gate executable");
    const auto before = quintptr(GetPropW(hwnd, L"S7DiagnosticAttempts"));
    require(PostMessageW(hwnd, HarnessMessage, command, 0), "S7 gate diagnostic dispatch");
    require(wait([&] { return command == 2 ? !IsWindow(hwnd) : quintptr(GetPropW(hwnd, L"S7DiagnosticAttempts")) > before; }, 15000), "S7 gate diagnostic completion");
    require(!IsWindow(hwnd) || !GetPropW(hwnd, L"S7GateFailed"), "S7 automated gate failed; live scene/diagnostics retained");
    return 0;
}

void gateDiagnosticTest()
{
    struct OwnedGate {
        QProcess process; QByteArray bytes; QString nonce; HWND endpoint{};
        std::vector<HANDLE> children;
        void read() { bytes += process.readAllStandardOutput(); bytes += process.readAllStandardError(); }
        ~OwnedGate() {
            if (process.state() != QProcess::NotRunning && endpoint) {
                PostMessageW(endpoint, HarnessMessage, 2, 0);
                (void)wait([&] { return process.state() == QProcess::NotRunning; }, 15000);
            }
            // Failure cleanup belongs only to this automated diagnostic test,
            // never to the explicitly launched real human Gate route.
            for (HANDLE h : children) { if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT) TerminateProcess(h, 3); CloseHandle(h); }
            if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(3000); }
        }
    } gate;
    gate.process.start(QCoreApplication::applicationFilePath(), {"human"});
    require(gate.process.waitForStarted(5000), "S7 diagnostic gate child startup");
    const auto ready = [&] { gate.read(); return gate.bytes.contains("GATE BEFORE SESSION ") && gate.bytes.endsWith('\n'); };
    require(wait(ready), "S7 diagnostic gate scene readiness");
    const auto lines = gate.bytes.split('\n'); QJsonObject owner, session;
    for (const auto& line : lines) {
        if (line.startsWith("GATE NONCE ")) gate.nonce = QString::fromLatin1(line.split(' ')[2]);
        if (line.startsWith("GATE BEFORE OWNER ")) owner = QJsonDocument::fromJson(line.mid(18)).object();
        if (line.startsWith("GATE BEFORE SESSION ")) session = QJsonDocument::fromJson(line.mid(20)).object();
    }
    const auto title = ("S7 gate " + gate.nonce).toStdWString();
    require(wait([&] { gate.endpoint = FindWindowW(nullptr, title.c_str()); return gate.endpoint != nullptr; }), "S7 diagnostic exact gate endpoint");
    const auto peer = reinterpret_cast<HWND>(session["hwnd"].toString().toULongLong());
    qputenv("DSE_S7_NONCE", gate.nonce.toUtf8()); const auto coordinator = ownerWindow();
    for (const auto& state : {owner, session}) {
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, DWORD(state["pid"].toInt()));
        require(h && WaitForSingleObject(h, 0) == WAIT_TIMEOUT, "S7 diagnostic retained exact scene lifetimes"); gate.children.push_back(h);
    }
    const auto check = [&](bool expected, quintptr previous) {
        const auto attempt = quintptr(GetPropW(gate.endpoint, L"S7DiagnosticAttempts"));
        require(PostMessageW(gate.endpoint, HarnessMessage, 1, 0), "S7 diagnostic request");
        require(wait([&] { gate.read(); return quintptr(GetPropW(gate.endpoint, L"S7DiagnosticAttempts")) > attempt; }), "S7 diagnostic exact attempt completed");
        const bool passed = quintptr(GetPropW(gate.endpoint, L"S7Checked")) > previous && !GetPropW(gate.endpoint, L"S7GateFailed");
        if (passed != expected) std::cerr << gate.bytes.constData();
        require(passed == expected, "S7 diagnostic expected outcome");
        require(gate.process.state() == QProcess::Running && IsWindow(peer)
            && std::all_of(gate.children.begin(), gate.children.end(), [](HANDLE h) { return WaitForSingleObject(h, 0) == WAIT_TIMEOUT; }),
            "S7 failed/valid diagnostic preserves the scene, Session and owner");
    };
    check(false, 0); // No Shell transition yet: real gate diagnostic fails, but retains all roles.
    std::cout << "S7 diagnostic-only SYNTHETIC Shell injection; no Explorer restart" << std::endl;
    require(PostMessageW(coordinator, HarnessMessage, 3, 0), "S7 diagnostic synthetic Shell seam");
    check(true, 0); // Continuously healthy HiddenToTray is accepted.
    const auto checked = quintptr(GetPropW(gate.endpoint, L"S7Checked"));
    require(PostMessageW(peer, HarnessMessage, 15, 0) && wait([&] { return IsWindowVisible(peer); }), "S7 unexplained Show setup");
    check(false, checked); // No witnessed qualification loss: visible state must NOT pass.
    require(PostMessageW(peer, HarnessMessage, 2, 0) && wait([&] { return !IsWindowVisible(peer); }), "S7 requalified test Hide");
    require(PostMessageW(coordinator, HarnessMessage, 11, 0) && wait([&] { return IsWindowVisible(peer); }), "S7 actual tray qualification loss surfaces Session");
    require(PostMessageW(coordinator, HarnessMessage, 12, 0), "S7 qualification recovery without automatic Hide");
    check(true, checked); // Freshly healthy control plus witnessed fail-visible is also accepted.
    require(wait([&] { gate.read(); return gate.bytes.contains("qualification-loss fail-visible; no automatic re-hide"); }), "S7 safety-path witness reported");
    const auto finalCheck = quintptr(GetPropW(gate.endpoint, L"S7Checked"));
    require(PostMessageW(peer, HarnessMessage, 16, 0) && wait([&] { return GetPropW(peer, L"S7HarnessFailed") || !IsWindow(peer); }),
        "S7 deterministic child diagnostic exception dispatched");
    check(true, finalCheck); // Even a child-side diagnostic failure must preserve state and answer the next real query.
    require(PostMessageW(gate.endpoint, HarnessMessage, 2, 0) && wait([&] { return gate.process.state() == QProcess::NotRunning; }), "S7 explicit normal gate teardown");
    gate.read(); require(gate.process.exitCode() == 0 && gate.bytes.contains("GATE NORMAL CLEANUP PASS"), "S7 all diagnostic-owned roles close normally");
}
void priorKeyRecovery(const QString& mode)
{
    const auto nonce = QUuid::createUuid().toString(); qputenv("DSE_S7_NONCE", nonce.toUtf8());
    const auto display = nonce + "-prior-key";
    Child c({"owner"}, "OWNER READY"); Child a({"peer", display}, "PEER READY");
    const auto before = a.query();
    c.send(6); // Real production requestRoleClose -> ordinary dirty Close/Pending dialog.
    require(wait([&] { return a.query()["closeOperation"].toObject()["result"].toInt(-1) == int(RoleCloseResult::Pending); }),
        "S7 prior real generation A ordinary Close Pending");
    const auto oldOperation = a.state["closeOperation"].toObject();
    require(oldOperation["generation"] == before["epoch"] && oldOperation["request"].toString().toULongLong()
        && a.state["closeCount"].toInt() == 1, "S7 prior exact generation A request/incarnation and one actual Close");
    a.send(24); a.query(); // Export actual key/role to the replacement child environment, retain the actual A pipe.
    if (mode != "prior-key-pending") {
        a.send(7); require(wait([&] { return a.query()["closeOperation"].toObject()["result"].toInt(-1) == int(RoleCloseResult::CloseAborted); }),
            "S7 prior original Cancel yields retained terminal result before retirement");
        require(wait([&] { return c.query()["currentClose"].toObject()["result"].toInt(-1) == int(RoleCloseResult::CloseAborted); }),
            "S7 prior original terminal result actually reached generation A");
    }
    c.crash(); // Only the exact test-owned A; no Shell operation.
    Replacement replacement; replacement.capture(c.pid);
    require(wait([&] { return a.query()["qualified"].toBool(); }), "S7 prior same role authenticated/qualified rejoin B");
    require(a.state["epoch"] != before["epoch"] && a.state["owner"].toInt() == int(replacement.pid)
        && a.state["incarnation"] == before["incarnation"] && a.state["engine"] == before["engine"],
        "S7 prior genuine retired A, new B, exact same incarnation/Engine");
    const auto observe = [&] {
        const auto sequence = quintptr(GetPropW(replacement.hwnd, L"S7PriorInspection"));
        require(PostMessageW(replacement.hwnd, HarnessMessage, 10, 0) && wait([&] {
            return quintptr(GetPropW(replacement.hwnd, L"S7PriorInspection")) > sequence; }), "S7 prior native B observation");
    };
    const auto property = [&](const wchar_t* name) { return int(quintptr(GetPropW(replacement.hwnd, name))) - 1; };
    observe();
    require(property(L"S7PriorResult") == -1 && property(L"S7PriorReplies") == 0, "S7 B does not inherit old Close ledger/batch");
    require(PostMessageW(replacement.hwnd, HarnessMessage, 14, 0), "S7 fresh B same-prior-key QueryState dispatched");
    const auto expected = mode == "prior-key-pending" ? RoleCloseResult::Pending : RoleCloseResult::CloseAborted;
    const bool queried = wait([&] { observe(); return property(L"S7PriorReplies") == 1 && property(L"S7PriorResult") == int(expected); }, 3500);
    std::cout << "PRIOR real A=" << oldOperation["generation"].toString().toStdString()
        << " request=" << oldOperation["request"].toString().toStdString() << " B=" << a.state["epoch"].toString().toStdString()
        << " incarnation=" << a.state["incarnation"].toString().toStdString() << " wireReplies=" << property(L"S7PriorReplies")
        << " result=" << property(L"S7PriorResult") << std::endl;
    require(queried && property(L"S7PriorWireResult") == int(expected),
        "S7 prior RED: fresh qualified B query of ACTUAL retired A key must return retained result over native pipe");
    require(property(L"S7PriorCurrentStateReplies") == 1, "S7 actual prior-key reply followed by CURRENT generation State, not old projection authority");
    if (mode == "prior-key-pending") {
        a.send(7); require(wait([&] { return a.query()["closeOperation"].toObject()["result"].toInt(-1) == int(RoleCloseResult::CloseAborted); }),
            "S7 prior original Pending finishes after replacement, without second Close");
        observe(); require(property(L"S7PriorReplies") == 1 && property(L"S7PriorResult") == int(RoleCloseResult::Pending),
            "S7 old completion after B is not unsolicited new-generation authority");
        require(PostMessageW(replacement.hwnd, HarnessMessage, 14, 0) && wait([&] {
            observe(); return property(L"S7PriorReplies") == 2 && property(L"S7PriorResult") == int(RoleCloseResult::CloseAborted); }),
            "S7 explicit fresh same-key query observes retained post-replacement Cancel");
    }
    observe(); const auto priorReplies = property(L"S7PriorReplies");
    require(PostMessageW(replacement.hwnd, HarnessMessage, 16, 0) && wait([&] {
        observe(); return property(L"S7PriorReplies") == priorReplies + 2; }), "S7 two current same-key queries explicitly reply, no blind retry");
    const auto preserved = a.query();
    require(preserved["closeCount"].toInt() == 1 && preserved["closeOperation"].toObject()["result"].toInt(-1) == int(RoleCloseResult::CloseAborted)
        && preserved["qualified"].toBool() && !preserved["hidden"].toBool(), "S7 queries neither execute Close nor overwrite current role state");
    replacement.inspect(1); // Existing exact registry/no-old-batch/no-duplicate-owner assertion.
    if (mode == "prior-key-fencing") {
        a.send(26); require(a.query()["qualified"].toBool(), "S7 retired native connection fails while B stays qualified");
        require(PostMessageW(replacement.hwnd, HarnessMessage, 17, 0), "S7 old mutation frames sent adversarially");
        a.send(14); require(wait([&] { return a.query()["qualified"].toBool() && !a.state["renewal"].toBool(); })
            && a.state["closeCount"].toInt() == 1 && !a.state["hidden"].toBool(), "S7 retired Show/Permit/Deploy/Undeploy/Close/state authority rejected");
        observe(); const auto malformedReplies = property(L"S7PriorReplies");
        require(PostMessageW(replacement.hwnd, HarnessMessage, 19, 0), "S7 malformed zero prior request sent");
        a.send(14); require(wait([&] { return a.query()["qualified"].toBool() && !a.state["renewal"].toBool(); }), "S7 malformed prior key cannot poison current qualification");
        observe(); require(property(L"S7PriorReplies") == malformedReplies, "S7 malformed prior key cannot acquire query authority");
        a.send(25); require(!a.query()["qualified"].toBool(), "S7 prior qualification loss prerequisite");
        require(PostMessageW(replacement.hwnd, HarnessMessage, 14, 0), "S7 unqualified old query attempted");
        observe(); observe(); require(property(L"S7PriorReplies") == malformedReplies, "S7 unqualified channel cannot use prior-key exception");
        a.send(14); require(wait([&] { return a.query()["qualified"].toBool(); }), "S7 authentic fresh qualification restored");
        observe(); require(property(L"S7PriorReplies") == malformedReplies,
            "S7 post-renewal wire-order barrier confirms unqualified prior Query produced no response");
    }
    if (mode != "prior-key-terminal") {
        require(PostMessageW(replacement.hwnd, HarnessMessage, 15, 0) && wait([&] {
            observe(); return property(L"S7UnknownReplies") == 1; }), "S7 valid unknown request in ACTUAL prior epoch replies, not silent drop");
        require(property(L"S7UnknownWireResult") == int(RoleCloseResult::Uncertain) && a.query()["closeCount"].toInt() == 1,
            "S7 unknown prior key explicitly Uncertain, never Rejected/RoleGone or second Close");
    }
    require(PostMessageW(replacement.hwnd, HarnessMessage, 21, 0) && wait([&] {
        observe(); return quintptr(GetPropW(replacement.hwnd, L"S7Roles")) == 3; }), "S7 normal Launcher role keeps B alive while Session admission changes");
    a.send(6); require(wait([&] { return a.process.state() == QProcess::NotRunning; }) && a.process.exitCode() == 0,
        "S7 prior original Session normal fixture cleanup");
    require(wait([&] { observe(); return quintptr(GetPropW(replacement.hwnd, L"S7Count")) == 1; }), "S7 exact original role death observed");
    const auto deadReceipt = property(L"S7PriorResult");
    require(deadReceipt == int(mode == "prior-key-terminal" ? RoleCloseResult::CloseAborted : RoleCloseResult::RoleGone),
        "S7 exact death preserves known actual-key terminal receipt / reconciles uncertain role receipt");
    require(property(L"S7PriorKeyMatchesOriginal") == (mode == "prior-key-terminal" ? 1 : 0),
        "S7 terminal receipt retains exact A key; RoleGone receipt explicitly belongs to later unknown key");
    // Same owner, real release/acquire of the exact Display Session admission slot.
    Child fresh({"peer", display}, "PEER READY");
    require(fresh.query()["incarnation"] != before["incarnation"] && fresh.state["displayId"] == before["displayId"],
        "S7 same DisplayId replacement is a NEW native role incarnation");
    require(PostMessageW(replacement.hwnd, HarnessMessage, 14, 0), "S7 query departed original incarnation");
    observe(); require(property(L"S7PriorResult") == deadReceipt && fresh.query()["closeCount"].toInt() == 0,
        "S7 old-role receipt never routes to same-display replacement or PID identity");
    require(PostMessageW(replacement.hwnd, HarnessMessage, 18, 0) && wait([&] {
        observe(); return property(L"S7CurrentPriorReplies") == 1 && property(L"S7CurrentResult") == int(RoleCloseResult::Uncertain)
            && property(L"S7CurrentPriorWireResult") == int(RoleCloseResult::Uncertain); }), "S7 new incarnation explicitly replies Uncertain, not inherited old Close result");
    require(fresh.query()["closeCount"].toInt() == 0 && fresh.state["qualified"].toBool(), "S7 new-role prior observation is not old mutation");
    if (mode == "prior-key-fencing") {
        require(PostMessageW(replacement.hwnd, HarnessMessage, 20, 0) && wait([&] { return !fresh.query()["qualified"].toBool(); }),
            "S7 wrong incarnation rejected on current native pipe");
    }
    fresh.send(6); require(wait([&] { return fresh.process.state() == QProcess::NotRunning; }) && fresh.process.exitCode() == 0,
        "S7 prior same-display replacement normal cleanup");
    require(wait([&] { observe(); return quintptr(GetPropW(replacement.hwnd, L"S7Count")) == 1; }), "S7 last Session native death before prior query/drain overlap");
    require(PostMessageW(replacement.hwnd, HarnessMessage, 22, 0), "S7 prior query overlaps actual zero-role normal drain");
    require(WaitForSingleObject(replacement.process, 3000) == WAIT_OBJECT_0, "S7 prior query bookkeeping cannot keep zero-role owner alive");
}
void run(const QString& mode)
{
    if (mode.startsWith("prior-key-")) { priorKeyRecovery(mode); return; }
    if (mode == "production-recovery") { productionRecovery(); return; }
    if (mode == "gate-diagnostics") { gateDiagnosticTest(); return; }
    const auto nonce = QUuid::createUuid().toString(); qputenv("DSE_S7_NONCE", nonce.toUtf8());
    Child c({"owner"}, "OWNER READY"); Child a({"peer", nonce + "-A"}, "PEER READY");
    std::unique_ptr<Child> b;
    if (mode == "concurrent" || mode == "batch-death") b = std::make_unique<Child>(QStringList{"peer", nonce + "-B"}, "PEER READY");
    const auto before = a.query(); const auto oldEpoch = before["epoch"].toString();
    require(c.query()["count"].toInt() == (b ? 2 : 1), "S7 registry prerequisite");
    if (mode == "maximized-loss" || mode == "minimized-max-loss")
    { a.send(12); require(wait([&] { return IsZoomed(a.hwnd); }), "S7 native maximized prerequisite");
        if (mode == "minimized-max-loss") { a.send(13); require(wait([&] { return IsIconic(a.hwnd); }), "S7 native minimized-maximized prerequisite"); } }
    a.hide(); if (b) b->hide();
    QWidget distraction; distraction.setWindowTitle("S7 independent foreground prerequisite"); distraction.show();
    surfaceAdmittedRole(distraction);
    require(wait([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(distraction.winId()); }, 2000),
        "S7 foreground prerequisite: independent test window must own foreground before loss");
    const auto foreground = GetForegroundWindow();
    std::cout << "FOREGROUND qualified HWND=" << quintptr(foreground) << " parent=" << GetCurrentProcessId() << std::endl;
    std::cout << "PRE-LOSS " << QJsonDocument(a.query()).toJson(QJsonDocument::Compact).constData() << std::endl;
    const auto focusPreserved = [&] {
        const auto after = GetForegroundWindow(); DWORD pid = 0; if (after) GetWindowThreadProcessId(after, &pid);
        wchar_t title[256]{}; if (after) GetWindowTextW(after, title, 256);
        std::cout << "FOREGROUND after HWND=" << quintptr(after) << " pid=" << pid << " title=" << QString::fromWCharArray(title).toStdString() << std::endl;
        if (after != foreground) { a.read(); std::cerr << a.bytes.constData(); }
        return after == foreground;
    };
    if (mode == "relaunch")
    {
        QProcess relaunch; relaunch.start(QCoreApplication::applicationFilePath(), {"ordinary-relaunch"}); require(relaunch.waitForStarted(3000), "S7 ordinary route startup");
        require(wait([&] { return relaunch.state() == QProcess::NotRunning; }) && relaunch.exitCode() == 0, "S7 ACTIVATE delivery");
        require(wait([&] { return IsWindowVisible(a.hwnd); }), "S7 RED E: ordinary relaunch left registered unresolved Session hidden");
        require(a.query()["owner"].toInt() == int(c.pid) && c.query()["count"].toInt() == 1, "S7 no duplicate/rebind");
    }
    else if (mode == "shell" || mode == "shell-pending")
    {
        if (mode == "shell-pending") { c.send(6); require(wait([&] { return IsWindowVisible(a.hwnd); }) && a.query()["close"].toBool(), "S7 real Pending Shell prerequisite"); }
        c.send(3); const auto responded = wait([&] { c.read(); return c.bytes.contains("SHELL RECOVERED") || c.process.state() == QProcess::NotRunning; });
        if (!responded) std::cerr << c.bytes.constData(); require(responded, "S7 shell response");
        if (c.process.state() != QProcess::Running) std::cerr << c.bytes.constData();
        require(c.bytes.contains("SHELL RECOVERED"), "S7 RED F: no shell adapter transition");
        require(wait([&] { c.read(); return c.bytes.contains("[Tray] Shell projection refreshed epoch=" + oldEpoch.toUtf8()); }),
            "S7 actual queued production Shell projection refresh executed");
        require(a.query()["hidden"].toBool() == (mode != "shell-pending") && a.state["qualified"].toBool() && a.state["epoch"].toString() == oldEpoch
            && trayWindows(c.pid) == 1 && trayWindows(a.pid) == 0, "S7 shell-only same owner/role/qualification; one logical icon");
        if (mode == "shell-pending") { a.send(7); require(wait([&] { return !IsWindowVisible(a.hwnd); }), "Shell-only healthy qualification preserves Cancel re-hide"); }
    }
    else if (mode == "disconnect" || mode == "maximized-loss" || mode == "minimized-max-loss")
    {
        c.send(2); require(wait([&] { return IsWindowVisible(a.hwnd); }), "S7 control loss fail-visible");
        require(c.query()["count"].toInt() == 1 && c.state["available"].toInt() == 1 && !a.query()["qualified"].toBool(), "S7 live unavailable retained, not death");
        require(focusPreserved(), "S7 fail-visible must not activate");
        require(!a.state["retrying"].toBool(), "S7 no replacement on live channel loss");
        if (mode != "disconnect") require(IsZoomed(a.hwnd) && !IsIconic(a.hwnd), "S7 fail-visible native maximized state preserved");
    }
    else
    {
        if (mode == "failed") { a.send(4); a.query(); }
        if (mode == "disconnect-death") { a.send(3); require(wait([&] { return IsWindowVisible(a.hwnd); }), "Disconnect before exact death"); }
        if (mode == "pending-death" || mode == "batch-death")
        { if (mode == "batch-death") c.send(5); else c.send(6);
            require(wait([&] { return IsWindowVisible(a.hwnd); }) && a.query()["close"].toBool(), "Actual role-local Close interaction before owner death"); }
        c.crash(); require(wait([&] { return IsWindowVisible(a.hwnd); }), "S7 verified death fail-visible");
        if (mode != "pending-death" && mode != "batch-death") require(focusPreserved(), "S7 death recovery no focus theft");
        if (mode == "failed")
        {
            require(wait([&] { const auto s = a.query(); return !s["retrying"].toBool() && s["retired"].toBool(); }),
                "S7 RED H: bounded failed recovery did not retire dead owner");
            require(!ownerWindow() && !a.state["qualified"].toBool() && IsWindowVisible(a.hwnd), "S7 failed bootstrap accessible/no owner");
            a.query(); a.read(); require(a.bytes.count("BOOTSTRAP FAILURE") == 1, "S7 exactly one failed launch attempt, no restart loop");
        }
        else
        {
            Replacement replacement; replacement.capture(c.pid);
            require(wait([&] { return a.query()["qualified"].toBool(); }), "S7 RED A/C: live Session did not rejoin");
            require(a.state["epoch"].toString() != oldEpoch && a.state["owner"].toInt() == int(replacement.pid)
                && a.state["incarnation"] == before["incarnation"] && a.state["engine"] == before["engine"] && !a.state["hidden"].toBool(),
                "S7 fresh generation, same role/Engine, no automatic re-hide");
            if (b) require(wait([&] { return b->query()["qualified"].toBool() && b->state["owner"].toInt() == int(replacement.pid); }), "S7 concurrent unique replacement/rejoin");
            replacement.inspect(b ? 2 : 1);
            require(trayWindows(replacement.pid) == 1 && trayWindows(a.pid) == 0, "S7 one replacement tray owner");
            if (mode == "fencing") { a.send(5); require(wait([&] { return a.query()["qualified"].toBool() && !a.state["renewal"].toBool(); })
                && !a.state["close"].toBool(), "S7 RED D: stale frame poisoned current qualification/state"); }
            if (mode == "pending-death" || mode == "batch-death") {
                require(a.state["close"].toBool() && quintptr(GetPropW(a.hwnd, L"S7Closes")) == 2,
                    "S7 one original local Close only; new owner must not reissue it or migrate batch");
                a.send(7); require(wait([&] { return a.query()["qualified"].toBool(); }), "S7 old Pending completion does not poison new channel");
                if (b) require(quintptr(GetPropW(b->hwnd, L"S7Closes")) == 1, "S7 old batch remainder was not closed by replacement owner");
            }
            a.hide(); require(a.query()["qualified"].toBool(), "S7 fresh-generation Hide");
            a.send(6); if (b) b->send(6);
            require(wait([&] { return a.process.state() == QProcess::NotRunning && (!b || b->process.state() == QProcess::NotRunning); }), "S7 normal peers teardown");
            return;
        }
    }
    a.send(6); require(wait([&] { return a.process.state() == QProcess::NotRunning; }), "S7 normal peer teardown");
    if (c.process.state() != QProcess::NotRunning) c.send(9);
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv); QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(":/icons/DesktopSceneEngine.ico"));
    try
    {
        require(argc >= 2, "S7 named case required"); const QString mode = QString::fromLocal8Bit(argv[1]);
        if (mode == "owner" || mode == CoordinatorBootstrapArgument) return owner(mode == CoordinatorBootstrapArgument);
        if (mode == "peer" && argc == 3) return peer(QString::fromLocal8Bit(argv[2]));
        if (mode == "ordinary-relaunch") return forwardActivation(launcherSlot()).outcome == ActivationForwardOutcome::Delivered ? 0 : 1;
        if (mode == "human") return humanGate();
        if (mode == "frame-baseline") return humanGate(true);
        if ((mode == "human-check" || mode == "human-finish") && argc == 3)
            return gateCommand(QString::fromLocal8Bit(argv[2]), mode == "human-check" ? 1 : 2);
        run(mode); std::cout << "S7 " << mode.toStdString() << " PASS" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "S7 FAIL: " << e.what() << std::endl; return 1; }
}
