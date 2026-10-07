#include "ApplicationPreferences.h"
#include "GlobalSettingsNotification.h"
#include "LauncherWindow.h"
#include "MainWindow.h"
#include "MainWindowTestDisplay.h"
#include "CanvasChromeFrame.h"
#include "BackgroundMusicWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace DesktopSceneEngine::Player;

class MainWindowDocumentLifecycleTestAccess
{
public:
    static UiLanguage effective(const MainWindow& w) { return w.translationState_.activeLanguage(); }
    static WorkspaceModel& model(MainWindow& w) { return w.workspaceModel_; }
    static ProjectDocumentState& document(MainWindow& w) { return w.documentState_; }
    static bool highlight(const MainWindow& w) { return w.canvasFrame_->isHighlighted(); }
    static void toggleHighlight(MainWindow& w) { w.highlightCanvasBoundaryAction_->trigger(); }
    static BackgroundMusicWindow* openBgm(MainWindow& w, PlaylistId id) { return w.openBackgroundMusicWindow(id); }
};

class LauncherWindowTestAccess
{
public:
    static void commit(LauncherWindow& window, UiLanguage language, bool (&publish)())
    { window.commitLanguage(language, publish); }
    static void catalog(LauncherWindow& window, const DesktopSceneEngine::Platform::DisplayEnumerationResult& result)
    { window.applyDisplayEnumeration(result); }
    static auto selected(const LauncherWindow& window) { return window.selectedDisplayId(); }
    static void actionStatus(LauncherWindow& window)
    { window.statusSourceText_ = "Display Session started."; window.updateStatus(); }
    static QString status(const LauncherWindow& window) { return window.statusLabel_->text(); }
};

namespace
{
bool require(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << std::endl;
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
    static const auto format = QSettings::registerFormat("dselanguage", readFixture, writeFixture);
    return format;
}

#ifdef _WIN32
class NotificationCounter final : public QWidget
{
public:
    int count = 0;
    bool zeroPayload = true;
    NotificationCounter() { (void)winId(); }
    bool nativeEvent(const QByteArray& type, void* nativeData, qintptr* result) override
    {
        const auto* message = static_cast<const MSG*>(nativeData);
        if (type == "windows_generic_MSG" && message->message == globalSettingsChangedMessageId())
        {
            ++count;
            zeroPayload = zeroPayload && message->wParam == 0 && message->lParam == 0;
        }
        return QWidget::nativeEvent(type, nativeData, result);
    }
};
#endif

template<class Predicate>
bool awaitEvents(Predicate predicate)
{
    if (predicate()) return true;
    QEventLoop loop;
    QTimer observation;
    QObject::connect(&observation, &QTimer::timeout, &loop, [&]
    { if (predicate()) loop.quit(); });
    observation.start(1);
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    loop.exec();
    return predicate();
}

QPushButton* settingsButton(LauncherWindow& window)
{
    const auto text = QCoreApplication::translate("LauncherWindow", "Settings");
    for (auto* button : window.findChildren<QPushButton*>())
        if (button->text() == text) return button;
    return nullptr;
}

enum class DialogOutcome { Accept, Cancel, Close };
bool dialogChoice(LauncherWindow& window, ApplicationPreferencesStore& store,
    ApplicationTranslationState& translation, DialogOutcome outcome, UiLanguage choice, UiLanguage before)
{
    auto* button = settingsButton(window);
    if (!require(button != nullptr, "Launcher must expose a Language Settings flow")) return false;
    bool stagingIsolated = false;
    QTimer::singleShot(0, &window, [&]
    {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* combo = dialog ? dialog->findChild<QComboBox*>() : nullptr;
        auto* buttons = dialog ? dialog->findChild<QDialogButtonBox*>() : nullptr;
        if (!combo || !buttons) { if (dialog) dialog->reject(); return; }
        combo->setCurrentIndex(combo->findData(static_cast<int>(choice)));
        stagingIsolated = store.load().language == before && translation.activeLanguage() == before;
        if (outcome == DialogOutcome::Close) dialog->close();
        else buttons->button(outcome == DialogOutcome::Accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    button->click();
    return require(stagingIsolated, "Dialog selection must not persist or apply before OK");
}

#ifdef _WIN32
UINT messageId() { return globalSettingsChangedMessageId(); }
void invalidate(MainWindow& window)
{
    SendMessageW(reinterpret_cast<HWND>(window.winId()), messageId(), 0, 0);
}
#endif

bool verifyLauncher()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.dselanguage"), fixtureFormat()};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    store.saveHighlightCanvasBoundary(true);
    ApplicationTranslationState translation;
    LauncherWindow window{translation, store};
    DesktopSceneEngine::Platform::DisplayDescriptor display;
    display.id = TestSupport::mainWindowTestDisplayId();
    display.displayNumber = 1;
    display.pixelWidth = 1920;
    display.pixelHeight = 1080;
    LauncherWindowTestAccess::catalog(window,
        {DesktopSceneEngine::Platform::DisplayEnumerationStatus::Success, {{display}}});
    window.findChild<QListWidget*>()->setCurrentRow(0);
    LauncherWindowTestAccess::actionStatus(window);
#ifdef _WIN32
    NotificationCounter notifications;
#endif
    const auto originalWrites = storageWrites;
    if (!dialogChoice(window, store, translation, DialogOutcome::Cancel, UiLanguage::SimplifiedChinese, UiLanguage::English)) return false;
    if (!dialogChoice(window, store, translation, DialogOutcome::Close, UiLanguage::SimplifiedChinese, UiLanguage::English)) return false;
    QApplication::processEvents();
    if (!require(storageWrites == originalWrites, "Cancel/window close must perform no storage write")) return false;
#ifdef _WIN32
    if (!require(notifications.count == 0, "Cancel must publish no invalidation")) return false;
#endif
    if (!require(store.load().language == UiLanguage::English && translation.activeLanguage() == UiLanguage::English,
        "Cancel must leave committed Language and Launcher unchanged")) return false;
    if (!dialogChoice(window, store, translation, DialogOutcome::Accept, UiLanguage::SimplifiedChinese, UiLanguage::English)) return false;
    QApplication::processEvents();
#ifdef _WIN32
    if (!require(awaitEvents([&] { return notifications.count > 0; })
        && notifications.count == 1 && notifications.zeroPayload,
        "Changed OK must publish one zero-payload invalidation")) return false;
#endif
    if (!require(settingsButton(window)->text() == QString::fromUtf8("设置"), "Launcher persistent Settings caption must retranslate")) return false;
    if (!require(LauncherWindowTestAccess::selected(window) == display.id
        && window.findChild<QListWidget*>()->currentItem()->text().startsWith(QString::fromUtf8("显示器 1"))
        && LauncherWindowTestAccess::status(window) == QCoreApplication::translate("LauncherWindow", "Display Session started."),
        "Launcher retranslation must preserve display identity/selection and last action status")) return false;
    if (!require(store.load().language == UiLanguage::SimplifiedChinese && store.load().highlightCanvasBoundary
        && translation.activeLanguage() == UiLanguage::SimplifiedChinese,
        "OK must commit Language, apply Launcher and preserve highlight")) return false;
    const auto committedWrites = storageWrites;
    const bool unchanged = dialogChoice(window, store, translation, DialogOutcome::Accept, UiLanguage::SimplifiedChinese, UiLanguage::SimplifiedChinese);
    QApplication::processEvents();
    return unchanged && require(storageWrites == committedWrites, "Unchanged OK must not write storage")
#ifdef _WIN32
        && require(notifications.count == 1, "Unchanged OK must not notify")
#endif
        ;
}

int published = 0;
ApplicationPreferencesStore* publishStore = nullptr;
ApplicationTranslationState* publishTranslation = nullptr;
UiLanguage expectedEffective = UiLanguage::English;
bool publishOrderCorrect = false;
bool observedPublisher()
{
    ++published;
    publishOrderCorrect = publishStore->reloadLanguage() == UiLanguage::SimplifiedChinese
        && publishTranslation->activeLanguage() == expectedEffective;
    return true;
}
bool failedPublisher() { observedPublisher(); return false; }

template<class Action>
int dismissWarnings(Action action)
{
    int warnings = 0;
    QTimer closer;
    QObject::connect(&closer, &QTimer::timeout, [&]
    {
        if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        { ++warnings; warning->accept(); }
    });
    closer.start(0);
    action();
    return warnings;
}

bool verifyPartialSuccess()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("partial.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    ApplicationTranslationState missing{directory.filePath("missing.qm")};
    LauncherWindow window{missing, store};
    published = 0;
    publishStore = &store;
    publishTranslation = &missing;
    expectedEffective = UiLanguage::English;
    const auto warnings = dismissWarnings([&] { LauncherWindowTestAccess::commit(window, UiLanguage::SimplifiedChinese, observedPublisher); });
    if (!require(warnings == 1 && published == 1 && publishOrderCorrect
        && store.load().language == UiLanguage::SimplifiedChinese && missing.activeLanguage() == UiLanguage::English,
        "Catalog failure after commit must retain desired Language, report fallback and still notify")) return false;

    store.saveLanguage(UiLanguage::English);
    ApplicationTranslationState translation;
    LauncherWindow other{translation, store};
    published = 0;
    publishTranslation = &translation;
    expectedEffective = UiLanguage::SimplifiedChinese;
    const auto publicationWarnings = dismissWarnings([&] { LauncherWindowTestAccess::commit(other, UiLanguage::SimplifiedChinese, failedPublisher); });
    return require(publicationWarnings == 1 && published == 1 && publishOrderCorrect
        && store.load().language == UiLanguage::SimplifiedChinese && translation.activeLanguage() == UiLanguage::SimplifiedChinese,
        "Publication failure must report partial success without rolling back persistence or local application");
}

bool verifyRejectedCommit()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("failure.dselanguage"), fixtureFormat()};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    ApplicationTranslationState translation;
    LauncherWindow window{translation, store};
    QFile file(settings.fileName());
    if (!require(file.open(QIODevice::ReadOnly), "Durable fixture must be readable before rejected commit")) return false;
    const auto before = file.readAll();
    file.close();
    published = 0;
    rejectWrites = true;
    const auto warnings = dismissWarnings([&] { LauncherWindowTestAccess::commit(window, UiLanguage::SimplifiedChinese, observedPublisher); });
    if (!require(file.open(QIODevice::ReadOnly), "Durable fixture must be readable after rejected commit"))
    { rejectWrites = false; return false; }
    const auto after = file.readAll();
    file.close();
    const bool result = require(warnings == 1 && published == 0 && translation.activeLanguage() == UiLanguage::English
        && before == after, "Failed write must not apply or publish attempted cached state as committed truth");
    rejectWrites = false;
    return result;
}

bool verifyConsumer(bool dirty)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    ApplicationTranslationState translation;
    MainWindow window{TestSupport::mainWindowTestDisplayId(), translation, store, store.load()};
    auto& model = MainWindowDocumentLifecycleTestAccess::model(window);
    auto& document = MainWindowDocumentLifecycleTestAccess::document(window);
    if (model.playlists().isEmpty()) (void)model.addPlaylist();
    document.cleanRevision = model.authoredRevision();
    auto* editor = MainWindowDocumentLifecycleTestAccess::openBgm(window, model.playlists().front().id);
    if (!require(editor != nullptr, "Tracked BGM editor precondition must hold")) return false;
    const auto alias = model.playlists().front().displayAlias;
    if (dirty) (void)model.addPlaylist();
    const auto revision = model.authoredRevision();
    const auto clean = document.cleanRevision;
    const auto wasDirty = document.isDirty(revision);
    if (!require(wasDirty == dirty, "Clean/dirty document precondition must hold")) return false;
    QSettings external{settings.fileName(), QSettings::IniFormat};
    external.setValue("ui/language", "zh_CN");
    external.setValue("preview/highlightCanvasBoundary", true);
    external.sync();
#ifdef _WIN32
    invalidate(window);
    if (!require(translation.activeLanguage() == UiLanguage::English,
        "Native receiver must queue, not apply during native dispatch")) return false;
#endif
    QApplication::processEvents();
    const bool applied = require(translation.activeLanguage() == UiLanguage::SimplifiedChinese,
        "Invalidation must reload authoritative Language while Engine is uninitialized/undeployed");
    const bool isolated = require(model.authoredRevision() == revision && document.cleanRevision == clean
        && document.isDirty(revision) == wasDirty,
        "Language reload must preserve clean/dirty authored revision relationship");
    const bool bgmRetranslated = require(editor->windowTitle()
        == QCoreApplication::translate("BackgroundMusicWindow", "Background Music — %1").arg(alias),
        "Native invalidation must retranslate the existing tracked BGM editor");
    auto* highlight = static_cast<QAction*>(nullptr);
    for (auto* action : window.findChildren<QAction*>())
        if (action->text() == QCoreApplication::translate("MainWindow", "Highlight Canvas Boundary")) highlight = action;
    return applied && isolated && bgmRetranslated && require(highlight && !highlight->isChecked(),
        "Language invalidation must not import another Session's highlight setting");
}

bool verifyQueuedLifecycle()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("queued.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    ApplicationTranslationState translation;
    {
        MainWindow window{TestSupport::mainWindowTestDisplayId(), translation, store, store.load()};
        store.saveLanguage(UiLanguage::SimplifiedChinese);
        invalidate(window);
        store.saveLanguage(UiLanguage::English);
        invalidate(window);
        invalidate(window);
        QApplication::processEvents();
        if (!require(translation.activeLanguage() == UiLanguage::English,
            "Repeated/reordered hints must reload latest truth, not replay a captured preference")) return false;
    }
    {
        MainWindow window{TestSupport::mainWindowTestDisplayId(), translation, store, store.load()};
        store.saveLanguage(UiLanguage::SimplifiedChinese);
        invalidate(window);
        // Destroy the receiver before its context-bound queued operation runs.
    }
    QApplication::processEvents();
    return require(translation.activeLanguage() == UiLanguage::English,
        "Queued settings work must be canceled when MainWindow is destroyed");
}

bool verifyAuthority()
{
    ApplicationTranslationState translation;
    MainWindow window{TestSupport::mainWindowTestDisplayId(), translation};
    for (auto* action : window.findChildren<QAction*>())
        if (action->text() == "English" || action->text() == QString::fromUtf8("简体中文"))
            return require(false, "MainWindow must expose no Language-authoring actions");
    return true;
}

bool verifyPersistenceFailure()
{
    QSettings invalid{QSettings::NativeFormat, QSettings::UserScope, QString{}, QString{}};
    ApplicationPreferencesStore store{invalid};
    return require(store.saveLanguage(UiLanguage::SimplifiedChinese) != QSettings::NoError,
        "Invalid persistence location must not report commit success");
}

bool verifyStartup()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    const auto initial = store.load();
    ApplicationTranslationState translation;
    static_cast<void>(translation.setLanguage(initial.language));
    store.saveLanguage(UiLanguage::SimplifiedChinese); // Commit in bootstrap-to-endpoint gap; no receiver yet.
    MainWindow window{TestSupport::mainWindowTestDisplayId(), translation, store, initial};
    return require(!window.isVisible() && translation.activeLanguage() == UiLanguage::SimplifiedChinese,
        "Hidden endpoint establishment must reconcile a commit missed after bootstrap before UI exposure");
}

int receiver(const QString& path)
{
    QSettings settings{path, QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    ApplicationTranslationState translation;
    const auto initial = store.load();
    static_cast<void>(translation.setLanguage(initial.language));
    MainWindow window{TestSupport::mainWindowTestDisplayId(), translation, store, initial};
    window.winId();
#ifdef _WIN32
    NotificationCounter notification;
#endif
    std::cout << "READY" << std::endl;
    QTimer observation;
    QObject::connect(&observation, &QTimer::timeout, &window, [&]
    {
        if (translation.activeLanguage() == UiLanguage::SimplifiedChinese
#ifdef _WIN32
            && notification.count > 0 && notification.zeroPayload
#endif
            )
        {
            std::cout << "RESTORED_AND_INVALIDATED" << std::endl;
            window.close();
            QCoreApplication::exit(0);
        }
    });
    observation.start(20); // Test observation only; production has no settings polling timer.
    QTimer::singleShot(2500, &window, [] { QCoreApplication::exit(1); });
    return QApplication::exec();
}

int readNative(const QString& applicationName)
{
    QSettings settings{QSettings::NativeFormat, QSettings::UserScope, "DesktopSceneEngineTests", applicationName};
    settings.setFallbacksEnabled(false);
    ApplicationPreferencesStore store{settings};
    const auto preferences = store.load();
    ApplicationTranslationState translation;
    static_cast<void>(translation.setLanguage(preferences.language));
    MainWindow window{TestSupport::mainWindowTestDisplayId(), translation, store, preferences};
    const bool result = translation.activeLanguage() == UiLanguage::English && preferences.highlightCanvasBoundary
        && MainWindowDocumentLifecycleTestAccess::highlight(window);
    window.close();
    if (result) std::cout << "NATIVE_FRESH_PROCESS_RESTORE" << std::endl;
    return result ? 0 : 1;
}

bool verifyNativePersistence()
{
    const auto applicationName = "GlobalLanguage-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool result = false;
    {
        QSettings settings{QSettings::NativeFormat, QSettings::UserScope, "DesktopSceneEngineTests", applicationName};
        settings.setFallbacksEnabled(false);
        ApplicationPreferencesStore store{settings};
        if (!require(store.saveLanguage(UiLanguage::SimplifiedChinese) == QSettings::NoError,
            "Isolated native registry fixture must accept Language write")) return false;
        store.saveHighlightCanvasBoundary(false);
        ApplicationTranslationState translation;
        static_cast<void>(translation.setLanguage(store.load().language));
        MainWindow window{TestSupport::mainWindowTestDisplayId(), translation, store, store.load()};
        LauncherWindow launcher{translation, store};
        result = dialogChoice(launcher, store, translation, DialogOutcome::Accept, UiLanguage::English, UiLanguage::SimplifiedChinese);
        QApplication::processEvents();
        MainWindowDocumentLifecycleTestAccess::toggleHighlight(window);
        QSettings observed{QSettings::NativeFormat, QSettings::UserScope, "DesktopSceneEngineTests", applicationName};
        result = require(observed.value("ui/language").toString() == "en"
            && observed.value("preview/highlightCanvasBoundary").toBool(),
            "Real Launcher Language flow and Session highlight action must reach native registry independently") && result;
        result = window.close() && launcher.close() && result;
    }
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--native-read", applicationName});
    result = require(child.waitForFinished(5000) && child.exitStatus() == QProcess::NormalExit
        && child.exitCode() == 0 && child.readAllStandardOutput().contains("NATIVE_FRESH_PROCESS_RESTORE"),
        "Fresh independent process must bootstrap Language and restore Canvas highlight from native registry") && result;
    // Only this uniquely named test-owned registry fixture is removed.
    QSettings cleanup{QSettings::NativeFormat, QSettings::UserScope, "DesktopSceneEngineTests", applicationName};
    cleanup.setFallbacksEnabled(false);
    cleanup.clear();
    cleanup.sync();
    return result;
}

bool verifyProcesses()
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("process.ini"), QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::English);
    QProcess a, b;
    const auto program = QCoreApplication::applicationFilePath();
    for (auto* child : {&a, &b})
    {
        child->start(program, {"--receiver", settings.fileName()});
        if (!require(child->waitForStarted() && child->waitForReadyRead()
            && child->readAllStandardOutput().contains("READY"), "Independent receiver process must establish HWND")) return false;
    }
    store.saveLanguage(UiLanguage::SimplifiedChinese);
    QProcess sender;
    sender.start(program, {"--sender"}); // A new sender unrelated to receiver launch history.
    bool result = require(sender.waitForFinished() && sender.exitCode() == 0, "Independent sender must publish invalidation");
    for (auto* child : {&a, &b})
        result = require(child->waitForFinished(5000) && child->exitStatus() == QProcess::NormalExit
            && child->exitCode() == 0 && child->readAllStandardOutput().contains("RESTORED_AND_INVALIDATED"),
            "Each independent MainWindow process must consume broadcast and exit normally") && result;
    return result;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() == 3 && args[1] == "--receiver") return receiver(args[2]);
    if (args.size() == 3 && args[1] == "--native-read") return readNative(args[2]);
    if (args.contains("--sender"))
    {
#ifdef _WIN32
        return broadcastGlobalSettingsChanged() ? 0 : 1;
#else
        return 1;
#endif
    }
    const QString name = args.size() == 2 ? args[1] : QString{};
    bool all = true;
    const auto run = [&](const char* key, auto check)
    {
        if (!name.isEmpty() && name != key) return;
        const bool result = check();
        std::cout << key << ": " << (result ? "PASS" : "FAIL") << std::endl;
        all = result && all;
    };
    run("launcher", verifyLauncher);
    run("authority", verifyAuthority);
    run("persistence", verifyPersistenceFailure);
    run("rejected-commit", verifyRejectedCommit);
    run("partial-success", verifyPartialSuccess);
    run("native-persistence", verifyNativePersistence);
    run("consumer-clean", [] { return verifyConsumer(false); });
    run("consumer-dirty", [] { return verifyConsumer(true); });
    run("startup", verifyStartup);
    run("queued-lifecycle", verifyQueuedLifecycle);
    run("processes", verifyProcesses);
    return all ? 0 : 1;
}
