#pragma once

#include "InstanceAdmission.h"

#include <QLocalServer>
#include <QPointer>
#include <QWidget>

#include <functional>

namespace DesktopSceneEngine::Player
{
inline constexpr int ActivationDeadlineMs = 8000;
inline constexpr int ActivationClientLifetimeMs = 2000;
inline constexpr int MaximumActivationBodyBytes = 128 * 1024;
inline constexpr int MaximumActivationClients = 16;

[[nodiscard]] QByteArray activationRequestFrame(const InstanceSlot& slot);
enum class ActivationRequest { Request, NoRequest };
void surfaceAdmittedRole(QWidget& role, ActivationRequest activation = ActivationRequest::Request);

// Process-owned transport only. Never authorizes admission or owns a role/Engine.
class InstanceActivationEndpoint final : public QObject
{
public:
    explicit InstanceActivationEndpoint(const InstanceSlot& slot);
    ~InstanceActivationEndpoint() override;
    [[nodiscard]] bool listen();
    void bindReadyRole(QWidget& role, std::function<void()> restorePresentation = {});
    // Coordinator-only: ACTIVATE may recreate an absent UI without ending admission.
    // A null result preserves the existing bounded admission-to-ready wait.
    void setReadyRoleProvider(std::function<QWidget*()> provider);
    void beginShutdown();
    [[nodiscard]] QString errorString() const { return server_.errorString(); }
    [[nodiscard]] bool isListening() const { return !closing_ && server_.isListening(); }

private:
    struct Request;
    void acceptConnections();
    void consume(QLocalSocket* socket, Request& request);
    InstanceSlot slot_;
    QLocalServer server_;
    QPointer<QWidget> role_;
    std::function<QWidget*()> roleProvider_;
    std::function<void()> restorePresentation_;
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
