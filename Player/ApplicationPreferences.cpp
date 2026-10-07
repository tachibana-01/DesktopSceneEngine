#include "ApplicationPreferences.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QMetaType>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <iostream>
#include <memory>
#include <utility>

namespace DesktopSceneEngine::Player
{
namespace
{

constexpr auto LanguageKey = "ui/language";
constexpr auto HighlightCanvasBoundaryKey = "preview/highlightCanvasBoundary";
constexpr auto EnglishLanguageValue = "en";
constexpr auto SimplifiedChineseLanguageValue = "zh_CN";

std::unique_ptr<QSettings> settingsOperation(const QSettings& source)
{
    // A fresh status belongs to this operation, not an earlier failed write.
    auto operation = source.organizationName().isEmpty() && !source.fileName().isEmpty()
        ? std::make_unique<QSettings>(source.fileName(), source.format())
        : std::make_unique<QSettings>(source.format(), source.scope(),
            source.organizationName(), source.applicationName());
    operation->setFallbacksEnabled(source.fallbacksEnabled());
    operation->setAtomicSyncRequired(source.isAtomicSyncRequired());
    return operation;
}

std::optional<QString> canvasOutsideModeKey(const DisplayId& displayId)
{
    if (!displayId.isPersistentOwnerCapable())
        return std::nullopt;
    const auto canonical = displayId.canonicalRepresentation();
    if (!canonical)
        return std::nullopt;
    // The opaque canonical Windows display-route identity is not a panel UUID.
    const auto digest = QCryptographicHash::hash(
        QByteArray::fromStdString(*canonical), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("displayPresentation/v1/") + QString::fromLatin1(digest)
        + QStringLiteral("/canvasOutsideMode");
}

QSettings::Status validateIniStorage(const QSettings& settings)
{
    if (settings.format() != QSettings::IniFormat)
        return QSettings::NoError; // Windows NativeFormat remains registry-backed.
    QFile file{settings.fileName()};
    if (!file.exists())
        return QSettings::NoError;
    if (!file.open(QIODevice::ReadOnly))
        return QSettings::AccessError;
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return QSettings::AccessError;
    QByteArrayView data{bytes};
    if (data.startsWith("\xef\xbb\xbf"))
        data = data.sliced(3);

    // QSettings shares parsed INI records, but not the status that discovered
    // a malformed record. Validate the backing representation on every read;
    // leave key/value decoding to Qt, without repairing or caching failures.
    const auto lineBreak = [](char ch) { return ch == '\r' || ch == '\n'; };
    qsizetype position = 0;
    while (position < data.size())
    {
        while (position < data.size() && (data[position] == ' ' || data[position] == '\t'
                || lineBreak(data[position])))
            ++position;
        if (position == data.size())
            break;
        if (data[position] == ';')
        {
            while (position < data.size() && !lineBreak(data[position]))
                ++position;
            continue;
        }
        const qsizetype start = position;
        qsizetype end = data.size();
        bool quoted = false;
        bool hasAssignment = false;
        while (position < data.size())
        {
            const char ch = data[position++];
            if (ch == '\\' && position < data.size())
            {
                const char escaped = data[position++];
                if (lineBreak(escaped) && position < data.size() && lineBreak(data[position])
                        && escaped != data[position])
                    ++position; // Escaped CRLF/LFCR is one continuation.
            }
            else if (ch == '"')
                quoted = !quoted;
            else if (!quoted && ch == '=')
                hasAssignment = true;
            else if (!quoted && (lineBreak(ch) || ch == ';'))
            {
                end = position - 1;
                if (ch == ';')
                    while (position < data.size() && !lineBreak(data[position]))
                        ++position;
                break;
            }
        }
        const auto record = data.sliced(start, end - start);
        if (record.startsWith('[') ? !record.contains(']') : !hasAssignment)
            return QSettings::FormatError;
    }
    return QSettings::NoError;
}

UiLanguage languageFromValue(const QVariant& value)
{
    return value.toString() == QString::fromLatin1(SimplifiedChineseLanguageValue)
        ? UiLanguage::SimplifiedChinese
        : UiLanguage::English;
}

bool highlightCanvasBoundaryFromValue(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::Bool)
    {
        return value.toBool();
    }

    const QString normalized = value.toString().trimmed().toLower();
    return normalized == QStringLiteral("true") || normalized == QStringLiteral("1");
}

QString languageValue(UiLanguage language)
{
    return language == UiLanguage::SimplifiedChinese
        ? QString::fromLatin1(SimplifiedChineseLanguageValue)
        : QString::fromLatin1(EnglishLanguageValue);
}

}

ApplicationTranslationState::ApplicationTranslationState(QString applicationTranslationPath)
    : applicationTranslationPath_(std::move(applicationTranslationPath))
{
}

ApplicationTranslationState::~ApplicationTranslationState()
{
    QCoreApplication::removeTranslator(&applicationTranslator_);
    QCoreApplication::removeTranslator(&qtBaseTranslator_);
}

UiLanguage ApplicationTranslationState::setLanguage(UiLanguage language)
{
    if (language == activeLanguage_)
    {
        return activeLanguage_;
    }

    QCoreApplication::removeTranslator(&applicationTranslator_);
    QCoreApplication::removeTranslator(&qtBaseTranslator_);
    activeLanguage_ = UiLanguage::English;

    if (language == UiLanguage::SimplifiedChinese)
    {
        const QString applicationTranslationPath = applicationTranslationPath_.isEmpty()
            ? QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("i18n/DesktopSceneEngine_zh_CN.qm"))
            : applicationTranslationPath_;
        if (applicationTranslator_.load(applicationTranslationPath))
        {
            const QString qtBaseCatalogName = QStringLiteral("qtbase_zh_CN.qm");
            QString qtBaseTranslationPath = QDir(
                QLibraryInfo::path(QLibraryInfo::TranslationsPath))
                .filePath(qtBaseCatalogName);
            bool qtBaseTranslationLoaded = qtBaseTranslator_.load(qtBaseTranslationPath);
            if (!qtBaseTranslationLoaded)
            {
                qtBaseTranslationPath = QDir(QCoreApplication::applicationDirPath())
                    .filePath(QStringLiteral("i18n/") + qtBaseCatalogName);
                qtBaseTranslationLoaded = qtBaseTranslator_.load(qtBaseTranslationPath);
            }

            if (qtBaseTranslationLoaded)
            {
                QCoreApplication::installTranslator(&qtBaseTranslator_);
            }
            else
            {
                std::cerr << "Unable to load Qt Chinese translation catalog: "
                          << qtBaseTranslationPath.toStdString() << std::endl;
            }

            if (QCoreApplication::installTranslator(&applicationTranslator_))
                activeLanguage_ = UiLanguage::SimplifiedChinese;
            else
            {
                QCoreApplication::removeTranslator(&qtBaseTranslator_);
                std::cerr << "Unable to install Chinese application translation catalog.\n";
            }
        }
        else
        {
            std::cerr << "Unable to load Chinese translation catalog: "
                      << applicationTranslationPath.toStdString() << std::endl;
        }
    }

    return activeLanguage_;
}

UiLanguage ApplicationTranslationState::activeLanguage() const noexcept
{
    return activeLanguage_;
}

ApplicationPreferencesStore::ApplicationPreferencesStore(QSettings& settings) noexcept
    : settings_(settings)
{
}

ApplicationPreferences ApplicationPreferencesStore::load() const
{
    settings_.sync();
    ApplicationPreferences preferences{};
    preferences.language = languageFromValue(settings_.value(QString::fromLatin1(LanguageKey)));
    preferences.highlightCanvasBoundary =
        highlightCanvasBoundaryFromValue(settings_.value(QString::fromLatin1(HighlightCanvasBoundaryKey)));
    return preferences;
}

QSettings::Status ApplicationPreferencesStore::saveLanguage(UiLanguage language) const
{
    if (language != UiLanguage::English && language != UiLanguage::SimplifiedChinese)
        return QSettings::FormatError;
    auto operation = settingsOperation(settings_);
    operation->setValue(QString::fromLatin1(LanguageKey), languageValue(language));
    operation->sync();
    return operation->status();
}

std::optional<UiLanguage> ApplicationPreferencesStore::reloadLanguage() const
{
    auto operation = settingsOperation(settings_);
    operation->sync();
    const auto value = operation->value(QString::fromLatin1(LanguageKey));
    if (operation->status() != QSettings::NoError) return std::nullopt;
    return languageFromValue(value);
}

void ApplicationPreferencesStore::saveHighlightCanvasBoundary(bool highlighted) const
{
    settings_.setValue(
        QString::fromLatin1(HighlightCanvasBoundaryKey),
        highlighted);
    settings_.sync();
}

CanvasOutsidePreferenceRead ApplicationPreferencesStore::loadCanvasOutsideMode(
    const DisplayId& displayId) const
{
    const auto key = canvasOutsideModeKey(displayId);
    if (!key)
        return {std::nullopt, QSettings::FormatError};
    const auto storageStatus = validateIniStorage(settings_);
    if (storageStatus != QSettings::NoError)
        return {std::nullopt, storageStatus};
    auto operation = settingsOperation(settings_);
    operation->setFallbacksEnabled(false);
    operation->sync();
    if (operation->status() != QSettings::NoError)
        return {std::nullopt, operation->status()};
    // INI section parsing can report a format error during the lookup itself.
    const bool hasValue = operation->contains(*key);
    if (operation->status() != QSettings::NoError)
        return {std::nullopt, operation->status()};
    if (!hasValue)
        return {};
    const QVariant value = operation->value(*key);
    if (operation->status() != QSettings::NoError)
        return {std::nullopt, operation->status()};
    if (value.metaType().id() != QMetaType::QString)
        return {std::nullopt, QSettings::FormatError};
    if (value.toString() == QStringLiteral("opaque"))
        return {CanvasOutsideMode::Opaque, QSettings::NoError};
    if (value.toString() == QStringLiteral("transparent"))
        return {CanvasOutsideMode::Transparent, QSettings::NoError};
    return {std::nullopt, QSettings::FormatError};
}

QSettings::Status ApplicationPreferencesStore::saveCanvasOutsideMode(
    const DisplayId& displayId,
    CanvasOutsideMode mode) const
{
    if (mode != CanvasOutsideMode::Opaque && mode != CanvasOutsideMode::Transparent)
        return QSettings::FormatError;
    const auto key = canvasOutsideModeKey(displayId);
    if (!key)
        return QSettings::FormatError;
    auto operation = settingsOperation(settings_);
    operation->setFallbacksEnabled(false);
    operation->setValue(*key, mode == CanvasOutsideMode::Opaque
        ? QStringLiteral("opaque") : QStringLiteral("transparent"));
    operation->sync();
    return operation->status();
}

}
