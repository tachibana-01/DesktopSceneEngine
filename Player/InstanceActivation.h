#pragma once

#include "InstanceAdmission.h"

#include <QLocalServer>
#include <QPointer>
#include <QWidget>

namespace DesktopSceneEngine::Player
{
inline constexpr int ActivationDeadlineMs = 8000;
inline constexpr int ActivationClientLifetimeMs = 2000;
inline constexpr int MaximumActivationBodyBytes = 128 * 1024;
inline constexpr int MaximumActivationClients = 16;

[[nodiscard]] QByteArray activationRequestFrame(const InstanceSlot& slot);
void surfaceAdmittedRole(QWidget& role);

// Process-owned transport only. Never authorizes admission or owns a role/Engine.
class InstanceActivationEndpoint final : public QObject
{
public:
    explicit InstanceActivationEndpoint(const InstanceSlot& slot);
    ~InstanceActivationEndpoint() override;
    [[nodiscard]] bool listen();
    void bindReadyRole(QWidget& role);
    void beginShutdown();
    [[nodiscard]] QString errorString() const { return server_.errorString(); }

private:
    struct Request;
    void acceptConnections();
    void consume(QLocalSocket* socket, Request& request);
    InstanceSlot slot_;
    QLocalServer server_;
    QPointer<QWidget> role_;
    bool closing_ = false;
};

enum class ActivationForwardOutcome { Delivered, TimedOut, Error };
struct ActivationForwardResult
{
    ActivationForwardOutcome outcome = ActivationForwardOutcome::Error;
    quint32 nativeError = 0;
    // Transient diagnostics, never occupancy data. False is legitimate OS policy denial.
    bool foregroundPermissionGranted = false;
};
[[nodiscard]] ActivationForwardResult forwardActivation(
    const InstanceSlot& slot, int deadlineMs = ActivationDeadlineMs);
}
