#pragma once

#include "InstanceActivation.h"
#include "DesktopDeployment.h"
#include "RoleClose.h"
#include "LauncherSessionsProjection.h"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QTimer>
#include <QUuid>
#include <QMenu>
#include <QSystemTrayIcon>
#include <DesktopSceneEngine/Platform/Display.h>

#include <functional>
#include <memory>
#include <vector>

class ApplicationCoordinatorTestAccess;
class QWinEventNotifier;

namespace DesktopSceneEngine::Player
{
class SessionPresentation;
inline constexpr auto CoordinatorBootstrapArgument = "--coordinator-bootstrap";
inline constexpr int MaximumCoordinatorSessions = 64;
inline constexpr int SessionQualificationLifetimeMs = 3000;
inline constexpr int SessionQualificationRenewalMs = 1000;

[[nodiscard]] QString coordinatorEndpointName(const InstanceSlot& launcherSlot);
[[nodiscard]] QByteArray coordinatorJoinFrame(const InstanceSlot& sessionSlot,
    const InstanceSlot& launcherSlot, const QUuid& incarnation, quintptr primaryWindow);

// Owns optional Launcher UI and non-owning, verified Session process-lifetime records.
// Admission remains the existing Launcher lease; this never owns a Session or Engine.
class ApplicationCoordinator final : public QObject
{
public:
    ApplicationCoordinator(const InstanceSlot& launcherSlot, InstanceActivationEndpoint& activation,
        bool bootstrap, std::function<void()> drained = {});
    ~ApplicationCoordinator() override;
    [[nodiscard]] bool listen();
    void setLauncherFactory(std::function<QWidget*()> factory);
    QWidget* showLauncher();
    void retireLauncher(QWidget& window);
    [[nodiscard]] QWidget* launcherWindow() const { return launcher_.get(); }
    [[nodiscard]] QUuid launcherIncarnation() const { return launcherIncarnation_; }
    [[nodiscard]] std::size_t sessionCount() const { return sessions_.size(); }
    [[nodiscard]] QUuid coordinatorEpoch() const { return epoch_; }
    [[nodiscard]] std::vector<LauncherRegisteredSession> verifiedRegisteredSessions() const;
    // Read-only registry facts; revision orders transport snapshots, never admission/lifecycle.
    [[nodiscard]] LauncherSessionsSnapshot launcherSessionSnapshot();
    void setSessionProjectionChanged(std::function<void(const LauncherSessionsSnapshot&)> callback);
    [[nodiscard]] LauncherCreateResult createSession(const QUuid& coordinatorEpoch, const DisplayId& displayId);
    [[nodiscard]] LauncherOpenResult openSession(const QUuid& coordinatorEpoch, const QUuid& incarnation);
    void showSession(const QUuid& incarnation);
    void requestSessionDeployment(const QUuid& incarnation, DesktopDeploymentIntent intent);
    [[nodiscard]] DesktopDeploymentResult sessionDeploymentResult(const QUuid& incarnation) const;
    [[nodiscard]] bool requestRoleClose(const QUuid& incarnation);
    void reconcileRoleClose(const QUuid& incarnation, std::optional<RoleCloseKey> key = std::nullopt);
    [[nodiscard]] std::optional<RoleCloseOperation> roleCloseOperation(const QUuid& incarnation) const;

private:
    friend class ::ApplicationCoordinatorTestAccess;
    struct SessionRecord;
    struct CreateRecord;
    void acceptConnections();
    bool registerSession(QLocalSocket& socket, const QByteArray& body);
    void removeTerminatedSessions();
    void refreshSessionProjection();
    void reconcileCreateOperations(const std::vector<LauncherRegisteredSession>& registered);
    void finishCreateOperation(CreateRecord& record, LauncherCreateState state,
        std::optional<QUuid> incarnation = std::nullopt);
    [[nodiscard]] bool hasPendingCreate() const;
    [[nodiscard]] std::vector<LauncherCreateOperation> createOperationProjection() const;
    void scheduleDrain();
    void updateActivationProvider();
    void restoreRegisteredSessions();
    void shellRecreated();
    void updateTray();
    void beginSessionControl(QLocalSocket& socket, const QUuid& incarnation);
    [[nodiscard]] bool recoveryAvailable(const SessionRecord& record) const;
    [[nodiscard]] bool launcherRecoveryAvailable() const;
    void refreshRoleMenu();
    void showRole(const QUuid& incarnation, ActivationRequest activation = ActivationRequest::Request);
    quint64 dispatchSessionShow(const QUuid& incarnation, ActivationRequest activation);
    void showAll();
    void advanceShowAll();
    void completeShowAllTarget(const QUuid& incarnation, quint64 request);
    void rememberClosedRole(const QUuid& incarnation, RoleCloseOperation operation);
    struct RolePresentation
    {
        QUuid incarnation;
        QString label;
        bool launcher = false;
        friend bool operator==(const RolePresentation&, const RolePresentation&) = default;
    };
    [[nodiscard]] std::vector<RolePresentation> orderedRoles();
    [[nodiscard]] bool dispatchRoleClose(const QUuid& incarnation);
    void closeAll();
    void queueCloseAllProgress();
    void advanceCloseAll();
    struct CloseBatch
    {
        std::vector<RolePresentation> snapshot; // Captured once; never replaced by a menu refresh.
        std::size_t index = 0;
        std::optional<RoleCloseKey> request;
        bool attempted = false;
        bool dispatching = false; // Ordinary Launcher Close may service a nested event loop.
        bool queried = false;
    };
    std::optional<CloseBatch> closeBatch_;
    bool closeProgressQueued_ = false;
    QAction* closeAllSeparator_ = nullptr;
    QAction* closeAllAction_ = nullptr;
    QAction* closeAllStatus_ = nullptr;
    InstanceSlot launcherSlot_;
    InstanceActivationEndpoint& activation_;
    QLocalServer server_;
    QTimer bootstrapDeadline_;
    QTimer drain_;
    QUuid epoch_ = QUuid::createUuid();
    std::function<QWidget*()> launcherFactory_;
    std::function<void()> drained_;
    std::unique_ptr<QWidget> launcher_;
    QUuid launcherIncarnation_;
    std::vector<std::unique_ptr<SessionRecord>> sessions_;
    std::vector<LauncherRegisteredSession> sessionProjection_;
    std::vector<std::unique_ptr<CreateRecord>> createOperations_;
    std::vector<LauncherCreateOperation> createProjection_;
    // Private seams are available only to the existing native test friend.
    // Production returns the original CreateProcess HANDLE, never a reopened PID.
    std::function<void*(const DisplaySessionLaunchRequest&)> launchSessionProcess_;
    int createObservationMs_ = ActivationDeadlineMs;
    quint64 sessionProjectionRevision_ = 0;
    std::optional<quint64> publishedSessionRevision_;
    bool sessionProjectionQueued_ = false;
    std::function<void(const LauncherSessionsSnapshot&)> sessionProjectionChanged_;
    bool launcherClosing_ = false;
    bool closing_ = false;
    QSystemTrayIcon tray_;
    std::unique_ptr<QWidget> shellEvents_; // Hidden broadcast receiver, never a primary role/tray owner.
    QMenu trayMenu_;
    QTimer trayHealth_;
    std::vector<RolePresentation> menuRoles_;
    std::function<Platform::DisplayEnumerationResult()> enumerateDisplays_ = Platform::enumerateActiveDisplays;
    QTimer trayClickGuard_;
    std::vector<QUuid> showAllSnapshot_;
    std::size_t showAllIndex_ = 0;
    QUuid showAllTerminal_;
    QUuid showAllAwaiting_;
    quint64 showAllRequest_ = 0;
    QTimer showAllDeadline_;
    struct ClosedRole { QUuid incarnation; RoleCloseOperation operation; };
    std::vector<ClosedRole> closedRoles_; // Bounded verified-death/retired-role receipts, not live roles.
    std::optional<RoleCloseOperation> launcherClose_;
    quint64 launcherCloseSequence_ = 0;
};

// Bounded ready-role Join and one bounded recovery for each independently verified
// owner death. The existing admission lease elects the owner; no process supervisor.
class SessionCoordinatorJoin final : public QObject
{
public:
    SessionCoordinatorJoin(const InstanceSlot& sessionSlot, const InstanceSlot& launcherSlot);
    ~SessionCoordinatorJoin() override;
    void start(QWidget& primaryWindow, bool allowBootstrap = true);
    void enableControl(SessionPresentation& presentation);
    using DesktopDeploymentCompletion = std::function<void(DesktopDeploymentResult)>;
    // Effects return on the Session GUI executor. The coordinator transport
    // correlates the terminal reply to the exact epoch and request.
    void enableDesktopDeploymentAsync(std::function<DesktopDeploymentProjection()> projection,
        std::function<void(DesktopDeploymentIntent, DesktopDeploymentCompletion)> execute);
    void publishDesktopDeployment();
    void publishCloseCapability();
    void beginRoleTeardown();
    void drainAndStop(std::function<void()> completed);
    void stop();
    [[nodiscard]] bool qualified() const;
    [[nodiscard]] QUuid incarnation() const { return incarnation_; }
    [[nodiscard]] bool joined() const { return joined_; }
    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] quint32 coordinatorPid() const { return coordinatorPid_; }

private:
    friend class ::ApplicationCoordinatorTestAccess;
    void attempt();
    void finish(bool joined);
    void consumeControl();
    void revokeQualification();
    void recoverDeadCoordinator(void* observedLifetime);
    void requestHidePermit(std::function<bool()> stillAllowed);
    void renewQualification();
    void finishDraining();
    void completeDesktopDeployment(const QUuid& epoch, quint64 request, DesktopDeploymentResult result);
    InstanceSlot sessionSlot_;
    InstanceSlot launcherSlot_;
    QPointer<QWidget> primaryWindow_;
    QUuid incarnation_ = QUuid::createUuid();
    QLocalSocket socket_;
    QTimer retry_;
    QTimer deadline_;
    QByteArray reply_;
    bool bootstrapAllowed_ = true;
    bool bootstrapAttempted_ = false;
    bool started_ = false;
    bool finished_ = false;
    bool joined_ = false;
    quint32 coordinatorPid_ = 0;
    QPointer<SessionPresentation> presentation_;
    QUuid coordinatorEpoch_;
    QUuid retiredCoordinatorEpoch_; // Previous authenticated channel, whose buffered frames are no longer authority.
    QElapsedTimer qualificationAge_;
    QElapsedTimer requestClock_;
    qint64 joinSentAt_ = 0;
    qint64 renewalSentAt_ = 0;
    qint64 hideSentAt_ = 0;
    QTimer renewal_;
    QTimer expiry_;
    void* coordinatorLifetime_ = nullptr;
    std::unique_ptr<QWinEventNotifier> coordinatorDeath_;
    bool recoveryQueued_ = false;
    std::function<bool()> bootstrapCoordinator_;
    quint64 sequence_ = 0;
    quint64 renewalRequest_ = 0;
    quint64 hideRequest_ = 0;
    quint64 hideShowGeneration_ = 0;
    std::function<bool()> hideStillAllowed_;
    quint64 lastShowRequest_ = 0;
    bool recoveryQualified_ = false;
    std::function<DesktopDeploymentProjection()> deploymentProjection_;
    std::function<void(DesktopDeploymentIntent, DesktopDeploymentCompletion)> deploymentExecute_;
    std::optional<DesktopDeploymentProjection> lastDeploymentProjection_;
    quint64 deploymentRevision_ = 0;
    quint64 lastDeploymentRequest_ = 0;
    quint64 closeRevision_ = 0;
    quint64 lastCloseRequest_ = 0;
    std::optional<bool> lastCloseCapability_;
    bool roleEnding_ = false;
    QTimer closeDrainDeadline_;
    std::function<void()> drainedClose_;
};
}
