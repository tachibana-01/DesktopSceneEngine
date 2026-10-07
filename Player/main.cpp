#include "ApplicationPreferences.h"
#include "DisplaySessionBootstrap.h"
#include "LauncherWindow.h"
#include "MainWindow.h"
#include "InstanceAdmission.h"
#include "InstanceActivation.h"

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
        DesktopSceneEngine::Player::LauncherWindow launcherWindow{translationState, preferencesStore};
        launcherWindow.setAcceptedCloseCallback([&activation] { activation.beginShutdown(); });
        launcherWindow.show();
        activation.bindReadyRole(launcherWindow);
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
    window.setAcceptedCloseCallback([&activation] { activation.beginShutdown(); });
    window.show();
    if (!window.initializeEngine())
    {
        window.close();
        return 1;
    }

    activation.bindReadyRole(window);
    const int result = application.exec();
    activation.beginShutdown();
    return result;
}
