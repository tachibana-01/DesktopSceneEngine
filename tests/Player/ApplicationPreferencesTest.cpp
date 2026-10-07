#include "ApplicationPreferences.h"

#include <DesktopSceneEngine/DisplayIdentity.h>
#include <DesktopSceneEngine/PresentationTarget.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>

#include <iostream>
#include <memory>

namespace
{

using DesktopSceneEngine::Player::ApplicationPreferences;
using DesktopSceneEngine::Player::ApplicationPreferencesStore;
using DesktopSceneEngine::Player::ApplicationTranslationState;
using DesktopSceneEngine::Player::UiLanguage;
using DesktopSceneEngine::CanvasOutsideMode;
using DesktopSceneEngine::DisplayId;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyDefaults()
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "A temporary preferences directory is required"))
    {
        return false;
    }

    QSettings settings{
        directory.filePath(QStringLiteral("preferences.ini")),
        QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    const ApplicationPreferences preferences = store.load();
    return require(preferences.language == UiLanguage::English
            && !preferences.highlightCanvasBoundary,
        "Missing preferences must load the English and disabled-highlight defaults");
}

QString canvasOutsideKey(const DisplayId& displayId)
{
    const auto canonical = displayId.canonicalRepresentation();
    return QStringLiteral("displayPresentation/v1/")
        + QString::fromLatin1(QCryptographicHash::hash(
            QByteArray::fromStdString(canonical.value()), QCryptographicHash::Sha256).toHex())
        + QStringLiteral("/canvasOutsideMode");
}

bool verifyCanvasOutsidePreferences()
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "Canvas-outside preferences need an isolated store"))
        return false;
    const auto displayA = DisplayId::fromTargetDevicePath("opaque-route-A");
    const auto displayB = DisplayId::fromTargetDevicePath("opaque-route-B");
    const auto unseen = DisplayId::fromTargetDevicePath("opaque-route-unseen");
    const QString path = directory.filePath(QStringLiteral("preferences.ini"));
    QSettings settings{path, QSettings::IniFormat};
    QSettings peerSettings{path, QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    ApplicationPreferencesStore peer{peerSettings};
    const auto missing = store.loadCanvasOutsideMode(unseen);
    if (!require(missing.status == QSettings::NoError && !missing.value
            && settings.allKeys().isEmpty(),
            "Missing per-route preferences must derive Opaque without writing a default"))
        return false;
    const auto savedA = store.saveCanvasOutsideMode(displayA, CanvasOutsideMode::Transparent);
    const auto savedB = peer.saveCanvasOutsideMode(displayB, CanvasOutsideMode::Opaque);
    if (savedA != QSettings::NoError || savedB != QSettings::NoError)
        std::cerr << "Canvas-outside write status: A=" << savedA << " B=" << savedB
                  << " file=" << settings.fileName().toStdString() << std::endl;
    if (!require(savedA == QSettings::NoError && savedB == QSettings::NoError,
            "Independent display preferences must save successfully"))
        return false;
    store.saveLanguage(UiLanguage::SimplifiedChinese);
    store.saveHighlightCanvasBoundary(true);
    QSettings reopened{path, QSettings::IniFormat};
    ApplicationPreferencesStore restored{reopened};
    const auto a = restored.loadCanvasOutsideMode(displayA);
    const auto b = restored.loadCanvasOutsideMode(displayB);
    if (!require(a.status == QSettings::NoError && a.value == CanvasOutsideMode::Transparent
            && b.status == QSettings::NoError && b.value == CanvasOutsideMode::Opaque
            && !restored.loadCanvasOutsideMode(unseen).value
            && reopened.value(canvasOutsideKey(displayA)) == QStringLiteral("transparent")
            && reopened.value(canvasOutsideKey(displayB)) == QStringLiteral("opaque")
            && restored.load().language == UiLanguage::SimplifiedChinese
            && restored.load().highlightCanvasBoundary,
            "Route-keyed tokens must reconstruct independently and preserve existing preferences"))
        return false;
    if (!require(restored.saveCanvasOutsideMode(displayA, CanvasOutsideMode::Opaque)
                == QSettings::NoError
            && reopened.value(canvasOutsideKey(displayA)) == QStringLiteral("opaque")
            && restored.loadCanvasOutsideMode(displayB).value == CanvasOutsideMode::Opaque,
            "An explicit Opaque selection must be stored, not removed"))
        return false;
    for (const QString& token : {QStringLiteral("Opaque"), QStringLiteral(" transparent"),
             QStringLiteral("unknown"), QStringLiteral("")})
    {
        reopened.setValue(canvasOutsideKey(displayA), token);
        reopened.sync();
        const auto invalid = restored.loadCanvasOutsideMode(displayA);
        if (!require(invalid.status == QSettings::FormatError && !invalid.value
                && reopened.value(canvasOutsideKey(displayA)) == token,
                "Unknown tokens must report an error without repairing the store"))
            return false;
    }
    const auto source = DisplayId::fromSourceDeviceName("DISPLAY1");
    const QStringList keys = reopened.allKeys();
    return require(restored.loadCanvasOutsideMode(source).status == QSettings::FormatError
            && restored.loadCanvasOutsideMode(DisplayId{}).status == QSettings::FormatError
            && restored.saveCanvasOutsideMode(source, CanvasOutsideMode::Opaque)
                == QSettings::FormatError
            && restored.saveCanvasOutsideMode(displayA, static_cast<CanvasOutsideMode>(255))
                == QSettings::FormatError
            && reopened.allKeys() == keys,
        "Invalid/nonpersistent identities and enum values must be rejected without writes");
}

bool verifyCanvasOutsideLookupFormatFailure()
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "Lookup format failure needs an isolated store"))
        return false;
    const auto display = DisplayId::fromTargetDevicePath("lookup-format-error-route");
    bool ok = true;
    for (const bool hasStoredMode : {true, false})
    for (const bool preParsed : {false, true})
    {
        const QString path = directory.filePath(QStringLiteral("malformed-%1-%2.ini")
            .arg(hasStoredMode ? QStringLiteral("present") : QStringLiteral("missing"),
                preParsed ? QStringLiteral("preparsed") : QStringLiteral("unparsed")));
        QByteArray original{"[displayPresentation]\nmalformed-entry\n"};
        if (hasStoredMode)
        {
            QByteArray key = canvasOutsideKey(display)
                .mid(QStringLiteral("displayPresentation/").size()).toUtf8();
            key.replace('/', '\\');
            original += key + "=transparent\n";
        }
        QFile fixture{path};
        if (!require(fixture.open(QIODevice::WriteOnly)
                && fixture.write(original) == original.size(),
                "Malformed preference fixture must be written completely"))
            return false;
        fixture.close();
        {
            QSettings settings{path, QSettings::IniFormat};
            ApplicationPreferencesStore store{settings};
            if (preParsed)
            {
                const bool contains = settings.contains(canvasOutsideKey(display));
                if (!require(settings.status() == QSettings::FormatError
                        && contains == hasStoredMode,
                        "Real QSettings parsing must expose the malformed section and partial cached data"))
                    return false;
            }
            const auto checkRead = [&](const ApplicationPreferencesStore& reader, const char* phase)
            {
                const auto result = reader.loadCanvasOutsideMode(display);
                if (result.status != QSettings::FormatError || result.value)
                    std::cerr << "Lookup format error: stored-mode=" << hasStoredMode
                              << " preparsed=" << preParsed << " phase=" << phase
                              << " status=" << result.status
                              << " value=" << result.value.has_value() << '\n';
                return require(result.status == QSettings::FormatError && !result.value,
                    "Every repeated/shared INI read must report failure without a mode");
            };
            ok = checkRead(store, "first") && ok;
            ok = checkRead(store, "same-store-repeat") && ok;
            QSettings peerSettings{path, QSettings::IniFormat};
            ApplicationPreferencesStore peer{peerSettings};
            ok = checkRead(peer, "independent-store") && ok;
        }
        if (!require(fixture.open(QIODevice::ReadOnly), "Malformed fixture must remain readable"))
            return false;
        ok = require(fixture.readAll() == original,
            "Preference read failure must not repair or rewrite malformed storage") && ok;
    }
    return ok;
}

bool verifyCanvasOutsideIniSyntaxCompatibility()
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "INI compatibility needs isolated storage"))
        return false;
    const auto display = DisplayId::fromTargetDevicePath("ini-syntax-route");
    const auto unseen = DisplayId::fromTargetDevicePath("ini-syntax-unseen");
    const QString path = directory.filePath(QStringLiteral("valid-syntax.ini"));
    QByteArray key = canvasOutsideKey(display)
        .mid(QStringLiteral("displayPresentation/").size()).toUtf8();
    key.replace('/', '\\');
    const QByteArray original = QByteArray{"\xef\xbb\xbf; comment without an assignment\r\n"
        "\r\n[unrelated] ; section comment\r\n"
        "quoted=\"first;=line\r\nsecond line\"\r\n"
        "continued=first\\\r\nsecond\r\n"
        "escaped\\;key=value ; trailing comment\r\n"
        "[displayPresentation]\r\n"} + key + "=\"transparent\" ; mode comment\r\n";
    QFile fixture{path};
    if (!require(fixture.open(QIODevice::WriteOnly) && fixture.write(original) == original.size(),
            "Valid INI compatibility fixture must be written completely"))
        return false;
    fixture.close();
    QSettings settings{path, QSettings::IniFormat};
    (void)settings.allKeys(); // Prove these records are valid under the real Qt parser.
    ApplicationPreferencesStore store{settings};
    const auto restored = store.loadCanvasOutsideMode(display);
    const auto missing = store.loadCanvasOutsideMode(unseen);
    return require(settings.status() == QSettings::NoError
            && restored.status == QSettings::NoError
            && restored.value == CanvasOutsideMode::Transparent
            && missing.status == QSettings::NoError && !missing.value
            && fixture.open(QIODevice::ReadOnly) && fixture.readAll() == original,
        "BOM/comments/quoted and continued INI records must preserve valid and missing reads without writes");
}

bool verifyCanvasOutsideFailuresAndFallbacks()
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "Preference failure test needs an isolated directory"))
        return false;
    const auto display = DisplayId::fromTargetDevicePath("failure-route");
    // A directory is not an INI file: actual QSettings must report access failure.
    QSettings inaccessible{directory.path(), QSettings::IniFormat};
    ApplicationPreferencesStore broken{inaccessible};
    const auto failedRead = broken.loadCanvasOutsideMode(display);
    if (!require(failedRead.status != QSettings::NoError && !failedRead.value
            && broken.saveCanvasOutsideMode(display, CanvasOutsideMode::Transparent)
                == QSettings::AccessError,
            "Actual storage failures must remain observable to the MainWindow"))
        return false;
    const QString organization = QStringLiteral("DSECanvasOutsideFallbackTest-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSettings organizationSettings{QSettings::NativeFormat, QSettings::UserScope,
        organization, QString{}};
    organizationSettings.setValue(canvasOutsideKey(display), QStringLiteral("transparent"));
    organizationSettings.setValue(QStringLiteral("ui/language"), QStringLiteral("zh_CN"));
    organizationSettings.sync();
    QSettings appSettings{QSettings::NativeFormat, QSettings::UserScope,
        organization, QStringLiteral("DisplayModes")};
    ApplicationPreferencesStore store{appSettings};
    const auto missing = store.loadCanvasOutsideMode(display);
    const bool ok = require(missing.status == QSettings::NoError && !missing.value
            && store.load().language == UiLanguage::SimplifiedChinese
            && store.saveCanvasOutsideMode(display, CanvasOutsideMode::Opaque) == QSettings::NoError
            && store.loadCanvasOutsideMode(display).value == CanvasOutsideMode::Opaque
            && organizationSettings.value(canvasOutsideKey(display)) == QStringLiteral("transparent"),
        "Display mode must disable fallback without changing existing language fallback or organization data");
    appSettings.clear(); appSettings.sync();
    organizationSettings.clear(); organizationSettings.sync();
    return ok;
}

int canvasOutsidePreferenceChild(const QStringList& arguments)
{
    if (arguments.size() != 7)
        return 2;
    const auto display = DisplayId::parseCanonicalRepresentation(
        arguments[4].toUtf8().toStdString());
    if (!display || (arguments[5] != QStringLiteral("opaque")
            && arguments[5] != QStringLiteral("transparent")))
        return 2;
    std::unique_ptr<QSettings> settings = arguments[2] == QStringLiteral("native")
        ? std::make_unique<QSettings>(QSettings::NativeFormat, QSettings::UserScope,
            arguments[3], QStringLiteral("DisplayModes"))
        : std::make_unique<QSettings>(arguments[3], QSettings::IniFormat);
    ApplicationPreferencesStore store{*settings};
    const auto finalMode = arguments[5] == QStringLiteral("opaque")
        ? CanvasOutsideMode::Opaque : CanvasOutsideMode::Transparent;
    const int writes = arguments[6].toInt();
    for (int index = 0; index < writes; ++index)
        if (store.saveCanvasOutsideMode(*display, index % 2 == 0
                ? CanvasOutsideMode::Opaque : CanvasOutsideMode::Transparent) != QSettings::NoError)
            return 1;
    if (writes > 0 && store.saveCanvasOutsideMode(*display, finalMode) != QSettings::NoError)
        return 1;
    const auto restored = store.loadCanvasOutsideMode(*display);
    return restored.status == QSettings::NoError && restored.value == finalMode ? 0 : 1;
}

bool verifyCanvasOutsideProcessWrites(bool native)
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "Concurrent preference test needs an isolated directory"))
        return false;
    const QString location = native
        ? QStringLiteral("DSECanvasOutsideProcessTest-") + QUuid::createUuid().toString(QUuid::WithoutBraces)
        : directory.filePath(QStringLiteral("concurrent.ini"));
    std::unique_ptr<QSettings> settings = native
        ? std::make_unique<QSettings>(QSettings::NativeFormat, QSettings::UserScope,
            location, QStringLiteral("DisplayModes"))
        : std::make_unique<QSettings>(location, QSettings::IniFormat);
    ApplicationPreferencesStore store{*settings};
    store.saveLanguage(UiLanguage::SimplifiedChinese);
    store.saveHighlightCanvasBoundary(true);
    const auto a = DisplayId::fromTargetDevicePath("process-route-A");
    const auto b = DisplayId::fromTargetDevicePath("process-route-B");
    const auto start = [&](QProcess& process, const DisplayId& display,
        const QString& token, int writes)
    {
        process.start(QCoreApplication::applicationFilePath(),
            {QStringLiteral("--canvas-outside-child"),
             native ? QStringLiteral("native") : QStringLiteral("ini"), location,
             QString::fromStdString(*display.canonicalRepresentation()), token,
             QString::number(writes)});
        return process.waitForStarted(5000);
    };
    const auto completed = [](QProcess& process)
    {
        const bool finished = process.state() == QProcess::NotRunning
            || process.waitForFinished(30000);
        if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            std::cerr << "Preference child failed: " << process.readAllStandardError().toStdString() << '\n';
        return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    QProcess writerA, writerB;
    bool ok = start(writerA, a, QStringLiteral("transparent"), 24)
        && start(writerB, b, QStringLiteral("opaque"), 24)
        && completed(writerA) && completed(writerB);
    const auto finalPreferences = store.load();
    ok = require(ok && store.loadCanvasOutsideMode(a).value == CanvasOutsideMode::Transparent
            && store.loadCanvasOutsideMode(b).value == CanvasOutsideMode::Opaque
            && finalPreferences.language == UiLanguage::SimplifiedChinese
            && finalPreferences.highlightCanvasBoundary,
        "Independent process writes must compose in both production NativeFormat and the INI seam") && ok;
    QProcess readerA, readerB;
    ok = start(readerA, a, QStringLiteral("transparent"), 0) && completed(readerA)
        && start(readerB, b, QStringLiteral("opaque"), 0) && completed(readerB) && ok;
    if (native)
    {
        settings->clear(); settings->sync(); // Only this test's unique organization/application.
    }
    return require(ok, "Fresh processes must restore each display's committed preference");
}

bool verifyRoundTripsAndIndependentFields()
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "A temporary preferences directory is required"))
    {
        return false;
    }

    QSettings settings{
        directory.filePath(QStringLiteral("preferences.ini")),
        QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    store.saveHighlightCanvasBoundary(true);

    if (!require(settings.value(QStringLiteral("ui/language")) == QStringLiteral("en")
            && settings.value(QStringLiteral("preview/highlightCanvasBoundary")).toBool(),
            "English and true highlight must use their stable persisted representations"))
    {
        return false;
    }

    QSettings englishSettings{
        directory.filePath(QStringLiteral("preferences.ini")),
        QSettings::IniFormat};
    ApplicationPreferencesStore englishStore{englishSettings};
    const ApplicationPreferences englishPreferences = englishStore.load();
    if (!require(englishPreferences.language == UiLanguage::English
            && englishPreferences.highlightCanvasBoundary,
            "en and true highlight must round-trip"))
    {
        return false;
    }

    store.saveLanguage(UiLanguage::SimplifiedChinese);

    QSettings reopenedSettings{
        directory.filePath(QStringLiteral("preferences.ini")),
        QSettings::IniFormat};
    ApplicationPreferencesStore reopenedStore{reopenedSettings};
    ApplicationPreferences preferences = reopenedStore.load();
    if (!require(preferences.language == UiLanguage::SimplifiedChinese
            && preferences.highlightCanvasBoundary,
            "Updating language must preserve an independently stored highlight"))
    {
        return false;
    }

    preferences.highlightCanvasBoundary = false;
    reopenedStore.saveHighlightCanvasBoundary(preferences.highlightCanvasBoundary);

    QSettings finalSettings{
        directory.filePath(QStringLiteral("preferences.ini")),
        QSettings::IniFormat};
    ApplicationPreferencesStore finalStore{finalSettings};
    const ApplicationPreferences finalPreferences = finalStore.load();
    return require(finalPreferences.language == UiLanguage::SimplifiedChinese
            && !finalPreferences.highlightCanvasBoundary,
        "Updating highlight must preserve the independently stored language and false value");
}

bool verifyIndependentStoreWritesCompose(bool languageFirst)
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "A temporary preferences directory is required"))
    {
        return false;
    }

    const QString settingsPath = directory.filePath(QStringLiteral("preferences.ini"));
    QSettings languageSettings{settingsPath, QSettings::IniFormat};
    QSettings highlightSettings{settingsPath, QSettings::IniFormat};
    ApplicationPreferencesStore languageStore{languageSettings};
    ApplicationPreferencesStore highlightStore{highlightSettings};

    ApplicationPreferences languageSnapshot = languageStore.load();
    ApplicationPreferences highlightSnapshot = highlightStore.load();
    languageSnapshot.language = UiLanguage::SimplifiedChinese;
    highlightSnapshot.highlightCanvasBoundary = true;
    if (languageFirst)
    {
        languageStore.saveLanguage(languageSnapshot.language);
        highlightStore.saveHighlightCanvasBoundary(
            highlightSnapshot.highlightCanvasBoundary);
    }
    else
    {
        highlightStore.saveHighlightCanvasBoundary(
            highlightSnapshot.highlightCanvasBoundary);
        languageStore.saveLanguage(languageSnapshot.language);
    }

    QSettings finalSettings{settingsPath, QSettings::IniFormat};
    ApplicationPreferencesStore finalStore{finalSettings};
    const ApplicationPreferences finalPreferences = finalStore.load();
    return require(
        finalPreferences.language == UiLanguage::SimplifiedChinese
            && finalPreferences.highlightCanvasBoundary,
        languageFirst
            ? "A language write followed by an independent highlight write must compose"
            : "A highlight write followed by an independent language write must compose");
}

bool verifyInvalidValuesFallbackSafely()
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "A temporary preferences directory is required"))
    {
        return false;
    }

    QSettings settings{
        directory.filePath(QStringLiteral("preferences.ini")),
        QSettings::IniFormat};
    settings.setValue(QStringLiteral("ui/language"), QStringLiteral("fr"));
    settings.setValue(QStringLiteral("preview/highlightCanvasBoundary"), QStringLiteral("enabled"));
    settings.sync();

    ApplicationPreferencesStore store{settings};
    const ApplicationPreferences invalidPreferences = store.load();
    if (!require(invalidPreferences.language == UiLanguage::English
            && !invalidPreferences.highlightCanvasBoundary,
            "Invalid persisted values must fall back safely"))
    {
        return false;
    }

    settings.setValue(QStringLiteral("preview/highlightCanvasBoundary"), QStringLiteral("TRUE"));
    if (!require(store.load().highlightCanvasBoundary,
            "Case-normalized true must be accepted"))
    {
        return false;
    }

    settings.setValue(QStringLiteral("preview/highlightCanvasBoundary"), false);
    if (!require(!store.load().highlightCanvasBoundary,
            "Boolean false must be accepted"))
    {
        return false;
    }

    settings.setValue(QStringLiteral("preview/highlightCanvasBoundary"), true);
    if (!require(store.load().highlightCanvasBoundary,
            "Boolean true must be accepted"))
    {
        return false;
    }

    settings.setValue(QStringLiteral("preview/highlightCanvasBoundary"), 1);
    if (!require(store.load().highlightCanvasBoundary,
            "Numeric true must be accepted"))
    {
        return false;
    }

    settings.setValue(QStringLiteral("preview/highlightCanvasBoundary"), QStringLiteral("0"));
    return require(!store.load().highlightCanvasBoundary,
        "Numeric false must be accepted");
}

bool verifySavedLanguageActivatesBeforeMainWindow()
{
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "A temporary preferences directory is required"))
    {
        return false;
    }

    QSettings settings{
        directory.filePath(QStringLiteral("preferences.ini")),
        QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::SimplifiedChinese);
    store.saveHighlightCanvasBoundary(true);

    ApplicationPreferences preferences = store.load();
    ApplicationTranslationState translationState{};
    preferences.language = translationState.setLanguage(preferences.language);

    if (!require(
            preferences.language == UiLanguage::SimplifiedChinese
                && translationState.activeLanguage() == UiLanguage::SimplifiedChinese,
            "A saved Chinese preference must activate the real application catalog"))
    {
        return false;
    }
    if (!require(
            QObject::tr("Display Session Unavailable")
                == QString::fromUtf8("\xE6\x98\xBE\xE7\xA4\xBA\xE4\xBC\x9A\xE8\xAF\x9D\xE4\xB8\x8D\xE5\x8F\xAF\xE7\x94\xA8"),
            "The startup diagnostic must be translated before any MainWindow exists"))
    {
        return false;
    }

    preferences.language = translationState.setLanguage(UiLanguage::English);
    return require(
               preferences.language == UiLanguage::English
                   && translationState.activeLanguage() == UiLanguage::English
                   && QObject::tr("Display Session Unavailable")
                       == QStringLiteral("Display Session Unavailable"),
               "Switching to English must remove the active application translation")
        && require(
            store.load().language == UiLanguage::SimplifiedChinese
                && store.load().highlightCanvasBoundary,
            "Changing effective language must not rewrite persisted preferences implicitly");
}

}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (application.arguments().value(1) == QStringLiteral("--canvas-outside-child"))
        return canvasOutsidePreferenceChild(application.arguments());
    return verifyDefaults() && verifyRoundTripsAndIndependentFields()
            && verifyCanvasOutsidePreferences()
            && verifyCanvasOutsideIniSyntaxCompatibility()
            && verifyCanvasOutsideLookupFormatFailure()
            && verifyCanvasOutsideFailuresAndFallbacks()
            && verifyCanvasOutsideProcessWrites(false)
            && verifyCanvasOutsideProcessWrites(true)
            && verifyIndependentStoreWritesCompose(true)
            && verifyIndependentStoreWritesCompose(false)
            && verifyInvalidValuesFallbackSafely()
            && verifySavedLanguageActivatesBeforeMainWindow()
        ? 0
        : 1;
}
