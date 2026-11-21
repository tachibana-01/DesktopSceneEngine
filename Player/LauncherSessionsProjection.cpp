#include "LauncherSessionsProjection.h"

#include <QJsonArray>
#include <QSet>

#include <stdexcept>

namespace DesktopSceneEngine::Player
{
namespace
{
QString displayIdentity(const DisplayId& id, bool requireOwner)
{
    const auto canonical = id.canonicalRepresentation();
    if (!canonical || (requireOwner && !id.isPersistentOwnerCapable())
        || DisplayId::parseCanonicalRepresentation(*canonical) != id)
        throw std::invalid_argument("Invalid canonical Display identity");
    const auto display = QString::fromStdString(*canonical);
    if (display.size() > 32768 || display.toUtf8().toStdString() != *canonical)
        throw std::invalid_argument("Invalid or oversized canonical Display identity");
    return display;
}

QString createState(LauncherCreateState state)
{
    switch (state)
    {
    case LauncherCreateState::Pending: return QStringLiteral("Pending");
    case LauncherCreateState::Registered: return QStringLiteral("Registered");
    case LauncherCreateState::LaunchFailed: return QStringLiteral("LaunchFailed");
    case LauncherCreateState::ExitedBeforeRegistration: return QStringLiteral("ExitedBeforeRegistration");
    case LauncherCreateState::Uncertain: return QStringLiteral("Uncertain");
    }
    throw std::invalid_argument("Unknown native Create state");
}

QString createStatus(LauncherCreateStatus status)
{
    switch (status)
    {
    case LauncherCreateStatus::Accepted: return QStringLiteral("Accepted");
    case LauncherCreateStatus::AlreadyPending: return QStringLiteral("AlreadyPending");
    case LauncherCreateStatus::AlreadyRegistered: return QStringLiteral("AlreadyRegistered");
    case LauncherCreateStatus::NotOwnerCapable: return QStringLiteral("NotOwnerCapable");
    case LauncherCreateStatus::Unresolved: return QStringLiteral("Unresolved");
    case LauncherCreateStatus::CatalogUnavailable: return QStringLiteral("CatalogUnavailable");
    case LauncherCreateStatus::CapacityExceeded: return QStringLiteral("CapacityExceeded");
    case LauncherCreateStatus::LaunchFailed: return QStringLiteral("LaunchFailed");
    case LauncherCreateStatus::StaleCoordinator: return QStringLiteral("StaleCoordinator");
    case LauncherCreateStatus::Unavailable: return QStringLiteral("Unavailable");
    }
    throw std::invalid_argument("Unknown native Create result");
}

QString openStatus(LauncherOpenStatus status)
{
    switch (status)
    {
    case LauncherOpenStatus::ActivationRequested: return QStringLiteral("ActivationRequested");
    case LauncherOpenStatus::RoleGone: return QStringLiteral("RoleGone");
    case LauncherOpenStatus::ControlUnavailable: return QStringLiteral("ControlUnavailable");
    case LauncherOpenStatus::Closing: return QStringLiteral("Closing");
    case LauncherOpenStatus::StaleCoordinator: return QStringLiteral("StaleCoordinator");
    case LauncherOpenStatus::ApiFailure: return QStringLiteral("ApiFailure");
    case LauncherOpenStatus::Unavailable: return QStringLiteral("Unavailable");
    }
    throw std::invalid_argument("Unknown native Open result");
}

QString closeResult(RoleCloseResult result)
{
    switch (result)
    {
    case RoleCloseResult::Received: return QStringLiteral("Received");
    case RoleCloseResult::Pending: return QStringLiteral("Pending");
    case RoleCloseResult::CloseAccepted: return QStringLiteral("CloseAccepted");
    case RoleCloseResult::CloseAborted: return QStringLiteral("CloseAborted");
    case RoleCloseResult::Rejected: return QStringLiteral("Rejected");
    case RoleCloseResult::Uncertain: return QStringLiteral("Uncertain");
    case RoleCloseResult::RoleGone:
        throw std::invalid_argument("RoleGone receipt cannot belong to a live registered Session");
    }
    throw std::invalid_argument("Unknown native Session close result");
}
}

QJsonObject launcherSessionsProjection(const LauncherSessionsSnapshot& snapshot)
{
    constexpr quint64 MaximumSafeTransportRevision = 9007199254740991ULL;
    constexpr std::size_t MaximumRegisteredSessions = 64;
    if (snapshot.coordinatorEpoch.isNull() || snapshot.revision > MaximumSafeTransportRevision
        || snapshot.registeredSessions.size() > MaximumRegisteredSessions
        || snapshot.createOperations.size() > MaximumRegisteredSessions)
        throw std::invalid_argument("Invalid native Session snapshot identity, revision, or bound");

    QJsonArray sessions;
    QSet<QString> displays;
    QSet<QUuid> incarnations;
    for (const auto& session : snapshot.registeredSessions)
    {
        const auto canonical = session.boundDisplayId.canonicalRepresentation();
        if (!canonical || !session.boundDisplayId.isPersistentOwnerCapable() || session.incarnation.isNull()
            || DisplayId::parseCanonicalRepresentation(*canonical) != session.boundDisplayId)
            throw std::invalid_argument("Invalid registered Session Display or incarnation identity");
        const auto display = QString::fromStdString(*canonical);
        if (display.size() > 32768 || display.toUtf8().toStdString() != *canonical
            || displays.contains(display) || incarnations.contains(session.incarnation))
            throw std::invalid_argument("Invalid or duplicate live registered Session identity");
        displays.insert(display);
        incarnations.insert(session.incarnation);
        QJsonObject row{{"boundDisplayId", display},
            {"incarnation", session.incarnation.toString(QUuid::WithoutBraces)},
            {"controlConnected", session.controlConnected}};
        if (session.closeOperation)
        {
            const auto& operation = *session.closeOperation;
            if (operation.key.generation.isNull() || !operation.key.request)
                throw std::invalid_argument("Invalid registered Session close-operation key");
            row.insert("closeOperation", QJsonObject{
                {"generation", operation.key.generation.toString(QUuid::WithoutBraces)},
                // Unlike transport revisions, protocol request keys use all 64 bits.
                {"request", QString::number(operation.key.request)},
                {"result", closeResult(operation.result)}});
        }
        sessions.append(row);
    }
    QJsonArray operations;
    QSet<QString> operationDisplays;
    QSet<QUuid> operationIds;
    for (const auto& operation : snapshot.createOperations)
    {
        const auto display = displayIdentity(operation.displayId, true);
        const bool registered = operation.state == LauncherCreateState::Registered;
        if (operation.operationId.isNull() || operationIds.contains(operation.operationId)
            || operationDisplays.contains(display)
            || registered != operation.incarnation.has_value()
            || (operation.incarnation && operation.incarnation->isNull()))
            throw std::invalid_argument("Invalid or duplicate native Create operation");
        operationIds.insert(operation.operationId);
        operationDisplays.insert(display);
        QJsonObject row{{"displayId", display}, {"operationId", operation.operationId.toString(QUuid::WithoutBraces)},
            {"state", createState(operation.state)}};
        if (operation.incarnation) row.insert("incarnation", operation.incarnation->toString(QUuid::WithoutBraces));
        operations.append(row);
    }
    return {{"coordinatorEpoch", snapshot.coordinatorEpoch.toString(QUuid::WithoutBraces)},
        {"sessionRevision", static_cast<qint64>(snapshot.revision)},
        {"registeredSessionsScope", "verified-registered-only"}, {"registeredSessions", sessions},
        {"createOperations", operations}};
}

QJsonObject launcherCreateResult(const LauncherCreateResult& result)
{
    const bool keyed = result.status == LauncherCreateStatus::Accepted
        || result.status == LauncherCreateStatus::AlreadyPending || result.status == LauncherCreateStatus::LaunchFailed;
    const bool registered = result.status == LauncherCreateStatus::AlreadyRegistered;
    if (result.coordinatorEpoch.isNull() || keyed != result.operationId.has_value()
        || registered != result.incarnation.has_value() || (result.operationId && result.operationId->isNull())
        || (result.incarnation && result.incarnation->isNull()))
        throw std::invalid_argument("Invalid native Create result identity");
    QJsonObject row{{"coordinatorEpoch", result.coordinatorEpoch.toString(QUuid::WithoutBraces)},
        {"displayId", displayIdentity(result.displayId, false)}, {"status", createStatus(result.status)}};
    if (result.operationId) row.insert("operationId", result.operationId->toString(QUuid::WithoutBraces));
    if (result.incarnation) row.insert("incarnation", result.incarnation->toString(QUuid::WithoutBraces));
    return row;
}

QJsonObject launcherOpenResult(const LauncherOpenResult& result)
{
    if (result.coordinatorEpoch.isNull() || result.incarnation.isNull())
        throw std::invalid_argument("Invalid native Open result identity");
    return {{"coordinatorEpoch", result.coordinatorEpoch.toString(QUuid::WithoutBraces)},
        {"incarnation", result.incarnation.toString(QUuid::WithoutBraces)}, {"status", openStatus(result.status)}};
}
}
