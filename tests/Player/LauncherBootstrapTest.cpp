#include "LauncherBootstrap.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <algorithm>
#include <stdexcept>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    int checks = 0;
    const auto check = [&](bool success, const char* name) {
        ++checks;
        if (!success) { std::cerr << "FAIL " << name << '\n'; std::exit(1); }
        std::cout << "PASS " << name << '\n';
    };
    const auto epoch = QUuid::createUuid();
    Platform::DisplayDescriptor target;
    target.id = DisplayId::fromTargetDevicePath("\\\\?\\DISPLAY#bootstrap-real-value");
    target.friendlyName = "Production monitor";
    target.sourceDeviceName = "\\\\.\\DISPLAY1";
    target.pixelWidth = 2560; target.pixelHeight = 1440; target.desktopX = -2560;
    target.primary = true; target.displayNumber = 1;
    target.refreshRate = Platform::DisplayRefreshRate{60000, 1001};
    auto source = target;
    source.id = DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY2");
    const Platform::DisplayEnumerationResult success{Platform::DisplayEnumerationStatus::Success, {{target, source}}};
    const auto project = [&](const auto& displays, const auto& roles, auto language) {
        return launcherBootstrapSnapshot(epoch, displays, roles, language, UiLanguage::English);
    };
    const auto snapshot = project(success, std::vector<LauncherRegisteredSession>{}, std::optional{UiLanguage::English});
    const auto rows = snapshot["displayEnumeration"].toObject()["displays"].toArray();
    check(snapshot["coordinatorEpoch"].toString() == epoch.toString(QUuid::WithoutBraces), "stable Coordinator epoch");
    check(rows.size() == 2 && rows[0].toObject()["friendlyName"] == "Production monitor"
        && rows[0].toObject()["desktopX"] == -2560
        && rows[0].toObject()["refreshRate"].toObject()["denominator"] == 1001, "successful descriptors preserve metadata");
    check(rows[0].toObject()["id"] == QString::fromStdString(*target.id.canonicalRepresentation())
        && rows[0].toObject()["persistentOwnerCapable"].toBool()
        && rows[0].toObject()["identityKind"] == "TargetDevicePath", "target canonical ID and owner capability");
    check(!rows[1].toObject()["persistentOwnerCapable"].toBool()
        && rows[1].toObject()["identityKind"] == "SourceDeviceName", "weak source identity remains weak");
    check(snapshot["registeredSessions"].toArray().isEmpty()
        && snapshot["registeredSessionsScope"] == "verified-registered-only", "empty registered set has limited scope");
    check(snapshot["language"].toObject()["committed"] == "en", "English committed bootstrap");
    const auto incarnation = QUuid::createUuid();
    const auto populated = launcherBootstrapSnapshot(epoch, success, {{target.id, incarnation}},
        UiLanguage::SimplifiedChinese, UiLanguage::SimplifiedChinese);
    const auto role = populated["registeredSessions"].toArray()[0].toObject();
    check(role["boundDisplayId"] == rows[0].toObject()["id"]
        && role["incarnation"] == incarnation.toString(QUuid::WithoutBraces), "minimal registered role projection");
    check(role["controlConnected"].isBool() && !role["controlConnected"].toBool()
        && populated["sessionRevision"].isDouble() && populated["sessionRevision"].toDouble() == 0,
        "A5 bootstrap carries authoritative control fact and shared Session revision");
    check(populated["language"].toObject()["committed"] == "zh_CN"
        && populated["language"].toObject()["effective"] == "zh_CN", "Chinese committed and effective bootstrap");
    const LauncherLanguageProjection language{UiLanguage::SimplifiedChinese, UiLanguage::English, 12};
    check(launcherBootstrapSnapshot(epoch, success, {}, language)["language"].toObject()
        == serializeLauncherLanguage(language), "A4 bootstrap reuses exact shared committed/effective/revision DTO");
    const auto failedLanguage = project(success, std::vector<LauncherRegisteredSession>{}, std::optional<UiLanguage>{});
    check(failedLanguage["language"].toObject()["readStatus"] == "Failure"
        && !failedLanguage["language"].toObject().contains("committed")
        && failedLanguage["language"].toObject()["effective"] == "en", "language read failure is distinct from effective language");
    for (const auto status : {Platform::DisplayEnumerationStatus::Success, Platform::DisplayEnumerationStatus::ApiFailure,
             Platform::DisplayEnumerationStatus::IncompletePathMetadata, Platform::DisplayEnumerationStatus::DuplicateIdentity})
    {
        auto result = success;
        result.status = status;
        if (status == Platform::DisplayEnumerationStatus::Success) result.snapshot.displays.clear();
        const auto enumeration = project(result, std::vector<LauncherRegisteredSession>{}, std::optional{UiLanguage::English})["displayEnumeration"].toObject();
        const char* expected = status == Platform::DisplayEnumerationStatus::Success ? "Success" :
            status == Platform::DisplayEnumerationStatus::ApiFailure ? "ApiFailure" :
            status == Platform::DisplayEnumerationStatus::IncompletePathMetadata ? "IncompletePathMetadata" : "DuplicateIdentity";
        check(enumeration["status"] == expected && enumeration["displays"].toArray().isEmpty(), expected);
    }
    const auto wire = QJsonDocument(snapshot).toJson();
    check(!wire.contains("slotFree") && !wire.contains("occupied") && !wire.contains("identityStatus")
        && !wire.contains("sessionStatus") && !wire.contains("PID") && !wire.contains("HWND"), "no occupancy claim, UI enums, or private native data");
    const int baselineChecks = checks;
    check(launcherDisplayCatalog(success) == snapshot["displayEnumeration"].toObject(),
        "A2 bootstrap and refresh share the identical catalog projection");
    auto single = success;
    single.snapshot.displays.resize(1);
    check(launcherDisplayCatalog(single)["displays"].toArray().size() == 1, "A2 single display refresh");
    check(launcherDisplayCatalog(success)["displays"].toArray().size() == 2, "A2 multiple display refresh");
    for (const auto status : {Platform::DisplayEnumerationStatus::Success, Platform::DisplayEnumerationStatus::ApiFailure,
             Platform::DisplayEnumerationStatus::IncompletePathMetadata, Platform::DisplayEnumerationStatus::DuplicateIdentity})
    {
        const Platform::DisplayEnumerationResult input{status, {}};
        const auto catalog = launcherDisplayCatalog(input);
        const auto bootstrap = project(input, std::vector<LauncherRegisteredSession>{}, std::optional{UiLanguage::English});
        check(catalog == bootstrap["displayEnumeration"].toObject() && catalog["displays"].toArray().isEmpty(),
            "A2 empty/failure outcomes use the same mapping in both commands");
    }
    const auto refreshCatalog = launcherDisplayCatalog(success);
    const auto refreshedRows = refreshCatalog["displays"].toArray();
    const auto canonical = refreshedRows[0].toObject()["id"].toString().toStdString();
    check(DisplayId::parseCanonicalRepresentation(canonical) == target.id, "A2 exact canonical ID round-trip");
    check(refreshedRows[1].toObject()["id"] == QString::fromStdString(*source.id.canonicalRepresentation())
        && !refreshedRows[1].toObject()["persistentOwnerCapable"].toBool(), "A2 weak identity remains in the catalog");
    auto changed = success;
    changed.snapshot.displays[0].friendlyName = "Renamed panel";
    changed.snapshot.displays[0].primary = false;
    changed.snapshot.displays[0].displayNumber = 8;
    changed.snapshot.displays[0].desktopY = -300;
    changed.snapshot.displays[0].pixelWidth = 3840;
    changed.snapshot.displays[0].refreshRate = Platform::DisplayRefreshRate{144, 1};
    const auto metadata = launcherDisplayCatalog(changed)["displays"].toArray()[0].toObject();
    check(metadata["id"] == refreshedRows[0].toObject()["id"] && metadata["friendlyName"] == "Renamed panel"
        && metadata["pixelWidth"] == 3840 && metadata["displayNumber"] == 8 && metadata["desktopY"] == -300
        && metadata["refreshRate"].toObject()["numerator"] == 144 && !metadata["primary"].toBool(),
        "A2 metadata changes preserve the exact native identity");
    std::reverse(changed.snapshot.displays.begin(), changed.snapshot.displays.end());
    check(launcherDisplayCatalog(changed)["displays"].toArray()[0].toObject()["id"] == refreshedRows[1].toObject()["id"],
        "A2 native ordering is preserved without adapter sorting");
    auto impossible = success;
    impossible.snapshot.displays[1].id = DisplayId{};
    bool rejected = false;
    try { (void)project(impossible, std::vector<LauncherRegisteredSession>{}, std::optional{UiLanguage::English}); }
    catch (const std::exception&) { rejected = true; }
    check(rejected, "A2 impossible descriptor rejects the whole projection instead of silently dropping a row");
    for (int scenario = 0; scenario < 7; ++scenario)
    {
        auto invalid = success;
        auto& row = invalid.snapshot.displays[1];
        switch (scenario)
        {
        case 0: row.id = target.id; break;
        case 1: row.pixelWidth = -1; break;
        case 2: row.displayNumber = -1; break;
        case 3: row.refreshRate = Platform::DisplayRefreshRate{60, 0}; break;
        case 4: row.friendlyName = std::string(1, static_cast<char>(0xff)); break;
        case 5: row.sourceDeviceName = std::string("NUL\0source", 10); break;
        case 6: row.refreshRate = Platform::DisplayRefreshRate{0, 1}; break;
        }
        bool completeFailure = false;
        try { (void)launcherDisplayCatalog(invalid); }
        catch (const std::invalid_argument&) { completeFailure = true; }
        check(completeFailure, "A2 impossible descriptor is never serialized as partial success");
    }
    std::cout << "A2 catalog projection checks: " << checks - baselineChecks << '/' << checks - baselineChecks << " PASS\n";
    std::cout << checks << "/" << checks << " snapshot checks PASS\n";
}
