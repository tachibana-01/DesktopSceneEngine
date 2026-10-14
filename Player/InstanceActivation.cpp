#include "InstanceActivation.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <windows.h>

#include <algorithm>
#include <memory>

namespace DesktopSceneEngine::Player
{
namespace
{
const QByteArray ActivationAck = QByteArrayLiteral("DSE-ACTIVATE-ACK-1");
}

QByteArray activationRequestFrame(const InstanceSlot& slot)
{
    QByteArray body = QByteArrayLiteral("DSE-ACTIVATE-1\n");
    body.append(slot.isLauncher() ? 'L' : 'S');
    body.append(slot.canonicalIdentity());
    if (body.size() > MaximumActivationBodyBytes) return {};
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    frame.append(body);
    return frame;
}

void surfaceAdmittedRole(QWidget& role, ActivationRequest activation)
{
    const bool requestActivation = activation == ActivationRequest::Request;
    const bool priorShowWithoutActivating = role.testAttribute(Qt::WA_ShowWithoutActivating);
    if (!requestActivation) role.setAttribute(Qt::WA_ShowWithoutActivating);
    if (!requestActivation && role.isMinimized()) role.hide();
    // Restore the existing state, not showNormal(): a minimized maximized role stays maximized.
    if (role.isMinimized()) role.setWindowState(role.windowState() & ~Qt::WindowMinimized);
    role.show();
    role.setAttribute(Qt::WA_ShowWithoutActivating, priorShowWithoutActivating);
    QWidget* target = &role;
    if (auto* modal = QApplication::activeModalWidget(); modal
        && (modal->windowModality() == Qt::ApplicationModal || modal == &role || role.isAncestorOf(modal)))
        target = modal;
    // Native QFileDialog has a hidden Qt modal wrapper, not the displayed dialog HWND.
    // Resolve the currently blocking owned popup transiently; never foreground that wrapper/parent.
    const bool hiddenModalWrapper = target->testAttribute(Qt::WA_DontShowOnScreen);
    QWidget* blockerOwner = hiddenModalWrapper && target->parentWidget() ? target->parentWidget()->window() : &role;
    const auto nativeBase = reinterpret_cast<HWND>((hiddenModalWrapper ? blockerOwner : target)->winId());
    if (hiddenModalWrapper || !IsWindowEnabled(nativeBase))
    {
        // GetLastActivePopup must start at the unowned root, including nested owned dialogs.
        const auto rootOwner = GetAncestor(nativeBase, GA_ROOTOWNER);
        const auto popup = GetLastActivePopup(rootOwner);
        DWORD popupProcess = 0;
        if (popup) GetWindowThreadProcessId(popup, &popupProcess);
        if (popup && popup != nativeBase && IsWindowVisible(popup) && IsWindowEnabled(popup)
            && popupProcess == GetCurrentProcessId()
            && GetAncestor(popup, GA_ROOTOWNER) == rootOwner)
        {
            if (!requestActivation) SetWindowPos(popup, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (requestActivation && !SetForegroundWindow(popup)) QApplication::alert(&role);
        }
        else if (requestActivation) QApplication::alert(&role);
        return;
    }
    target->raise();
    if (!requestActivation) return;
    target->activateWindow();
    if (!SetForegroundWindow(reinterpret_cast<HWND>(target->winId()))) QApplication::alert(target);
}

struct InstanceActivationEndpoint::Request
{
    QByteArray bytes;
    bool completed = false;
};

InstanceActivationEndpoint::InstanceActivationEndpoint(const InstanceSlot& slot) : slot_(slot)
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    server_.setMaxPendingConnections(MaximumActivationClients);
    server_.setListenBacklogSize(MaximumActivationClients);
    connect(&server_, &QLocalServer::newConnection, this, [this] { acceptConnections(); });
}

InstanceActivationEndpoint::~InstanceActivationEndpoint() { beginShutdown(); }

bool InstanceActivationEndpoint::listen()
{
    return !closing_ && !activationRequestFrame(slot_).isEmpty() && server_.listen(slot_.endpointName());
}

void InstanceActivationEndpoint::bindReadyRole(QWidget& role, std::function<void()> restorePresentation)
{
    if (closing_) return;
    role_ = &role;
    restorePresentation_ = std::move(restorePresentation);
    connect(&role, &QObject::destroyed, this, [this] { beginShutdown(); });
    // Complete requests accepted during a legitimate admission-to-ready startup gap.
    for (auto* socket : findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
        QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection);
}

void InstanceActivationEndpoint::setReadyRoleProvider(std::function<QWidget*()> provider)
{
    if (closing_) return;
    roleProvider_ = std::move(provider);
    for (auto* socket : findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly))
        QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection);
}

void InstanceActivationEndpoint::acceptConnections()
{
    while (server_.hasPendingConnections())
    {
        auto* socket = server_.nextPendingConnection();
        if (!socket) continue;
        if (closing_ || findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly).size() >= MaximumActivationClients)
        {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        socket->setParent(this);
        socket->setReadBufferSize(MaximumActivationBodyBytes + 5);
        auto request = std::make_shared<Request>();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, request] { consume(socket, *request); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        // One finite request lifetime, not a slot heartbeat or occupancy poll. Never extended by trickled bytes.
        QTimer::singleShot(ActivationClientLifetimeMs, socket, [socket] { socket->abort(); socket->deleteLater(); });
        consume(socket, *request);
    }
}

void InstanceActivationEndpoint::consume(QLocalSocket* socket, Request& request)
{
    if (closing_ || request.completed) return;
    request.bytes.append(socket->readAll());
    if (request.bytes.size() < 4) return;
    const auto size = qFromBigEndian<quint32>(request.bytes.constData());
    if (size == 0 || size > MaximumActivationBodyBytes || request.bytes.size() > static_cast<qsizetype>(size) + 4)
    {
        request.completed = true;
        socket->abort();
        return;
    }
    if (request.bytes.size() != static_cast<qsizetype>(size) + 4) return;
    if (request.bytes != activationRequestFrame(slot_))
    {
        request.completed = true;
        socket->abort();
        return;
    }
    QPointer<QWidget> role = roleProvider_ ? roleProvider_() : role_.data();
    if (!role) return;
    request.completed = true;
    if (restorePresentation_) restorePresentation_();
    if (closing_ || !role) { socket->abort(); return; }
    surfaceAdmittedRole(*role);
    if (closing_ || !role) { socket->abort(); return; }
    socket->write(ActivationAck);
    socket->disconnectFromServer();
}

void InstanceActivationEndpoint::beginShutdown()
{
    closing_ = true;
    role_.clear();
    roleProvider_ = {};
    restorePresentation_ = {};
    server_.close();
    for (auto* socket : findChildren<QLocalSocket*>(QString{}, Qt::FindDirectChildrenOnly)) socket->abort();
    // QObject destruction closes/deletes accepted sockets before the enclosing bootstrap releases the lease.
}

ActivationForwardResult forwardActivation(const InstanceSlot& slot, int deadlineMs)
{
    ActivationForwardResult result;
    const auto frame = activationRequestFrame(slot);
    if (frame.isEmpty() || deadlineMs <= 0) return result;
    QElapsedTimer time;
    time.start();
    const auto pipeName = (QStringLiteral("\\\\.\\pipe\\") + slot.endpointName()).toStdWString();
    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (time.elapsed() < deadlineMs)
    {
        // QLocalSocket::connectToServer on Qt 6.10.1 Windows has an inline 5s PIPE_BUSY wait.
        // Open an overlapped, non-inheritable native pipe without that wait, then adopt it through the public Qt API.
        pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        result.nativeError = GetLastError();
        if (result.nativeError != ERROR_FILE_NOT_FOUND && result.nativeError != ERROR_PIPE_BUSY)
            return result; // Access/resource/native errors are not an unoccupied slot.
        const auto remaining = deadlineMs - time.elapsed();
        if (remaining <= 0) break;
        QThread::msleep(static_cast<unsigned long>((std::min)(qint64{20}, remaining)));
    }
    if (pipe == INVALID_HANDLE_VALUE) { result.outcome = ActivationForwardOutcome::TimedOut; return result; }
    QLocalSocket socket;
    if (!socket.setSocketDescriptor(reinterpret_cast<qintptr>(pipe), QLocalSocket::ConnectedState, QIODevice::ReadWrite))
    {
        CloseHandle(pipe);
        return result;
    }
    socket.setReadBufferSize(ActivationAck.size() + 1);
    ULONG serverPid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &serverPid) || serverPid == 0)
    {
        result.nativeError = GetLastError();
        return result;
    }
    result.foregroundPermissionGranted = AllowSetForegroundWindow(serverPid);
    result.nativeError = 0;
    socket.write(frame); // Permission transfer precedes ACTIVATE; no PID is persisted or sent in the protocol.
    while (socket.bytesToWrite() > 0)
    {
        const auto remaining = deadlineMs - time.elapsed();
        if (remaining <= 0) { result.outcome = ActivationForwardOutcome::TimedOut; return result; }
        if (!socket.waitForBytesWritten(static_cast<int>(remaining)))
        {
            result.outcome = time.elapsed() >= deadlineMs ? ActivationForwardOutcome::TimedOut : ActivationForwardOutcome::Error;
            return result;
        }
    }
    QByteArray ack;
    while (ack.size() < ActivationAck.size())
    {
        ack.append(socket.readAll());
        if (ack.size() >= ActivationAck.size()) break;
        const auto remaining = deadlineMs - time.elapsed();
        if (remaining <= 0) { result.outcome = ActivationForwardOutcome::TimedOut; return result; }
        if (!socket.waitForReadyRead(static_cast<int>(remaining)))
        {
            ack.append(socket.readAll());
            if (ack == ActivationAck) break;
            result.outcome = time.elapsed() >= deadlineMs ? ActivationForwardOutcome::TimedOut : ActivationForwardOutcome::Error;
            return result;
        }
    }
    result.outcome = ack == ActivationAck ? ActivationForwardOutcome::Delivered : ActivationForwardOutcome::Error;
    return result;
}
}
