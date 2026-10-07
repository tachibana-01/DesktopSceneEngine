#include "InstanceAdmission.h"
#include "InstanceActivation.h"

#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <windows.h>

#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <type_traits>

namespace
{
using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;
bool require(bool value, const char* message)
{
    if (!value) std::cerr << message << std::endl;
    return value;
}
bool waitUntil(const std::function<bool()>& check, int timeout = 5000)
{
    QElapsedTimer time; time.start();
    do
    {
        QApplication::processEvents();
        if (check()) return true;
        QThread::msleep(5);
    } while (time.elapsed() < timeout);
    return false;
}
InstanceSlot sessionSlot(const QString& suffix)
{
    return *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{
        DisplayId::fromTargetDevicePath(suffix.toStdString())}, *currentInteractiveInstanceScope());
}
QString nonce() { return "P0-test-" + QUuid::createUuid().toString(); }
void startChild(QProcess& child, const QStringList& args)
{
    auto env = QProcessEnvironment::systemEnvironment();
    const auto path = env.value("Path", env.value("PATH"));
    env.remove("PATH"); env.remove("Path"); env.insert("Path", path);
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), args);
    if (!child.waitForStarted(5000)) throw std::runtime_error("Test child failed to start");
}
struct OwnerChild
{
    QProcess process;
    QByteArray output;
    void read() { output += process.readAllStandardOutput(); }
    ~OwnerChild()
    {
        if (process.state() == QProcess::NotRunning) return;
        process.kill(); process.waitForFinished(5000); // This test's isolated resource-owning fixture only.
    }
};
int childRole(const QString& identity, int delay, int budget)
{
    const auto slot = sessionSlot(identity);
    InstanceAdmissionLease lease;
    const auto outcome = lease.acquire(slot);
    if (outcome == InstanceAdmissionOutcome::Occupied)
    {
        const auto result = forwardActivation(slot, budget);
        std::cout << "FORWARDER owned=0 result=" << static_cast<int>(result.outcome)
            << " permission=" << result.foregroundPermissionGranted << std::endl;
        return result.outcome == ActivationForwardOutcome::Delivered ? 0 : 2;
    }
    if (!lease.ownsAdmission()) return 3;
    std::cout << "LEASE" << std::endl;
    InstanceActivationEndpoint endpoint(slot);
    QWidget role;
    role.setWindowTitle("P0 test-owned role");
    endpoint.bindReadyRole(role);
    QTimer::singleShot(delay, &endpoint, [&] {
        if (!endpoint.listen()) { QCoreApplication::exit(4); return; }
        std::cout << "ENDPOINT" << std::endl;
    });
    QTimer::singleShot(15000, &endpoint, [&] { endpoint.beginShutdown(); QCoreApplication::quit(); });
    const int result = QCoreApplication::exec();
    endpoint.beginShutdown();
    return result;
}
bool keys()
{
    const auto scope = *currentInteractiveInstanceScope();
    const auto id = DisplayId::fromTargetDevicePath("display:\\路线\nA");
    const auto a = *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{id}, scope);
    const auto same = *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{id}, scope);
    auto otherUser = scope; otherUser.userSid += "-other";
    auto otherSession = scope; ++otherSession.windowsSessionId;
    const auto b = sessionSlot("display:\\路线\na");
    const auto longSlot = sessionSlot(QString(20000, QChar('x')));
    return require(a.mutexName() == same.mutexName() && a.endpointName() == same.endpointName(), "Exact identity derives stable names")
        && require(a.canonicalIdentity() == QByteArray::fromStdString(*id.canonicalRepresentation()), "Canonical UTF-8 must survive hashing")
        && require(a.mutexName() != b.mutexName(), "Case and Unicode must not be normalized")
        && require(a.endpointName() != InstanceSlot::fromProcessMode(DisplaySessionProcessMode{id}, otherUser)->endpointName(), "Pipe user scope is explicit")
        && require(a.endpointName() != InstanceSlot::fromProcessMode(DisplaySessionProcessMode{id}, otherSession)->endpointName(), "Pipe Windows session scope is explicit")
        && require(longSlot.mutexName().size() < 256 && longSlot.endpointName().size() < 256 && !activationRequestFrame(longSlot).isEmpty(), "Long identity needs bounded native names, not truncation")
        && require(!InstanceSlot::fromProcessMode(DisplaySessionProcessMode{DisplayId{}}, scope)
            && !InstanceSlot::fromProcessMode(DisplaySessionProcessMode{DisplayId::fromSourceDeviceName("DISPLAY1")}, scope)
            && !InstanceSlot::fromProcessMode(ProcessModeParseError::InvalidArguments, scope), "Invalid/non-owning identity fails closed")
        && require(InstanceSlot::fromProcessMode(LauncherProcessMode{}, scope)->mutexName() != a.mutexName(), "Launcher and Session slots differ");
}
bool ownership()
{
    static_assert(!std::is_move_constructible_v<InstanceAdmissionLease>);
    const auto slot = sessionSlot(nonce());
    HANDLE existing = CreateMutexW(nullptr, FALSE, slot.mutexName().toStdWString().c_str());
    if (!existing) return false;
    bool ok;
    {
        InstanceAdmissionLease lease;
        ok = require(lease.acquire(slot) == InstanceAdmissionOutcome::Acquired, "Existing but unowned mutex is free");
        InstanceAdmissionOutcome result = InstanceAdmissionOutcome::Error;
        std::thread contender([&] { InstanceAdmissionLease other; result = other.acquire(slot); }); contender.join();
        ok = require(result == InstanceAdmissionOutcome::Occupied, "Another thread cannot own a held slot") && ok;
        ok = require(lease.acquire(slot) == InstanceAdmissionOutcome::Error && lease.ownsAdmission(), "No recursive acquisition or early release") && ok;
    }
    InstanceAdmissionLease afterTeardown;
    ok = require(afterTeardown.acquire(slot) == InstanceAdmissionOutcome::Acquired, "Normal teardown releases ownership while another handle retains the object") && ok;
    CloseHandle(existing);
    return ok;
}
bool errors()
{
    const auto slot = sessionSlot(nonce());
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, slot.mutexName().toStdWString().c_str());
    InstanceAdmissionLease lease;
    const auto result = lease.acquire(slot);
    CloseHandle(event);
    return require(result == InstanceAdmissionOutcome::Error && !lease.ownsAdmission()
        && lease.nativeError() != 0, "Native object collision fails closed");
}
bool abandoned()
{
    const auto id = nonce(); const auto slot = sessionSlot(id);
    OwnerChild owner;
    startChild(owner.process, {"--child", id, "0", "3000"});
    if (!waitUntil([&] { owner.read(); return owner.output.contains("LEASE"); })) return false;
    HANDLE retained = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, slot.mutexName().toStdWString().c_str());
    if (!retained) return false;
    owner.process.kill();
    const bool died = owner.process.waitForFinished(5000);
    InstanceAdmissionLease recovery;
    const auto result = recovery.acquire(slot);
    CloseHandle(retained);
    return require(died && result == InstanceAdmissionOutcome::AbandonedAndAcquired && recovery.ownsAdmission(), "Crash grants real abandoned ownership without cleanup");
}
bool startupGap(bool timeout)
{
    const auto id = nonce();
    OwnerChild owner;
    startChild(owner.process, {"--child", id, timeout ? "5000" : "400", "3000"});
    if (!waitUntil([&] { owner.read(); return owner.output.contains("LEASE"); })) return false;
    QProcess loser;
    QElapsedTimer time; time.start();
    startChild(loser, {"--child", id, "0", timeout ? "200" : "3000"});
    const bool done = loser.waitForFinished(5000);
    const auto output = loser.readAllStandardOutput();
    std::cout << output.constData() << "elapsedMs=" << time.elapsed() << std::endl;
    return require(done && loser.exitCode() == (timeout ? 2 : 0) && output.contains("owned=0")
        && !output.contains("LEASE") && owner.process.state() != QProcess::NotRunning
        && time.elapsed() < (timeout ? 2000 : 5000), "Startup gap forwards or fails boundedly, never admits loser");
}
bool exchange(const InstanceSlot& slot, const QByteArray& bytes, bool expectedAck, bool fragmented = false)
{
    QLocalSocket client; client.connectToServer(slot.endpointName());
    if (!client.waitForConnected(1000)) return false;
    QApplication::processEvents();
    if (fragmented)
    {
        client.write(bytes.left(2)); client.flush(); QApplication::processEvents();
        client.write(bytes.mid(2, 4)); client.flush(); QApplication::processEvents();
        client.write(bytes.mid(6));
    }
    else client.write(bytes);
    client.flush();
    QByteArray ack;
    const bool closed = waitUntil([&] { ack += client.readAll(); return client.state() == QLocalSocket::UnconnectedState; }, 3500);
    ack += client.readAll();
    return require(closed && (expectedAck ? ack == "DSE-ACTIVATE-ACK-1" : ack.isEmpty()), "Request completes/cancels with exactly expected ACK");
}
bool protocol()
{
    const auto slot = sessionSlot(nonce());
    InstanceAdmissionLease lease;
    if (lease.acquire(slot) != InstanceAdmissionOutcome::Acquired) return false;
    InstanceActivationEndpoint endpoint(slot);
    QWidget role; endpoint.bindReadyRole(role);
    if (!endpoint.listen()) return false;
    const auto frame = activationRequestFrame(slot);
    QByteArray oversized(4, '\0'); qToBigEndian<quint32>(MaximumActivationBodyBytes + 1, oversized.data());
    auto wrongCommand = frame; wrongCommand[4] = 'X';
    const auto wrongRole = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope());
    for (const auto& invalid : {QByteArray{}, QByteArray(4, '\0'), oversized, frame.left(7), wrongCommand,
        activationRequestFrame(sessionSlot(nonce())), activationRequestFrame(wrongRole), frame + frame})
    {
        role.hide();
        if (!exchange(slot, invalid, false) || role.isVisible()) return false;
    }
    { QLocalSocket client; client.connectToServer(slot.endpointName()); client.waitForConnected(1000); QApplication::processEvents(); client.abort(); }
    QApplication::processEvents();
    role.hide();
    if (!exchange(slot, frame, true, true) || !role.isVisible()) return false;
    role.showMaximized(); role.showMinimized();
    if (!exchange(slot, frame, true) || role.isMinimized() || !role.isMaximized()) return false;
    QDialog modal(&role); modal.setWindowModality(Qt::ApplicationModal); modal.show(); QApplication::processEvents();
    if (!exchange(slot, frame, true) || QApplication::activeModalWidget() != &modal) return false;
    modal.close(); endpoint.beginShutdown(); return true;
}
bool capacity()
{
    const auto slot = sessionSlot(nonce());
    InstanceActivationEndpoint endpoint(slot);
    QWidget role; endpoint.bindReadyRole(role);
    if (!endpoint.listen()) return false;
    std::vector<std::unique_ptr<QLocalSocket>> clients;
    for (int i = 0; i < MaximumActivationClients + 4; ++i)
    {
        auto socket = std::make_unique<QLocalSocket>(); socket->connectToServer(slot.endpointName());
        if (!socket->waitForConnected(1000)) return false;
        QApplication::processEvents(); clients.push_back(std::move(socket));
    }
    if (!require(endpoint.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly).size() <= MaximumActivationClients, "Idle footprint is bounded")) return false;
    if (!waitUntil([&] { return endpoint.findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly).isEmpty(); }, 3500)) return false;
    return exchange(slot, activationRequestFrame(slot), true);
}
bool nativeModal(bool nested)
{
    QWidget role;
    role.setWindowTitle("P0 test-owned native-dialog role");
    role.show();
    QApplication::processEvents();
    const auto roleHwnd = reinterpret_cast<HWND>(role.winId());
    QDialog outer(&role);
    if (nested)
    {
        outer.setWindowModality(Qt::ApplicationModal);
        outer.show();
        QApplication::processEvents();
    }
    HWND other = CreateWindowExW(0, L"STATIC", L"P0 test-owned activation distraction",
        WS_OVERLAPPEDWINDOW, 100, 100, 250, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!other) return false;
    ShowWindow(other, SW_SHOWNOACTIVATE);
    QWidget* nativeDialogOwner = nested ? static_cast<QWidget*>(&outer) : &role;
    QFileDialog dialog(nativeDialogOwner, "P0 test-owned native Open dialog");
    dialog.setFileMode(QFileDialog::ExistingFile);
    bool observed = false;
    bool ok = false;
    QElapsedTimer deadline; deadline.start();
    QTimer probe;
    probe.setInterval(50);
    QObject::connect(&probe, &QTimer::timeout, &dialog, [&] {
        const auto popup = GetLastActivePopup(roleHwnd);
        wchar_t windowClass[32]{};
        if (popup && popup != roleHwnd && IsWindowVisible(popup)) GetClassNameW(popup, windowClass, 32);
        if (wcscmp(windowClass, L"#32770") == 0)
        {
            probe.stop();
            observed = true;
            const bool wrapperHidden = dialog.testAttribute(Qt::WA_DontShowOnScreen)
                && !IsWindowVisible(reinterpret_cast<HWND>(dialog.winId()));
            const bool distracted = SetForegroundWindow(other) && GetForegroundWindow() == other;
            surfaceAdmittedRole(role);
            ok = require(wrapperHidden, "Native QFileDialog uses a hidden Qt modal wrapper")
                && require(distracted, "Fixture must move foreground away from the native modal")
                && require(GetForegroundWindow() == popup && !IsWindowEnabled(reinterpret_cast<HWND>(nativeDialogOwner->winId()))
                    && IsWindowVisible(popup), "ACTIVATE must surface the real native modal, not its hidden Qt wrapper or disabled parent");
            PostMessageW(popup, WM_CLOSE, 0, 0);
        }
        else if (deadline.elapsed() > 5000)
        {
            probe.stop();
            dialog.reject();
        }
    });
    probe.start();
    dialog.exec();
    probe.stop();
    DestroyWindow(other);
    return require(observed, "Fixture must observe the real Windows native file dialog") && ok;
}
bool shutdown()
{
    const auto slot = sessionSlot(nonce());
    InstanceAdmissionLease lease;
    if (lease.acquire(slot) != InstanceAdmissionOutcome::Acquired) return false;
    InstanceActivationEndpoint endpoint(slot);
    auto role = std::make_unique<QWidget>(); endpoint.bindReadyRole(*role);
    if (!endpoint.listen()) return false;
    QLocalSocket client; client.connectToServer(slot.endpointName()); client.waitForConnected(1000); QApplication::processEvents();
    client.write(activationRequestFrame(slot)); client.flush();
    endpoint.beginShutdown(); role.reset(); QApplication::processEvents();
    InstanceAdmissionOutcome other = InstanceAdmissionOutcome::Error;
    std::thread contender([&] { InstanceAdmissionLease next; other = next.acquire(slot); }); contender.join();
    return require(other == InstanceAdmissionOutcome::Occupied && lease.ownsAdmission() && client.readAll().isEmpty(), "Shutdown cancels queued ACTIVATE while lease remains held through all teardown");
}
bool readyGap()
{
    const auto slot = sessionSlot(nonce());
    InstanceActivationEndpoint endpoint(slot);
    if (!endpoint.listen()) return false;
    QWidget role;
    QLocalSocket client; client.connectToServer(slot.endpointName()); client.waitForConnected(1000); QApplication::processEvents();
    client.write(activationRequestFrame(slot)); client.flush(); QApplication::processEvents();
    if (role.isVisible() || !client.readAll().isEmpty()) return false;
    endpoint.bindReadyRole(role);
    QByteArray ack;
    return waitUntil([&] { ack += client.readAll(); return ack == "DSE-ACTIVATE-ACK-1"; }) && role.isVisible();
}
bool busyPipe()
{
    const auto slot = sessionSlot(nonce());
    InstanceAdmissionLease lease;
    if (lease.acquire(slot) != InstanceAdmissionOutcome::Acquired) return false;
    const auto name = (QStringLiteral("\\\\.\\pipe\\") + slot.endpointName()).toStdWString();
    HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT, 1, 128, 128, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE) return false;
    OVERLAPPED pending{}; pending.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ConnectNamedPipe(server, &pending);
    HANDLE occupiedClient = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    QElapsedTimer time; time.start();
    const auto result = forwardActivation(slot, 200);
    const auto elapsed = time.elapsed();
    if (occupiedClient != INVALID_HANDLE_VALUE) CloseHandle(occupiedClient);
    CancelIoEx(server, &pending);
    CloseHandle(server); CloseHandle(pending.hEvent);
    return require(occupiedClient != INVALID_HANDLE_VALUE && result.outcome == ActivationForwardOutcome::TimedOut
        && result.nativeError == ERROR_PIPE_BUSY && elapsed < 1500 && lease.ownsAdmission(),
        "Busy native pipe must honor the forwarding deadline without Qt's inline five-second wait or duplicate admission");
}
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments();
    if (args.value(1) == "--child" && args.size() == 5) return childRole(args[2], args[3].toInt(), args[4].toInt());
    const auto name = args.value(1);
    bool ok = false;
    if (name == "keys") ok = keys();
    if (name == "ownership") ok = ownership();
    if (name == "errors") ok = errors();
    if (name == "abandoned") ok = abandoned();
    if (name == "startup-gap") ok = startupGap(false);
    if (name == "startup-timeout") ok = startupGap(true);
    if (name == "protocol") ok = protocol();
    if (name == "capacity") ok = capacity();
    if (name == "native-modal") ok = nativeModal(false);
    if (name == "native-modal-nested") ok = nativeModal(true);
    if (name == "shutdown") ok = shutdown();
    if (name == "ready-gap") ok = readyGap();
    if (name == "busy-pipe") ok = busyPipe();
    std::cout << name.toStdString() << (ok ? " PASS" : " FAIL") << std::endl;
    return ok ? 0 : 1;
}
