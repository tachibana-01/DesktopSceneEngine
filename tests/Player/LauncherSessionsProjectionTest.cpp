#include "LauncherSessionsProjection.h"
#include "LauncherBootstrap.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    int checks = 0;
    const auto check = [&](bool success, const char* description) {
        ++checks;
        if (!success) { std::cerr << "FAIL " << description << '\n'; std::exit(1); }
        std::cout << "PASS " << description << '\n';
    };
    const auto rejects = [&](const LauncherSessionsSnapshot& snapshot, const char* description) {
        bool rejected = false;
        try { (void)launcherSessionsProjection(snapshot); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, description);
    };
    const auto epoch = QUuid::createUuid();
    const auto firstDisplay = DisplayId::fromTargetDevicePath("\\\\?\\DISPLAY#projection:Panel-A");
    const auto secondDisplay = DisplayId::fromTargetDevicePath("\\\\?\\DISPLAY#projection:Panel-B");
    const auto firstIncarnation = QUuid::createUuid();
    const auto secondIncarnation = QUuid::createUuid();
    const LauncherRegisteredSession first{firstDisplay, firstIncarnation, true, std::nullopt};
    const LauncherRegisteredSession second{secondDisplay, secondIncarnation, false, std::nullopt};
    LauncherSessionsSnapshot snapshot{epoch, 8, {}};
    const auto empty = launcherSessionsProjection(snapshot);
    check(empty.size() == 5 && empty["registeredSessions"].toArray().isEmpty()
        && empty["createOperations"].isArray() && empty["createOperations"].toArray().isEmpty(),
        "empty registry is an empty fact set without a generated Session");
    check(empty["coordinatorEpoch"] == epoch.toString(QUuid::WithoutBraces)
        && empty["sessionRevision"].toDouble() == 8
        && empty["registeredSessionsScope"] == "verified-registered-only",
        "snapshot preserves epoch, transport ordering, and registered-only scope");

    snapshot.registeredSessions = {first};
    const auto one = launcherSessionsProjection(snapshot);
    const auto oneRow = one["registeredSessions"].toArray()[0].toObject();
    check(oneRow.size() == 3 && oneRow["boundDisplayId"] == QString::fromStdString(*firstDisplay.canonicalRepresentation())
        && oneRow["incarnation"] == firstIncarnation.toString(QUuid::WithoutBraces),
        "one registered Session preserves exact canonical DisplayId and incarnation");
    check(oneRow["controlConnected"].isBool() && oneRow["controlConnected"].toBool()
        && !oneRow.contains("closeOperation"), "control fact is explicit and absent close operation remains absent");

    snapshot.registeredSessions = {first, second};
    const auto multiple = launcherSessionsProjection(snapshot);
    const auto rows = multiple["registeredSessions"].toArray();
    check(rows.size() == 2 && rows[1].toObject()["boundDisplayId"] == QString::fromStdString(*secondDisplay.canonicalRepresentation())
        && rows[1].toObject()["incarnation"] == secondIncarnation.toString(QUuid::WithoutBraces),
        "multiple independent registered Sessions retain exact identities and native ordering");
    check(!rows[1].toObject()["controlConnected"].toBool()
        && rows[0].toObject()["controlConnected"].toBool(), "control loss does not remove or rename a Session");
    const auto wire = QJsonDocument(multiple).toJson(QJsonDocument::Compact);
    check(!wire.contains("AVAILABLE") && !wire.contains("STARTING") && !wire.contains("ACTIVE")
        && !wire.contains("DISCONNECTED") && !wire.contains("CLOSING") && !wire.contains("CRASHED"),
        "native fact projection contains no derived lifecycle or occupancy claim");
    check(!wire.contains("pid") && !wire.contains("PID") && !wire.contains("HWND")
        && !wire.contains("handle") && !wire.contains("pipe"), "private process verification data stays native");

    const auto priorOwner = QUuid::createUuid();
    const auto closeKey = RoleCloseKey{priorOwner, (std::numeric_limits<quint64>::max)()};
    const struct { RoleCloseResult result; const char* text; } closeCases[]{
        {RoleCloseResult::Received, "Received"}, {RoleCloseResult::Pending, "Pending"},
        {RoleCloseResult::CloseAccepted, "CloseAccepted"}, {RoleCloseResult::CloseAborted, "CloseAborted"},
        {RoleCloseResult::Rejected, "Rejected"}, {RoleCloseResult::Uncertain, "Uncertain"}};
    for (const auto& closeCase : closeCases)
    {
        snapshot.registeredSessions[0].closeOperation = RoleCloseOperation{closeKey, closeCase.result};
        const auto projected = launcherSessionsProjection(snapshot)["registeredSessions"].toArray()[0].toObject();
        const auto operation = projected["closeOperation"].toObject();
        check(projected["incarnation"] == firstIncarnation.toString(QUuid::WithoutBraces)
            && operation.size() == 3 && operation["result"] == closeCase.text,
            "authoritative live close state never removes the exact Session");
        check(operation["generation"] == priorOwner.toString(QUuid::WithoutBraces)
            && operation["request"].isString() && operation["request"] == "18446744073709551615",
            "close recovery preserves prior-owner key and uint64 request without JS precision loss");
    }
    auto changed = snapshot;
    check(changed == snapshot && changed.registeredSessions[0] == snapshot.registeredSessions[0],
        "value equality recognizes identical raw snapshots including keyed close facts");
    changed.registeredSessions[0].closeOperation->result = RoleCloseResult::Pending;
    check(changed != snapshot, "value equality detects close-result mutation for lifecycle invalidation");
    changed = snapshot;
    changed.registeredSessions[0].controlConnected = false;
    check(changed != snapshot, "value equality detects control-channel observation mutation");
    changed = snapshot;
    changed.registeredSessions[0].closeOperation.reset();
    check(changed != snapshot, "value equality distinguishes no close operation from an observed one");

    const LauncherLanguageProjection language{UiLanguage::SimplifiedChinese, UiLanguage::English, 11};
    const Platform::DisplayEnumerationResult displays{Platform::DisplayEnumerationStatus::Success, {}};
    const auto bootstrap = launcherBootstrapSnapshot(snapshot, displays, language);
    const auto shared = launcherSessionsProjection(snapshot);
    check(bootstrap["coordinatorEpoch"] == shared["coordinatorEpoch"]
        && bootstrap["sessionRevision"] == shared["sessionRevision"]
        && bootstrap["registeredSessions"] == shared["registeredSessions"]
        && bootstrap["registeredSessionsScope"] == shared["registeredSessionsScope"]
        && bootstrap["createOperations"] == shared["createOperations"],
        "bootstrap and live events use the identical authoritative Session serializer");
    check(bootstrap["language"].toObject() == serializeLauncherLanguage(language)
        && bootstrap["displayEnumeration"].toObject() == launcherDisplayCatalog(displays),
        "shared Session projection leaves A2 Display and A4 Language serialization intact");
    const auto forwarded = launcherBootstrapSnapshot(epoch, displays, {first}, language);
    check(forwarded["sessionRevision"].toDouble() == 0
        && forwarded["registeredSessions"] == launcherSessionsProjection({epoch, 0, {first}})["registeredSessions"],
        "legacy bootstrap signature forwards through shared mapping with initial revision zero");

    snapshot.registeredSessions.erase(snapshot.registeredSessions.begin());
    const auto afterRemoval = launcherSessionsProjection(snapshot)["registeredSessions"].toArray();
    check(afterRemoval.size() == 1 && afterRemoval[0].toObject()["incarnation"] == secondIncarnation.toString(QUuid::WithoutBraces),
        "removed exact incarnation leaves no historical or crashed ghost record");
    snapshot.registeredSessions.clear();
    check(launcherSessionsProjection(snapshot)["registeredSessions"].toArray().isEmpty(),
        "ordinary registry removal does not manufacture a close operation or state");

    auto invalid = snapshot;
    invalid.coordinatorEpoch = {};
    rejects(invalid, "null Coordinator epoch is a complete projection failure");
    invalid = snapshot;
    invalid.revision = 9007199254740992ULL;
    rejects(invalid, "unsafe JavaScript integer revision cannot silently lose ordering precision");
    invalid.revision = 9007199254740991ULL;
    check(launcherSessionsProjection(invalid)["sessionRevision"].toDouble() == 9007199254740991.0,
        "maximum safe ordering revision is preserved exactly");
    invalid = snapshot;
    invalid.registeredSessions = {first, first};
    rejects(invalid, "duplicate exact Session record rejects the entire snapshot");
    invalid.registeredSessions = {first, {firstDisplay, secondIncarnation, false, std::nullopt}};
    rejects(invalid, "two live incarnations on one exact Display are flagged instead of choosing one");
    invalid.registeredSessions = {first, {secondDisplay, firstIncarnation, false, std::nullopt}};
    rejects(invalid, "one incarnation on two Displays is an invariant failure");
    invalid.registeredSessions = {{DisplayId{}, firstIncarnation}};
    rejects(invalid, "invalid bound Display identity is not silently dropped");
    invalid.registeredSessions = {{DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY1"), firstIncarnation}};
    rejects(invalid, "weak source identity cannot represent a registered Display admission");
    invalid.registeredSessions = {{firstDisplay, QUuid{}}};
    rejects(invalid, "null Session incarnation is not silently dropped");
    invalid.registeredSessions = {{DisplayId::fromTargetDevicePath(std::string(32769, 'x')), firstIncarnation}};
    rejects(invalid, "oversized canonical identity rejects rather than exceeding the bridge bound");
    invalid.registeredSessions = {first};
    invalid.registeredSessions[0].closeOperation = RoleCloseOperation{{{}, 1}, RoleCloseResult::Pending};
    rejects(invalid, "null close-operation generation is rejected");
    invalid.registeredSessions[0].closeOperation = RoleCloseOperation{{epoch, 0}, RoleCloseResult::Pending};
    rejects(invalid, "zero close-operation request is rejected");
    invalid.registeredSessions[0].closeOperation = RoleCloseOperation{{epoch, 1}, static_cast<RoleCloseResult>(255)};
    rejects(invalid, "unknown native close result is rejected");
    invalid.registeredSessions[0].closeOperation = RoleCloseOperation{{epoch, 1}, RoleCloseResult::RoleGone};
    rejects(invalid, "RoleGone receipt cannot create a ghost in the live Session projection");
    invalid = snapshot;
    for (int index = 0; index < 64; ++index)
        invalid.registeredSessions.push_back({DisplayId::fromTargetDevicePath("projection-capacity-" + std::to_string(index)),
            QUuid::createUuid()});
    check(launcherSessionsProjection(invalid)["registeredSessions"].toArray().size() == 64,
        "the bounded Coordinator registry capacity is supported");
    invalid.registeredSessions.push_back({secondDisplay, secondIncarnation});
    rejects(invalid, "oversized registry is a complete projection failure");

    const auto operationId = QUuid::createUuid();
    snapshot.createOperations = {{firstDisplay, operationId, LauncherCreateState::Pending, std::nullopt}};
    const auto pending = launcherSessionsProjection(snapshot);
    const auto pendingRow = pending["createOperations"].toArray()[0].toObject();
    check(pendingRow["displayId"] == QString::fromStdString(*firstDisplay.canonicalRepresentation())
        && pendingRow["operationId"] == operationId.toString(QUuid::WithoutBraces)
        && pendingRow["state"] == "Pending" && pendingRow.size() == 3,
        "A6 pending operation preserves separate target and correlation identity without inventing incarnation");
    check(pending["registeredSessions"].toArray().isEmpty(),
        "A6 accepted operation does not fabricate a live registered Session");
    check(launcherBootstrapSnapshot(snapshot, displays, language)["createOperations"] == pending["createOperations"],
        "A6 bootstrap and live projection share the exact operation mapping");
    for (const auto state : {LauncherCreateState::Registered, LauncherCreateState::LaunchFailed,
        LauncherCreateState::ExitedBeforeRegistration, LauncherCreateState::Uncertain})
    {
        auto terminal = snapshot;
        terminal.createOperations[0].state = state;
        if (state == LauncherCreateState::Registered) terminal.createOperations[0].incarnation = firstIncarnation;
        const auto row = launcherSessionsProjection(terminal)["createOperations"].toArray()[0].toObject();
        check(row["state"].isString() && row.contains("incarnation") == (state == LauncherCreateState::Registered),
            "A6 terminal receipt has an incarnation only for a genuinely registered outcome");
    }
    auto invalidOperation = snapshot;
    invalidOperation.createOperations.push_back(snapshot.createOperations[0]);
    rejects(invalidOperation, "A6 duplicate same-Display operations rejected");
    invalidOperation.createOperations.back().displayId = secondDisplay;
    rejects(invalidOperation, "A6 one operation UUID cannot identify two Displays");
    invalidOperation = snapshot;
    invalidOperation.createOperations[0].incarnation = firstIncarnation;
    rejects(invalidOperation, "A6 Pending cannot claim a Session incarnation");
    invalidOperation.createOperations[0].state = LauncherCreateState::Registered;
    invalidOperation.createOperations[0].incarnation.reset();
    rejects(invalidOperation, "A6 Registered receipt requires the real joined incarnation");
    invalidOperation = snapshot;
    invalidOperation.createOperations[0].operationId = {};
    rejects(invalidOperation, "A6 null operation correlation UUID rejected");
    invalidOperation = snapshot;
    invalidOperation.createOperations[0].state = static_cast<LauncherCreateState>(255);
    rejects(invalidOperation, "A6 unknown operation state rejected");
    invalidOperation = snapshot;
    invalidOperation.createOperations[0].displayId = DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY1");
    rejects(invalidOperation, "A6 accepted Create operation cannot promote weak Display identity");
    const auto receipt = launcherCreateResult({epoch, firstDisplay, LauncherCreateStatus::Accepted, operationId});
    check(receipt["operationId"] == operationId.toString(QUuid::WithoutBraces)
        && receipt["status"] == "Accepted" && !receipt.contains("incarnation"),
        "A6 Create accepted result is a receipt, never Session-ready truth");
    const auto activated = launcherOpenResult({epoch, firstIncarnation, LauncherOpenStatus::ActivationRequested});
    check(activated["incarnation"] == firstIncarnation.toString(QUuid::WithoutBraces)
        && activated["coordinatorEpoch"] == epoch.toString(QUuid::WithoutBraces)
        && activated["status"] == "ActivationRequested" && activated.size() == 3,
        "A6 Open result keeps exact role identity and makes no foreground guarantee");

    std::cout << checks << '/' << checks << " shared Session/Create projection checks PASS\n";
}
