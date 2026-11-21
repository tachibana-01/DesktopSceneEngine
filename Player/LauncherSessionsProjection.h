#pragma once

#include "RoleClose.h"
#include <DesktopSceneEngine/DisplayIdentity.h>

#include <QJsonObject>
#include <QUuid>

#include <optional>
#include <vector>

namespace DesktopSceneEngine::Player
{
// Raw Coordinator observations only. Registry absence says nothing about an
// admission slot or an unjoined process. Identity remains the exact incarnation.
struct LauncherRegisteredSession
{
    DisplayId boundDisplayId;
    QUuid incarnation;
    bool controlConnected = false;
    std::optional<RoleCloseOperation> closeOperation;

    friend bool operator==(const LauncherRegisteredSession& left, const LauncherRegisteredSession& right)
    {
        if (left.boundDisplayId != right.boundDisplayId || left.incarnation != right.incarnation
            || left.controlConnected != right.controlConnected
            || left.closeOperation.has_value() != right.closeOperation.has_value()) return false;
        return !left.closeOperation || (left.closeOperation->key == right.closeOperation->key
            && left.closeOperation->result == right.closeOperation->result);
    }
};

// A launch receipt is correlation, never a Session/admission identity. Terminal
// receipts may outlive the observed process and do not populate the live set.
enum class LauncherCreateState { Pending, Registered, LaunchFailed, ExitedBeforeRegistration, Uncertain };
struct LauncherCreateOperation
{
    DisplayId displayId;
    QUuid operationId;
    LauncherCreateState state = LauncherCreateState::Pending;
    std::optional<QUuid> incarnation;
    friend bool operator==(const LauncherCreateOperation&, const LauncherCreateOperation&) = default;
};

enum class LauncherCreateStatus
{
    Accepted, AlreadyPending, AlreadyRegistered, NotOwnerCapable, Unresolved,
    CatalogUnavailable, CapacityExceeded, LaunchFailed, StaleCoordinator, Unavailable
};
struct LauncherCreateResult
{
    QUuid coordinatorEpoch;
    DisplayId displayId;
    LauncherCreateStatus status = LauncherCreateStatus::Unavailable;
    std::optional<QUuid> operationId;
    std::optional<QUuid> incarnation;
};

enum class LauncherOpenStatus
{
    ActivationRequested, RoleGone, ControlUnavailable, Closing, StaleCoordinator, ApiFailure, Unavailable
};
struct LauncherOpenResult
{
    QUuid coordinatorEpoch;
    QUuid incarnation;
    LauncherOpenStatus status = LauncherOpenStatus::Unavailable;
};

struct LauncherSessionsSnapshot
{
    QUuid coordinatorEpoch;
    quint64 revision = 0; // Full-snapshot transport ordering, never domain lifecycle.
    std::vector<LauncherRegisteredSession> registeredSessions;
    std::vector<LauncherCreateOperation> createOperations;

    friend bool operator==(const LauncherSessionsSnapshot&, const LauncherSessionsSnapshot&) = default;
};

// Shared by bootstrap and live projection. Invalid/duplicate registry facts
// reject the complete snapshot; they are never silently dropped or selected.
// RoleGone receipts belong to history after verified removal, not this live set.
[[nodiscard]] QJsonObject launcherSessionsProjection(const LauncherSessionsSnapshot& snapshot);
[[nodiscard]] QJsonObject launcherCreateResult(const LauncherCreateResult& result);
[[nodiscard]] QJsonObject launcherOpenResult(const LauncherOpenResult& result);
}
