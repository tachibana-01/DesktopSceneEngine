// A6 isolated integration proof. Every child uses the production process-mode
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
constexpr UINT HarnessMessage = WM_APP + 186;
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
{ return QDir(qEnvironmentVariable("DSE_A6_TEST_JOURNAL")).filePath(QString::number(GetCurrentProcessId()) + QStringLiteral(".jsonl")); }
void peerReport(QJsonObject value)
{
    value.insert(QStringLiteral("pid"), QString::number(GetCurrentProcessId()));
    QFile file(peerJournal()); require(file.open(QIODevice::WriteOnly | QIODevice::Append), "A6 isolated peer journal opens");
    require(file.write(QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n') > 0, "A6 peer journal writes"); file.close();
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
        "A6 peer actual Display admission");
    peerReport({{"event", "admissionOwned"}});
    if (qEnvironmentVariable("DSE_A6_TEST_BEFORE_JOIN_EXIT") == QStringLiteral("1"))
    { peerReport({{"event", "exitBeforeJoin"}}); return 0; }
    InstanceActivationEndpoint activation(slot);
    require(activation.listen(), "A6 peer actual activation endpoint");
    OrdinaryRole role; role.setWindowTitle(QStringLiteral("A6 isolated production-protocol Session")); role.resize(520, 330);
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
        else if (command == 7) { require(modal != nullptr, "A6 actual owned modal exists"); modal->reject(); modal.reset(); }
        else if (command == 8) { join.stop(); peerReport({{"event", "controlStopped"}}); }
        else throw std::runtime_error("A6 unknown test-only peer command");
        peerReport({{"event", "command"}, {"command", command}});
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    peerReport({{"event", "readyRole"}, {"displayId", canonical(id)},
        {"incarnation", join.incarnation().toString(QUuid::WithoutBraces)},
        {"hwnd", QString::number(reinterpret_cast<quintptr>(harness.window))}, {"roleHwnd", QString::number(role.winId())}});
    if (qEnvironmentVariable("DSE_A6_TEST_HOLD_JOIN") != QStringLiteral("1")) beginJoin();
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
            "A6 same-image pinned process and exact owned HWNDs");
    }
    ~Peer() { if (lifetime) CloseHandle(lifetime); }
    bool alive() const { return lifetime && WaitForSingleObject(lifetime, 0) == WAIT_TIMEOUT; }
    void command(int value)
    {
        DWORD owner = 0;
        require(alive() && GetWindowThreadProcessId(control, &owner) && owner == pid
            && PostMessageW(control, HarnessMessage, value, 0), "A6 commands only pinned owned process HWND");
    }
    int count(const QString& event) const
    {
        const auto rows = journal(directory);
        return static_cast<int>(std::count_if(rows.begin(), rows.end(), [&](const auto& row) {
            return row.value(QStringLiteral("pid")).toString().toUInt() == pid && row.value(QStringLiteral("event")).toString() == event;
        }));
    }
    void await(const QString& event) const { require(waitUntil([&] { return count(event) > 0; }), "A6 peer observed actual test-control event"); }
    void exited()
    {
        require(waitUntil([&] { return !alive(); }), "A6 ordinary child exit bounded");
        DWORD code = 999; require(GetExitCodeProcess(lifetime, &code) && code == 0 && count(QStringLiteral("cleanExit")) == 1,
            "A6 normal process exit and native Join drain");
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
    QSettings settings{directory.filePath(QStringLiteral("a6-preferences.ini")), QSettings::IniFormat};
    ApplicationPreferencesStore preferences{settings}; ApplicationTranslationState translation;
    LauncherLanguage language{preferences, translation};
    InstanceSlot slot = launcherSlot(); InstanceAdmissionLease admission; InstanceActivationEndpoint activation{slot};
    int drains = 0; ApplicationCoordinator coordinator{slot, activation, false, [&] { ++drains; }};
    std::vector<LauncherSessionsSnapshot> events;
    std::function<void(const LauncherSessionsSnapshot&)> onProjection;
    std::vector<std::unique_ptr<Peer>> peers;
    std::vector<std::unique_ptr<QProcess>> external;
    int hostCreations = 0; int launchCount = 0; int catalogMode = 0; bool recreate = true; bool webHost = false;
    // Space, embedded quote and trailing backslash exercise the actual retained
    // CreateProcess command-line quoting, then the exact canonical child parser.
    DisplayId first = DisplayId::fromTargetDevicePath("A6 exact \"target-A-" + QUuid::createUuid().toString().toStdString() + "\\");
    DisplayId second = DisplayId::fromTargetDevicePath("A6-exact-target-B-" + QUuid::createUuid().toString().toStdString());
    DisplayId weak = DisplayId::fromSourceDeviceName("A6-weak-source");

    Platform::DisplayEnumerationResult catalog() const
    {
        if (catalogMode == 2) return {Platform::DisplayEnumerationStatus::ApiFailure};
        Platform::DisplayEnumerationResult value{Platform::DisplayEnumerationStatus::Success}; int number = 0;
        for (const auto& id : {first, second, weak})
        {
            if (catalogMode == 1 && id == first) continue;
            Platform::DisplayDescriptor descriptor; descriptor.id = id;
            descriptor.friendlyName = id == first ? "A6 target A" : id == second ? "A6 target B" : "A6 weak identity";
            descriptor.displayNumber = ++number; descriptor.pixelWidth = 1920; descriptor.pixelHeight = 1080; descriptor.primary = id == first;
            value.snapshot.displays.push_back(std::move(descriptor));
        }
        return value;
    }
    explicit Fixture(bool web = false) : webHost(web)
    {
        require(directory.isValid(), "A6 isolated directory");
        require(preferences.saveLanguage(UiLanguage::English) == QSettings::NoError, "A6 isolated prefs initialized");
        qputenv("DSE_A6_TEST_JOURNAL", directory.path().toUtf8());
        require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired,
            "A6 refuses interference with a running production Coordinator");
        require(activation.listen() && coordinator.listen(), "A6 real Coordinator activation and Join endpoints");
        ApplicationCoordinatorTestAccess::catalog(coordinator, [&] { return catalog(); });
        ApplicationCoordinatorTestAccess::observeLaunches(coordinator, [&] { ++launchCount; });
        coordinator.setSessionProjectionChanged([&](const LauncherSessionsSnapshot& value) {
            require(value.coordinatorEpoch == coordinator.coordinatorEpoch(), "A6 epoch retained");
            require(events.empty() || value.revision > events.back().revision, "A6 delivered live revision advances");
            (void)launcherSessionsProjection(value);
            std::set<QString> targets, operations, roles;
            for (const auto& role : value.registeredSessions)
                require(targets.insert(canonical(role.boundDisplayId)).second && roles.insert(role.incarnation.toString()).second,
                    "A6 no two authoritative Sessions own one Display");
            targets.clear();
            for (const auto& operation : value.createOperations)
                require(targets.insert(canonical(operation.displayId)).second && operations.insert(operation.operationId.toString()).second,
                    "A6 at most one retained operation per exact Display");
            events.push_back(value);
            if (auto* host = dynamic_cast<WebViewLauncherHost*>(coordinator.launcherWindow())) host->sessionsChanged(value);
            if (onProjection) onProjection(value);
        });
        language.setOnChanged([&](const LauncherLanguageProjection& value) {
            if (auto* host = dynamic_cast<WebViewLauncherHost*>(coordinator.launcherWindow())) host->languageChanged(value);
        });
        coordinator.setLauncherFactory([&]() -> QWidget* {
            if (!webHost)
            {
                auto* window = new BasicLauncher; window->accepted = [&, window] { coordinator.retireLauncher(*window); }; return window;
            }
            auto* host = new WebViewLauncherHost([&] { return launcherBootstrapSnapshot(coordinator.launcherSessionSnapshot(), catalog(), language.reload()); },
                [&] { return launcherDisplayCatalog(catalog()); }, [](const DisplayId& id) { return identifyLauncherDisplay(id); }, language);
            host->setSessionCommands([&](const QUuid& epoch, const DisplayId& id) {
                const auto value = coordinator.createSession(epoch, id);
                report({{"event", "createCommand"}, {"result", launcherCreateResult(value)}, {"launchCount", launchCount}});
                return launcherCreateResult(value);
            }, [&](const QUuid& epoch, const QUuid& incarnation) {
                const auto value = coordinator.openSession(epoch, incarnation);
                report({{"event", "openCommand"}, {"result", launcherOpenResult(value)}, {"launchCount", launchCount}});
                return launcherOpenResult(value);
            });
            host->setAcceptedCloseCallback([&, host] {
                coordinator.retireLauncher(*host);
                if (recreate) QTimer::singleShot(0, &coordinator, [&] { require(coordinator.showLauncher(), "A6 Host recreated by Coordinator"); });
            });
            report({{"event", "hostCreated"}, {"creation", ++hostCreations}, {"hwnd", QString::number(host->winId())}}); return host;
        });
        require(coordinator.showLauncher(), "A6 Coordinator-owned Launcher starts");
    }
    ~Fixture()
    {
        onProjection = {}; coordinator.setSessionProjectionChanged({}); language.setOnChanged({});
        // A failed assertion must still ordinary-close every test-owned ready
        // role, including a child observed before nextPeer() took its handle.
        try { cleanupOwned(); } catch (...) { report({{"event", "cleanupFailure"}, {"pass", false}}); }
        for (const auto& process : external) if (process->state() != QProcess::NotRunning) process->waitForFinished(1000);
        qunsetenv("DSE_A6_TEST_JOURNAL"); qunsetenv("DSE_A6_TEST_HOLD_JOIN"); qunsetenv("DSE_A6_TEST_BEFORE_JOIN_EXIT");
    }
    const LauncherSessionsSnapshot& delivered() const { require(!events.empty(), "A6 initial delivered snapshot"); return events.back(); }
    std::optional<LauncherCreateOperation> operation(const DisplayId& id) const
    { if (!events.empty()) for (const auto& value : delivered().createOperations) if (value.displayId == id) return value; return std::nullopt; }
    std::optional<LauncherRegisteredSession> role(const QUuid& id) const
    { if (!events.empty()) for (const auto& value : delivered().registeredSessions) if (value.incarnation == id) return value; return std::nullopt; }
    void state(const DisplayId& target, LauncherCreateState expected)
    { require(waitUntil([&] { const auto value = operation(target); return value && value->state == expected; }), "A6 real operation terminal/pending pushed observation"); }
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
        }), "A6 actual launched same-image child reaches real ready role");
        peers.push_back(std::make_unique<Peer>(ready, directory.path())); return *peers.back();
    }
    void startExternal(const DisplayId& target, bool held = false)
    {
        auto process = std::make_unique<QProcess>(); auto env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS")); env.remove(QStringLiteral("WEBVIEW2_USER_DATA_FOLDER"));
        if (held) env.insert(QStringLiteral("DSE_A6_TEST_HOLD_JOIN"), QStringLiteral("1"));
        else env.remove(QStringLiteral("DSE_A6_TEST_HOLD_JOIN"));
        process->setProcessEnvironment(env);
        const auto request = makeDisplaySessionLaunchRequest(QCoreApplication::applicationFilePath(), target);
        require(request.has_value(), "A6 external same-image production launch request");
        process->start(request->program, request->arguments); require(process->waitForStarted(5000), "A6 external child starts");
        external.push_back(std::move(process));
    }
    Peer& externalPeer(const DisplayId& target, bool held = false)
    { startExternal(target, held); return nextPeer(target); }
    LauncherCreateResult create(const DisplayId& id, bool held = false)
    {
        if (held) qputenv("DSE_A6_TEST_HOLD_JOIN", "1"); else qunsetenv("DSE_A6_TEST_HOLD_JOIN");
        const auto result = coordinator.createSession(coordinator.coordinatorEpoch(), id);
        qunsetenv("DSE_A6_TEST_HOLD_JOIN"); return result;
    }
    void observed(Peer& peer)
    {
        require(waitUntil([&] { const auto value = role(peer.incarnation); return value && value->boundDisplayId == peer.display && value->controlConnected; }),
            "A6 real authenticated Join/control published without pull polling");
        require(peer.alive(), "A6 registered Session is living pinned peer");
    }
    void close(Peer& peer)
    {
        peer.command(1); peer.exited();
        require(waitUntil([&] { return !role(peer.incarnation); }), "A6 verified death removes exact joined role live");
        const auto current = operation(peer.display);
        require(!current || current->state != LauncherCreateState::Pending, "A6 ordinary death leaves no STARTING");
    }
    void retired()
    {
        QPointer<QWidget> old = coordinator.launcherWindow(); const auto incarnation = coordinator.launcherIncarnation();
        require(old && old->close() && waitUntil([&] { return !old; }), "A6 old native Launcher retires");
        require(coordinator.showLauncher() && coordinator.launcherIncarnation() != incarnation, "A6 new native Launcher owns fresh lifetime");
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
            require(waitUntil([&] { return !peer->alive(); }), "A6 failed-run cleanup ordinary-close owned peer");
        }
        require(waitUntil([&] { return coordinator.sessionCount() == 0; }), "A6 owned roles actually removed at cleanup");
    }
};

void nativeChecks()
{
    Fixture fixture; int checks = 0, repeats = 0, races = 0;
    const auto check = [&](const char* name) { ++checks; report({{"event", "check"}, {"name", name}, {"pass", true}}); };
    require(waitUntil([&] { return !fixture.events.empty(); }), "A6 initial native push");
    require(fixture.coordinator.createSession(QUuid::createUuid(), fixture.first).status == LauncherCreateStatus::StaleCoordinator, "A6 stale Create epoch rejected");
    require(fixture.coordinator.openSession(QUuid::createUuid(), QUuid::createUuid()).status == LauncherOpenStatus::StaleCoordinator, "A6 stale Open epoch rejected");
    require(fixture.create(fixture.weak).status == LauncherCreateStatus::NotOwnerCapable, "A6 weak identity rejected");
    fixture.catalogMode = 1; require(fixture.create(fixture.first).status == LauncherCreateStatus::Unresolved, "A6 exact disappeared target rejected");
    fixture.catalogMode = 2; require(fixture.create(fixture.first).status == LauncherCreateStatus::CatalogUnavailable, "A6 enumeration failure rejected");
    fixture.catalogMode = 0; require(fixture.launchCount == 0, "A6 invalid capabilities never launch");
    check("native epoch/weak/unresolved/catalog checks reject before process launch");
    for (int cycle = 0; cycle != 10; ++cycle)
    {
        const auto before = fixture.launchCount; const auto created = fixture.create(fixture.first);
        require(created.status == LauncherCreateStatus::Accepted && created.operationId && fixture.launchCount == before + 1,
            "A6 accepted Create launches one real child and owns operation identity");
        auto& peer = fixture.nextPeer(fixture.first); fixture.observed(peer); fixture.state(fixture.first, LauncherCreateState::Registered);
        require(fixture.operation(fixture.first)->incarnation == peer.incarnation, "A6 actual Join supplies exact operation Session incarnation");
        const auto existing = fixture.create(fixture.first);
        require(existing.status == LauncherCreateStatus::AlreadyRegistered && existing.incarnation == peer.incarnation && fixture.launchCount == before + 1,
            "A6 known exact registered role short circuits without hidden Open or new process");
        fixture.close(peer); ++repeats;
        report({{"event", "repeat"}, {"cycle", repeats}, {"pass", true}, {"incarnation", peer.incarnation.toString(QUuid::WithoutBraces)}, {"pid", QString::number(peer.pid)}});
    }
    check("10 actual Create/admission/Join/normal Close cycles: no duplicate/stale/STARTING ghost");
    for (int cycle = 0; cycle != 5; ++cycle)
    {
        const auto before = fixture.launchCount; const auto accepted = fixture.create(fixture.first, true);
        require(accepted.status == LauncherCreateStatus::Accepted, "A6 held actual Create accepted");
        auto& peer = fixture.nextPeer(fixture.first); fixture.state(fixture.first, LauncherCreateState::Pending);
        const auto duplicate = fixture.create(fixture.first);
        require(duplicate.status == LauncherCreateStatus::AlreadyPending && duplicate.operationId == accepted.operationId && fixture.launchCount == before + 1,
            "A6 duplicate pending request returns same native operation and launches no second child");
        peer.command(2); fixture.observed(peer); fixture.state(fixture.first, LauncherCreateState::Registered); fixture.close(peer);
        ++races; report({{"event", "race"}, {"kind", "duplicate-create"}, {"cycle", cycle + 1}, {"pass", true}});
    }
    check("duplicate Create keeps one process and one native pending operation");
    for (int cycle = 0; cycle != 5; ++cycle)
    {
        auto& winner = fixture.externalPeer(fixture.first, true);
        require(!fixture.role(winner.incarnation), "A6 admission owner can exist before Coordinator registry");
        const auto accepted = fixture.create(fixture.first);
        require(accepted.status == LauncherCreateStatus::Accepted, "A6 missing registry does not reject legitimate admission attempt");
        require(waitUntil([&] {
            const auto rows = journal(fixture.directory.path());
            return static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const auto& value) {
                return value.value(QStringLiteral("event")) == QStringLiteral("duplicateExit") && value.value(QStringLiteral("activationDelivered")).toBool();
            })) == cycle + 1;
        }), "A6 occupied child forwards genuine activation then exits");
        require(winner.alive() && winner.count(QStringLiteral("activationForwarded")) > 0 && !fixture.role(winner.incarnation),
            "A6 external owner remains admitted/activated before Join and no fake ACTIVE");
        winner.command(2); fixture.observed(winner);
        // Depending on observed order the duplicate death is terminal first; a
        // later actual Join remains the sole Session authority in either case.
        require(waitUntil([&] { const auto operation = fixture.operation(fixture.first); return operation && operation->state != LauncherCreateState::Pending; }),
            "A6 external winner/duplicate death produces truthful bounded terminal operation");
        require(fixture.delivered().registeredSessions.size() == 1, "A6 external-start race has one exact authoritative owner");
        fixture.close(winner); ++races; report({{"event", "race"}, {"kind", "external-owner-before-join"}, {"cycle", cycle + 1}, {"pass", true}});
    }
    check("external-start/Create occupied race preserves owner, activation and truthful terminal state");
    for (int cycle = 0; cycle != 5; ++cycle)
    {
        // Dispatch the external process and native Create without waiting for
        // either child's admission/endpoint/ready event. The production mutex
        // decides which same-image contender wins. Both defer only their Join.
        fixture.startExternal(fixture.first, true);
        const auto accepted = fixture.create(fixture.first, true);
        require(accepted.status == LauncherCreateStatus::Accepted, "A6 simultaneous admission contender Create accepted");
        auto& winner = fixture.nextPeer(fixture.first);
        require(waitUntil([&] {
            const auto rows = journal(fixture.directory.path());
            return static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const auto& value) {
                return value.value(QStringLiteral("event")) == QStringLiteral("duplicateExit") && value.value(QStringLiteral("activationDelivered")).toBool();
            })) == cycle + 6;
        }), "A6 simultaneous contenders yield exactly one occupied/activation-forwarded duplicate");
        require(winner.alive() && !fixture.role(winner.incarnation), "A6 simultaneous winner owns admission without fake pre-Join ACTIVE");
        winner.command(2); fixture.observed(winner);
        require(waitUntil([&] { const auto operation = fixture.operation(fixture.first); return operation && operation->state != LauncherCreateState::Pending; }),
            "A6 simultaneous external/Create contention has truthful terminal correlation");
        require(fixture.delivered().registeredSessions.size() == 1, "A6 simultaneous admission has one authoritative owner");
        fixture.close(winner); ++races;
        report({{"event", "race"}, {"kind", "simultaneous-external-create"}, {"cycle", cycle + 1}, {"pass", true}});
    }
    check("simultaneous external process/native Create contenders preserve one admission owner");
    for (int cycle = 0; cycle != 5; ++cycle)
    {
        require(fixture.create(fixture.first, true).status == LauncherCreateStatus::Accepted, "A6 death-race Create accepted");
        auto& peer = fixture.nextPeer(fixture.first); fixture.state(fixture.first, LauncherCreateState::Pending);
        peer.command(2); fixture.observed(peer); peer.command(1); peer.exited();
        require(waitUntil([&] { const auto operation = fixture.operation(fixture.first); return !fixture.role(peer.incarnation)
            && operation && operation->state != LauncherCreateState::Pending; }), "A6 joined role immediate ordinary death has no ACTIVE/STARTING ghost");
        ++races; report({{"event", "race"}, {"kind", "join-then-death"}, {"cycle", cycle + 1}, {"pass", true}});
    }
    check("Join/immediate death resolves operation and removes exact Session");
    for (int cycle = 0; cycle != 5; ++cycle)
    {
        const auto before = fixture.launchCount; const auto created = fixture.create(fixture.first, true);
        auto& peer = fixture.nextPeer(fixture.first); fixture.state(fixture.first, LauncherCreateState::Pending);
        fixture.retired();
        const auto bootstrap = fixture.coordinator.launcherSessionSnapshot();
        require(bootstrap.createOperations.size() <= 2 && std::any_of(bootstrap.createOperations.begin(), bootstrap.createOperations.end(), [&](const auto& value) {
            return value.operationId == created.operationId && value.state == LauncherCreateState::Pending;
        }) && fixture.launchCount == before + 1, "A6 pending operation survives real Launcher retirement/recreation with no command replay");
        peer.command(2); fixture.observed(peer); fixture.state(fixture.first, LauncherCreateState::Registered); fixture.close(peer);
        ++races; report({{"event", "race"}, {"kind", "pending-launcher-recreate"}, {"cycle", cycle + 1}, {"pass", true}});
    }
    check("pending operation survives five real Launcher retire/recreate lifetimes");
    {
        const auto created = fixture.create(fixture.first, true); auto& held = fixture.nextPeer(fixture.first);
        fixture.state(fixture.first, LauncherCreateState::Pending);
        auto& wrong = fixture.externalPeer(fixture.second); fixture.observed(wrong);
        require(fixture.operation(fixture.first)->operationId == created.operationId && fixture.operation(fixture.first)->state == LauncherCreateState::Pending,
            "A6 wrong Display Join cannot complete exact target operation");
        fixture.close(wrong); held.command(2); fixture.observed(held); fixture.state(fixture.first, LauncherCreateState::Registered); fixture.close(held);
    }
    check("wrong-Display Join does not complete another Display operation");
    {
        qputenv("DSE_A6_TEST_BEFORE_JOIN_EXIT", "1"); const auto result = fixture.create(fixture.first); qunsetenv("DSE_A6_TEST_BEFORE_JOIN_EXIT");
        require(result.status == LauncherCreateStatus::Accepted, "A6 exit-before-Join launch accepted without ready fabrication");
        fixture.state(fixture.first, LauncherCreateState::ExitedBeforeRegistration);
        require(fixture.delivered().registeredSessions.empty(), "A6 exited child never inserted as Session");
    }
    check("verified launched process exit before Join ends STARTING truthfully");
    {
        ApplicationCoordinatorTestAccess::budget(fixture.coordinator, 150);
        require(fixture.create(fixture.first, true).status == LauncherCreateStatus::Accepted, "A6 uncertain test starts real held child");
        auto& peer = fixture.nextPeer(fixture.first); fixture.state(fixture.first, LauncherCreateState::Uncertain);
        require(peer.alive() && !fixture.role(peer.incarnation), "A6 deadline is uncertainty only, never readiness or process death");
        peer.command(2); fixture.observed(peer); fixture.close(peer);
        ApplicationCoordinatorTestAccess::budget(fixture.coordinator, ActivationDeadlineMs);
    }
    check("bounded observation deadline ends STARTING as Uncertain without false death/ACTIVE");
    QUuid stale;
    {
        require(fixture.create(fixture.first).status == LauncherCreateStatus::Accepted, "A6 Open source creates real role");
        auto& peer = fixture.nextPeer(fixture.first); fixture.observed(peer); stale = peer.incarnation;
        require(fixture.coordinator.openSession(fixture.coordinator.coordinatorEpoch(), stale).status == LauncherOpenStatus::ActivationRequested,
            "A6 exact live Open uses production activation route");
        peer.command(4); peer.await(QStringLiteral("maximized")); peer.command(3); peer.await(QStringLiteral("minimized"));
        require(fixture.coordinator.openSession(fixture.coordinator.coordinatorEpoch(), stale).status == LauncherOpenStatus::ActivationRequested,
            "A6 exact minimized/maximized Open accepted");
        require(waitUntil([&] { return !IsIconic(peer.role) && IsZoomed(peer.role); }), "A6 production restore preserves maximized state");
        peer.command(6); peer.await(QStringLiteral("modal"));
        require(fixture.coordinator.openSession(fixture.coordinator.coordinatorEpoch(), stale).status == LauncherOpenStatus::ActivationRequested,
            "A6 production modal Open accepted");
        require(waitUntil([&] { return GetLastActivePopup(peer.role) != peer.role; }), "A6 actual native modal remains interaction target");
        peer.command(7); fixture.close(peer);
        require(fixture.create(fixture.first).status == LauncherCreateStatus::Accepted, "A6 replacement real role Create");
        auto& replacement = fixture.nextPeer(fixture.first); fixture.observed(replacement);
        const auto before = fixture.launchCount;
        require(replacement.incarnation != stale && fixture.coordinator.openSession(fixture.coordinator.coordinatorEpoch(), stale).status == LauncherOpenStatus::RoleGone
            && fixture.launchCount == before, "A6 stale Open neither retargets same-Display replacement nor creates process");
        fixture.close(replacement);
    }
    check("exact Open restore/modal routing and stale incarnation never retarget/fallback");
    ApplicationCoordinatorTestAccess::failure(fixture.coordinator, true);
    require(fixture.create(fixture.first).status == LauncherCreateStatus::LaunchFailed, "A6 isolated launch failure explicit");
    fixture.state(fixture.first, LauncherCreateState::LaunchFailed);
    require(fixture.delivered().registeredSessions.empty() && fixture.delivered().createOperations.size() <= 2,
        "A6 launch failure leaves no fabricated role/unbounded receipt accumulation");
    check("real owner launch seam failure leaves no STARTING and bounded receipt storage");
    QPointer<QWidget> launcher = fixture.coordinator.launcherWindow();
    require(launcher->close() && waitUntil([&] { return !launcher && fixture.drains == 1; }), "A6 clean final Coordinator drain");
    check("clean final shutdown after zero roles and no pending Create");
    QJsonArray rows; for (const auto& value : journal(fixture.directory.path())) rows.append(value);
    report({{"event", "peerJournal"}, {"rows", rows}});
    report({{"event", "summary"}, {"pass", true}, {"checks", checks}, {"createJoinCloseCycles", repeats}, {"raceCycles", races},
        {"launchCount", fixture.launchCount}, {"sessionEvents", static_cast<qint64>(fixture.events.size())}, {"userPreferencesTouched", false}});
}

int hostFixture()
{
    Fixture fixture(true); QWidget control; NativeHarness harness;
    harness.window = reinterpret_cast<HWND>(control.winId());
    fixture.onProjection = [&](const LauncherSessionsSnapshot& value) { report({{"event", "projection"}, {"payload", launcherSessionsProjection(value)}}); };
    harness.invoke = [&](int command) {
        QTimer::singleShot(0, &fixture.coordinator, [&, command] {
            if (command == 20) qputenv("DSE_A6_TEST_HOLD_JOIN", "1");
            else if (command == 21) {
                auto& peer = fixture.nextPeer(fixture.first); peer.command(2);
                report({{"event", "hostPeer"}, {"pid", QString::number(peer.pid)}, {"incarnation", peer.incarnation.toString(QUuid::WithoutBraces)},
                    {"displayId", canonical(peer.display)}, {"roleHwnd", QString::number(reinterpret_cast<quintptr>(peer.role))}});
            }
            else if (command == 22) { require(!fixture.peers.empty(), "A6 Host peer exists"); fixture.close(*fixture.peers.back()); }
            else if (command == 23) { require(!fixture.peers.empty(), "A6 Host peer exists"); fixture.peers.back()->command(3); }
            else if (command == 24) { require(!fixture.peers.empty(), "A6 Host peer exists"); fixture.peers.back()->command(4); }
            else if (command == 25) {
                require(fixture.coordinator.launcherWindow(), "A6 current Host exists"); fixture.coordinator.launcherWindow()->close();
            }
            else if (command == 26) {
                require(fixture.delivered().registeredSessions.empty(), "A6 Host shutdown requires zero roles"); fixture.recreate = false;
                if (fixture.coordinator.launcherWindow()) fixture.coordinator.launcherWindow()->close();
                QTimer::singleShot(0, &fixture.coordinator, [] { QApplication::quit(); });
            }
            else if (command == 27) fixture.catalogMode = 1;
            else if (command == 28) fixture.catalogMode = 0;
            else if (command == 29) fixture.cleanupOwned();
            else throw std::runtime_error("A6 unknown Host test command");
            report({{"event", "harnessCommand"}, {"command", command}, {"launchCount", fixture.launchCount}});
        });
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    report({{"event", "harnessReady"}, {"hwnd", QString::number(reinterpret_cast<quintptr>(harness.window))},
        {"firstDisplayId", canonical(fixture.first)}, {"secondDisplayId", canonical(fixture.second)},
        {"preferenceFile", fixture.settings.fileName()}, {"scope", "isolated same-image production CreateProcess/admission/Join/Host; synthetic catalog"}});
    const int result = QApplication::exec(); QApplication::instance()->removeNativeEventFilter(&harness);
    for (const auto& peer : fixture.peers) require(!peer->alive(), "A6 Host final owned peer exited");
    report({{"event", "cleanShutdown"}, {"sessionCount", static_cast<qint64>(fixture.coordinator.sessionCount())},
        {"launchCount", fixture.launchCount}, {"hostCreations", fixture.hostCreations}}); return result;
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
        if (arguments.value(1) == QStringLiteral("--display-session"))
        {
            const auto mode = parseApplicationProcessMode(arguments.mid(1));
            const auto* session = std::get_if<DisplaySessionProcessMode>(&mode);
            require(session && session->displayId.isPersistentOwnerCapable(), "A6 same-executable production Session CLI parser");
            return sessionPeer(session->displayId);
        }
        require(arguments.size() == 1, "A6 harness expects native checks, --host, or production --display-session");
        nativeChecks(); return 0;
    }
    catch (const std::exception& error) { report({{"event", "failure"}, {"message", QString::fromUtf8(error.what())}}); return 1; }
}
