// Test-only composition of the production Launcher host and language owner.
// This executable never opens DesktopSceneEngine/Player user preferences.
#include "ApplicationPreferences.h"
#include "GlobalSettingsNotification.h"
#include "LauncherBootstrap.h"
#include "LauncherIdentify.h"
#include "LauncherLanguage.h"
#include "WebViewLauncherHost.h"

#include <QApplication>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUuid>

#include <functional>
#include <iostream>
#include <memory>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

namespace
{
void report(const QJsonObject& value)
{
    std::cout << QJsonDocument{value}.toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

QString argument(const QStringList& arguments, const QString& prefix)
{
    for (const auto& value : arguments)
        if (value.startsWith(prefix)) return value.mid(prefix.size());
    return {};
}
}

int main(int argc, char** argv)
{
    QApplication application{argc, argv};
    application.setQuitOnLastWindowClosed(false);
    const auto arguments = application.arguments();
    const auto fixtureName = argument(arguments, QStringLiteral("--application="));
    const QRegularExpression fixturePattern{
        QStringLiteral("^LauncherLanguageHost-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")};
    if (!fixturePattern.match(fixtureName).hasMatch())
    {
        std::cerr << "Requires a unique LauncherLanguageHost-<UUID> test application.\n";
        return 2;
    }
    QSettings settings{QSettings::NativeFormat, QSettings::UserScope,
        QStringLiteral("DesktopSceneEngineTests"), fixtureName};
    settings.setFallbacksEnabled(false);
    ApplicationPreferencesStore store{settings};
    if (arguments.contains(QStringLiteral("--cleanup")))
    {
        settings.clear();
        settings.sync();
        report({{QStringLiteral("cleaned"), settings.status() == QSettings::NoError}});
        return settings.status() == QSettings::NoError ? 0 : 3;
    }
    if (arguments.contains(QStringLiteral("--read")))
    {
        const auto committed = store.reloadLanguage();
        report({{QStringLiteral("committed"), committed ? launcherLanguageCode(*committed) : QString{}},
            {QStringLiteral("isolatedApplication"), fixtureName}});
        return committed ? 0 : 3;
    }
    const bool fallback = arguments.contains(QStringLiteral("--missing-catalog"));
    ApplicationTranslationState translation{fallback
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/a4-test-missing-translation.qm")
        : QString{}};
    int publicationCount = 0;
    LauncherLanguage language{store, translation, [&] {
        const bool published = !arguments.contains(QStringLiteral("--notification-failure"))
            && broadcastGlobalSettingsChanged();
        report({{QStringLiteral("event"), QStringLiteral("publication")},
            {QStringLiteral("count"), ++publicationCount}, {QStringLiteral("published"), published}});
        return published;
    }};
    const auto externalWrite = argument(arguments, QStringLiteral("--write="));
    if (!externalWrite.isEmpty())
    {
        const auto requested = parseLauncherLanguage(externalWrite);
        if (!requested) return 2;
        const auto result = language.commit(*requested);
        report({{QStringLiteral("event"), QStringLiteral("externalCommit")},
            {QStringLiteral("result"), result}});
        return result.value(QStringLiteral("status")) == QStringLiteral("COMMITTED")
            || result.value(QStringLiteral("status")) == QStringLiteral("UNCHANGED") ? 0 : 3;
    }
    // Initialize only this exact UUID-scoped test key. Production has no test switch.
    if (!settings.contains(QStringLiteral("ui/language"))
        && store.saveLanguage(UiLanguage::English) != QSettings::NoError) return 3;

    const auto epoch = QUuid::createUuid();
    std::unique_ptr<WebViewLauncherHost> host;
    int creation = 0;
    const bool recreate = !arguments.contains(QStringLiteral("--no-recreate"));
    std::function<void()> createHost;
    language.setOnChanged([&](const LauncherLanguageProjection& projection) {
        report({{QStringLiteral("event"), QStringLiteral("languageChanged")},
            {QStringLiteral("language"), serializeLauncherLanguage(projection)}});
        if (host) host->languageChanged(projection);
    });
    createHost = [&] {
        host = std::make_unique<WebViewLauncherHost>([&] {
            return launcherBootstrapSnapshot(epoch, Platform::enumerateActiveDisplays(), {}, language.reload());
        }, [] {
            return launcherDisplayCatalog(Platform::enumerateActiveDisplays());
        }, [](const DisplayId& id) {
            return identifyLauncherDisplay(id);
        }, language);
        host->setAcceptedCloseCallback([&] {
            // Defer destruction past the production closeEvent callback stack.
            QTimer::singleShot(0, &application, [&] {
                host.reset();
                report({{QStringLiteral("event"), QStringLiteral("hostDestroyed")},
                    {QStringLiteral("creation"), creation}});
                if (recreate && creation == 1) createHost();
                else application.quit();
            });
        });
        ++creation;
        host->show();
        report({{QStringLiteral("event"), QStringLiteral("hostCreated")},
            {QStringLiteral("creation"), creation},
            {QStringLiteral("hwnd"), QString::number(host->winId())},
            {QStringLiteral("isolatedApplication"), fixtureName}});
    };
    createHost();
    const int result = application.exec();
    language.setOnChanged({});
    host.reset();
    report({{QStringLiteral("event"), QStringLiteral("cleanShutdown")}});
    return result;
}
