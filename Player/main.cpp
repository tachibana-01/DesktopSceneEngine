#include "ApplicationPreferences.h"
#include "DisplaySessionBootstrap.h"
#include "LauncherWindow.h"
#include "MainWindow.h"
#include "InstanceAdmission.h"
#include "InstanceActivation.h"
#include "ApplicationCoordinator.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStringList>

#include <variant>
#include <iostream>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/DesktopSceneEngine.ico")));

    QSettings settings{
        QSettings::NativeFormat,
        QSettings::UserScope,
        QStringLiteral("DesktopSceneEngine"),
        QStringLiteral("Player")};
    DesktopSceneEngine::Player::ApplicationPreferencesStore preferencesStore{settings};
    DesktopSceneEngine::Player::ApplicationPreferences preferences = preferencesStore.load();
    DesktopSceneEngine::Player::ApplicationTranslationState translationState{};
    static_cast<void>(translationState.setLanguage(preferences.language));

    QStringList applicationArguments = QCoreApplication::arguments();
    if (!applicationArguments.isEmpty())
    {
        applicationArguments.removeFirst();
    }
    const bool coordinatorBootstrap = applicationArguments == QStringList{
        QString::fromLatin1(DesktopSceneEngine::Player::CoordinatorBootstrapArgument)};
    if (coordinatorBootstrap) applicationArguments.clear();
    const DesktopSceneEngine::Player::ApplicationProcessMode processMode =
        DesktopSceneEngine::Player::parseApplicationProcessMode(applicationArguments);

    if (const auto* error = std::get_if<DesktopSceneEngine::Player::ProcessModeParseError>(
            &processMode))
    {
        const QString message =
            *error == DesktopSceneEngine::Player::ProcessModeParseError::NonPersistentDisplayIdentity
            ? QCoreApplication::translate(
                "ApplicationBootstrap",
                "The Display Session identity is not eligible to own a session."
            )
            : QCoreApplication::translate(
                "ApplicationBootstrap",
                "The application startup arguments are invalid."
            );
        QMessageBox::critical(
            nullptr,
            QCoreApplication::translate("ApplicationBootstrap", "Startup Error"),
            message
        );
        return 1;
    }

    const auto scope = DesktopSceneEngine::Player::currentInteractiveInstanceScope();
    const auto slot = scope ? DesktopSceneEngine::Player::InstanceSlot::fromProcessMode(processMode, *scope) : std::nullopt;
    if (!slot)
    {
        std::cerr << "[Instance] Cannot derive current-user interactive admission slot." << std::endl;
        return 1;
    }
    // Declaration order is the process resource contract: role -> endpoint -> lease destruction.
    DesktopSceneEngine::Player::InstanceAdmissionLease admission;
    const auto outcome = admission.acquire(*slot);
    if (outcome == DesktopSceneEngine::Player::InstanceAdmissionOutcome::Error)
    {
        std::cerr << "[Instance] Admission failed: " << admission.nativeError() << std::endl;
        return 1;
    }
    if (outcome == DesktopSceneEngine::Player::InstanceAdmissionOutcome::Occupied)
    {
        // An internal missing-coordinator attempt must never create/surface Launcher UI.
        if (coordinatorBootstrap) return 0;
        const auto forwarded = DesktopSceneEngine::Player::forwardActivation(*slot);
        if (forwarded.outcome != DesktopSceneEngine::Player::ActivationForwardOutcome::Delivered)
        {
            std::cerr << "[Instance] ACTIVATE delivery failed; no duplicate role created. Native error="
                << forwarded.nativeError << std::endl;
            return 1;
        }
        return 0;
    }
    DesktopSceneEngine::Player::InstanceActivationEndpoint activation{*slot};
    if (!activation.listen())
    {
        std::cerr << "[Instance] Activation endpoint failed: " << activation.errorString().toStdString() << std::endl;
        return 1;
    }
    if (std::holds_alternative<DesktopSceneEngine::Player::LauncherProcessMode>(processMode))
    {
        application.setQuitOnLastWindowClosed(false);
        DesktopSceneEngine::Player::ApplicationCoordinator coordinator{*slot, activation, coordinatorBootstrap};
        coordinator.setLauncherFactory([&] {
            auto* launcher = new DesktopSceneEngine::Player::LauncherWindow{translationState, preferencesStore};
            launcher->setAcceptedCloseCallback([&coordinator, launcher] { coordinator.retireLauncher(*launcher); });
            return launcher;
        });
        if (!coordinator.listen())
        {
            std::cerr << "[Coordinator] Join endpoint could not start." << std::endl;
            activation.beginShutdown();
            return 1;
        }
        if (!coordinatorBootstrap) coordinator.showLauncher();
        const int result = application.exec();
        activation.beginShutdown();
        return result;
    }

    const auto& displaySessionMode =
        std::get<DesktopSceneEngine::Player::DisplaySessionProcessMode>(processMode);
    MainWindow window{
        displaySessionMode.displayId,
        translationState,
        preferencesStore,
        preferences};
    // Hide must not end the process when its last primary window becomes invisible.
    // Ordinary accepted Close remains the explicit Session process exit authority.
    application.setQuitOnLastWindowClosed(false);
    window.show();
    if (!window.initializeEngine())
    {
        window.close();
        return 1;
    }

    activation.bindReadyRole(window, [&window] { (void)window.trayPresentation().restore(); });
    const auto launcherSlot = DesktopSceneEngine::Player::InstanceSlot::fromProcessMode(
        DesktopSceneEngine::Player::LauncherProcessMode{}, *scope);
    DesktopSceneEngine::Player::SessionCoordinatorJoin coordinatorJoin{*slot, *launcherSlot};
    coordinatorJoin.enableControl(window.trayPresentation());
    coordinatorJoin.enableDesktopDeployment([&window] { return window.desktopDeploymentProjection(); },
        [&window](DesktopSceneEngine::Player::DesktopDeploymentIntent intent) { return window.requestDesktopDeployment(intent); });
    window.setAcceptedCloseCallback([&] {
        coordinatorJoin.beginRoleTeardown();
        activation.beginShutdown();
        QTimer::singleShot(0, &coordinatorJoin, [&] {
            coordinatorJoin.drainAndStop([&] { application.quit(); });
        }); // Ordinary teardown finishes before result drainage/explicit process exit, including hidden clean Close.
    });
    coordinatorJoin.start(window);
    const int result = application.exec();
    activation.beginShutdown();
    return result;
}
