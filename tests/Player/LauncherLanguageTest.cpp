#include "LauncherLanguage.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QWidget>

#include <iostream>
#include <stdexcept>

using namespace DesktopSceneEngine::Player;

namespace
{
int checks = 0;
bool require(bool condition, const char* message)
{
    ++checks;
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

int storageWrites = 0;
bool rejectWrites = false;
bool readFixture(QIODevice& device, QSettings::SettingsMap& settings)
{
    settings = QJsonDocument::fromJson(device.readAll()).object().toVariantMap();
    return true;
}
bool writeFixture(QIODevice& device, const QSettings::SettingsMap& settings)
{
    ++storageWrites;
    if (rejectWrites) return false;
    return device.write(QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson()) >= 0;
}
QSettings::Format fixtureFormat()
{
    static const auto format = QSettings::registerFormat("dsea4language", readFixture, writeFixture);
    return format;
}
QByteArray bytes(const QString& path)
{
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool hasStatus(const QJsonObject& result, const char* status)
{
    return result.value("status").toString() == QLatin1String{status};
}
bool warning(const QJsonObject& result, const char* value)
{
    return result.value("warnings").toArray().contains(QLatin1String{value});
}

template<class Predicate>
bool awaitEvents(Predicate predicate)
{
    if (predicate()) return true;
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
    poll.start(1);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    return predicate();
}

bool verifyProjection()
{
    const auto chinese = parseLauncherLanguage(QStringLiteral("zh_CN"));
    if (!require(chinese == UiLanguage::SimplifiedChinese
            && parseLauncherLanguage(QStringLiteral("en")) == UiLanguage::English,
            "The shared parser accepts precisely the production canonical locales")) return false;
    for (const QString& invalid : {QStringLiteral("zh-CN"), QStringLiteral("EN"),
             QStringLiteral(" en"), QStringLiteral("fr"), QString{}})
        if (!require(!parseLauncherLanguage(invalid), "Noncanonical locales must be rejected")) return false;
    const auto mismatch = serializeLauncherLanguage({UiLanguage::SimplifiedChinese, UiLanguage::English, 4});
    const auto failed = serializeLauncherLanguage({std::nullopt, UiLanguage::English, 5});
    return require(mismatch.value("committed") == "zh_CN" && mismatch.value("effective") == "en"
            && mismatch.value("readStatus") == "Success" && mismatch.value("revision").toInteger() == 4,
            "A single DTO preserves committed/effective distinction and revision")
        && require(failed.value("readStatus") == "Failure" && !failed.contains("committed")
            && failed.value("effective") == "en", "Read failure never fabricates committed English");
}

bool verifyCommits()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("commit.dsea4language"), fixtureFormat()};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    store.saveHighlightCanvasBoundary(true);
    ApplicationTranslationState translation;
    int published = 0;
    int changed = 0;
    bool ordered = false;
    LauncherLanguage owner{store, translation, [&]
    {
        ++published;
        ordered = store.reloadLanguage() == UiLanguage::SimplifiedChinese
            && translation.activeLanguage() == UiLanguage::SimplifiedChinese && changed == 1;
        return true;
    }};
    owner.setOnChanged([&](const LauncherLanguageProjection&) { ++changed; });
    const auto oldRevision = owner.current().revision;
    const auto chinese = owner.commit(UiLanguage::SimplifiedChinese);
    if (!hasStatus(chinese, "COMMITTED") || !ordered)
        std::cerr << "Commit diagnostic: " << QJsonDocument{chinese}.toJson(QJsonDocument::Compact).toStdString()
                  << " published=" << published << " changed=" << changed << " ordered=" << ordered
                  << " durable=" << bytes(settings.fileName()).toStdString() << '\n';
    if (!require(hasStatus(chinese, "COMMITTED") && ordered && published == 1 && changed == 1
            && owner.current().committed == UiLanguage::SimplifiedChinese
            && owner.current().effective == UiLanguage::SimplifiedChinese
            && owner.current().revision > oldRevision && chinese.value("warnings").toArray().isEmpty(),
            "en to zh_CN saves, applies and projects before publishing")) return false;
    if (!require(settings.value("ui/language") == "zh_CN"
            && settings.value("preview/highlightCanvasBoundary").toBool(),
            "Language commits preserve the key, canonical token and unrelated preference")) return false;
    const int savedWrites = storageWrites;
    const auto unchangedRevision = owner.current().revision;
    const auto same = owner.commit(UiLanguage::SimplifiedChinese);
    if (!require(hasStatus(same, "UNCHANGED") && storageWrites == savedWrites && published == 1
            && changed == 1 && owner.current().revision == unchangedRevision,
            "Same-value commit performs no write, publish, projection event or revision bump")) return false;
    (void)translation.setLanguage(UiLanguage::English);
    const auto noApply = owner.commit(UiLanguage::SimplifiedChinese);
    if (!require(hasStatus(noApply, "UNCHANGED") && translation.activeLanguage() == UiLanguage::English
            && storageWrites == savedWrites && published == 1,
            "Same-value commit must not reinstall translation even if effective currently differs")) return false;
    (void)owner.reload();
    const auto english = owner.commit(UiLanguage::English);
    if (!require(hasStatus(english, "COMMITTED") && owner.current().committed == UiLanguage::English
            && translation.activeLanguage() == UiLanguage::English && published == 2,
            "zh_CN to en persists and removes the real translator")) return false;
    bool invalidRejected = false;
    const int beforeInvalid = storageWrites;
    try { (void)owner.commit(static_cast<UiLanguage>(99)); }
    catch (const std::invalid_argument&) { invalidRejected = true; }
    return require(invalidRejected && storageWrites == beforeInvalid && published == 2,
        "Invalid C++ locale is rejected before touching persistence or publication");
}

bool verifyFailuresAndWarnings()
{
    QTemporaryDir directory;
    {
        // A directory cannot be opened as an INI file. Unlike a failed custom
        // parser's shared cache, this is a persistent per-operation read error.
        QSettings settings{directory.path(), QSettings::IniFormat};
        ApplicationPreferencesStore store{settings};
        ApplicationTranslationState translation;
        int published = 0;
        const int beforeWrites = storageWrites;
        LauncherLanguage owner{store, translation, [&] { ++published; return true; }};
        const auto result = owner.commit(UiLanguage::SimplifiedChinese);
        if (!hasStatus(result, "READ_FAILURE"))
            std::cerr << "Read failure diagnostic: " << QJsonDocument{result}.toJson(QJsonDocument::Compact).toStdString()
                      << " writes=" << storageWrites - beforeWrites << " publishes=" << published << '\n';
        const bool ok = require(hasStatus(result, "READ_FAILURE") && !owner.current().committed
                && owner.current().effective == UiLanguage::English
                && storageWrites == beforeWrites && published == 0,
            "Fresh read failure preserves failure and performs no save/apply/publish");
        if (!ok) return false;
    }
    {
        QSettings settings{directory.filePath("save-error.dsea4language"), fixtureFormat()};
        ApplicationPreferencesStore store{settings};
        store.saveLanguage(UiLanguage::English);
        const QByteArray durableBefore = bytes(settings.fileName());
        ApplicationTranslationState translation;
        int published = 0;
        LauncherLanguage owner{store, translation, [&] { ++published; return true; }};
        rejectWrites = true;
        const auto result = owner.commit(UiLanguage::SimplifiedChinese);
        const bool ok = require(hasStatus(result, "SAVE_FAILURE")
                && result.value("language").toObject().value("committed") == "en"
                && owner.current().committed == UiLanguage::English
                && translation.activeLanguage() == UiLanguage::English && published == 0
                && bytes(settings.fileName()) == durableBefore,
            "Rejected save returns old fresh committed truth; durable bytes/effective/publication stay unchanged");
        rejectWrites = false; // Qt may retry its retained failed-write cache later; no store semantics are changed here.
        if (!ok) return false;
    }
    {
        QSettings settings{directory.filePath("fallback.ini"), QSettings::IniFormat};
        ApplicationPreferencesStore store{settings};
        store.saveLanguage(UiLanguage::English);
        ApplicationTranslationState translation{directory.filePath("missing.qm")};
        int published = 0;
        LauncherLanguage owner{store, translation, [&] { ++published; return false; }};
        const auto result = owner.commit(UiLanguage::SimplifiedChinese);
        if (!require(hasStatus(result, "COMMITTED") && warning(result, "TranslationFallback")
                && warning(result, "NotificationFailure") && published == 1
                && store.reloadLanguage() == UiLanguage::SimplifiedChinese
                && owner.current().committed == UiLanguage::SimplifiedChinese
                && owner.current().effective == UiLanguage::English,
            "Catalog and publication warnings are post-commit partial success, never rollback")) return false;
    }
    {
        QSettings settings{directory.filePath("publish.ini"), QSettings::IniFormat};
        ApplicationPreferencesStore store{settings};
        store.saveLanguage(UiLanguage::English);
        ApplicationTranslationState translation;
        LauncherLanguage owner{store, translation, [] { return false; }};
        const auto result = owner.commit(UiLanguage::SimplifiedChinese);
        if (!require(hasStatus(result, "COMMITTED") && warning(result, "NotificationFailure")
                && !warning(result, "TranslationFallback")
                && translation.activeLanguage() == UiLanguage::SimplifiedChinese
                && store.reloadLanguage() == UiLanguage::SimplifiedChinese,
            "Notification failure retains both successfully saved and locally effective language")) return false;
    }
    return true;
}

#ifdef _WIN32
QWidget* receiver()
{
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "DSELauncherLanguageInvalidationReceiver") return widget;
    return nullptr;
}
void invalidate(QWidget& endpoint)
{
    SendMessageW(reinterpret_cast<HWND>(endpoint.winId()), globalSettingsChangedMessageId(), 0, 0);
}

bool verifyInvalidation()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("invalidation.dsea4language"), fixtureFormat()};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    ApplicationTranslationState translation;
    int published = 0;
    int changed = 0;
    {
        LauncherLanguage owner{store, translation, [&] { ++published; return true; }};
        owner.setOnChanged([&](const LauncherLanguageProjection&) { ++changed; });
        auto* endpoint = receiver();
        if (!require(endpoint && IsWindow(reinterpret_cast<HWND>(endpoint->winId()))
                && GetParent(reinterpret_cast<HWND>(endpoint->winId())) != HWND_MESSAGE
                && !endpoint->isVisible(),
                "Invalidation endpoint is a real hidden top-level native window, not HWND_MESSAGE")) return false;
        store.saveLanguage(UiLanguage::SimplifiedChinese);
        const int beforeWrites = storageWrites;
        invalidate(*endpoint);
        if (!require(translation.activeLanguage() == UiLanguage::English,
                "Native dispatch queues translation work instead of applying reentrantly")) return false;
        QApplication::processEvents();
        if (!require(owner.current().committed == UiLanguage::SimplifiedChinese
                && owner.current().effective == UiLanguage::SimplifiedChinese
                && storageWrites == beforeWrites && published == 0 && changed == 1,
                "External invalidation rereads/applies fresh truth without writes or rebroadcast")) return false;
        const auto revision = owner.current().revision;
        invalidate(*endpoint);
        invalidate(*endpoint);
        QApplication::processEvents();
        if (!require(owner.current().revision == revision && changed == 1
                && published == 0 && storageWrites == beforeWrites,
                "Repeated/self invalidation is idempotent with no duplicate event or feedback")) return false;
        store.saveLanguage(UiLanguage::English);
        invalidate(*endpoint);
        store.saveLanguage(UiLanguage::SimplifiedChinese);
        invalidate(*endpoint);
        QApplication::processEvents();
        if (!require(owner.current().committed == UiLanguage::SimplifiedChinese
                && owner.current().revision == revision && changed == 1,
                "Queued invalidations read latest storage, never captured stale language")) return false;
        store.saveLanguage(UiLanguage::English);
        invalidate(*endpoint);
        // The queued callback is bound to the owner; destroying it must cancel work.
    }
    QApplication::processEvents();
    return require(!receiver() && translation.activeLanguage() == UiLanguage::SimplifiedChinese
            && published == 0, "Owner destruction removes endpoint and cancels queued settings work");
}

class NotificationCounter final : public QWidget
{
public:
    NotificationCounter() { (void)winId(); }
    int count = 0;
    bool zeroPayload = true;
    bool nativeEvent(const QByteArray& type, void* data, qintptr* result) override
    {
        if (type == "windows_generic_MSG" && data)
        {
            const auto* message = static_cast<const MSG*>(data);
            if (message->message == globalSettingsChangedMessageId())
            {
                ++count;
                zeroPayload = zeroPayload && message->wParam == 0 && message->lParam == 0;
            }
        }
        return QWidget::nativeEvent(type, data, result);
    }
};

struct NativeFixture
{
    QString application = QStringLiteral("A4-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSettings settings{QSettings::NativeFormat, QSettings::UserScope,
        QStringLiteral("DesktopSceneEngineTests"), application};
    NativeFixture() { settings.setFallbacksEnabled(false); }
    ~NativeFixture() { settings.clear(); settings.sync(); }
};

int nativeChild(const QStringList& arguments)
{
    if (arguments.size() != 5 || !arguments[2].startsWith("A4-")) return 2;
    const auto language = parseLauncherLanguage(arguments[3]);
    if (!language) return 2;
    QSettings settings{QSettings::NativeFormat, QSettings::UserScope,
        QStringLiteral("DesktopSceneEngineTests"), arguments[2]};
    settings.setFallbacksEnabled(false);
    ApplicationPreferencesStore store{settings};
    if (arguments[4] == "write")
        return store.saveLanguage(*language) == QSettings::NoError && broadcastGlobalSettingsChanged() ? 0 : 1;
    return store.reloadLanguage() == language ? 0 : 1;
}

bool child(const QString& application, const QString& language, const QString& action)
{
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), {"--native-child", application, language, action});
    const bool started = process.waitForStarted(5000);
    const bool finished = started && process.waitForFinished(10000);
    if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        std::cerr << process.readAllStandardError().toStdString();
    return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool verifyNativePersistenceAndProcesses()
{
    NativeFixture fixture;
    ApplicationPreferencesStore store{fixture.settings};
    store.saveLanguage(UiLanguage::English);
    {
        ApplicationTranslationState translation;
        LauncherLanguage owner{store, translation, [] { return true; }};
        if (!require(hasStatus(owner.commit(UiLanguage::SimplifiedChinese), "COMMITTED"),
                "NativeFormat isolated test store accepts real canonical commit")) return false;
    }
    if (!require(child(fixture.application, "zh_CN", "read"),
            "A new process restores committed language from the isolated NativeFormat key")) return false;
    ApplicationTranslationState translation;
    LauncherLanguage owner{store, translation};
    if (!require(owner.reload().committed == UiLanguage::SimplifiedChinese
            && owner.current().effective == UiLanguage::SimplifiedChinese,
            "Owner destroy/recreate + reload restores committed and effective state")) return false;
    NotificationCounter notifications;
    int changed = 0;
    owner.setOnChanged([&](const LauncherLanguageProjection&) { ++changed; });
    if (!require(child(fixture.application, "en", "write"),
            "Independent process saves and broadcasts a zero-payload invalidation")) return false;
    if (!require(awaitEvents([&] { return owner.current().committed == UiLanguage::English
                && notifications.count >= 1; })
            && translation.activeLanguage() == UiLanguage::English && changed == 1
            && notifications.count == 1 && notifications.zeroPayload,
            "Cross-process invalidation reaches real HWND and imports authoritative current language once")) return false;
    const auto result = owner.commit(UiLanguage::SimplifiedChinese);
    if (!require(hasStatus(result, "COMMITTED")
            && awaitEvents([&] { return notifications.count == 2; }),
            "Native successful commit publishes one real broadcast")) return false;
    QApplication::processEvents();
    return require(notifications.count == 2 && notifications.zeroPayload && changed == 2,
        "Own broadcast neither republishes nor repeats changed notification");
}
#endif

bool verifyTransactionRevision()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("revision.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    ApplicationTranslationState translation;
    LauncherLanguage* instance = nullptr;
    LauncherLanguage owner{store, translation, [&]
    {
        store.saveLanguage(UiLanguage::English);
        instance->consumeInvalidation();
        return true;
    }};
    instance = &owner;
    const auto result = owner.commit(UiLanguage::SimplifiedChinese);
    const auto transaction = result.value("language").toObject();
    return require(hasStatus(result, "COMMITTED") && transaction.value("committed") == "zh_CN"
            && owner.current().committed == UiLanguage::English
            && transaction.value("revision").toInteger() < static_cast<qint64>(owner.current().revision),
        "A later invalidation cannot make an older commit result masquerade as the newest projection");
}
}

int main(int argc, char** argv)
{
    QApplication application{argc, argv};
    application.setQuitOnLastWindowClosed(false);
#ifdef _WIN32
    if (application.arguments().value(1) == "--native-child") return nativeChild(application.arguments());
#endif
    const bool ok = verifyProjection() && verifyCommits() && verifyFailuresAndWarnings()
        && verifyTransactionRevision()
#ifdef _WIN32
        && verifyInvalidation() && verifyNativePersistenceAndProcesses()
#endif
        ;
    if (ok) std::cout << "Launcher language focused checks: " << checks << "/" << checks << " PASS\n";
    else std::cout << "Launcher language focused checks: FAIL after " << checks << " assertions\n";
    return ok ? 0 : 1;
}
