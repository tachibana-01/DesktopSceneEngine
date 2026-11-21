#include "LauncherBootstrap.h"
#include <QJsonArray>
#include <QSet>
#include <stdexcept>

namespace DesktopSceneEngine::Player
{
namespace
{
QString enumerationStatus(Platform::DisplayEnumerationStatus value)
{
    switch (value)
    {
    case Platform::DisplayEnumerationStatus::Success: return QStringLiteral("Success");
    case Platform::DisplayEnumerationStatus::ApiFailure: return QStringLiteral("ApiFailure");
    case Platform::DisplayEnumerationStatus::IncompletePathMetadata: return QStringLiteral("IncompletePathMetadata");
    case Platform::DisplayEnumerationStatus::DuplicateIdentity: return QStringLiteral("DuplicateIdentity");
    }
    throw std::invalid_argument("Unknown native display enumeration status");
}

QString displayText(const std::string& value)
{
    const auto text = QString::fromStdString(value);
    if (text.size() > 32768 || text.contains(QChar{0}) || text.toUtf8().toStdString() != value)
        throw std::invalid_argument("Invalid native display text");
    return text;
}
}

QJsonObject launcherDisplayCatalog(const Platform::DisplayEnumerationResult& displays)
{
    const auto status = enumerationStatus(displays.status);
    QJsonArray rows;
    QSet<QString> identities;
    if (displays.status == Platform::DisplayEnumerationStatus::Success)
    {
        if (displays.snapshot.displays.size() > 256)
            throw std::invalid_argument("Native display catalog exceeds bridge bound");
        for (const auto& display : displays.snapshot.displays)
        {
            const auto canonical = display.id.canonicalRepresentation();
            if (!canonical || display.pixelWidth < 0 || display.pixelHeight < 0 || display.displayNumber < 0
                || (display.refreshRate && (!display.refreshRate->numerator || !display.refreshRate->denominator)))
                throw std::invalid_argument("Invalid native display descriptor");
            const auto id = displayText(*canonical);
            if (identities.contains(id) || DisplayId::parseCanonicalRepresentation(*canonical) != display.id)
                throw std::invalid_argument("Invalid or duplicate native display identity");
            identities.insert(id);
            QJsonObject row{
                {"id", id},
                {"identityKind", display.id.kind() == DisplayIdentityKind::TargetDevicePath
                    ? "TargetDevicePath" : "SourceDeviceName"},
                {"persistentOwnerCapable", display.id.isPersistentOwnerCapable()},
                {"friendlyName", displayText(display.friendlyName)},
                {"sourceDeviceName", displayText(display.sourceDeviceName)},
                {"desktopX", display.desktopX}, {"desktopY", display.desktopY},
                {"pixelWidth", display.pixelWidth}, {"pixelHeight", display.pixelHeight},
                {"primary", display.primary}, {"displayNumber", display.displayNumber}};
            if (display.refreshRate)
                row.insert("refreshRate", QJsonObject{
                    {"numerator", static_cast<qint64>(display.refreshRate->numerator)},
                    {"denominator", static_cast<qint64>(display.refreshRate->denominator)}});
            rows.append(row);
        }
    }
    return {{"status", status}, {"displays", rows}};
}

QJsonObject launcherBootstrapSnapshot(const QUuid& epoch,
    const Platform::DisplayEnumerationResult& displays,
    const std::vector<LauncherRegisteredSession>& registeredSessions,
    std::optional<UiLanguage> committedLanguage, UiLanguage effectiveLanguage)
{
    return launcherBootstrapSnapshot(epoch, displays, registeredSessions,
        LauncherLanguageProjection{committedLanguage, effectiveLanguage, 0});
}

QJsonObject launcherBootstrapSnapshot(const QUuid& epoch,
    const Platform::DisplayEnumerationResult& displays,
    const std::vector<LauncherRegisteredSession>& registeredSessions,
    const LauncherLanguageProjection& language)
{
    return launcherBootstrapSnapshot(LauncherSessionsSnapshot{epoch, 0, registeredSessions}, displays, language);
}

QJsonObject launcherBootstrapSnapshot(const LauncherSessionsSnapshot& sessions,
    const Platform::DisplayEnumerationResult& displays, const LauncherLanguageProjection& language)
{
    auto result = launcherSessionsProjection(sessions);
    result.insert("displayEnumeration", launcherDisplayCatalog(displays));
    result.insert("language", serializeLauncherLanguage(language));
    result.insert("host", QJsonObject{{"ready", true}, {"launcherRoleReady", true}});
    return result;
}
}
