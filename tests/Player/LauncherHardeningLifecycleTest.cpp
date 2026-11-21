// A7 isolated integration proof. Every child uses the production process-mode
// parser, same-image admission/activation/Join/control implementations. A small
// ordinary-close QWidget replaces the Engine only in this test executable.
#include "ApplicationCoordinator.h"
#include "ApplicationPreferences.h"
#include "LauncherBootstrap.h"
#include "LauncherIdentify.h"
#include "LauncherLanguage.h"
#include "LauncherRolePresentation.h"
#include "SessionPresentation.h"
#include "WebViewLauncherHost.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QIcon>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

class ApplicationCoordinatorTestAccess
{
public:
    static void catalog(ApplicationCoordinator& value, std::function<Platform::DisplayEnumerationResult()> catalog)
    { value.enumerateDisplays_ = std::move(catalog); }
    static void failure(ApplicationCoordinator& value, bool fail)
    {
        if (fail) value.launchSessionProcess_ = [](const DisplaySessionLaunchRequest&) -> void* { return nullptr; };
    }
    static void observeLaunches(ApplicationCoordinator& value, std::function<void()> launched)
    {
        const auto original = value.launchSessionProcess_;
        value.launchSessionProcess_ = [original, launched](const DisplaySessionLaunchRequest& request) -> void* {
            launched(); return original(request);
        };
    }
    static void budget(ApplicationCoordinator& value, int milliseconds) { value.createObservationMs_ = milliseconds; }
};

namespace
{
constexpr UINT HarnessMessage = WM_APP + 187;
constexpr int ConditionBudgetMs = 10000;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool waitUntil(const std::function<bool()>& predicate, int budget = ConditionBudgetMs)
{
    QElapsedTimer elapsed; elapsed.start();
    do { QApplication::processEvents(); if (predicate()) return true; QThread::msleep(5); }
    while (elapsed.elapsed() < budget);
    return predicate();
}
void report(const QJsonObject& value)
{ std::cout << QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString() << std::endl; }
QString canonical(const DisplayId& id)
{ const auto value = id.canonicalRepresentation(); return value ? QString::fromUtf8(value->data(), static_cast<qsizetype>(value->size())) : QString{}; }
InstanceSlot launcherSlot()
{ return *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope()); }
InstanceSlot sessionSlot(const DisplayId& id)
{ return *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{id}, *currentInteractiveInstanceScope()); }

QString peerJournal()
{ return QDir(qEnvironmentVariable("DSE_A7_TEST_JOURNAL")).filePath(QString::number(GetCurrentProcessId()) + QStringLiteral(".jsonl")); }
void peerReport(QJsonObject value)
{
    value.insert(QStringLiteral("pid"), QString::number(GetCurrentProcessId()));
    QFile file(peerJournal()); require(file.open(QIODevice::WriteOnly | QIODevice::Append), "A7 isolated peer journal opens");
    require(file.write(QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n') > 0, "A7 peer journal writes"); file.close();
}
class NativeHarness final : public QAbstractNativeEventFilter
{
public:
    HWND window{}; std::function<void(int)> invoke;
    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override
    {
        const auto* msg = static_cast<MSG*>(message);
        if (msg->hwnd != window || msg->message != HarnessMessage) return false;
        invoke(static_cast<int>(msg->wParam)); if (result) *result = 0; return true;
    }
};
class OrdinaryRole final : public QWidget
{
public:
    std::function<void(QCloseEvent*)> ordinaryClose;
    void closeEvent(QCloseEvent* event) override { ordinaryClose(event); }
};

int sessionPeer(const DisplayId& id)
{
    peerReport({{"event", "started"}, {"displayId", canonical(id)}});
    const auto slot = sessionSlot(id);
    InstanceAdmissionLease admission;
    const auto outcome = admission.acquire(slot);
    if (outcome == InstanceAdmissionOutcome::Occupied)
    {
        peerReport({{"event", "occupied"}});
        const auto activated = forwardActivation(slot);
        peerReport({{"event", "duplicateExit"}, {"activationDelivered", activated.outcome == ActivationForwardOutcome::Delivered}});
        return activated.outcome == ActivationForwardOutcome::Delivered ? 0 : 2;
    }
    require(outcome == InstanceAdmissionOutcome::Acquired || outcome == InstanceAdmissionOutcome::AbandonedAndAcquired,
        "A7 peer actual Display admission");
    peerReport({{"event", "admissionOwned"}});
    if (qEnvironmentVariable("DSE_A7_TEST_BEFORE_JOIN_EXIT") == QStringLiteral("1"))
    { peerReport({{"event", "exitBeforeJoin"}}); return 0; }
    InstanceActivationEndpoint activation(slot);
    require(activation.listen(), "A7 peer actual activation endpoint");
    OrdinaryRole role; role.setWindowTitle(QStringLiteral("A7 isolated production-protocol Session")); role.resize(520, 330);
    SessionPresentation presentation(role);
    SessionCoordinatorJoin join(slot, launcherSlot()); join.enableControl(presentation);
    bool joined = false;
    std::unique_ptr<QDialog> modal;
    role.ordinaryClose = [&](QCloseEvent* event) {
        peerReport({{"event", "ordinaryClose"}});
        presentation.beginTeardown(); join.beginRoleTeardown(); activation.beginShutdown(); event->accept();
        QTimer::singleShot(0, &role, [&] { join.drainAndStop([] { QApplication::quit(); }); });
    };
    role.show();
    activation.bindReadyRole(role, [&] { peerReport({{"event", "activationForwarded"}}); (void)presentation.restore(); });
    QWidget control;
    NativeHarness harness;
    harness.window = reinterpret_cast<HWND>(control.winId());
    const auto beginJoin = [&] {
        if (joined) return;
        joined = true; join.start(role, false); peerReport({{"event", "joinStarted"}});
    };
    harness.invoke = [&](int command) {
        if (command == 1) role.close();
        else if (command == 2) beginJoin();
        else if (command == 3) { role.showMinimized(); peerReport({{"event", "minimized"}}); }
        else if (command == 4) { role.showMaximized(); peerReport({{"event", "maximized"}}); }
        else if (command == 5) { role.hide(); peerReport({{"event", "hidden"}}); }
        else if (command == 6) {
            modal = std::make_unique<QDialog>(&role); modal->setWindowModality(Qt::ApplicationModal); modal->show();
            peerReport({{"event", "modal"}, {"hwnd", QString::number(modal->winId())}});
        }
        else if (command == 7) { require(modal != nullptr, "A7 actual owned modal exists"); modal->reject(); modal.reset(); }
        else if (command == 8) { join.stop(); peerReport({{"event", "controlStopped"}}); }
        else throw std::runtime_error("A7 unknown test-only peer command");
        peerReport({{"event", "command"}, {"command", command}});
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    peerReport({{"event", "readyRole"}, {"displayId", canonical(id)},
        {"incarnation", join.incarnation().toString(QUuid::WithoutBraces)},
        {"hwnd", QString::number(reinterpret_cast<quintptr>(harness.window))}, {"roleHwnd", QString::number(role.winId())}});
    if (qEnvironmentVariable("DSE_A7_TEST_HOLD_JOIN") != QStringLiteral("1")) beginJoin();
    const int result = QApplication::exec();
    QApplication::instance()->removeNativeEventFilter(&harness);
    peerReport({{"event", "cleanExit"}}); return result;
}

std::vector<QJsonObject> journal(const QString& directory)
{
    std::vector<QJsonObject> result;
    for (const auto& name : QDir(directory).entryList({QStringLiteral("*.jsonl")}, QDir::Files))
    {
        QFile file(QDir(directory).filePath(name)); if (!file.open(QIODevice::ReadOnly)) continue;
        for (const auto& line : file.readAll().split('\n'))
        { const auto row = QJsonDocument::fromJson(line).object(); if (!row.isEmpty()) result.push_back(row); }
    }
    return result;
}
class Peer
{
public:
    DWORD pid{}; HANDLE lifetime{}; HWND control{}; HWND role{}; QUuid incarnation; DisplayId display; QString directory;
    Peer(const QJsonObject& ready, QString where) : directory(std::move(where))
    {
        pid = ready.value(QStringLiteral("pid")).toString().toUInt();
        control = reinterpret_cast<HWND>(ready.value(QStringLiteral("hwnd")).toString().toULongLong());
        role = reinterpret_cast<HWND>(ready.value(QStringLiteral("roleHwnd")).toString().toULongLong());
        incarnation = QUuid(ready.value(QStringLiteral("incarnation")).toString());
        display = *DisplayId::parseCanonicalRepresentation(ready.value(QStringLiteral("displayId")).toString().toStdString());
        lifetime = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
        DWORD owner = 0, roleOwner = 0;
        wchar_t image[32768]{}; DWORD length = static_cast<DWORD>(std::size(image));
        require(lifetime && QueryFullProcessImageNameW(lifetime, 0, image, &length)
            && QFileInfo(QString::fromWCharArray(image)).canonicalFilePath().compare(QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath(), Qt::CaseInsensitive) == 0
            && GetWindowThreadProcessId(control, &owner) && owner == pid
            && GetWindowThreadProcessId(role, &roleOwner) && roleOwner == pid && !incarnation.isNull(),
            "A7 same-image pinned process and exact owned HWNDs");
    }
    ~Peer() { if (lifetime) CloseHandle(lifetime); }
    bool alive() const { return lifetime && WaitForSingleObject(lifetime, 0) == WAIT_TIMEOUT; }
    void command(int value)
    {
        DWORD owner = 0;
        require(alive() && GetWindowThreadProcessId(control, &owner) && owner == pid
            && PostMessageW(control, HarnessMessage, value, 0), "A7 commands only pinned owned process HWND");
    }
    int count(const QString& event) const
    {
        const auto rows = journal(directory);
        return static_cast<int>(std::count_if(rows.begin(), rows.end(), [&](const auto& row) {
            return row.value(QStringLiteral("pid")).toString().toUInt() == pid && row.value(QStringLiteral("event")).toString() == event;
        }));
    }
    void await(const QString& event) const { require(waitUntil([&] { return count(event) > 0; }), "A7 peer observed actual test-control event"); }
    void exited()
    {
        require(waitUntil([&] { return !alive(); }), "A7 ordinary child exit bounded");
        DWORD code = 999; require(GetExitCodeProcess(lifetime, &code) && code == 0 && count(QStringLiteral("cleanExit")) == 1,
            "A7 normal process exit and native Join drain");
    }
};

class BasicLauncher final : public QWidget, public LauncherRolePresentation
{
public:
    SessionPresentation presentation{*this}; std::function<void()> accepted;
    SessionPresentation& trayPresentation() override { return presentation; }
    void closeEvent(QCloseEvent* event) override { presentation.beginTeardown(); event->accept(); if (accepted) accepted(); }
};

struct Fixture
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath(QStringLiteral("a7-preferences.ini")), QSettings::IniFormat};
    ApplicationPreferencesStore preferences{settings}; ApplicationTranslationState translation;
    LauncherLanguage language{preferences, translation, [&] { ++languagePublications; barrier(QStringLiteral("Language")); return true; }};
    InstanceSlot slot = launcherSlot(); InstanceAdmissionLease admission; InstanceActivationEndpoint activation{slot};
    int drains = 0; ApplicationCoordinator coordinator{slot, activation, false, [&] { ++drains; }};
    std::vector<LauncherSessionsSnapshot> events;
    std::function<void(const LauncherSessionsSnapshot&)> onProjection;
    std::vector<std::unique_ptr<Peer>> peers;
    std::vector<std::unique_ptr<QProcess>> external;
    int hostCreations = 0; int launchCount = 0; int catalogMode = 0; bool recreate = true; bool webHost = false;
    int languagePublications = 0, languageChanges = 0, sessionDeliveries = 0;
    int bootstrapCommands = 0, refreshCommands = 0, identifyCommands = 0, createCommands = 0, openCommands = 0;
    QString holdProvider; bool providerReleased = true;
    QPointer<WebViewLauncherHost> heldHost; QString heldGeneration, activeProvider;
    void barrier(const QString& kind)
    {
        if (holdProvider != kind) return;
        holdProvider.clear(); providerReleased = false;
        heldHost = dynamic_cast<WebViewLauncherHost*>(coordinator.launcherWindow());
        heldGeneration = heldHost ? heldHost->pageGeneration() : QString{}; activeProvider = kind;
        report({{"event", "providerPending"}, {"provider", kind}, {"generation", heldGeneration}});
        require(waitUntil([&] { return providerReleased; }, 30000), "A7 controlled provider release observed");
        auto value = counters(); value.insert("event", "providerReleased"); value.insert("provider", kind);
        value.insert("originalGeneration", heldGeneration); report(value);
        heldHost.clear(); heldGeneration.clear(); activeProvider.clear();
    }
    QJsonObject counters() const
    {
        auto* currentHost = dynamic_cast<WebViewLauncherHost*>(coordinator.launcherWindow());
        return {{"currentHostPresent", currentHost != nullptr}, {"currentHostReady", currentHost && currentHost->isWebViewReady()},
            {"currentHostGeneration", currentHost ? currentHost->pageGeneration() : QString{}},
            {"heldHostAlive", !heldHost.isNull()}, {"heldHostReady", heldHost && heldHost->isWebViewReady()},
            {"heldHostGeneration", heldHost ? heldHost->pageGeneration() : QString{}}, {"activeProvider", activeProvider},
            {"bootstrap", bootstrapCommands}, {"refresh", refreshCommands}, {"identify", identifyCommands},
            {"create", createCommands}, {"open", openCommands}, {"launchCount", launchCount},
            {"languagePublications", languagePublications}, {"languageChanges", languageChanges},
            {"sessionDeliveries", sessionDeliveries}, {"hostCreations", hostCreations},
            {"sessionCount", static_cast<qint64>(coordinator.sessionCount())}, {"coordinatorPid", QString::number(GetCurrentProcessId())}};
    }
    // Space, embedded quote and trailing backslash exercise the actual retained
    // CreateProcess command-line quoting, then the exact canonical child parser.
    DisplayId first = DisplayId::fromTargetDevicePath("A7 exact \"target-A-" + QUuid::createUuid().toString().toStdString() + "\\");
    DisplayId second = DisplayId::fromTargetDevicePath("A7-exact-target-B-" + QUuid::createUuid().toString().toStdString());
    DisplayId weak = DisplayId::fromSourceDeviceName("A7-weak-source");

    Platform::DisplayEnumerationResult catalog() const
    {
        if (catalogMode == 2) return {Platform::DisplayEnumerationStatus::ApiFailure};
        Platform::DisplayEnumerationResult value{Platform::DisplayEnumerationStatus::Success}; int number = 0;
        for (const auto& id : {first, second, weak})
        {
            if (catalogMode == 1 && id == first) continue;
            Platform::DisplayDescriptor descriptor; descriptor.id = id;
            descriptor.friendlyName = id == first ? "A7 target A" : id == second ? "A7 target B" : "A7 weak identity";
            descriptor.displayNumber = ++number; descriptor.pixelWidth = 1920; descriptor.pixelHeight = 1080; descriptor.primary = id == first;
            value.snapshot.displays.push_back(std::move(descriptor));
        }
        return value;
    }
    explicit Fixture(bool web = false) : webHost(web)
    {
        require(directory.isValid(), "A7 isolated directory");
        require(preferences.saveLanguage(UiLanguage::English) == QSettings::NoError, "A7 isolated prefs initialized");
        qputenv("DSE_A7_TEST_JOURNAL", directory.path().toUtf8());
        require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired,
            "A7 refuses interference with a running production Coordinator");
        require(activation.listen() && coordinator.listen(), "A7 real Coordinator activation and Join endpoints");
        ApplicationCoordinatorTestAccess::catalog(coordinator, [&] { return catalog(); });
        ApplicationCoordinatorTestAccess::observeLaunches(coordinator, [&] { ++launchCount; });
        coordinator.setSessionProjectionChanged([&](const LauncherSessionsSnapshot& value) {
            require(value.coordinatorEpoch == coordinator.coordinatorEpoch(), "A7 epoch retained");
            require(events.empty() || value.revision > events.back().revision, "A7 delivered live revision advances");
            (void)launcherSessionsProjection(value);
            std::set<QString> targets, operations, roles;
            for (const auto& role : value.registeredSessions)
                require(targets.insert(canonical(role.boundDisplayId)).second && roles.insert(role.incarnation.toString()).second,
                    "A7 no two authoritative Sessions own one Display");
            targets.clear();
            for (const auto& operation : value.createOperations)
                require(targets.insert(canonical(operation.displayId)).second && operations.insert(operation.operationId.toString()).second,
                    "A7 at most one retained operation per exact Display");
            events.push_back(value); ++sessionDeliveries;
            report({{"event", "projection"}, {"payload", launcherSessionsProjection(value)}});
            if (auto* host = dynamic_cast<WebViewLauncherHost*>(coordinator.launcherWindow())) host->sessionsChanged(value);
            if (onProjection) onProjection(value);
        });
        language.setOnChanged([&](const LauncherLanguageProjection& value) {
            ++languageChanges; report({{"event", "languageChange"}, {"payload", serializeLauncherLanguage(value)}});
            if (auto* host = dynamic_cast<WebViewLauncherHost*>(coordinator.launcherWindow())) host->languageChanged(value);
        });
        coordinator.setLauncherFactory([&]() -> QWidget* {
            if (!webHost)
            {
                auto* window = new BasicLauncher; window->accepted = [&, window] { coordinator.retireLauncher(*window); }; return window;
            }
            auto* host = new WebViewLauncherHost([&] {
                    ++bootstrapCommands;
                    auto value = launcherBootstrapSnapshot(coordinator.launcherSessionSnapshot(), catalog(), language.reload());
                    report({{"event", "bootstrapCommand"}, {"payload", value}}); return value;
                }, [&] {
                    ++refreshCommands; const auto value = launcherDisplayCatalog(catalog());
                    barrier(QStringLiteral("Refresh")); report({{"event", "refreshCommand"}, {"payload", value}}); return value;
                }, [&](const DisplayId& id) {
                    ++identifyCommands;
                    const auto value = identifyLauncherDisplay(id, [](const DisplayId&, std::uint32_t) { return Platform::DisplayIdentifyStatus::Shown; });
                    barrier(QStringLiteral("Identify")); report({{"event", "identifyCommand"}, {"payload", value}}); return value;
                }, language);
            host->setSessionCommands([&](const QUuid& epoch, const DisplayId& id) {
                ++createCommands; const auto value = coordinator.createSession(epoch, id);
                barrier(QStringLiteral("Create"));
                report({{"event", "createCommand"}, {"result", launcherCreateResult(value)}, {"launchCount", launchCount}});
                return launcherCreateResult(value);
            }, [&](const QUuid& epoch, const QUuid& incarnation) {
                ++openCommands; const auto value = coordinator.openSession(epoch, incarnation);
                barrier(QStringLiteral("Open"));
                report({{"event", "openCommand"}, {"result", launcherOpenResult(value)}, {"launchCount", launchCount}});
                return launcherOpenResult(value);
            });
            host->setAcceptedCloseCallback([&, host] {
                coordinator.retireLauncher(*host);
                if (recreate) QTimer::singleShot(0, &coordinator, [&] { require(coordinator.showLauncher(), "A7 Host recreated by Coordinator"); });
                report({{"event", "hostRetired"}, {"creation", hostCreations}});
            });
            report({{"event", "hostCreated"}, {"creation", ++hostCreations}, {"hwnd", QString::number(host->winId())}}); return host;
        });
        ApplicationCoordinatorTestAccess::budget(coordinator, 60000);
        require(coordinator.showLauncher(), "A7 Coordinator-owned Launcher starts");
    }
    ~Fixture()
    {
        onProjection = {}; coordinator.setSessionProjectionChanged({}); language.setOnChanged({});
        // A failed assertion must still ordinary-close every test-owned ready
        // role, including a child observed before nextPeer() took its handle.
        try { cleanupOwned(); } catch (...) { report({{"event", "cleanupFailure"}, {"pass", false}}); }
        for (const auto& process : external) if (process->state() != QProcess::NotRunning) process->waitForFinished(1000);
        qunsetenv("DSE_A7_TEST_JOURNAL"); qunsetenv("DSE_A7_TEST_HOLD_JOIN"); qunsetenv("DSE_A7_TEST_BEFORE_JOIN_EXIT");
    }
    const LauncherSessionsSnapshot& delivered() const { require(!events.empty(), "A7 initial delivered snapshot"); return events.back(); }
    std::optional<LauncherCreateOperation> operation(const DisplayId& id) const
    { if (!events.empty()) for (const auto& value : delivered().createOperations) if (value.displayId == id) return value; return std::nullopt; }
    std::optional<LauncherRegisteredSession> role(const QUuid& id) const
    { if (!events.empty()) for (const auto& value : delivered().registeredSessions) if (value.incarnation == id) return value; return std::nullopt; }
    void state(const DisplayId& target, LauncherCreateState expected)
    { require(waitUntil([&] { const auto value = operation(target); return value && value->state == expected; }), "A7 real operation terminal/pending pushed observation"); }
    Peer& nextPeer(const DisplayId& target)
    {
        QJsonObject ready;
        require(waitUntil([&] {
            for (const auto& value : journal(directory.path()))
                if (value.value(QStringLiteral("event")) == QStringLiteral("readyRole")
                    && value.value(QStringLiteral("displayId")).toString() == canonical(target)
                    && std::none_of(peers.begin(), peers.end(), [&](const auto& item) { return item->pid == value.value(QStringLiteral("pid")).toString().toUInt(); }))
                { ready = value; return true; }
            return false;
        }), "A7 actual launched same-image child reaches real ready role");
        peers.push_back(std::make_unique<Peer>(ready, directory.path())); return *peers.back();
    }
    void startExternal(const DisplayId& target, bool held = false)
    {
        auto process = std::make_unique<QProcess>(); auto env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS")); env.remove(QStringLiteral("WEBVIEW2_USER_DATA_FOLDER"));
        if (held) env.insert(QStringLiteral("DSE_A7_TEST_HOLD_JOIN"), QStringLiteral("1"));
        else env.remove(QStringLiteral("DSE_A7_TEST_HOLD_JOIN"));
        process->setProcessEnvironment(env);
        const auto request = makeDisplaySessionLaunchRequest(QCoreApplication::applicationFilePath(), target);
        require(request.has_value(), "A7 external same-image production launch request");
        process->start(request->program, request->arguments); require(process->waitForStarted(5000), "A7 external child starts");
        external.push_back(std::move(process));
    }
    Peer& externalPeer(const DisplayId& target, bool held = false)
    { startExternal(target, held); return nextPeer(target); }
    LauncherCreateResult create(const DisplayId& id, bool held = false)
    {
        if (held) qputenv("DSE_A7_TEST_HOLD_JOIN", "1"); else qunsetenv("DSE_A7_TEST_HOLD_JOIN");
        const auto result = coordinator.createSession(coordinator.coordinatorEpoch(), id);
        qunsetenv("DSE_A7_TEST_HOLD_JOIN"); return result;
    }
    void observed(Peer& peer)
    {
        require(waitUntil([&] { const auto value = role(peer.incarnation); return value && value->boundDisplayId == peer.display && value->controlConnected; }),
            "A7 real authenticated Join/control published without pull polling");
        require(peer.alive(), "A7 registered Session is living pinned peer");
    }
    void close(Peer& peer)
    {
        peer.command(1); peer.exited();
        require(waitUntil([&] { return !role(peer.incarnation); }), "A7 verified death removes exact joined role live");
        const auto current = operation(peer.display);
        require(!current || current->state != LauncherCreateState::Pending, "A7 ordinary death leaves no STARTING");
    }
    void retired()
    {
        QPointer<QWidget> old = coordinator.launcherWindow(); const auto incarnation = coordinator.launcherIncarnation();
        require(old && old->close() && waitUntil([&] { return !old; }), "A7 old native Launcher retires");
        require(coordinator.showLauncher() && coordinator.launcherIncarnation() != incarnation, "A7 new native Launcher owns fresh lifetime");
    }
    void cleanupOwned()
    {
        for (const auto& row : journal(directory.path()))
        {
            if (row.value(QStringLiteral("event")) != QStringLiteral("readyRole")) continue;
            const auto pid = row.value(QStringLiteral("pid")).toString().toUInt();
            if (std::any_of(peers.begin(), peers.end(), [&](const auto& item) { return item->pid == pid; })) continue;
            const auto window = reinterpret_cast<HWND>(row.value(QStringLiteral("hwnd")).toString().toULongLong());
            DWORD owner = 0;
            if (GetWindowThreadProcessId(window, &owner) && owner == pid)
                peers.push_back(std::make_unique<Peer>(row, directory.path()));
        }
        for (const auto& peer : peers) if (peer->alive())
        {
            peer->command(1);
            require(waitUntil([&] { return !peer->alive(); }), "A7 failed-run cleanup ordinary-close owned peer");
        }
        require(waitUntil([&] { return coordinator.sessionCount() == 0; }), "A7 owned roles actually removed at cleanup");
    }
};


void reportPeer(const Peer& peer)
{
    report({{"event", "hostPeer"}, {"pid", QString::number(peer.pid)},
        {"incarnation", peer.incarnation.toString(QUuid::WithoutBraces)},
        {"displayId", canonical(peer.display)}, {"roleHwnd", QString::number(reinterpret_cast<quintptr>(peer.role))}});
}
void selfTest()
{
    Fixture fixture; int checks = 0;
    const auto check = [&](const char* name) { ++checks; report({{"event", "check"}, {"name", name}, {"pass", true}}); };
    auto& stable = fixture.externalPeer(fixture.first); fixture.observed(stable);
    const auto epoch = fixture.coordinator.coordinatorEpoch();
    QPointer<QWidget> old = fixture.coordinator.launcherWindow();
    require(old->close() && waitUntil([&] { return !old; }), "A7 native Launcher retires");
    require(stable.alive() && fixture.coordinator.sessionCount() == 1 && fixture.drains == 0, "A7 headless Coordinator retains exact living Session");
    check("actual native Session survives Launcher retirement");
    fixture.catalogMode = 1; require(fixture.preferences.saveLanguage(UiLanguage::SimplifiedChinese) == QSettings::NoError, "A7 isolated language change while absent");
    fixture.language.consumeInvalidation();
    auto& external = fixture.externalPeer(fixture.second); fixture.observed(external); fixture.close(external);
    require(fixture.coordinator.showLauncher() && fixture.coordinator.coordinatorEpoch() == epoch && stable.alive(), "A7 same Coordinator recreates presentation after external Join/death");
    const auto fresh = launcherBootstrapSnapshot(fixture.coordinator.launcherSessionSnapshot(), fixture.catalog(), fixture.language.reload());
    require(fresh.value("language").toObject().value("effective").toString() == QStringLiteral("zh_CN")
        && fixture.coordinator.sessionCount() == 1, "A7 fresh projection reads absent-period native truth");
    check("fresh native truth survives absent Launcher external Session Join and death");
    fixture.catalogMode = 0; const auto before = fixture.launchCount; const auto created = fixture.create(fixture.second, true);
    require(created.status == LauncherCreateStatus::Accepted && created.operationId, "A7 pending actual Create accepted");
    auto& pending = fixture.nextPeer(fixture.second); fixture.state(fixture.second, LauncherCreateState::Pending);
    fixture.retired();
    require(fixture.operation(fixture.second)->operationId == created.operationId && fixture.launchCount == before + 1, "A7 native owned operation and intended child survive Launcher lifetime");
    pending.command(2); fixture.observed(pending); fixture.state(fixture.second, LauncherCreateState::Registered); fixture.close(pending);
    require(stable.alive() && fixture.coordinator.coordinatorEpoch() == epoch, "A7 primary pinned Session and epoch survive");
    check("pending Create survives native presentation recreation and completes with real Join");
    fixture.close(stable); QPointer<QWidget> final = fixture.coordinator.launcherWindow();
    require(final->close() && waitUntil([&] { return !final && fixture.drains == 1; }), "A7 native final zero-role drain remains bounded");
    check("clean existing zero-role Coordinator drain");
    report({{"event", "summary"}, {"pass", true}, {"checks", checks}, {"launchCount", fixture.launchCount}, {"sessionEvents", fixture.sessionDeliveries}});
}
int hostFixture()
{
    Fixture fixture(true); QWidget control; NativeHarness harness;
    harness.window = reinterpret_cast<HWND>(control.winId());
    harness.invoke = [&](int command) {
        QTimer::singleShot(0, &fixture.coordinator, [&, command] {
            if (command == 20) qputenv("DSE_A7_TEST_HOLD_JOIN", "1");
            else if (command == 21) {
                auto& peer = fixture.nextPeer(fixture.first); peer.command(2); fixture.observed(peer); reportPeer(peer);
            }
            else if (command == 22) {
                const auto live = std::find_if(fixture.peers.rbegin(), fixture.peers.rend(), [](const auto& value) { return value->alive(); });
                require(live != fixture.peers.rend(), "A7 live peer exists before ordinary close"); fixture.close(**live);
            }
            else if (command == 25) {
                fixture.recreate = true; require(fixture.coordinator.launcherWindow(), "A7 current Host exists");
                fixture.coordinator.launcherWindow()->close();
            }
            else if (command == 26) {
                require(fixture.coordinator.sessionCount() == 0, "A7 Host shutdown requires zero roles");
                fixture.recreate = false; if (fixture.coordinator.launcherWindow()) fixture.coordinator.launcherWindow()->close();
                QTimer::singleShot(0, &fixture.coordinator, [] { QApplication::quit(); });
            }
            else if (command == 27) fixture.catalogMode = 1;
            else if (command == 28) fixture.catalogMode = 0;
            else if (command == 29) fixture.cleanupOwned();
            else if (command == 30) {
                fixture.recreate = false; require(fixture.coordinator.launcherWindow(), "A7 retiring current Host exists");
                fixture.coordinator.launcherWindow()->close();
            }
            else if (command == 31) {
                fixture.recreate = true; require(fixture.coordinator.showLauncher(), "A7 explicit Coordinator route recreates Host");
            }
            else if (command == 32) {
                const auto requested = fixture.language.current().effective == UiLanguage::English
                    ? UiLanguage::SimplifiedChinese : UiLanguage::English;
                require(fixture.preferences.saveLanguage(requested) == QSettings::NoError, "A7 isolated external language source writes");
                fixture.language.consumeInvalidation();
            }
            else if (command == 33 || command == 34) {
                auto& peer = fixture.externalPeer(command == 33 ? fixture.first : fixture.second);
                fixture.observed(peer); reportPeer(peer);
            }
            else if (command >= 40 && command <= 44) {
                const QString names[]{QStringLiteral("Refresh"), QStringLiteral("Language"), QStringLiteral("Create"),
                    QStringLiteral("Open"), QStringLiteral("Identify")};
                fixture.holdProvider = names[command - 40];
            }
            else if (command == 50) fixture.providerReleased = true;
            else if (command == 60) { /* A barrier acknowledged after all prior native dispatch. */ }
            else if (command == 61) fixture.language.consumeInvalidation();
            else if (command == 62) fixture.catalogMode = 2;
            else if (command == 63) {
                require(fixture.coordinator.sessionCount() == 0, "A7 atomic idle absence starts without Sessions");
                fixture.recreate = false; require(fixture.coordinator.launcherWindow(), "A7 idle Host exists");
                fixture.coordinator.launcherWindow()->close();
                require(waitUntil([&] { return !fixture.coordinator.launcherWindow(); }, 75), "A7 deferred presentation retirement observed inside existing idle drain");
                fixture.catalogMode = 1;
                const auto next = fixture.language.current().effective == UiLanguage::English ? UiLanguage::SimplifiedChinese : UiLanguage::English;
                require(fixture.preferences.saveLanguage(next) == QSettings::NoError, "A7 idle absent fresh preference truth");
                fixture.language.consumeInvalidation(); fixture.recreate = true;
                require(fixture.coordinator.showLauncher(), "A7 idle present restored within existing bounded drain");
                require(fixture.drains == 0, "A7 idle test does not suppress existing zero-role drain");
            }
            else throw std::runtime_error("A7 unknown Host test command");
            auto value = fixture.counters(); value.insert("event", "harnessCommand"); value.insert("command", command);
            report(value);
        });
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    report({{"event", "harnessReady"}, {"hwnd", QString::number(reinterpret_cast<quintptr>(harness.window))},
        {"firstDisplayId", canonical(fixture.first)}, {"secondDisplayId", canonical(fixture.second)},
        {"preferenceFile", fixture.settings.fileName()}, {"coordinatorPid", QString::number(GetCurrentProcessId())},
        {"scope", "isolated real same-image production Coordinator/admission/Join/Host; controlled synthetic catalog/Identify; isolated preferences; no physical overlay assertion"}});
    const int result = QApplication::exec(); QApplication::instance()->removeNativeEventFilter(&harness);
    fixture.cleanupOwned();
    for (const auto& peer : fixture.peers) require(!peer->alive(), "A7 Host final pinned owned peer exited");
    QJsonArray rows; for (const auto& value : journal(fixture.directory.path())) rows.append(value);
    report({{"event", "peerJournal"}, {"rows", rows}});
    auto clean = fixture.counters(); clean.insert("event", "cleanShutdown"); clean.insert("ownedPeerCount", static_cast<qint64>(fixture.peers.size()));
    report(clean); return result;
}
}
int main(int argc, char** argv)
{
    QApplication application(argc, argv); application.setQuitOnLastWindowClosed(false);
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    try
    {
        const auto arguments = application.arguments();
        if (arguments.value(1) == QStringLiteral("--host")) return hostFixture();
        if (arguments.size() == 1 || arguments.value(1) == QStringLiteral("--self-test")) { selfTest(); return 0; }
        if (arguments.value(1) == QStringLiteral("--display-session"))
        {
            const auto mode = parseApplicationProcessMode(arguments.mid(1));
            const auto* session = std::get_if<DisplaySessionProcessMode>(&mode);
            require(session && session->displayId.isPersistentOwnerCapable(), "A7 same-executable production Session CLI parser");
            return sessionPeer(session->displayId);
        }
        require(false, "A7 expects --host or production --display-session"); return 2;
    }
    catch (const std::exception& error) { report({{"event", "failure"}, {"message", QString::fromUtf8(error.what())}}); return 1; }
}
