#include "ApplicationCoordinator.h"
#include "SessionPresentation.h"
#include "LauncherWindow.h"
#include "DisplayPresentation.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QProcess>
#include <QWinEventNotifier>
#include <QtEndian>

#include <windows.h>

#include <algorithm>
#include <iostream>
#include <limits>

namespace DesktopSceneEngine::Player
{
namespace
{
const QByteArray JoinMagic = QByteArrayLiteral("DSE-COORDINATOR-JOIN-1\n");
const QByteArray JoinAck = QByteArrayLiteral("DSE-COORDINATOR-JOIN-ACK-1");
const QByteArray ControlJoinMagic = QByteArrayLiteral("DSE-SESSION-CONTROL-1\n");
enum class VisibilityMessage : char { Renew = 'R', Qualified = 'Q', HidePermit = 'H', Permit = 'P', Show = 'S', Result = 'D', State = 'V',
    DeploymentState = 'T', Deploy = 'E', Undeploy = 'U', DeploymentReceived = 'A', DeploymentResult = 'F',
    Close = 'C', CloseQuery = 'K', CloseCapability = 'B', CloseReceived = 'J', CloseState = 'O' };
constexpr qsizetype VisibilityFrameBytes = 42; // kind, epoch, incarnation, request, result/state
constexpr qsizetype MaximumVisibilityBufferedBytes = VisibilityFrameBytes * 16;

// Qt's own tray message window performs NIM_ADD on TaskbarCreated. This receiver
// only refreshes coordinator-owned projections after that broadcast, including
// headless owners. It never observes/replaces Desktop presentation endpoints.
class CoordinatorShellEvents final : public QWidget
{
public:
    explicit CoordinatorShellEvents(std::function<void()> recreated) : recreated_(std::move(recreated))
    {
        setObjectName(QStringLiteral("coordinatorShellEvents"));
        setWindowTitle(QStringLiteral("DSE Shell events")); // Not an application primary-window caption.
        setAttribute(Qt::WA_ShowWithoutActivating);
        message_ = RegisterWindowMessageW(L"TaskbarCreated");
        const auto hwnd = reinterpret_cast<HWND>(winId()); // Not HWND_MESSAGE: receives the Shell broadcast.
        if (message_) ChangeWindowMessageFilterEx(hwnd, message_, MSGFLT_ALLOW, nullptr);
    }
protected:
    bool nativeEvent(const QByteArray& type, void* raw, qintptr* result) override
    {
        if (type == "windows_generic_MSG" && raw && message_ && static_cast<MSG*>(raw)->message == message_)
            recreated_();
        return QWidget::nativeEvent(type, raw, result);
    }
private:
    UINT message_ = 0;
    std::function<void()> recreated_;
};

bool writeVisibility(QLocalSocket& socket, const QByteArray& frame)
{
    if (socket.state() != QLocalSocket::ConnectedState) return false;
    if (frame.size() != VisibilityFrameBytes
        || socket.bytesToWrite() > MaximumVisibilityBufferedBytes - frame.size())
    { socket.abort(); return false; }
    if (socket.write(frame) != frame.size()) { socket.abort(); return false; }
    return true;
}

QByteArray visibilityFrame(VisibilityMessage kind, const QUuid& epoch, const QUuid& incarnation,
    quint64 request, quint8 value)
{
    QByteArray frame(1, static_cast<char>(kind));
    frame += epoch.toRfc4122();
    frame += incarnation.toRfc4122();
    const auto offset = frame.size();
    frame.resize(offset + 8);
    qToBigEndian<quint64>(request, frame.data() + offset);
    frame += static_cast<char>(value);
    return frame;
}

struct ProcessHandle
{
    HANDLE value = nullptr;
    ~ProcessHandle() { if (value) CloseHandle(value); }
};

QByteArray groupDigest(const InstanceSlot& slot)
{
    return QCryptographicHash::hash(slot.endpointName().toUtf8(), QCryptographicHash::Sha256);
}

// Validate the native peer, not a supplied PID. The retained handle pins its lifetime.
bool sameApplicationPeer(HANDLE process, DWORD pid)
{
    const auto scope = currentInteractiveInstanceScope();
    DWORD session = 0;
    if (!scope || !ProcessIdToSessionId(pid, &session) || session != scope->windowsSessionId) return false;
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &rawToken)) return false;
    ProcessHandle token{rawToken};
    DWORD bytes = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> user(bytes);
    if (!bytes || !GetTokenInformation(token.value, TokenUser, user.data(), bytes, &bytes)) return false;
    HANDLE selfToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &selfToken)) return false;
    ProcessHandle self{selfToken};
    DWORD selfBytes = 0;
    GetTokenInformation(self.value, TokenUser, nullptr, 0, &selfBytes);
    std::vector<unsigned char> selfUser(selfBytes);
    if (!selfBytes || !GetTokenInformation(self.value, TokenUser, selfUser.data(), selfBytes, &selfBytes)
        || !EqualSid(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,
            reinterpret_cast<TOKEN_USER*>(selfUser.data())->User.Sid)) return false;
    wchar_t peerPath[32768]{}, selfPath[32768]{};
    DWORD length = 32768;
    const DWORD selfLength = GetModuleFileNameW(nullptr, selfPath, 32768);
    return selfLength > 0 && selfLength < 32768
        && QueryFullProcessImageNameW(process, 0, peerPath, &length)
        && QString::fromWCharArray(peerPath, static_cast<int>(length)).compare(
            QString::fromWCharArray(selfPath, static_cast<int>(selfLength)), Qt::CaseInsensitive) == 0;
}

HANDLE openPipe(const QString& endpoint)
{
    const auto name = (QStringLiteral("\\\\.\\pipe\\") + endpoint).toStdWString();
    return CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
}

bool activationOwnedBy(const InstanceSlot& slot, DWORD pid)
{
    const HANDLE pipe = openPipe(slot.endpointName());
    if (pipe == INVALID_HANDLE_VALUE) return false;
    ULONG owner = 0;
    const bool same = GetNamedPipeServerProcessId(pipe, &owner) && owner == pid;
    CloseHandle(pipe);
    return same;
}
}

QString coordinatorEndpointName(const InstanceSlot& launcherSlot)
{
    return launcherSlot.endpointName() + QStringLiteral(".Join");
}

QByteArray coordinatorJoinFrame(const InstanceSlot& sessionSlot, const InstanceSlot& launcherSlot,
    const QUuid& incarnation, quintptr primaryWindow)
{
    if (sessionSlot.isLauncher() || !launcherSlot.isLauncher() || incarnation.isNull() || !primaryWindow)
        return {};
    QByteArray body = JoinMagic + incarnation.toRfc4122();
    const auto offset = body.size();
    body.resize(offset + 8);
    qToBigEndian<quint64>(static_cast<quint64>(primaryWindow), body.data() + offset);
    body += groupDigest(launcherSlot);
    body += sessionSlot.canonicalIdentity();
    if (body.size() > MaximumActivationBodyBytes) return {};
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    return frame + body;
}

struct ApplicationCoordinator::SessionRecord
{
    ProcessHandle process;
    DWORD pid = 0;
    quintptr window = 0;
    QUuid incarnation;
    QByteArray identity;
    std::unique_ptr<QWinEventNotifier> death;
    QPointer<QLocalSocket> control;
    QPointer<QTimer> controlExpiry;
    bool hidden = false; // Derived Session projection only.
    quint64 lastRequest = 0;
    quint64 showRequest = 0;
    std::optional<Platform::DisplayDescriptor> lastDisplay; // Last-known friendly projection, not display ownership.
    std::optional<DesktopDeploymentProjection> deployment;
    quint64 deploymentRevision = 0;
    quint64 deploymentRequest = 0;
    quint64 deploymentCompletedRequest = 0;
    DesktopDeploymentResult deploymentResult = DesktopDeploymentResult::Unavailable;
    QPointer<QTimer> deploymentDeadline;
    std::optional<bool> canClose;
    quint64 closeRevision = 0;
    quint64 closeSequence = 0;
    std::optional<RoleCloseOperation> closeOperation;
    bool closeQueryOutstanding = false;
    QPointer<QTimer> closeDeadline;
};

ApplicationCoordinator::ApplicationCoordinator(const InstanceSlot& launcherSlot,
    InstanceActivationEndpoint& activation, bool bootstrap, std::function<void()> drained)
    : launcherSlot_(launcherSlot), activation_(activation), drained_(std::move(drained))
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    server_.setMaxPendingConnections(MaximumActivationClients);
    server_.setListenBacklogSize(MaximumActivationClients);
    connect(&server_, &QLocalServer::newConnection, this, [this] { acceptConnections(); });
    bootstrapDeadline_.setSingleShot(true);
    connect(&bootstrapDeadline_, &QTimer::timeout, this, [this] { scheduleDrain(); });
    // A headless bootstrap is a bounded pending startup, never a permanent role.
    if (bootstrap) bootstrapDeadline_.start(ActivationDeadlineMs);
    drain_.setSingleShot(true);
    connect(&drain_, &QTimer::timeout, this, [this] {
        if (launcher_ || !sessions_.empty() || bootstrapDeadline_.isActive()) return;
        closing_ = true;
        server_.close();
        activation_.beginShutdown();
        if (drained_) drained_();
        else QCoreApplication::quit();
    });
    updateActivationProvider();
    tray_.setIcon(QApplication::windowIcon());
    tray_.setContextMenu(&trayMenu_);
    closeAllSeparator_ = trayMenu_.addSeparator();
    closeAllAction_ = trayMenu_.addAction(QString{});
    connect(closeAllAction_, &QAction::triggered, this, [this] { closeAll(); });
    closeAllStatus_ = trayMenu_.addAction(QString{});
    closeAllStatus_->setEnabled(false);
    closeAllStatus_->setVisible(false);
    connect(&trayMenu_, &QMenu::aboutToShow, this, [this] { updateTray(); });
    trayClickGuard_.setSingleShot(true);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        // Context uses Qt's real menu; double/middle click have no independent command.
        if (reason != QSystemTrayIcon::Trigger || trayClickGuard_.isActive() || !showAllSnapshot_.empty()) return;
        trayClickGuard_.start(QApplication::doubleClickInterval());
        showAll();
    });
    showAllDeadline_.setSingleShot(true);
    connect(&showAllDeadline_, &QTimer::timeout, this, [this] {
        showAllAwaiting_ = {}; showAllRequest_ = 0; advanceShowAll(); // Skip uncertain presentation, never retry.
    });
    trayHealth_.setInterval(SessionQualificationRenewalMs);
    connect(&trayHealth_, &QTimer::timeout, this, [this] { updateTray(); });
    trayHealth_.start();
    shellEvents_ = std::make_unique<CoordinatorShellEvents>([this] { shellRecreated(); });
}

ApplicationCoordinator::~ApplicationCoordinator()
{
    closing_ = true;
    if (auto* launcher = dynamic_cast<LauncherWindow*>(launcher_.get())) launcher->trayPresentation().beginTeardown();
    showAllDeadline_.stop(); trayClickGuard_.stop();
    trayHealth_.stop();
    tray_.hide();
    activation_.setReadyRoleProvider({});
    server_.close();
    for (auto* socket : findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly)) socket->abort();
}

bool ApplicationCoordinator::listen()
{
    if (!launcherSlot_.isLauncher() || !server_.listen(coordinatorEndpointName(launcherSlot_))) return false;
    scheduleDrain();
    return true;
}

void ApplicationCoordinator::setLauncherFactory(std::function<QWidget*()> factory)
{
    launcherFactory_ = std::move(factory);
}

void ApplicationCoordinator::updateActivationProvider()
{
    activation_.setReadyRoleProvider([this] {
        auto* launcher = showLauncher();
        if (launcher) restoreRegisteredSessions(); // Ordinary relaunch is independent of tray paint/control health.
        return launcher;
    });
}

void ApplicationCoordinator::restoreRegisteredSessions()
{
    removeTerminatedSessions();
    const auto scope = currentInteractiveInstanceScope();
    if (!scope) return;
    for (const auto& record : sessions_)
    {
        if (recoveryAvailable(*record))
        { (void)dispatchSessionShow(record->incarnation, ActivationRequest::NoRequest); continue; }
        const auto id = DisplayId::parseCanonicalRepresentation(record->identity.toStdString());
        const auto slot = id ? InstanceSlot::fromProcessMode(DisplaySessionProcessMode{*id}, *scope) : std::nullopt;
        if (!slot || WaitForSingleObject(record->process.value, 0) != WAIT_TIMEOUT) continue;
        const auto pipe = openPipe(slot->endpointName());
        if (pipe == INVALID_HANDLE_VALUE) continue;
        ULONG owner = 0;
        if (!GetNamedPipeServerProcessId(pipe, &owner) || owner != record->pid
            || WaitForSingleObject(record->process.value, 0) != WAIT_TIMEOUT)
        { CloseHandle(pipe); continue; } // Exact retained process, never PID/DisplayId alone.
        auto* request = new QLocalSocket(this);
        if (!request->setSocketDescriptor(reinterpret_cast<qintptr>(pipe), QLocalSocket::ConnectedState, QIODevice::ReadWrite))
        { CloseHandle(pipe); request->deleteLater(); continue; }
        connect(request, &QLocalSocket::disconnected, request, &QObject::deleteLater);
        connect(request, &QLocalSocket::readyRead, request, [request] { request->readAll(); });
        const auto frame = activationRequestFrame(*slot);
        if (request->write(frame) != frame.size()) request->abort();
        QTimer::singleShot(ActivationClientLifetimeMs, request, [request] { request->abort(); request->deleteLater(); });
    } // Bounded asynchronous exact-role ACTIVATE; no new Session/Engine, no GUI-thread blocking forwardActivation.
}

void ApplicationCoordinator::shellRecreated()
{
    QTimer::singleShot(0, this, [this] {
        if (closing_) return;
        updateTray(); // Qt owns re-registration; API visibility is not physical icon evidence.
        std::cout << "[Tray] Shell projection refreshed epoch=" << epoch_.toString().toStdString()
            << " sessions=" << sessions_.size() << std::endl;
    });
}

QWidget* ApplicationCoordinator::showLauncher()
{
    if (closing_ || launcherClosing_) return nullptr;
    if (!launcher_ && launcherFactory_)
    {
        launcher_.reset(launcherFactory_());
        if (launcher_)
        {
            launcherIncarnation_ = QUuid::createUuid();
            launcherClose_.reset();
            drain_.stop();
            launcher_->show();
            updateTray();
            if (auto* launcher = dynamic_cast<LauncherWindow*>(launcher_.get()))
            {
                const auto incarnation = launcherIncarnation_;
                launcher->trayPresentation().bindRecovery([this, incarnation] {
                    return incarnation == launcherIncarnation_ && launcherRecoveryAvailable();
                }, [this, incarnation] {
                    if (incarnation == launcherIncarnation_ && launcherRecoveryAvailable())
                        if (auto* current = dynamic_cast<LauncherWindow*>(launcher_.get())) (void)current->trayPresentation().applyHidePermit();
                });
                launcher->trayPresentation().bindCloseReport([this, incarnation, launcher] {
                    if (incarnation == launcherIncarnation_ && launcherClose_)
                    {
                        if (const auto& operation = launcher->trayPresentation().closeOperation(); operation
                            && operation->key == launcherClose_->key) launcherClose_ = operation;
                        updateTray();
                    }
                });
            }
        }
    }
    if (auto* launcher = dynamic_cast<LauncherWindow*>(launcher_.get())) (void)launcher->trayPresentation().restore();
    return launcher_.get();
}

void ApplicationCoordinator::retireLauncher(QWidget& window)
{
    if (launcher_.get() != &window || launcherClosing_) return;
    launcherClosing_ = true;
    // Delete only after returning from the real closeEvent, not inside the widget's stack.
    QTimer::singleShot(0, this, [this] {
        if (launcherClose_) rememberClosedRole(launcherIncarnation_, *launcherClose_);
        launcher_.reset();
        launcherIncarnation_ = {};
        launcherClose_.reset();
        launcherClosing_ = false;
        updateActivationProvider();
        scheduleDrain();
        updateTray();
    });
}

void ApplicationCoordinator::scheduleDrain()
{
    updateTray();
    if (!closing_ && !launcher_ && sessions_.empty() && !bootstrapDeadline_.isActive() && !drain_.isActive())
        drain_.start(100); // Short bounded event/result drainage, not persistent zero-role idle.
}

void ApplicationCoordinator::acceptConnections()
{
    while (server_.hasPendingConnections())
    {
        auto* socket = server_.nextPendingConnection();
        if (!socket) continue;
        const auto sockets = findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly);
        const auto pending = std::count_if(sockets.begin(), sockets.end(), [](const auto* s) {
            return !s->property("sessionControl").toBool();
        });
        if (closing_ || pending >= MaximumActivationClients || sockets.size() >= MaximumCoordinatorSessions + MaximumActivationClients)
        {
            socket->abort(); socket->deleteLater(); continue;
        }
        socket->setParent(this);
        socket->setReadBufferSize(MaximumActivationBodyBytes + 5);
        auto bytes = std::make_shared<QByteArray>();
        auto completed = std::make_shared<bool>(false);
        auto persistent = std::make_shared<bool>(false);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, bytes, completed, persistent] {
            if (*completed || closing_) return;
            *bytes += socket->readAll();
            if (bytes->size() < 4) return;
            const auto size = qFromBigEndian<quint32>(bytes->constData());
            if (size == 0 || size > MaximumActivationBodyBytes || bytes->size() > static_cast<qsizetype>(size) + 4)
            { *completed = true; socket->abort(); return; }
            if (bytes->size() != static_cast<qsizetype>(size) + 4) return;
            *completed = true;
            auto body = bytes->mid(4);
            const bool control = body.startsWith(ControlJoinMagic);
            if (control) body.replace(0, ControlJoinMagic.size(), JoinMagic);
            if (!registerSession(*socket, body)) { socket->abort(); return; }
            if (control)
            {
                *persistent = true;
                beginSessionControl(*socket, QUuid::fromRfc4122(body.mid(JoinMagic.size(), 16)));
                return;
            }
            socket->write(JoinAck + epoch_.toRfc4122());
            socket->disconnectFromServer();
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(ActivationClientLifetimeMs, socket, [socket, persistent] {
            if (!*persistent) { socket->abort(); socket->deleteLater(); }
        });
        QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection);
    }
}

bool ApplicationCoordinator::registerSession(QLocalSocket& socket, const QByteArray& body)
{
    const auto headerBytes = JoinMagic.size() + 16 + 8 + 32;
    if (body.size() <= headerBytes || !body.startsWith(JoinMagic)) return false;
    const auto incarnation = QUuid::fromRfc4122(body.mid(JoinMagic.size(), 16));
    const auto window = qFromBigEndian<quint64>(body.constData() + JoinMagic.size() + 16);
    if (incarnation.isNull() || window > (std::numeric_limits<quintptr>::max)()
        || body.mid(JoinMagic.size() + 24, 32) != groupDigest(launcherSlot_)) return false;
    const auto identity = body.mid(headerBytes);
    const auto id = DisplayId::parseCanonicalRepresentation(std::string_view{
        identity.constData(), static_cast<std::size_t>(identity.size())});
    const auto scope = currentInteractiveInstanceScope();
    if (!id || !scope) return false;
    const auto slot = InstanceSlot::fromProcessMode(DisplaySessionProcessMode{*id}, *scope);
    if (!slot || slot->canonicalIdentity() != identity) return false;
    ULONG pid = 0;
    const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
    if (!GetNamedPipeClientProcessId(pipe, &pid) || !pid) return false;
    auto record = std::make_unique<SessionRecord>();
    record->process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!record->process.value || !sameApplicationPeer(record->process.value, pid)
        || WaitForSingleObject(record->process.value, 0) != WAIT_TIMEOUT) return false;
    const auto hwnd = reinterpret_cast<HWND>(static_cast<quintptr>(window));
    DWORD owner = 0;
    if (!IsWindow(hwnd) || !GetWindowThreadProcessId(hwnd, &owner) || owner != pid
        || GetAncestor(hwnd, GA_ROOT) != hwnd || GetWindow(hwnd, GW_OWNER)
        || !activationOwnedBy(*slot, pid)) return false;
    removeTerminatedSessions();
    for (const auto& old : sessions_)
        if (old->identity == identity || old->pid == pid)
            return old->identity == identity && old->pid == pid && old->window == window && old->incarnation == incarnation;
    if (sessions_.size() >= MaximumCoordinatorSessions) return false;
    record->pid = pid;
    record->window = static_cast<quintptr>(window);
    record->incarnation = incarnation;
    record->identity = identity;
    record->death = std::make_unique<QWinEventNotifier>(record->process.value);
    auto* notifier = record->death.get();
    connect(notifier, &QWinEventNotifier::activated, this, [this, notifier] {
        notifier->setEnabled(false);
        // Avoid deleting a notifier in its own activation stack.
        QTimer::singleShot(0, this, [this] { removeTerminatedSessions(); });
    });
    sessions_.push_back(std::move(record));
    bootstrapDeadline_.stop();
    drain_.stop();
    updateTray();
    return true;
}

bool ApplicationCoordinator::recoveryAvailable(const SessionRecord& record) const
{
    const auto hwnd = reinterpret_cast<HWND>(record.window);
    DWORD pid = 0;
    return !closing_ && record.control && record.control->state() == QLocalSocket::ConnectedState
        && tray_.isVisible() && !tray_.icon().isNull() && QSystemTrayIcon::isSystemTrayAvailable()
        && WaitForSingleObject(record.process.value, 0) == WAIT_TIMEOUT && IsWindow(hwnd)
        && GetWindowThreadProcessId(hwnd, &pid) && pid == record.pid;
}

void ApplicationCoordinator::updateTray()
{
    const bool live = !closing_ && (launcher_ || !sessions_.empty());
    if (live && !tray_.icon().isNull() && QSystemTrayIcon::isSystemTrayAvailable()) tray_.show();
    else tray_.hide();
    if (auto* launcher = dynamic_cast<LauncherWindow*>(launcher_.get())) launcher->trayPresentation().refreshEligibility();
    refreshRoleMenu();
    queueCloseAllProgress();
}

void ApplicationCoordinator::showSession(const QUuid& incarnation)
{
    (void)dispatchSessionShow(incarnation, ActivationRequest::Request);
}

quint64 ApplicationCoordinator::dispatchSessionShow(const QUuid& incarnation, ActivationRequest activation)
{
    for (const auto& record : sessions_)
        if (record->incarnation == incarnation && recoveryAvailable(*record))
        {
            // Explicit idempotent Show; completion below is not inferred from write/receipt.
            const auto request = ++record->showRequest;
            if (activation == ActivationRequest::Request) (void)AllowSetForegroundWindow(record->pid);
            return writeVisibility(*record->control, visibilityFrame(VisibilityMessage::Show, epoch_, incarnation,
                request, activation == ActivationRequest::Request)) ? request : 0;
        }
    return 0;
}

bool ApplicationCoordinator::launcherRecoveryAvailable() const
{
    return !closing_ && !launcherClosing_ && launcher_ && server_.isListening() && activation_.isListening()
        && tray_.isVisible() && !tray_.icon().isNull() && QSystemTrayIcon::isSystemTrayAvailable();
}

void ApplicationCoordinator::requestSessionDeployment(const QUuid& incarnation, DesktopDeploymentIntent intent)
{
    if (closeBatch_) return; // Only coordinator-originated mutations; ordinary local UI is unchanged.
    for (const auto& record : sessions_)
        if (record->incarnation == incarnation)
        {
            const auto request = ++record->deploymentRequest;
            if (record->deploymentDeadline) record->deploymentDeadline->stop();
            if (!recoveryAvailable(*record))
            {
                record->deploymentResult = DesktopDeploymentResult::Unavailable;
                record->deploymentCompletedRequest = request; // Fence older in-flight receipts/results too.
                return;
            }
            record->deploymentResult = DesktopDeploymentResult::Uncertain;
            if (writeVisibility(*record->control, visibilityFrame(intent == DesktopDeploymentIntent::Deploy
                    ? VisibilityMessage::Deploy : VisibilityMessage::Undeploy, epoch_, incarnation, request, 0)))
                record->deploymentDeadline->start(SessionQualificationLifetimeMs);
            return; // No inferred completion, retry, Show or activation.
        }
}

DesktopDeploymentResult ApplicationCoordinator::sessionDeploymentResult(const QUuid& incarnation) const
{
    for (const auto& record : sessions_) if (record->incarnation == incarnation) return record->deploymentResult;
    return DesktopDeploymentResult::Unavailable;
}

bool ApplicationCoordinator::requestRoleClose(const QUuid& incarnation)
{
    if (closeBatch_) return false;
    return dispatchRoleClose(incarnation);
}

bool ApplicationCoordinator::dispatchRoleClose(const QUuid& incarnation)
{
    if (closing_) return false;
    if (launcher_ && incarnation == launcherIncarnation_)
    {
        auto* launcher = dynamic_cast<LauncherWindow*>(launcher_.get());
        if (!launcher || launcherClosing_ || (launcherClose_ && (!terminalCloseResult(launcherClose_->result)
            || launcherClose_->result == RoleCloseResult::CloseAccepted))) return false;
        const RoleCloseKey key{epoch_, ++launcherCloseSequence_};
        launcherClose_ = RoleCloseOperation{key, RoleCloseResult::Received};
        launcherClose_->result = launcher->trayPresentation().requestClose(key);
        updateTray();
        return true;
    }
    for (const auto& record : sessions_) if (record->incarnation == incarnation)
    {
        if (record->closeOperation && (!terminalCloseResult(record->closeOperation->result)
            || record->closeOperation->result == RoleCloseResult::CloseAccepted
            || record->closeOperation->result == RoleCloseResult::RoleGone)) return false;
        const RoleCloseKey key{epoch_, ++record->closeSequence};
        record->closeOperation = RoleCloseOperation{key, RoleCloseResult::Rejected};
        record->closeQueryOutstanding = false;
        if (recoveryAvailable(*record))
        {
            record->closeOperation->result = RoleCloseResult::Uncertain;
            (void)AllowSetForegroundWindow(record->pid); // Existing permission request, never foreground-policy bypass.
            if (writeVisibility(*record->control, visibilityFrame(VisibilityMessage::Close, epoch_, incarnation, key.request, 0)))
                record->closeDeadline->start(SessionQualificationLifetimeMs); // Delivery only, never a human decision timeout.
        }
        updateTray();
        return true;
    }
    return false; // Stale incarnation does not route to a replacement or recreate a role.
}

void ApplicationCoordinator::reconcileRoleClose(const QUuid& incarnation, std::optional<RoleCloseKey> key)
{
    if (key && (key->generation.isNull() || !key->request)) return;
    for (const auto& record : sessions_) if (record->incarnation == incarnation)
    {
        if (!recoveryAvailable(*record)) return; // Only the current authenticated, living control peer may observe a key.
        if (key && (!record->closeOperation || record->closeOperation->key != *key))
        {
            if (record->closeOperation && (!terminalCloseResult(record->closeOperation->result)
                || record->closeOperation->result == RoleCloseResult::CloseAccepted
                || record->closeOperation->result == RoleCloseResult::RoleGone)) return;
            record->closeOperation = RoleCloseOperation{*key, RoleCloseResult::Uncertain};
            record->closeSequence = (std::max)(record->closeSequence, key->request);
        }
        if (!record->closeOperation || !record->control || record->control->state() != QLocalSocket::ConnectedState) return;
        record->closeQueryOutstanding = true;
        const auto& requested = record->closeOperation->key;
        (void)writeVisibility(*record->control, visibilityFrame(VisibilityMessage::CloseQuery,
            requested.generation, incarnation, requested.request, 0)); // Observe SAME key, never dispatch Close again.
        return;
    }
}

std::optional<RoleCloseOperation> ApplicationCoordinator::roleCloseOperation(const QUuid& incarnation) const
{
    if (incarnation == launcherIncarnation_) return launcherClose_;
    for (const auto& record : sessions_) if (record->incarnation == incarnation) return record->closeOperation;
    for (const auto& record : closedRoles_) if (record.incarnation == incarnation) return record.operation;
    return std::nullopt; // Unknown role is not proof of RoleGone.
}

void ApplicationCoordinator::rememberClosedRole(const QUuid& incarnation, RoleCloseOperation operation)
{
    if (incarnation.isNull()) return;
    if (!terminalCloseResult(operation.result)) operation.result = RoleCloseResult::RoleGone;
    std::erase_if(closedRoles_, [&](const auto& old) { return old.incarnation == incarnation; });
    if (closedRoles_.size() >= MaximumCoordinatorSessions) closedRoles_.erase(closedRoles_.begin());
    closedRoles_.push_back({incarnation, operation});
}

std::vector<ApplicationCoordinator::RolePresentation> ApplicationCoordinator::orderedRoles()
{
    const auto enumeration = enumerateDisplays_();
    struct OrderedSession { SessionRecord* record; std::size_t rank; QString label; };
    std::vector<OrderedSession> ordered;
    const auto unresolved = (std::numeric_limits<std::size_t>::max)();
    for (const auto& record : sessions_)
    {
        auto rank = unresolved;
        if (enumeration.status == Platform::DisplayEnumerationStatus::Success)
            for (std::size_t i = 0; i < enumeration.snapshot.displays.size(); ++i)
                if (enumeration.snapshot.displays[i].id.canonicalRepresentation() == std::optional<std::string>{record->identity.toStdString()})
                { rank = i; record->lastDisplay = enumeration.snapshot.displays[i]; break; }
        auto label = record->lastDisplay ? displayLabel(*record->lastDisplay) : QString::fromUtf8(record->identity);
        if (rank == unresolved) label += QCoreApplication::translate("ApplicationCoordinator", " — Disconnected");
        ordered.push_back({record.get(), rank, label});
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.rank != b.rank ? a.rank < b.rank : a.record->identity < b.record->identity;
    }); // Resolved rank comes directly from Platform; do not duplicate its geometry sort.
    std::vector<RolePresentation> roles;
    if (launcher_ && !launcherClosing_) roles.push_back({launcherIncarnation_, QCoreApplication::translate("ApplicationCoordinator", "Launcher"), true});
    for (std::size_t i = 0; i < ordered.size(); ++i)
    {
        auto label = ordered[i].label;
        if (std::count_if(ordered.begin(), ordered.end(), [&](const auto& other) { return other.label == label; }) > 1)
        {
            const auto disconnected = QCoreApplication::translate("ApplicationCoordinator", " — Disconnected");
            if (ordered[i].rank == unresolved) label.chop(disconnected.size());
            label += QStringLiteral(" [%1]").arg(i + 1); // Presentation-only, never a routing key.
            if (ordered[i].rank == unresolved) label += disconnected;
        }
        roles.push_back({ordered[i].record->incarnation, label, false});
    }
    return roles;
}

void ApplicationCoordinator::refreshRoleMenu()
{
    const auto roles = orderedRoles();
    if (roles != menuRoles_)
    {
        for (auto* action : trayMenu_.actions())
        {
            if (!action->menu()) continue; // Stable top-level actions outlive derived role menus.
            trayMenu_.removeAction(action);
            if (action->menu()) action->menu()->deleteLater();
        }
        menuRoles_ = roles;
        for (const auto& role : menuRoles_)
        {
            auto* submenu = new QMenu(role.label, &trayMenu_);
            submenu->menuAction()->setData(role.incarnation);
            auto* show = submenu->addAction(QCoreApplication::translate("ApplicationCoordinator", "Show"));
            connect(show, &QAction::triggered, this, [this, incarnation = role.incarnation] { showRole(incarnation); });
            if (!role.launcher) submenu->addAction(QString{});
            submenu->addSeparator();
            auto* close = submenu->addAction(QCoreApplication::translate("ApplicationCoordinator", "Close"));
            connect(close, &QAction::triggered, this, [this, incarnation = role.incarnation] { (void)requestRoleClose(incarnation); });
            trayMenu_.insertMenu(closeAllSeparator_, submenu);
        }
    }
    const auto actions = trayMenu_.actions();
    for (qsizetype i = 0; i < static_cast<qsizetype>(menuRoles_.size()); ++i)
    {
        auto* show = actions[i]->menu()->actions().front();
        show->setText(QCoreApplication::translate("ApplicationCoordinator", "Show"));
        const auto& role = menuRoles_[static_cast<std::size_t>(i)];
        auto* close = actions[i]->menu()->actions().back();
        close->setText(QCoreApplication::translate("ApplicationCoordinator", "Close"));
        bool closeAvailable = false;
        bool available = role.launcher ? launcherRecoveryAvailable() : false;
        if (role.launcher)
            if (auto* launcher = dynamic_cast<LauncherWindow*>(launcher_.get()))
                closeAvailable = available && launcher->trayPresentation().canClose();
        if (!role.launcher)
            for (const auto& record : sessions_) if (record->incarnation == role.incarnation)
            {
                available = recoveryAvailable(*record);
                const bool outstanding = record->closeOperation && !terminalCloseResult(record->closeOperation->result);
                closeAvailable = available && record->canClose.value_or(false) && !outstanding;
                auto* deployment = actions[i]->menu()->actions()[1];
                const auto projection = record->deployment.value_or(DesktopDeploymentProjection{});
                const auto intent = projection.retained ? DesktopDeploymentIntent::Undeploy : DesktopDeploymentIntent::Deploy;
                deployment->setText(QCoreApplication::translate("ApplicationCoordinator", projection.retained ? "Undeploy" : "Deploy"));
                deployment->setEnabled(!closeBatch_ && available && !outstanding && record->deployment.has_value()
                    && (projection.retained ? projection.canUndeploy : projection.canDeploy));
                // Rebind only this derived menu intent. A retained stale action still has an explicit intent/UUID.
                disconnect(deployment, nullptr, this, nullptr);
                connect(deployment, &QAction::triggered, this, [this, incarnation = role.incarnation, intent] {
                    requestSessionDeployment(incarnation, intent);
                });
            }
        show->setEnabled(available);
        close->setEnabled(!closeBatch_ && closeAvailable);
    }
    closeAllAction_->setText(QCoreApplication::translate("ApplicationCoordinator", "Close All"));
    // Modal/capability projections do not globally prohibit invoking a fixed batch.
    closeAllAction_->setEnabled(!closing_ && !closeBatch_ && !menuRoles_.empty());
    closeAllStatus_->setVisible(closeBatch_.has_value());
    if (closeBatch_ && closeBatch_->index < closeBatch_->snapshot.size())
    {
        const auto& current = closeBatch_->snapshot[closeBatch_->index];
        const auto operation = roleCloseOperation(current.incarnation);
        const bool uncertain = closeBatch_->attempted && (!operation || !closeBatch_->request
            || operation->key != *closeBatch_->request || operation->result == RoleCloseResult::Uncertain);
        closeAllStatus_->setText(QCoreApplication::translate("ApplicationCoordinator",
            uncertain ? "Close outcome not confirmed: %1" : "Closing: %1").arg(current.label));
    }
}

void ApplicationCoordinator::closeAll()
{
    if (closing_ || closeBatch_) return; // No concurrent or queued second invocation.
    removeTerminatedSessions(); // Existing native lifetime verification, never HWND/timeout inference.
    auto snapshot = orderedRoles();
    if (snapshot.empty()) return;
    closeBatch_.emplace();
    closeBatch_->snapshot = std::move(snapshot);
    updateTray();
}

void ApplicationCoordinator::queueCloseAllProgress()
{
    if (!closeBatch_ || closeBatch_->dispatching || closeProgressQueued_ || closing_) return;
    closeProgressQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        closeProgressQueued_ = false;
        advanceCloseAll();
    });
}

void ApplicationCoordinator::advanceCloseAll()
{
    if (!closeBatch_ || closeBatch_->dispatching || closing_) return;
    auto& batch = *closeBatch_;
    if (batch.index == batch.snapshot.size())
    {
        closeBatch_.reset();
        updateTray(); // Survivors remain ordinary roles; zero-role drain remains S1-owned.
        return;
    }
    const auto incarnation = batch.snapshot[batch.index].incarnation;
    const auto living = [&] {
        return (launcher_ && launcherIncarnation_ == incarnation)
            || std::any_of(sessions_.begin(), sessions_.end(), [&](const auto& r) { return r->incarnation == incarnation; });
    };
    const auto advance = [&] {
        ++batch.index; batch.request.reset(); batch.attempted = false; batch.queried = false;
        updateTray(); // Yield before the next ordinary Close; never broadcast into nested interactions.
    };
    if (!living())
    {
        // This snapshot came from the verified registry. Only verified process death
        // or ordinary Launcher retirement removes its exact incarnation from it.
        advance(); return;
    }
    if (!batch.attempted)
    {
        batch.attempted = true;
        batch.dispatching = true;
        const bool requested = dispatchRoleClose(incarnation); // The SAME S5 construction/admission/result path.
        batch.dispatching = false;
        if (!requested)
        {
            // An already-outstanding S5 Close is pre-existing Busy, not this batch's
            // interaction. Never adopt it, queue another Close or wait for its modal.
            advance(); return;
        }
        const auto operation = roleCloseOperation(incarnation);
        if (operation) batch.request = operation->key;
        refreshRoleMenu();
    }
    const auto operation = roleCloseOperation(incarnation);
    if (!operation || !batch.request || operation->key != *batch.request) return; // No fabricated completion/retry.
    if (terminalCloseResult(operation->result)) { advance(); return; }
    if (operation->result == RoleCloseResult::Uncertain && !batch.queried)
        for (const auto& record : sessions_) if (record->incarnation == incarnation)
        {
            if (record->closeDeadline && record->closeDeadline->isActive()) return; // S5 delivery observation still pending.
            batch.queried = true;
            if (!record->closeQueryOutstanding) reconcileRoleClose(incarnation, batch.request);
            return; // One observation, SAME key. Lost owner does not reconstruct/resume this batch.
        }
    // Received/Pending waits for semantic completion, without a human decision timer.
}

void ApplicationCoordinator::showRole(const QUuid& incarnation, ActivationRequest activation)
{
    if (closing_) return;
    if (launcher_ && !launcherClosing_ && incarnation == launcherIncarnation_)
    {
        if (auto* launcher = dynamic_cast<LauncherWindow*>(launcher_.get()))
            if (!launcher->trayPresentation().restore()) return;
        surfaceAdmittedRole(*launcher_, activation);
        std::cout << "[Tray] Launcher Show activation=" << (activation == ActivationRequest::Request ? "request" : "none")
            << " incarnation=" << incarnation.toString().toStdString() << std::endl;
    }
    else (void)dispatchSessionShow(incarnation, activation);
}

void ApplicationCoordinator::showAll()
{
    if (closing_ || !showAllSnapshot_.empty()) return;
    updateTray();
    for (const auto& role : menuRoles_) showAllSnapshot_.push_back(role.incarnation);
    if (showAllSnapshot_.empty()) return;
    showAllTerminal_ = showAllSnapshot_.front();
    std::rotate(showAllSnapshot_.begin(), showAllSnapshot_.begin() + 1, showAllSnapshot_.end());
    showAllIndex_ = 0;
    advanceShowAll();
}

void ApplicationCoordinator::advanceShowAll()
{
    if (closing_ || !showAllAwaiting_.isNull()) return;
    while (showAllIndex_ < showAllSnapshot_.size())
    {
        const auto incarnation = showAllSnapshot_[showAllIndex_++];
        const auto activation = incarnation == showAllTerminal_ ? ActivationRequest::Request : ActivationRequest::NoRequest;
        if (launcher_ && !launcherClosing_ && incarnation == launcherIncarnation_) { showRole(incarnation, activation); continue; }
        const auto request = dispatchSessionShow(incarnation, activation);
        if (!request) continue;
        showAllAwaiting_ = incarnation; showAllRequest_ = request;
        showAllDeadline_.start(SessionQualificationLifetimeMs);
        return;
    }
    showAllSnapshot_.clear(); showAllTerminal_ = {}; showAllIndex_ = 0;
}

void ApplicationCoordinator::completeShowAllTarget(const QUuid& incarnation, quint64 request)
{
    if (incarnation != showAllAwaiting_ || request != showAllRequest_) return;
    showAllDeadline_.stop(); showAllAwaiting_ = {}; showAllRequest_ = 0;
    QTimer::singleShot(0, this, [this] { advanceShowAll(); });
}

void ApplicationCoordinator::beginSessionControl(QLocalSocket& socket, const QUuid& incarnation)
{
    const auto found = std::find_if(sessions_.begin(), sessions_.end(), [&](const auto& r) { return r->incarnation == incarnation; });
    if (found == sessions_.end() || (*found)->control) { socket.abort(); return; }
    auto* record = found->get();
    record->control = &socket;
    socket.setProperty("sessionControl", true);
    socket.setReadBufferSize(MaximumVisibilityBufferedBytes + 1);
    auto bytes = std::make_shared<QByteArray>();
    auto* expiry = new QTimer(&socket);
    expiry->setSingleShot(true);
    record->controlExpiry = expiry;
    auto* deploymentDeadline = new QTimer(&socket);
    deploymentDeadline->setSingleShot(true);
    record->deploymentDeadline = deploymentDeadline;
    auto* closeDeadline = new QTimer(&socket);
    closeDeadline->setObjectName(QStringLiteral("roleCloseDeliveryDeadline"));
    closeDeadline->setSingleShot(true);
    record->closeDeadline = closeDeadline;
    connect(closeDeadline, &QTimer::timeout, this, [this, incarnation] {
        for (const auto& r : sessions_) if (r->incarnation == incarnation && r->closeOperation
            && !terminalCloseResult(r->closeOperation->result) && r->closeOperation->result != RoleCloseResult::Pending)
        {
            r->closeOperation->result = RoleCloseResult::Uncertain;
            reconcileRoleClose(incarnation); // One bounded observation attempt, not a new mutating request.
            updateTray();
        }
    });
    connect(deploymentDeadline, &QTimer::timeout, this, [this, incarnation] {
        for (const auto& r : sessions_) if (r->incarnation == incarnation)
        {
            r->deploymentResult = DesktopDeploymentResult::Uncertain;
            r->deploymentCompletedRequest = r->deploymentRequest;
            updateTray();
        }
    });
    connect(expiry, &QTimer::timeout, &socket, &QLocalSocket::abort);
    expiry->start(SessionQualificationLifetimeMs);
    connect(&socket, &QLocalSocket::disconnected, this, [this, incarnation, peer = QPointer<QLocalSocket>(&socket)] {
        for (const auto& r : sessions_)
            if (r->incarnation == incarnation && r->control == peer)
            {
                r->control.clear(); r->hidden = false;
                r->canClose.reset();
                if (r->closeDeadline) r->closeDeadline->stop();
                if (r->closeOperation && !terminalCloseResult(r->closeOperation->result))
                    r->closeOperation->result = RoleCloseResult::Uncertain;
                if (r->deploymentDeadline) r->deploymentDeadline->stop();
                if (r->deploymentCompletedRequest != r->deploymentRequest)
                {
                    r->deploymentResult = DesktopDeploymentResult::Uncertain;
                    r->deploymentCompletedRequest = r->deploymentRequest;
                }
            }
        updateTray(); // Lost control is NOT recorded process/role death.
        if (incarnation == showAllAwaiting_) completeShowAllTarget(incarnation, showAllRequest_);
    });
    connect(&socket, &QLocalSocket::readyRead, this, [this, peer = QPointer<QLocalSocket>(&socket), incarnation, bytes] {
        if (!peer || closing_) return;
        *bytes += peer->readAll();
        if (bytes->size() > MaximumVisibilityBufferedBytes) { peer->abort(); return; }
        const auto current = std::find_if(sessions_.begin(), sessions_.end(), [&](const auto& r) {
            return r->incarnation == incarnation && r->control == peer;
        });
        if (current == sessions_.end()) { peer->abort(); return; }
        auto& r = **current;
        while (bytes->size() >= VisibilityFrameBytes)
        {
            const auto frame = bytes->first(VisibilityFrameBytes); bytes->remove(0, VisibilityFrameBytes);
            const auto kind = static_cast<VisibilityMessage>(frame.front());
            const auto value = static_cast<quint8>(frame.back());
            const RoleCloseKey closeKey{QUuid::fromRfc4122(frame.mid(1, 16)), qFromBigEndian<quint64>(frame.constData() + 33)};
            const bool queriedOldKey = kind == VisibilityMessage::CloseState && r.closeQueryOutstanding
                && r.closeOperation && r.closeOperation->key == closeKey;
            if (QUuid::fromRfc4122(frame.mid(17, 16)) != incarnation
                || (kind == VisibilityMessage::DeploymentState ? value > 7
                    : kind == VisibilityMessage::DeploymentResult ? value > 3
                    : kind == VisibilityMessage::CloseState ? value > static_cast<quint8>(RoleCloseResult::Uncertain) : value > 1))
            { peer->abort(); return; }
            if (closeKey.generation != epoch_ && !queriedOldKey) continue; // Delayed previous-owner report, not current projection/result.
            const auto request = qFromBigEndian<quint64>(frame.constData() + 33);
            if (kind == VisibilityMessage::Renew || kind == VisibilityMessage::HidePermit)
            {
                if (!request || request <= r.lastRequest) { peer->abort(); return; }
                r.lastRequest = request;
                updateTray();
                const bool qualified = recoveryAvailable(r);
                if (kind == VisibilityMessage::Renew && r.controlExpiry) r.controlExpiry->start(SessionQualificationLifetimeMs);
                if (!writeVisibility(*peer, visibilityFrame(kind == VisibilityMessage::Renew ? VisibilityMessage::Qualified : VisibilityMessage::Permit,
                    epoch_, incarnation, request, qualified))) return;
            }
            else if (kind == VisibilityMessage::State && request == 0) r.hidden = frame.back() != 0;
            else if (kind == VisibilityMessage::Result && request && request <= r.showRequest)
            {
                if (request == r.showRequest && frame.back()) r.hidden = false;
                completeShowAllTarget(incarnation, request);
            }
            else if (kind == VisibilityMessage::CloseCapability && request)
            {
                if (request > r.closeRevision) { r.closeRevision = request; r.canClose = value != 0; }
            }
            else if ((kind == VisibilityMessage::CloseReceived || kind == VisibilityMessage::CloseState) && request)
            {
                if (r.closeOperation && r.closeOperation->key == closeKey)
                {
                    if (kind == VisibilityMessage::CloseReceived)
                    {
                        if (r.closeOperation->result == RoleCloseResult::Uncertain) r.closeOperation->result = RoleCloseResult::Received;
                    }
                    else
                    {
                        const auto result = static_cast<RoleCloseResult>(value);
                        if (!terminalCloseResult(r.closeOperation->result)) r.closeOperation->result = result;
                        if (result == RoleCloseResult::Pending || terminalCloseResult(result))
                            if (r.closeDeadline) r.closeDeadline->stop();
                        if (terminalCloseResult(result) || result == RoleCloseResult::Uncertain) r.closeQueryOutstanding = false;
                    }
                } // Unknown/older results never finish a new operation.
            }
            else if (kind == VisibilityMessage::DeploymentState && request)
            {
                if (request > r.deploymentRevision)
                {
                    r.deploymentRevision = request;
                    r.deployment = DesktopDeploymentProjection{(value & 1) != 0, (value & 2) != 0, (value & 4) != 0};
                } // Older/equal revision never rewinds the current incarnation's projection.
            }
            else if ((kind == VisibilityMessage::DeploymentReceived || kind == VisibilityMessage::DeploymentResult)
                && request && request <= r.deploymentRequest)
            {
                if (request == r.deploymentRequest)
                {
                    if (kind == VisibilityMessage::DeploymentReceived && request <= r.deploymentCompletedRequest) continue;
                    r.deploymentResult = kind == VisibilityMessage::DeploymentReceived
                        ? DesktopDeploymentResult::Received : static_cast<DesktopDeploymentResult>(value);
                    if (kind == VisibilityMessage::DeploymentResult)
                    {
                        r.deploymentCompletedRequest = request;
                        if (r.deploymentDeadline) r.deploymentDeadline->stop();
                    }
                }
            }
            else { peer->abort(); return; }
        }
        updateTray();
    });
    updateTray();
    (void)writeVisibility(socket, visibilityFrame(VisibilityMessage::Qualified, epoch_, incarnation, 0, recoveryAvailable(*record)));
}

void ApplicationCoordinator::removeTerminatedSessions()
{
    for (const auto& record : sessions_)
        if (WaitForSingleObject(record->process.value, 0) == WAIT_OBJECT_0 && record->closeOperation)
            rememberClosedRole(record->incarnation, *record->closeOperation);
    std::erase_if(sessions_, [](const auto& record) {
        // Access error, lost connection and hidden/closed HWND are NOT verified process death.
        return WaitForSingleObject(record->process.value, 0) == WAIT_OBJECT_0;
    });
    if (!showAllAwaiting_.isNull() && std::none_of(sessions_.begin(), sessions_.end(), [this](const auto& record) { return record->incarnation == showAllAwaiting_; }))
        completeShowAllTarget(showAllAwaiting_, showAllRequest_);
    scheduleDrain();
}

SessionCoordinatorJoin::SessionCoordinatorJoin(const InstanceSlot& sessionSlot, const InstanceSlot& launcherSlot)
    : sessionSlot_(sessionSlot), launcherSlot_(launcherSlot)
{
    bootstrapCoordinator_ = [] {
        return QProcess::startDetached(QCoreApplication::applicationFilePath(), {QString::fromLatin1(CoordinatorBootstrapArgument)});
    };
    requestClock_.start();
    retry_.setInterval(20);
    connect(&retry_, &QTimer::timeout, this, [this] { attempt(); });
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { finish(false); });
    socket_.setReadBufferSize(JoinAck.size() + 17);
    connect(&socket_, &QLocalSocket::readyRead, this, [this] {
        if (presentation_) { consumeControl(); return; }
        reply_ += socket_.readAll();
        if (reply_.size() >= JoinAck.size() + 16)
            finish(reply_.size() == JoinAck.size() + 16 && reply_.startsWith(JoinAck)
                && !QUuid::fromRfc4122(reply_.mid(JoinAck.size())).isNull());
    }, Qt::QueuedConnection); // Ordinary Close may exec() a dialog: do not hold QIODevice's non-recursive readyRead emission.
    connect(&socket_, &QLocalSocket::disconnected, this, [this] { revokeQualification(); if (!finished_) finish(false); });
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this] { revokeQualification(); if (!finished_) finish(false); });
    renewal_.setInterval(SessionQualificationRenewalMs);
    connect(&renewal_, &QTimer::timeout, this, [this] { renewQualification(); });
    expiry_.setSingleShot(true);
    connect(&expiry_, &QTimer::timeout, this, [this] { revokeQualification(); socket_.abort(); });
    closeDrainDeadline_.setSingleShot(true);
    connect(&closeDrainDeadline_, &QTimer::timeout, this, [this] { finishDraining(); });
    connect(&socket_, &QLocalSocket::bytesWritten, this, [this] {
        if (drainedClose_ && socket_.bytesToWrite() == 0) finishDraining();
    });
}

SessionCoordinatorJoin::~SessionCoordinatorJoin()
{
    // QLocalSocket destruction can emit disconnected synchronously. Retire its
    // callbacks and abort while timer/reply members are alive, not during member teardown.
    stop();
    disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
    coordinatorDeath_.reset();
    if (coordinatorLifetime_) CloseHandle(coordinatorLifetime_);
}

void SessionCoordinatorJoin::enableControl(SessionPresentation& presentation)
{
    if (started_) return;
    presentation_ = &presentation;
    socket_.setReadBufferSize(MaximumVisibilityBufferedBytes + 1);
    presentation.bindRecovery([this] { return qualified(); }, [this] { requestHidePermit(); });
    presentation.bindCloseReport([this] {
        publishCloseCapability(); publishDesktopDeployment();
        if (presentation_ && presentation_->closeOperation()
            && presentation_->closeOperation()->key.generation == coordinatorEpoch_)
        {
            const auto operation = *presentation_->closeOperation();
            (void)writeVisibility(socket_, visibilityFrame(VisibilityMessage::CloseState,
                operation.key.generation, incarnation_, operation.key.request, static_cast<quint8>(operation.result)));
            socket_.flush();
        }
    });
}

void SessionCoordinatorJoin::enableDesktopDeployment(std::function<DesktopDeploymentProjection()> projection,
    std::function<DesktopDeploymentResult(DesktopDeploymentIntent)> execute)
{
    if (started_) return;
    deploymentProjection_ = std::move(projection);
    deploymentExecute_ = std::move(execute);
}

void SessionCoordinatorJoin::publishDesktopDeployment()
{
    if (!joined_ || !deploymentProjection_ || socket_.state() != QLocalSocket::ConnectedState) return;
    const auto projection = deploymentProjection_();
    if (!lastDeploymentProjection_ || projection != *lastDeploymentProjection_)
    { lastDeploymentProjection_ = projection; ++deploymentRevision_; }
    const quint8 value = (projection.retained ? 1 : 0) | (projection.canDeploy ? 2 : 0) | (projection.canUndeploy ? 4 : 0);
    (void)writeVisibility(socket_, visibilityFrame(VisibilityMessage::DeploymentState, coordinatorEpoch_, incarnation_, deploymentRevision_, value));
}

void SessionCoordinatorJoin::publishCloseCapability()
{
    if (!joined_ || !presentation_ || socket_.state() != QLocalSocket::ConnectedState) return;
    const bool capability = presentation_->canClose();
    if (!lastCloseCapability_ || *lastCloseCapability_ != capability)
    { lastCloseCapability_ = capability; ++closeRevision_; }
    (void)writeVisibility(socket_, visibilityFrame(VisibilityMessage::CloseCapability,
        coordinatorEpoch_, incarnation_, closeRevision_, capability));
}

void SessionCoordinatorJoin::beginRoleTeardown()
{
    roleEnding_ = true;
    recoveryQualified_ = false;
    retry_.stop(); deadline_.stop(); renewal_.stop(); expiry_.stop();
    // Keep only result/query drainage alive until the ordinary closeEvent has finished Engine/BGM teardown.
}

void SessionCoordinatorJoin::drainAndStop(std::function<void()> completed)
{
    if (drainedClose_) return;
    drainedClose_ = std::move(completed);
    socket_.flush();
    if (socket_.bytesToWrite() == 0 || socket_.state() != QLocalSocket::ConnectedState) finishDraining();
    else closeDrainDeadline_.start(100); // Transport drainage, not a Save/Discard/Cancel timeout.
}

void SessionCoordinatorJoin::finishDraining()
{
    closeDrainDeadline_.stop();
    auto completed = std::move(drainedClose_);
    drainedClose_ = {};
    stop();
    if (completed) completed();
}

void SessionCoordinatorJoin::stop()
{
    roleEnding_ = true;
    finished_ = true;
    retry_.stop(); deadline_.stop(); renewal_.stop(); expiry_.stop();
    recoveryQualified_ = false;
    if (presentation_) { presentation_->bindCloseReport({}); presentation_->bindRecovery({}, {}); }
    socket_.abort();
}

bool SessionCoordinatorJoin::qualified() const
{
    return !roleEnding_ && joined_ && recoveryQualified_ && presentation_ && coordinatorLifetime_
        && socket_.state() == QLocalSocket::ConnectedState && qualificationAge_.isValid()
        && qualificationAge_.elapsed() < SessionQualificationLifetimeMs
        && WaitForSingleObject(coordinatorLifetime_, 0) == WAIT_TIMEOUT;
}

void SessionCoordinatorJoin::revokeQualification()
{
    recoveryQualified_ = false;
    renewal_.stop(); expiry_.stop(); hideRequest_ = 0;
    if (presentation_) presentation_->refreshEligibility();
}

void SessionCoordinatorJoin::recoverDeadCoordinator(void* observedLifetime)
{
    recoveryQueued_ = false;
    if (roleEnding_ || !primaryWindow_ || !presentation_ || !observedLifetime
        || observedLifetime != coordinatorLifetime_ || WaitForSingleObject(observedLifetime, 0) != WAIT_OBJECT_0) return;
    revokeQualification(); // Fail visible without activation before attempting any replacement.
    finished_ = true; // Abort callbacks retire the old channel, never finish the forthcoming Join.
    joined_ = false;
    socket_.abort();
    coordinatorDeath_.reset();
    CloseHandle(coordinatorLifetime_); coordinatorLifetime_ = nullptr;
    coordinatorPid_ = 0; retiredCoordinatorEpoch_ = coordinatorEpoch_; coordinatorEpoch_ = {};
    reply_.clear();
    sequence_ = renewalRequest_ = hideRequest_ = lastShowRequest_ = 0;
    lastDeploymentRequest_ = lastCloseRequest_ = 0;
    lastDeploymentProjection_.reset(); deploymentRevision_ = 0;
    lastCloseCapability_.reset(); closeRevision_ = 0;
    qualificationAge_.invalidate();
    bootstrapAttempted_ = false; // Exactly once for this retained, now-signaled owner lifetime.
    finished_ = false;
    std::cout << "[Coordinator] Verified owner death; bounded rejoin incarnation=" << incarnation_.toString().toStdString() << std::endl;
    deadline_.start(ActivationDeadlineMs); retry_.start(); attempt();
}

void SessionCoordinatorJoin::renewQualification()
{
    if (socket_.state() != QLocalSocket::ConnectedState || renewalRequest_) return;
    publishDesktopDeployment(); // Sample target-owned capability, including local UI/display reconciliation changes.
    publishCloseCapability();
    renewalRequest_ = ++sequence_;
    renewalSentAt_ = requestClock_.elapsed();
    (void)writeVisibility(socket_, visibilityFrame(VisibilityMessage::Renew, coordinatorEpoch_, incarnation_, renewalRequest_, true));
    if (presentation_) presentation_->refreshEligibility();
}

void SessionCoordinatorJoin::requestHidePermit()
{
    if (!qualified() || hideRequest_) return;
    hideRequest_ = ++sequence_;
    hideSentAt_ = requestClock_.elapsed();
    hideShowGeneration_ = presentation_->showGeneration();
    (void)writeVisibility(socket_, visibilityFrame(VisibilityMessage::HidePermit, coordinatorEpoch_, incarnation_, hideRequest_, true));
}

void SessionCoordinatorJoin::consumeControl()
{
    reply_ += socket_.readAll();
    if (reply_.size() > MaximumVisibilityBufferedBytes) { socket_.abort(); return; }
    while (reply_.size() >= VisibilityFrameBytes)
    {
        const auto frame = reply_.first(VisibilityFrameBytes); reply_.remove(0, VisibilityFrameBytes);
        const auto epoch = QUuid::fromRfc4122(frame.mid(1, 16));
        const auto request = qFromBigEndian<quint64>(frame.constData() + 33);
        const auto kind = static_cast<VisibilityMessage>(frame.front());
        // Epoch identifies the requested ledger key, not renewed owner authority.
        // Only a qualified current channel may make this read-only exception.
        const bool qualifiedCloseQuery = kind == VisibilityMessage::CloseQuery && request && frame.back() == 0 && qualified();
        if (!epoch.isNull() && epoch == retiredCoordinatorEpoch_ && !qualifiedCloseQuery) continue;
        if (epoch.isNull() || QUuid::fromRfc4122(frame.mid(17, 16)) != incarnation_
            || (!coordinatorEpoch_.isNull() && epoch != coordinatorEpoch_ && !qualifiedCloseQuery)
            || (frame.back() != 0 && frame.back() != 1))
        { socket_.abort(); return; }
        if (kind == VisibilityMessage::Qualified && ((!finished_ && request == 0) || (request && request == renewalRequest_)))
        {
            const auto age = requestClock_.elapsed() - (request ? renewalSentAt_ : joinSentAt_);
            // A queued old reply cannot manufacture a new lease ahead of an overdue timer.
            if (age >= SessionQualificationLifetimeMs
                || (finished_ && (!qualificationAge_.isValid()
                    || qualificationAge_.elapsed() >= SessionQualificationLifetimeMs)))
            { socket_.abort(); return; }
            coordinatorEpoch_ = epoch;
            renewalRequest_ = 0;
            qualificationAge_.restart();
            recoveryQualified_ = frame.back() != 0;
            expiry_.start(SessionQualificationLifetimeMs);
            if (!finished_) finish(true);
            if (presentation_) presentation_->refreshEligibility();
            publishDesktopDeployment();
            publishCloseCapability();
        }
        else if (kind == VisibilityMessage::Permit && request && request == hideRequest_)
        {
            hideRequest_ = 0;
            if (frame.back() && requestClock_.elapsed() - hideSentAt_ < SessionQualificationLifetimeMs
                && qualified() && presentation_ && hideShowGeneration_ == presentation_->showGeneration())
                (void)presentation_->applyHidePermit();
        }
        else if (kind == VisibilityMessage::Show && request && request > lastShowRequest_)
        {
            lastShowRequest_ = request;
            const bool completed = presentation_ && presentation_->restore() && primaryWindow_;
            if (completed) surfaceAdmittedRole(*primaryWindow_, frame.back() ? ActivationRequest::Request : ActivationRequest::NoRequest);
            std::cout << "[Tray] Show completed=" << completed << " activation=" << (frame.back() ? "request" : "none")
                << " incarnation=" << incarnation_.toString().toStdString() << std::endl;
            if (!writeVisibility(socket_, visibilityFrame(VisibilityMessage::Result, epoch, incarnation_, request, completed))) return;
        }
        else if (kind == VisibilityMessage::CloseQuery && request && frame.back() == 0)
        {
            if (!qualifiedCloseQuery) continue;
            const auto result = presentation_ ? presentation_->queryClose({epoch, request}) : RoleCloseResult::Uncertain;
            if (!writeVisibility(socket_, visibilityFrame(VisibilityMessage::CloseState, epoch, incarnation_, request,
                static_cast<quint8>(result)))) return; // Authenticated same-incarnation observation only; old key cannot execute.
        }
        else if (kind == VisibilityMessage::Close && request && frame.back() == 0)
        {
            if (!writeVisibility(socket_, visibilityFrame(VisibilityMessage::CloseReceived, epoch, incarnation_, request, 1))) return;
            const RoleCloseKey key{epoch, request};
            const bool fresh = request > lastCloseRequest_;
            lastCloseRequest_ = (std::max)(lastCloseRequest_, request);
            const auto result = !presentation_ ? RoleCloseResult::Rejected
                : !fresh ? presentation_->queryClose(key)
                : roleEnding_ ? RoleCloseResult::Rejected : presentation_->requestClose(key);
            publishCloseCapability(); publishDesktopDeployment();
            if (epoch != coordinatorEpoch_ || socket_.state() != QLocalSocket::ConnectedState) return;
            if (!writeVisibility(socket_, visibilityFrame(VisibilityMessage::CloseState, epoch, incarnation_, request,
                static_cast<quint8>(result)))) return;
            socket_.flush();
        }
        else if ((kind == VisibilityMessage::Deploy || kind == VisibilityMessage::Undeploy)
            && request && request > lastDeploymentRequest_ && frame.back() == 0)
        {
            lastDeploymentRequest_ = request;
            if (!writeVisibility(socket_, visibilityFrame(VisibilityMessage::DeploymentReceived, epoch, incarnation_, request, 1))) return;
            const auto result = !roleEnding_ && presentation_ && !presentation_->closePending()
                && !presentation_->ending() && deploymentExecute_ ? deploymentExecute_(kind == VisibilityMessage::Deploy
                ? DesktopDeploymentIntent::Deploy : DesktopDeploymentIntent::Undeploy) : DesktopDeploymentResult::Rejected;
            publishDesktopDeployment();
            std::cout << "[Tray] Deployment result=" << static_cast<int>(result) << " request=" << request << std::endl;
            if (epoch != coordinatorEpoch_ || socket_.state() != QLocalSocket::ConnectedState) return;
            if (!writeVisibility(socket_, visibilityFrame(VisibilityMessage::DeploymentResult, epoch, incarnation_, request,
                static_cast<quint8>(result)))) return;
        }
        else { socket_.abort(); return; }
        if (presentation_ && socket_.state() == QLocalSocket::ConnectedState)
            if (!writeVisibility(socket_, visibilityFrame(VisibilityMessage::State, coordinatorEpoch_, incarnation_, 0, presentation_->hiddenToTray()))) return;
    }
}

void SessionCoordinatorJoin::start(QWidget& primaryWindow, bool allowBootstrap)
{
    if (started_) return;
    started_ = true;
    primaryWindow_ = &primaryWindow;
    bootstrapAllowed_ = allowBootstrap;
    deadline_.start(ActivationDeadlineMs);
    retry_.start();
    attempt();
}

void SessionCoordinatorJoin::attempt()
{
    if (finished_) return;
    if (!primaryWindow_) { finish(false); return; }
    const HANDLE pipe = openPipe(coordinatorEndpointName(launcherSlot_));
    if (pipe == INVALID_HANDLE_VALUE)
    {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY) { finish(false); return; }
        if (error == ERROR_FILE_NOT_FOUND && bootstrapAllowed_ && !bootstrapAttempted_)
        {
            bootstrapAttempted_ = true;
            if (!bootstrapCoordinator_()) finish(false);
        }
        return;
    }
    retry_.stop();
    ULONG pid = 0;
    ProcessHandle process;
    const bool peerKnown = GetNamedPipeServerProcessId(pipe, &pid) && pid != 0;
    if (peerKnown) process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process.value || !sameApplicationPeer(process.value, pid)
        || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT || !activationOwnedBy(launcherSlot_, pid))
    { CloseHandle(pipe); finish(false); return; }
    coordinatorPid_ = pid;
    if (presentation_)
    {
        coordinatorLifetime_ = process.value; process.value = nullptr;
        coordinatorDeath_ = std::make_unique<QWinEventNotifier>(coordinatorLifetime_);
        connect(coordinatorDeath_.get(), &QWinEventNotifier::activated, this, [this] {
            coordinatorDeath_->setEnabled(false);
            revokeQualification();
            socket_.abort();
            if (!roleEnding_ && !recoveryQueued_ && WaitForSingleObject(coordinatorLifetime_, 0) == WAIT_OBJECT_0)
            {
                recoveryQueued_ = true;
                const auto observed = coordinatorLifetime_;
                QTimer::singleShot(0, this, [this, observed] { recoverDeadCoordinator(observed); });
            }
        });
    }
    if (!socket_.setSocketDescriptor(reinterpret_cast<qintptr>(pipe), QLocalSocket::ConnectedState, QIODevice::ReadWrite))
    { CloseHandle(pipe); finish(false); return; }
    auto frame = coordinatorJoinFrame(sessionSlot_, launcherSlot_, incarnation_,
        static_cast<quintptr>(primaryWindow_->winId()));
    if (presentation_ && !frame.isEmpty())
    {
        frame.replace(4, JoinMagic.size(), ControlJoinMagic);
        qToBigEndian<quint32>(static_cast<quint32>(frame.size() - 4), frame.data());
    }
    joinSentAt_ = requestClock_.elapsed();
    if (frame.isEmpty() || socket_.write(frame) != frame.size()) finish(false);
}

void SessionCoordinatorJoin::finish(bool joined)
{
    if (finished_) return;
    finished_ = true;
    joined_ = joined;
    retry_.stop();
    deadline_.stop();
    if (!joined_ || !presentation_) socket_.abort();
    else renewal_.start();
    if (joined_) std::cout << "Coordinator Session joined. PID=" << coordinatorPid_ << std::endl;
    else std::cerr << "[Coordinator] Ready Session Join unavailable; Session remains independently visible." << std::endl;
}
}
