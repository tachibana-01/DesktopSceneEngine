// Isolated same-image process proof for A5. Native protocol/admission/control are
// the production implementations. The small QWidget is an ordinary-close role,
// not a fake Join peer or a replacement for the production Human Gate.
#include "ApplicationCoordinator.h"
#include "ApplicationPreferences.h"
#include "LauncherBootstrap.h"
#include "LauncherIdentify.h"
#include "LauncherLanguage.h"
#include "LauncherRolePresentation.h"
#include "SessionPresentation.h"
#include "WebViewLauncherHost.h"

#include <QAbstractButton>
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPixmap>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <Windows.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_set>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

namespace
{
constexpr UINT HarnessMessage = WM_APP + 185; // This executable's owned HWNDs only.
constexpr int ConditionBudgetMs = 10000;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

bool waitUntil(const std::function<bool()>& predicate, int budget = ConditionBudgetMs)
{
    QElapsedTimer elapsed;
    elapsed.start();
    do
    {
        QApplication::processEvents();
        if (predicate()) return true;
        QThread::msleep(5);
    } while (elapsed.elapsed() < budget);
    return predicate();
}

void report(const QJsonObject& object)
{
    std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

QString canonical(const DisplayId& id)
{
    const auto value = id.canonicalRepresentation();
    return value ? QString::fromUtf8(value->data(), static_cast<qsizetype>(value->size())) : QString{};
}

InstanceSlot launcherSlot()
{
    return *InstanceSlot::fromProcessMode(LauncherProcessMode{}, *currentInteractiveInstanceScope());
}

InstanceSlot sessionSlot(const DisplayId& id)
{
    return *InstanceSlot::fromProcessMode(DisplaySessionProcessMode{id}, *currentInteractiveInstanceScope());
}

class NativeHarness final : public QAbstractNativeEventFilter
{
public:
    HWND window{};
    std::function<void(int)> invoke;
    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override
    {
        const auto* msg = static_cast<MSG*>(message);
        if (msg->hwnd != window || msg->message != HarnessMessage) return false;
        invoke(static_cast<int>(msg->wParam));
        if (result) *result = 0;
        return true;
    }
};

class OrdinaryRole final : public QWidget
{
public:
    std::function<void(QCloseEvent*)> ordinaryClose;
    void closeEvent(QCloseEvent* event) override { ordinaryClose(event); }
};

int peer(const QString& identity, const QString& mode)
{
    const auto id = DisplayId::parseCanonicalRepresentation(identity.toStdString());
    require(id && id->isPersistentOwnerCapable(), "A5 peer requires an exact canonical test DisplayId");
    const auto slot = sessionSlot(*id);
    InstanceAdmissionLease admission;
    require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired, "A5 test-owned Session admission");
    InstanceActivationEndpoint activation(slot);
    require(activation.listen(), "A5 actual Session activation endpoint");
    OrdinaryRole role;
    role.setWindowTitle(QStringLiteral("A5 isolated Session role"));
    SessionPresentation presentation(role);
    SessionCoordinatorJoin join(slot, launcherSlot());
    join.enableControl(presentation);
    int closes = 0;
    bool dirty = mode == QStringLiteral("decision");
    bool accepted = false;
    std::unique_ptr<QDialog> blocker;
    role.ordinaryClose = [&](QCloseEvent* event) {
        ++closes;
        report({{"event", "ordinaryClose"}, {"count", closes}});
        if (dirty)
        {
            QMessageBox decision(QMessageBox::Question, QStringLiteral("A5 isolated document"),
                QStringLiteral("Ordinary Close decision"), QMessageBox::Discard | QMessageBox::Cancel, &role);
            report({{"event", "prompt"}, {"count", closes}});
            if (decision.exec() != QMessageBox::Discard)
            {
                event->ignore();
                presentation.completeClose(false);
                return;
            }
            dirty = false;
        }
        presentation.beginTeardown();
        accepted = true;
        event->accept();
        // The current platform owns terminal completion explicitly. Publish it
        // while the Coordinator control channel can still report the decision.
        presentation.completeClose(true);
        join.beginRoleTeardown();
        activation.beginShutdown();
        report({{"event", "ordinaryAccepted"}, {"count", closes}});
        // The hold mode supplies a deterministic real process-alive barrier:
        // CloseAccepted can be projected while the pinned process is alive.
        if (mode != QStringLiteral("hold"))
            QTimer::singleShot(0, &role, [&] { join.drainAndStop([] { QApplication::quit(); }); });
    };
    role.show();
    activation.bindReadyRole(role, [&] { (void)presentation.restore(); });
    // An accepted QWidget Close can retire the primary HWND before process exit.
    // Test controls therefore use their own hidden HWND, never role identity.
    QWidget controlWindow;
    NativeHarness harness;
    harness.window = reinterpret_cast<HWND>(controlWindow.winId());
    harness.invoke = [&](int command) {
        if (command == 1) role.close();
        else if (command == 2) { join.stop(); report({{"event", "controlStopped"}}); }
        else if (command == 3 || command == 4)
        {
            auto* decision = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            require(decision, "A5 decision targets the actual ordinary-close modal");
            const auto button = command == 3 ? QMessageBox::Cancel : QMessageBox::Discard;
            require(decision->button(button), "A5 actual modal decision button");
            decision->button(button)->click();
        }
        else if (command == 5)
        {
            require(accepted, "A5 release process-alive barrier only after actual ordinary Close accepted");
            join.drainAndStop([] { QApplication::quit(); });
        }
        else if (command == 6)
        {
            blocker = std::make_unique<QDialog>(&role);
            blocker->setWindowModality(Qt::ApplicationModal);
            blocker->show();
            report({{"event", "blockerReady"}});
        }
        else if (command == 7)
        {
            require(blocker != nullptr, "A5 test-owned blocker exists");
            blocker->reject(); blocker.reset();
            report({{"event", "blockerGone"}});
        }
        else throw std::runtime_error("A5 unknown test peer command");
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    join.start(role, false);
    require(waitUntil([&] { return join.qualified(); }), "A5 production native Join/control qualification");
    report({{"event", "peerReady"}, {"boundDisplayId", canonical(*id)},
        {"incarnation", join.incarnation().toString(QUuid::WithoutBraces)},
        {"hwnd", QString::number(reinterpret_cast<quintptr>(harness.window))},
        {"roleHwnd", QString::number(role.winId())}});
    const int result = QApplication::exec();
    QApplication::instance()->removeNativeEventFilter(&harness);
    report({{"event", "peerShutdown"}, {"ordinaryCloseCount", closes}, {"accepted", accepted}});
    return result;
}

struct Child
{
    QProcess process;
    QByteArray output;
    HWND window{};
    HANDLE lifetime{};
    DisplayId display;
    QUuid incarnation;
    explicit Child(DisplayId id, const QString& mode = QStringLiteral("normal")) : display(std::move(id))
    {
        auto environment = QProcessEnvironment::systemEnvironment();
        // Isolated peer never starts WebView; don't inherit a remote-debugging port.
        environment.remove(QStringLiteral("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS"));
        environment.remove(QStringLiteral("WEBVIEW2_USER_DATA_FOLDER"));
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--peer"), canonical(display), mode});
        require(process.waitForStarted(5000), "A5 same-image independent child process starts");
        if (!waitUntil([&] { read(); return output.contains("\"event\":\"peerReady\"") || process.state() == QProcess::NotRunning; }))
        { std::cerr << output.constData(); throw std::runtime_error("A5 child actual Join readiness timeout"); }
        QJsonObject ready;
        for (const auto& line : output.split('\n'))
        {
            const auto value = QJsonDocument::fromJson(line).object();
            if (value.value(QStringLiteral("event")) == QStringLiteral("peerReady")) ready = value;
        }
        require(process.state() == QProcess::Running && !ready.isEmpty(), "A5 child exits before actual readiness");
        window = reinterpret_cast<HWND>(ready.value(QStringLiteral("hwnd")).toString().toULongLong());
        const auto roleWindow = reinterpret_cast<HWND>(ready.value(QStringLiteral("roleHwnd")).toString().toULongLong());
        incarnation = QUuid(ready.value(QStringLiteral("incarnation")).toString());
        DWORD owner = 0;
        DWORD roleOwner = 0;
        lifetime = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(process.processId()));
        require(lifetime && IsWindow(window) && GetWindowThreadProcessId(window, &owner)
            && owner == process.processId() && IsWindow(roleWindow) && GetWindowThreadProcessId(roleWindow, &roleOwner)
            && roleOwner == process.processId() && !incarnation.isNull()
            && ready.value(QStringLiteral("boundDisplayId")).toString() == canonical(display),
            "A5 pinned independent process/HWND/exact identity proof");
    }
    void read() { output += process.readAllStandardOutput(); output += process.readAllStandardError(); }
    bool has(const char* marker) { read(); return output.contains(marker); }
    void await(const char* marker)
    {
        if (!waitUntil([&] { return has(marker); }))
        { std::cerr << output.constData(); throw std::runtime_error(marker); }
    }
    bool alive() const { return lifetime && WaitForSingleObject(lifetime, 0) == WAIT_TIMEOUT; }
    void command(int value)
    {
        DWORD owner = 0;
        require(alive() && GetWindowThreadProcessId(window, &owner) && owner == process.processId()
            && PostMessageW(window, HarnessMessage, value, 0), "A5 command only the owned verified process HWND");
    }
    void exited()
    {
        require(waitUntil([&] { return process.state() == QProcess::NotRunning; }), "A5 ordinary child process exit");
        read();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode()) std::cerr << output.constData();
        require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && !alive(), "A5 clean ordinary child shutdown");
    }
    void crash()
    {
        require(alive(), "A5 kill injection has a living owned child");
        process.kill();
        require(process.waitForFinished(5000) && !alive(), "A5 pinned process lifetime proves test-injected death");
        read();
    }
    ~Child()
    {
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(5000); }
        if (lifetime) CloseHandle(lifetime);
    }
};

class BasicLauncher final : public QWidget, public LauncherRolePresentation
{
public:
    SessionPresentation presentation{*this};
    std::function<void()> accepted;
    SessionPresentation& trayPresentation() override { return presentation; }
    void closeEvent(QCloseEvent* event) override
    {
        presentation.beginTeardown(); event->accept(); if (accepted) accepted();
    }
};

Platform::DisplayEnumerationResult catalog(const DisplayId& first, const DisplayId& second, int mode)
{
    if (mode == 2) return {Platform::DisplayEnumerationStatus::ApiFailure};
    Platform::DisplayEnumerationResult result{Platform::DisplayEnumerationStatus::Success};
    int number = 0;
    for (const auto& id : {first, second})
    {
        if (mode == 1 && id == first) continue;
        Platform::DisplayDescriptor descriptor;
        descriptor.id = id; descriptor.friendlyName = id == first ? "A5 exact target A" : "A5 exact target B";
        descriptor.displayNumber = ++number; descriptor.pixelWidth = 1920; descriptor.pixelHeight = 1080;
        descriptor.primary = id == first;
        result.snapshot.displays.push_back(std::move(descriptor));
    }
    return result;
}

struct Fixture
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath(QStringLiteral("a5-preferences.ini")), QSettings::IniFormat};
    ApplicationPreferencesStore preferences{settings};
    ApplicationTranslationState translation;
    LauncherLanguage language{preferences, translation};
    InstanceSlot slot = launcherSlot();
    InstanceAdmissionLease admission;
    InstanceActivationEndpoint activation{slot};
    int drains = 0;
    ApplicationCoordinator coordinator{slot, activation, false, [&] { ++drains; }};
    std::vector<LauncherSessionsSnapshot> events;
    std::function<void(const LauncherSessionsSnapshot&)> onProjection;
    int hostCreations = 0;
    bool webHost = false;
    bool recreate = true;
    int catalogMode = 0;
    DisplayId first = DisplayId::fromTargetDevicePath("A5-\xE7\xB2\xBE\xE7\xA1\xAE-target-A-" + QUuid::createUuid().toString().toStdString());
    DisplayId second = DisplayId::fromTargetDevicePath("A5-exact-target-B-" + QUuid::createUuid().toString().toStdString());

    explicit Fixture(bool web = false) : webHost(web)
    {
        require(directory.isValid(), "A5 isolated preferences directory");
        require(preferences.saveLanguage(UiLanguage::English) == QSettings::NoError, "A5 isolated test store initialized");
        require(admission.acquire(slot) == InstanceAdmissionOutcome::Acquired,
            "A5 refuses interference: another real Coordinator already owns the Launcher admission");
        require(activation.listen() && coordinator.listen(), "A5 actual Coordinator activation/Join endpoints");
        language.setOnChanged([&](const LauncherLanguageProjection& value) {
            if (auto* host = dynamic_cast<WebViewLauncherHost*>(coordinator.launcherWindow())) host->languageChanged(value);
        });
        coordinator.setSessionProjectionChanged([&](const LauncherSessionsSnapshot& value) {
            validate(value);
            require(events.empty() || (events.back().coordinatorEpoch == value.coordinatorEpoch
                && events.back().revision < value.revision), "A5 full Session updates strictly advance within exact epoch");
            events.push_back(value);
            if (auto* host = dynamic_cast<WebViewLauncherHost*>(coordinator.launcherWindow())) host->sessionsChanged(value);
            if (onProjection) onProjection(value);
        });
        coordinator.setLauncherFactory([&]() -> QWidget* {
            if (!webHost)
            {
                auto* window = new BasicLauncher;
                window->accepted = [&, window] { coordinator.retireLauncher(*window); };
                return window;
            }
            auto* host = new WebViewLauncherHost([&] {
                return launcherBootstrapSnapshot(coordinator.launcherSessionSnapshot(),
                    catalog(first, second, catalogMode), language.reload());
            }, [&] { return launcherDisplayCatalog(catalog(first, second, catalogMode)); },
                [](const DisplayId& id) { return identifyLauncherDisplay(id); }, language);
            host->setAcceptedCloseCallback([&, host] {
                coordinator.retireLauncher(*host);
                if (recreate)
                    QTimer::singleShot(0, &coordinator, [&] { require(coordinator.showLauncher(), "A5 real Coordinator host recreation"); });
            });
            report({{"event", "hostCreated"}, {"creation", ++hostCreations},
                {"hwnd", QString::number(host->winId())}});
            return host;
        });
        require(coordinator.showLauncher(), "A5 initial real Coordinator-owned Launcher");
    }

    ~Fixture()
    {
        onProjection = {};
        coordinator.setSessionProjectionChanged({});
        language.setOnChanged({});
    }

    void validate(const LauncherSessionsSnapshot& snapshot)
    {
        require(snapshot.coordinatorEpoch == coordinator.coordinatorEpoch(), "A5 Coordinator epoch preserved");
        const auto wire = launcherSessionsProjection(snapshot);
        require(wire.value(QStringLiteral("registeredSessions")).isArray(), "A5 complete valid typed Session projection");
        require(wire.value(QStringLiteral("registeredSessions")).toArray().size() == static_cast<qsizetype>(snapshot.registeredSessions.size()),
            "A5 no silently discarded live Session facts");
        std::unordered_set<std::string> displays, incarnations;
        for (const auto& role : snapshot.registeredSessions)
        {
            require(!role.incarnation.isNull() && displays.insert(canonical(role.boundDisplayId).toStdString()).second
                && incarnations.insert(role.incarnation.toString().toStdString()).second, "A5 no duplicate Display/incarnation projection");
            require(!role.closeOperation || role.closeOperation->result != RoleCloseResult::RoleGone,
                "A5 RoleGone history never creates a ghost live Session");
        }
    }

    LauncherSessionsSnapshot snapshot()
    {
        auto value = coordinator.launcherSessionSnapshot(); validate(value); return value;
    }
    std::optional<LauncherRegisteredSession> role(const QUuid& incarnation)
    {
        // Await the delivered native push, never a pull getter that could itself
        // rebuild/queue the projection and mask a missing mutation notification.
        if (events.empty()) return std::nullopt;
        for (const auto& entry : events.back().registeredSessions) if (entry.incarnation == incarnation) return entry;
        return std::nullopt;
    }
    void observed(Child& child, bool control = true)
    {
        require(waitUntil([&] {
            const auto value = role(child.incarnation);
            return value && value->boundDisplayId == child.display && value->controlConnected == control;
        }), "A5 exact joined/live/control observation");
        require(child.alive(), "A5 registered projection corresponds to pinned living process");
        const auto pulled = snapshot();
        require(std::any_of(pulled.registeredSessions.begin(), pulled.registeredSessions.end(),
            [&](const auto& entry) { return entry.incarnation == child.incarnation && entry.boundDisplayId == child.display
                && entry.controlConnected == control; }), "A5 pull value agrees after push observation");
    }
    void removed(Child& child)
    {
        require(!child.alive(), "A5 remove proof begins with actual signalled process lifetime");
        require(waitUntil([&] { return !role(child.incarnation) && coordinator.sessionCount() == 0; }),
            "A5 verified native death removes exact Session without ghost state");
        require(waitUntil([&] { return !events.empty() && events.back().registeredSessions.empty(); }),
            "A5 removal is published live, without refresh/reload");
    }
    void closeResult(Child& child, RoleCloseResult result)
    {
        require(waitUntil([&] { const auto value = role(child.incarnation);
            return value && value->closeOperation && value->closeOperation->result == result; }),
            "A5 exact authoritative close result projected while role remains alive");
        require(child.alive(), "A5 close result is not process death");
        require(waitUntil([&] {
            for (auto it = events.rbegin(); it != events.rend(); ++it)
                for (const auto& value : it->registeredSessions)
                    if (value.incarnation == child.incarnation && value.closeOperation
                        && value.closeOperation->result == result) return true;
            return false;
        }), "A5 authoritative close result reaches live projection callback");
    }
};

void nativeChecks()
{
    Fixture fixture;
    int checks = 0;
    const auto check = [&](const char* name) { report({{"event", "check"}, {"name", name}, {"pass", true}}); ++checks; };
    require(waitUntil([&] { return !fixture.events.empty(); }) && fixture.snapshot().registeredSessions.empty(),
        "A5 empty authoritative registry appears as empty facts");
    check("empty registry/epoch and initial live publication");
    QUuid previous;
    for (int cycle = 0; cycle != 20; ++cycle)
    {
        const auto before = fixture.snapshot().revision;
        Child child(fixture.first);
        fixture.observed(child);
        require(child.incarnation != previous && fixture.snapshot().registeredSessions.size() == 1
            && !fixture.role(child.incarnation)->closeOperation, "A5 repeated exact slot has a fresh incarnation/no synthetic close");
        require(waitUntil([&] { return fixture.events.back().revision > before
            && fixture.events.back().registeredSessions.size() == 1
            && fixture.events.back().registeredSessions.front().incarnation == child.incarnation; }),
            "A5 Join published live without refresh");
        previous = child.incarnation;
        if (cycle % 2 == 0) { child.command(1); child.exited(); }
        else child.crash();
        fixture.removed(child);
        require(!fixture.coordinator.roleCloseOperation(child.incarnation), "A5 ordinary/crash death invents no close receipt");
        report({{"event", "repeat"}, {"cycle", cycle + 1}, {"pass", true},
            {"death", cycle % 2 == 0 ? "ordinary-close" : "killed-owned-child"},
            {"incarnation", child.incarnation.toString(QUuid::WithoutBraces)}});
    }
    check("20 same-slot actual Join/death cycles: no stale/duplicate/hang/missed death");
    {
        Child first(fixture.first), second(fixture.second);
        fixture.observed(first); fixture.observed(second);
        require(fixture.snapshot().registeredSessions.size() == 2, "A5 two exact distinct slots projected together");
        first.command(1); first.exited();
        require(waitUntil([&] { return !fixture.role(first.incarnation) && fixture.role(second.incarnation).has_value(); }),
            "A5 one exact death does not remove another independent Session");
        second.command(1); second.exited(); fixture.removed(second);
    }
    check("multiple independent Sessions retain distinct exact identities");
    {
        Child child(fixture.first, QStringLiteral("hold")); fixture.observed(child);
        child.command(1); child.await("\"event\":\"ordinaryAccepted\"");
        require(child.alive() && fixture.role(child.incarnation) && !fixture.role(child.incarnation)->closeOperation,
            "A5 ordinary X stays registered until death, without synthetic closing");
        child.command(5); child.exited(); fixture.removed(child);
    }
    check("ordinary Close accepted while alive creates no keyed close operation");
    {
        Child child(fixture.first); fixture.observed(child);
        QPointer<QWidget> first = fixture.coordinator.launcherWindow();
        const auto incarnation = fixture.coordinator.launcherIncarnation();
        require(first->close() && waitUntil([&] { return !first; }), "A5 actual Launcher QObject retirement");
        require(child.alive() && fixture.role(child.incarnation), "A5 Launcher retirement preserves independent Session");
        require(fixture.coordinator.showLauncher() && fixture.coordinator.launcherIncarnation() != incarnation
            && fixture.role(child.incarnation), "A5 recreated Launcher observes same native role incarnation");
        child.command(1); child.exited(); fixture.removed(child);
    }
    check("Launcher retirement/recreation retains Session authority independently");
    {
        Child child(fixture.first); fixture.observed(child);
        child.command(2); child.await("\"event\":\"controlStopped\""); fixture.observed(child, false);
        require(fixture.coordinator.requestRoleClose(child.incarnation), "A5 existing native close request admitted for unavailable role");
        fixture.closeResult(child, RoleCloseResult::Rejected);
        child.command(1); child.exited(); fixture.removed(child);
    }
    check("control loss retains exact living role; Rejected preserves it");
    {
        Child child(fixture.first, QStringLiteral("decision")); fixture.observed(child);
        require(fixture.coordinator.requestRoleClose(child.incarnation), "A5 native ordinary-close request");
        child.await("\"event\":\"prompt\""); fixture.closeResult(child, RoleCloseResult::Pending);
        child.command(3); fixture.closeResult(child, RoleCloseResult::CloseAborted);
        require(fixture.coordinator.requestRoleClose(child.incarnation), "A5 retry after real Cancel gets a fresh existing protocol key");
        fixture.closeResult(child, RoleCloseResult::Pending);
        child.command(2); child.await("\"event\":\"controlStopped\"");
        fixture.observed(child, false); fixture.closeResult(child, RoleCloseResult::Uncertain);
        // This living role still owns its ordinary modal. Process death is injected
        // only into this owned test child, never inferred from channel loss.
        child.crash(); fixture.removed(child);
        const auto history = fixture.coordinator.roleCloseOperation(child.incarnation);
        require(history && history->result == RoleCloseResult::RoleGone, "A5 verified death creates only existing close history receipt");
    }
    check("Pending/CloseAborted/Uncertain/RoleGone are real protocol observations without ghost role");
    {
        Child child(fixture.first, QStringLiteral("hold")); fixture.observed(child);
        require(fixture.coordinator.requestRoleClose(child.incarnation), "A5 existing native close request accepted");
        fixture.closeResult(child, RoleCloseResult::CloseAccepted);
        const auto identity = fixture.role(child.incarnation);
        require(identity && identity->boundDisplayId == child.display && child.alive(),
            "A5 CloseAccepted retains exact role until independently verified death");
        child.command(5); child.exited(); fixture.removed(child);
        require(fixture.coordinator.roleCloseOperation(child.incarnation)->result == RoleCloseResult::CloseAccepted,
            "A5 terminal history survives outside live projection");
    }
    check("CloseAccepted is not exited; terminal receipt excluded from live set after death");
    QPointer<QWidget> launcher = fixture.coordinator.launcherWindow();
    require(launcher->close() && waitUntil([&] { return !launcher && fixture.drains == 1; }),
        "A5 clean final native Coordinator drain after zero real roles");
    check("clean final shutdown after zero roles");
    report({{"event", "summary"}, {"pass", true}, {"checks", checks}, {"cycles", 20},
        {"sessionEvents", static_cast<qint64>(fixture.events.size())}, {"userPreferencesTouched", false},
        {"scope", "isolated same-image actual admission/activation/Join/control/process lifetimes; synthetic exact target IDs, no physical monitor claim"}});
}

int hostFixture()
{
    Fixture fixture(true);
    std::unique_ptr<Child> child;
    QWidget harnessWindow;
    NativeHarness harness;
    harness.window = reinterpret_cast<HWND>(harnessWindow.winId());
    fixture.onProjection = [&](const LauncherSessionsSnapshot& snapshot) {
        report({{"event", "projection"}, {"payload", launcherSessionsProjection(snapshot)}});
    };
    harness.invoke = [&](int command) {
        QTimer::singleShot(0, &fixture.coordinator, [&, command] {
            if (command == 10)
            {
                require(!child || child->process.state() == QProcess::NotRunning, "A5 no overlapping owned child on exact slot");
                child = std::make_unique<Child>(fixture.first);
                fixture.observed(*child);
                report({{"event", "hostPeerReady"}, {"incarnation", child->incarnation.toString(QUuid::WithoutBraces)},
                    {"boundDisplayId", canonical(child->display)}, {"pid", QString::number(child->process.processId())}});
            }
            else if (command == 11) { require(child != nullptr, "A5 owned child exists"); child->command(1); }
            else if (command == 12) { require(child != nullptr, "A5 owned child exists"); child->command(2); }
            else if (command == 13) { require(child != nullptr, "A5 owned child exists"); child->crash(); }
            else if (command == 14) { require(fixture.coordinator.launcherWindow(), "A5 current host exists"); fixture.coordinator.launcherWindow()->close(); }
            else if (command == 15)
            {
                require(!child || !child->alive(), "A5 normal final host shutdown requires no live owned Session");
                fixture.recreate = false;
                if (fixture.coordinator.launcherWindow()) fixture.coordinator.launcherWindow()->close();
                QTimer::singleShot(0, &fixture.coordinator, [] { QApplication::quit(); });
            }
            else if (command == 17) fixture.catalogMode = 1;
            else if (command == 18) fixture.catalogMode = 0;
            else if (command == 19) fixture.catalogMode = 2;
            else throw std::runtime_error("A5 unknown test-only host command");
            report({{"event", "harnessCommand"}, {"command", command}});
        });
    };
    QApplication::instance()->installNativeEventFilter(&harness);
    report({{"event", "harnessReady"}, {"hwnd", QString::number(reinterpret_cast<quintptr>(harness.window))},
        {"firstDisplayId", canonical(fixture.first)}, {"secondDisplayId", canonical(fixture.second)},
        {"preferenceFile", fixture.settings.fileName()},
        {"scope", "test-only real Coordinator/Host with isolated INI prefs and controlled synthetic Display catalog"}});
    const int result = QApplication::exec();
    QApplication::instance()->removeNativeEventFilter(&harness);
    require(!child || !child->alive(), "A5 host exits cleanly after its owned child exited");
    report({{"event", "cleanShutdown"}, {"sessionCount", static_cast<qint64>(fixture.coordinator.sessionCount())},
        {"hostCreations", fixture.hostCreations}});
    return result;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QPixmap icon(16, 16); icon.fill(Qt::green); QApplication::setWindowIcon(QIcon(icon));
    try
    {
        const auto arguments = application.arguments();
        if (arguments.value(1) == QStringLiteral("--peer")) return peer(arguments.value(2), arguments.value(3));
        if (arguments.value(1) == QStringLiteral("--host")) return hostFixture();
        require(arguments.size() == 1, "A5 lifecycle test expects no arguments or --host/--peer mode");
        nativeChecks(); return 0;
    }
    catch (const std::exception& error)
    {
        report({{"event", "failure"}, {"message", QString::fromUtf8(error.what())}});
        return 1;
    }
}
