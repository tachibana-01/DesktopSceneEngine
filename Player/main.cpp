#include "ApplicationPreferences.h"
#include "DisplaySessionBootstrap.h"
#include "WebViewLauncherHost.h"
#include "LauncherBootstrap.h"
#include "LauncherIdentify.h"
#include "LauncherLanguage.h"
#include "InstanceAdmission.h"
#include "InstanceActivation.h"
#include "ApplicationCoordinator.h"
#include "ModelCSessionWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QSettings>
#include <QStringList>

#include <variant>
#include <iostream>

namespace
{
int showStartupFailure(QApplication& application,
    DesktopSceneEngine::Player::ProductWindowRole role, const QString& title, const QString& message)
{
    using namespace DesktopSceneEngine::Player;
    ProductWindowHost failure{role};
    failure.setWindowTitle(title);
    failure.resize(720, 480);
    failure.setProductPresentationStage(ProductPresentationStage::Failed, message);
    QObject::connect(&application, &QApplication::lastWindowClosed, &failure, [&] { application.exit(1); });
    failure.show();
    return application.exec();
}
}

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
        return showStartupFailure(application, DesktopSceneEngine::Player::ProductWindowRole::Launcher,
            QCoreApplication::translate("ApplicationBootstrap", "Startup Error"),
            message);
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
        DesktopSceneEngine::Player::LauncherLanguage language{preferencesStore, translationState};
        DesktopSceneEngine::Player::ApplicationCoordinator coordinator{*slot, activation, coordinatorBootstrap};
        language.setOnChanged([&coordinator](const DesktopSceneEngine::Player::LauncherLanguageProjection& projection) {
            if (auto* host = dynamic_cast<DesktopSceneEngine::Player::WebViewLauncherHost*>(coordinator.launcherWindow()))
                host->languageChanged(projection);
        });
        coordinator.setSessionProjectionChanged([&coordinator](const DesktopSceneEngine::Player::LauncherSessionsSnapshot& snapshot) {
            if (auto* host = dynamic_cast<DesktopSceneEngine::Player::WebViewLauncherHost*>(coordinator.launcherWindow()))
                host->sessionsChanged(snapshot);
        });
        coordinator.setLauncherFactory([&] {
            auto* launcher = new DesktopSceneEngine::Player::WebViewLauncherHost{[&] {
                return DesktopSceneEngine::Player::launcherBootstrapSnapshot(
                    coordinator.launcherSessionSnapshot(), DesktopSceneEngine::Platform::enumerateActiveDisplays(),
                    language.reload());
            }, [] {
                return DesktopSceneEngine::Player::launcherDisplayCatalog(
                    DesktopSceneEngine::Platform::enumerateActiveDisplays());
            }, [](const DesktopSceneEngine::DisplayId& id) {
                return DesktopSceneEngine::Player::identifyLauncherDisplay(id);
            }, language};
            qInfo("[ProductComposition] role=launcher shell=cp4");
            launcher->setSessionCommands([&coordinator](const QUuid& epoch, const DesktopSceneEngine::DisplayId& id) {
                return DesktopSceneEngine::Player::launcherCreateResult(coordinator.createSession(epoch, id));
            }, [&coordinator](const QUuid& epoch, const QUuid& incarnation) {
                return DesktopSceneEngine::Player::launcherOpenResult(coordinator.openSession(epoch, incarnation));
            });
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
    using namespace DesktopSceneEngine::Player;
    application.setQuitOnLastWindowClosed(false);
    const auto launcherSlot = InstanceSlot::fromProcessMode(LauncherProcessMode{}, *scope);
    SessionCoordinatorJoin join{*slot, *launcherSlot};
    // The relay outlives all C work, including exceptional retirement. It
    // stays on GUI and resolves weak adapter lifetimes only on that thread.
    SessionGuiRelay relay;
    SessionControlExecutor control;
    try
    {
        ModelCSessionWindow window{control, relay, displaySessionMode.displayId, translationState, join.incarnation()};
        bool startupFailed = false;
        join.enableControl(window.trayPresentation());
        join.enableDesktopDeploymentAsync([&window] { return window.desktopDeploymentProjection(); },
            [&window](auto intent, auto complete) { window.requestDesktopDeployment(intent, std::move(complete)); });
        window.setAcceptedCloseCallback([&] {
            join.beginRoleTeardown();
            activation.beginShutdown();
            QTimer::singleShot(0, &join, [&] { join.drainAndStop([&] { application.exit(startupFailed ? 1 : 0); }); });
        });
        control.start(displaySessionMode.displayId, preferences, [] {
            return std::make_unique<QSettings>(QSettings::NativeFormat, QSettings::UserScope,
                QStringLiteral("DesktopSceneEngine"), QStringLiteral("Player"));
        }, [&, guard = QPointer<ModelCSessionWindow>{&window}](bool ready) {
            (void)relay.post([&, guard, ready] {
                if (!guard) return;
                if (!ready)
                {
                    startupFailed = true;
                    qCritical("[SessionControl] Failed to start autonomous Model C Session");
                    guard->failControlStartup(QCoreApplication::translate(
                        "ApplicationBootstrap", "The Session runtime could not start."));
                    return;
                }
                qInfo("[ProductComposition] role=session shell=cp4 route=default");
                qInfo("[SessionComposition] model-c-production control autonomous");
                window.startPresentation();
                if (window.productFrameFailed())
                {
                    startupFailed = true;
                    return;
                }
                activation.bindReadyRole(window, [&window] { (void)window.trayPresentation().restore(); });
                join.start(window);
            });
        }, window.platformFactory());
        window.show();
        const int result = application.exec();
        window.releasePresentation();
        control.stop();
        activation.beginShutdown();
        return result;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[SessionControl] Model C startup failed: " << error.what() << '\n';
        const int result = showStartupFailure(application, ProductWindowRole::Session,
            QStringLiteral("DesktopSceneEngine"),
            QCoreApplication::translate("ApplicationBootstrap", "The production Session presentation could not start."));
        activation.beginShutdown();
        return result;
    }
}
