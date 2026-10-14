#include "ApplicationCoordinator.h"
#include "ApplicationPreferences.h"
#include "LauncherWindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

#include <windows.h>

#include <functional>
#include <iostream>
#include <stdexcept>

// Observe real QObject destruction/native socket events; no alternate Join implementation.
class ApplicationCoordinatorTestAccess
{
public:
    static QTimer& retry(DesktopSceneEngine::Player::SessionCoordinatorJoin& join) { return join.retry_; }
    static QTimer& deadline(DesktopSceneEngine::Player::SessionCoordinatorJoin& join) { return join.deadline_; }
    static QLocalSocket& socket(DesktopSceneEngine::Player::SessionCoordinatorJoin& join) { return join.socket_; }
};

namespace
{
using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

bool waitUntil(const std::function<bool()>& predicate, int timeout = 10000)
{
    QElapsedTimer timer; timer.start();
    do
    {
        QApplication::processEvents();
        if (predicate()) return true;
        QThread::msleep(5);
    } while (timer.elapsed() < timeout);
    return false;
}

InstanceSlot launcherSlot()
{
    return *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope());
}

InstanceSlot sessionSlot(const QString& identity)
{
    return *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{
        DisplayId::fromTargetDevicePath(identity.toStdString())}, *currentInteractiveInstanceScope());
}

bool exchange(const QString& endpoint, const QByteArray& frame, bool ack, bool fragmented = false)
{
    QLocalSocket socket;
    socket.connectToServer(endpoint);
    if (!socket.waitForConnected(1000)) return false;
    if (fragmented)
    {
        socket.write(frame.left(3)); socket.flush();
        QTimer::singleShot(10, &socket, [&] { socket.write(frame.mid(3)); });
    }
    else socket.write(frame);
    QByteArray reply;
    const bool done = waitUntil([&] {
        reply += socket.readAll();
        return socket.state() == QLocalSocket::UnconnectedState;
    }, 4000);
    const QByteArray prefix = "DSE-COORDINATOR-JOIN-ACK-1";
    return done && (ack ? reply.size() == prefix.size() + 16 && reply.startsWith(prefix)
        && !QUuid::fromRfc4122(reply.mid(prefix.size())).isNull() : reply.isEmpty());
}

int sessionFixture(const QString& identity, quintptr foreignWindow)
{
    const auto slot = sessionSlot(identity);
    const auto launcher = launcherSlot();
    InstanceAdmissionLease admission;
    require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "Fixture admission failed");
    InstanceActivationEndpoint activation(slot);
    require(activation.listen(), "Fixture ACTIVATE endpoint failed");
    QWidget role; role.setWindowTitle("S1 coordinator fixture Session"); role.show();
    activation.bindReadyRole(role);
    const auto incarnation = QUuid::createUuid();
    const auto frame = coordinatorJoinFrame(slot, launcher, incarnation, static_cast<quintptr>(role.winId()));
    const auto endpoint = coordinatorEndpointName(launcher);
    const auto nonceOffset = 4 + QByteArrayLiteral("DSE-COORDINATOR-JOIN-1\n").size();
    auto wrongGroup = frame;
    wrongGroup[nonceOffset + 24] = static_cast<char>(wrongGroup.at(nonceOffset + 24) ^ 1);
    require(exchange(endpoint, wrongGroup, false), "Wrong group must be rejected");
    require(exchange(endpoint, coordinatorJoinFrame(slot, launcher, incarnation, foreignWindow), false),
        "Supplied HWND from another process must be rejected");
    const auto foreign = sessionSlot(identity + "-foreign");
    require(exchange(endpoint, coordinatorJoinFrame(foreign, launcher, incarnation,
        static_cast<quintptr>(role.winId())), false), "Peer must own the exact Session activation/admission identity");
    auto nullNonce = frame;
    nullNonce.replace(nonceOffset, 16, QByteArray(16, '\0'));
    require(exchange(endpoint, nullNonce, false), "Null incarnation must be rejected");
    require(exchange(endpoint, QByteArray(4, '\0'), false), "Zero frame size must be rejected");
    QByteArray huge(4, '\0'); qToBigEndian<quint32>(MaximumActivationBodyBytes + 1, huge.data());
    require(exchange(endpoint, huge, false), "Oversized frame must be rejected without allocation");
    require(exchange(endpoint, frame, true, true), "Fragmented legitimate Join must be accepted");
    require(exchange(endpoint, frame, true), "Same incarnation duplicate must be idempotent");
    require(exchange(endpoint, coordinatorJoinFrame(slot, launcher, QUuid::createUuid(),
        static_cast<quintptr>(role.winId())), false), "Different incarnation must not replace a live role");
    auto invalidIdentity = frame; invalidIdentity[invalidIdentity.size() - 1] = '\0';
    require(exchange(endpoint, invalidIdentity, false), "Malformed exact identity must be rejected");
    std::cout << "Join protocol PASS" << std::endl;
    return QApplication::exec();
}

struct CoordinatorFixture
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("preferences.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore preferences{settings};
    ApplicationTranslationState translation;
    InstanceSlot slot = launcherSlot();
    InstanceAdmissionLease admission;
    InstanceActivationEndpoint activation{slot};
    int drains = 0;
    ApplicationCoordinator coordinator;

    explicit CoordinatorFixture(bool bootstrap = false)
        : coordinator(slot, activation, bootstrap, [this] { ++drains; })
    {
        require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "Existing coordinator: refuse interference");
        require(activation.listen() && coordinator.listen(), "Coordinator endpoints could not start");
        coordinator.setLauncherFactory([this] {
            auto* window = new LauncherWindow(translation, preferences);
            window->setAcceptedCloseCallback([this, window] { coordinator.retireLauncher(*window); });
            return window;
        });
    }
};

struct Child
{
    QProcess process;
    QByteArray output;
    void read() { output += process.readAllStandardOutput(); output += process.readAllStandardError(); }
    ~Child()
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill(); process.waitForFinished(5000); // Only this test-created fixture, never a user Session.
        }
    }
};

void uiLifetime()
{
    CoordinatorFixture fixture;
    auto& coordinator = fixture.coordinator;
    QPointer<QWidget> first = coordinator.showLauncher();
    const auto incarnation = coordinator.launcherIncarnation();
    require(first && !incarnation.isNull() && first->isVisible(), "Real Launcher must be visible and owned");
    require(first->close(), "Ordinary Launcher close must be accepted");
    require(waitUntil([&] { return !first; }), "Close must destroy the Launcher QObject, not merely hide it");
    QPointer<QWidget> second = coordinator.showLauncher();
    require(second && second->isVisible() && coordinator.launcherIncarnation() != incarnation,
        "UI recreation must use a new incarnation");
    require(fixture.drains == 0, "UI recreation must cancel pending zero-role drain");
    require(second->close() && waitUntil([&] { return !second && fixture.drains == 1; }),
        "Launcher-only normal close must destroy UI and drain once");
}

void protocolLifetime()
{
    CoordinatorFixture fixture;
    QPointer<QWidget> launcher = fixture.coordinator.showLauncher();
    Child child;
    auto env = QProcessEnvironment::systemEnvironment();
    const auto path = env.value("Path", env.value("PATH"));
    env.remove("Path"); env.remove("PATH"); env.insert("Path", path);
    child.process.setProcessEnvironment(env);
    child.process.start(QCoreApplication::applicationFilePath(), {"fixture",
        "S1-peer-测试-" + QUuid::createUuid().toString(), QString::number(static_cast<quintptr>(launcher->winId()))});
    require(child.process.waitForStarted(5000), "Real Session fixture failed to start");
    require(waitUntil([&] { child.read(); return child.output.contains("Join protocol PASS")
        || child.process.state() == QProcess::NotRunning; }), "Join protocol fixture timed out");
    require(child.output.contains("Join protocol PASS") && fixture.coordinator.sessionCount() == 1,
        "Native peer/group/HWND/exact identity/incarnation checks failed");
    require(launcher->close() && waitUntil([&] { return !launcher; }), "Launcher UI must be destroyed");
    require(fixture.drains == 0 && fixture.coordinator.sessionCount() == 1,
        "Disconnected Join socket must not remove the living independent Session");
    // Malformed ordinary ACTIVATE cannot recreate a retired Launcher UI.
    require(exchange(fixture.slot.endpointName(), QByteArray(4, '\0'), false)
        && !fixture.coordinator.launcherWindow(), "Invalid ACTIVATE must not create UI");
    EnumWindows([](HWND hwnd, LPARAM pid) -> BOOL {
        DWORD owner = 0; GetWindowThreadProcessId(hwnd, &owner);
        wchar_t caption[128]{}; GetWindowTextW(hwnd, caption, 128);
        if (owner == static_cast<DWORD>(pid) && wcscmp(caption, L"S1 coordinator fixture Session") == 0)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return TRUE;
    }, static_cast<LPARAM>(child.process.processId()));
    require(waitUntil([&] { return child.process.state() == QProcess::NotRunning
        && fixture.coordinator.sessionCount() == 0 && fixture.drains == 1; }),
        "Verified process termination must remove exactly that Session and drain");
    require(child.process.exitStatus() == QProcess::NormalExit && child.process.exitCode() == 0,
        "Session fixture must close normally; forced cleanup is not proof");
}

void bootstrapTimeout()
{
    CoordinatorFixture fixture(true);
    require(waitUntil([&] { return fixture.drains == 1; }, ActivationDeadlineMs + 3000),
        "No-role bootstrap must exit after its finite startup budget");
    require(!fixture.coordinator.launcherWindow() && fixture.coordinator.sessionCount() == 0,
        "Coordinator existence must not count as a phantom Launcher role");
}

void unavailableJoin()
{
    auto scope = *currentInteractiveInstanceScope(); scope.userSid += QUuid::createUuid().toString();
    const auto absent = *InstanceSlot::fromProcessMode(LauncherProcessMode{}, scope);
    QWidget role; role.show();
    QPointer<QWidget> retained = &role;
    SessionCoordinatorJoin join(sessionSlot("S1-unavailable-" + QUuid::createUuid().toString()), absent);
    join.start(role, false);
    require(waitUntil([&] { return join.finished(); }, ActivationDeadlineMs + 3000)
        && !join.joined() && retained && role.isVisible(),
        "Bounded Join failure must not close/hide/destroy the independently visible Session");
}

void pendingTeardown()
{
    CoordinatorFixture fixture;
    require(fixture.coordinator.showLauncher() != nullptr, "Coordinator Launcher must be ready");
    const auto slot = sessionSlot("S1-pending-teardown-" + QUuid::createUuid().toString());
    InstanceAdmissionLease admission;
    require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "Pending fixture admission failed");
    InstanceActivationEndpoint activation(slot);
    require(activation.listen(), "Pending fixture endpoint failed");
    QWidget role; role.show(); activation.bindReadyRole(role);
    auto join = std::make_unique<SessionCoordinatorJoin>(slot, fixture.slot);
    QPointer<SessionCoordinatorJoin> retained = join.get();
    join->start(role, false);
    // Real native socket adoption, but no event-loop turn to deliver the server's ACK.
    // Destruction is exactly ordinary Session exit during the asynchronous Join.
    require(!join->finished() && !join->joined() && join->coordinatorPid() == GetCurrentProcessId(),
        "Fixture must have a connected, genuinely unfinished Join before teardown");
    QObject observer;
    int destroyedTimers = 0;
    bool socketDisconnected = false;
    bool lateDisconnect = false;
    QObject::connect(&ApplicationCoordinatorTestAccess::retry(*join), &QObject::destroyed,
        &observer, [&] { ++destroyedTimers; });
    QObject::connect(&ApplicationCoordinatorTestAccess::deadline(*join), &QObject::destroyed,
        &observer, [&] { ++destroyedTimers; });
    QObject::connect(&ApplicationCoordinatorTestAccess::socket(*join), &QLocalSocket::disconnected,
        &observer, [&] { socketDisconnected = true; lateDisconnect = destroyedTimers != 0; });
    join.reset();
    require(socketDisconnected && !lateDisconnect && destroyedTimers == 2,
        "Pending native socket must disconnect while callback timer dependencies are still alive");
    require(!retained && role.isVisible() && admission.ownsAdmission(),
        "Pending Join teardown must be normal and leave the independent role/admission intact");
    std::cout << "Pending native Join destroyed normally before ACK" << std::endl;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try
    {
        const auto name = application.arguments().value(1);
        if (name == "fixture") return sessionFixture(application.arguments().value(2),
            static_cast<quintptr>(application.arguments().value(3).toULongLong()));
        application.setQuitOnLastWindowClosed(false);
        if (name == "ui-lifetime") uiLifetime();
        else if (name == "protocol-lifetime") protocolLifetime();
        else if (name == "bootstrap-timeout") bootstrapTimeout();
        else if (name == "unavailable-join") unavailableJoin();
        else if (name == "pending-teardown") pendingTeardown();
        else throw std::runtime_error("Unknown S1 coordinator test case");
        std::cout << "S1 " << name.toStdString() << " PASS" << std::endl;
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << std::endl; return 1; }
}
