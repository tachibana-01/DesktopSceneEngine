#pragma once

#include "DisplaySessionBootstrap.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace DesktopSceneEngine::Player
{
struct InteractiveInstanceScope
{
    QString userSid;
    quint32 windowsSessionId = 0;
};

[[nodiscard]] std::optional<InteractiveInstanceScope> currentInteractiveInstanceScope();

// A derived name is not a second Display identity. canonicalIdentity retains the exact UTF-8.
class InstanceSlot final
{
public:
    [[nodiscard]] static std::optional<InstanceSlot> fromProcessMode(
        const ApplicationProcessMode& mode, const InteractiveInstanceScope& scope);
    [[nodiscard]] const QString& mutexName() const { return mutexName_; }
    [[nodiscard]] const QString& endpointName() const { return endpointName_; }
    [[nodiscard]] const QByteArray& canonicalIdentity() const { return canonicalIdentity_; }
    [[nodiscard]] bool isLauncher() const { return launcher_; }

private:
    InstanceSlot() = default;
    bool launcher_ = true;
    QByteArray canonicalIdentity_;
    QString mutexName_;
    QString endpointName_;
};

enum class InstanceAdmissionOutcome { Acquired, AbandonedAndAcquired, Occupied, Error };

// Non-movable: the bootstrap/UI thread acquires once and releases last on that same thread.
class InstanceAdmissionLease final
{
public:
    InstanceAdmissionLease() = default;
    ~InstanceAdmissionLease();
    InstanceAdmissionLease(const InstanceAdmissionLease&) = delete;
    InstanceAdmissionLease& operator=(const InstanceAdmissionLease&) = delete;
    [[nodiscard]] InstanceAdmissionOutcome acquire(const InstanceSlot& slot);
    [[nodiscard]] quint32 nativeError() const { return nativeError_; }
    [[nodiscard]] bool ownsAdmission() const { return owned_; }

private:
    void* handle_ = nullptr;
    quint32 ownerThread_ = 0;
    quint32 nativeError_ = 0;
    bool attempted_ = false;
    bool owned_ = false;
};
}
