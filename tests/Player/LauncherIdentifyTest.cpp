#include "LauncherIdentify.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    int checks = 0;
    const auto check = [&](bool valid, const char* label) {
        ++checks;
        if (!valid) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
        std::cout << "PASS " << label << '\n';
    };
    const auto target = DisplayId::fromTargetDevicePath("\\\\?\\DISPLAY#Identify#\xe5\xb1\x8f");
    const auto weak = DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY2");
    check(LauncherIdentifyDurationMs == 2500U, "production duration remains 2500 ms");
    for (const auto status : {Platform::DisplayIdentifyStatus::Shown, Platform::DisplayIdentifyStatus::Unresolved,
        Platform::DisplayIdentifyStatus::InvalidRequest, Platform::DisplayIdentifyStatus::ApiFailure})
    {
        int calls = 0;
        const auto response = identifyLauncherDisplay(target, [&](const DisplayId& received, std::uint32_t duration) {
            ++calls;
            check(received == target, "exact ID passed unchanged to the capability");
            check(duration == 2500U, "duration belongs to the native adapter");
            return status;
        });
        const char* expected = status == Platform::DisplayIdentifyStatus::Shown ? "Shown" :
            status == Platform::DisplayIdentifyStatus::Unresolved ? "Unresolved" :
            status == Platform::DisplayIdentifyStatus::InvalidRequest ? "InvalidRequest" : "ApiFailure";
        check(calls == 1 && response.size() == 2 && response["status"] == expected
            && response["displayId"] == QString::fromStdString(*target.canonicalRepresentation()),
            "four-way capability outcome and canonical target preserved");
    }
    const auto weakBefore = weak;
    const auto shown = identifyLauncherDisplay(weak, [&](const DisplayId& received, std::uint32_t duration) {
        check(received == weak && duration == 2500U, "weak identity can request Identify");
        return Platform::DisplayIdentifyStatus::Shown;
    });
    check(shown["status"] == "Shown" && weak == weakBefore && !weak.isPersistentOwnerCapable()
        && weak.kind() == DisplayIdentityKind::SourceDeviceName, "Shown never promotes weak identity");
    int invalidCalls = 0;
    bool invalidRejected = false;
    try { (void)identifyLauncherDisplay(DisplayId{}, [&](const DisplayId&, std::uint32_t) {
        ++invalidCalls; return Platform::DisplayIdentifyStatus::Shown;
    }); } catch (const std::invalid_argument&) { invalidRejected = true; }
    check(invalidRejected && invalidCalls == 0, "invalid adapter precondition cannot invoke the capability");
    bool invalidStatusRejected = false;
    try { (void)identifyLauncherDisplay(target, [](const DisplayId&, std::uint32_t) {
        return static_cast<Platform::DisplayIdentifyStatus>(-1);
    }); } catch (const std::invalid_argument&) { invalidStatusRejected = true; }
    check(invalidStatusRejected, "unknown capability result cannot masquerade as Shown");
    std::cout << checks << '/' << checks << " focused Identify checks PASS\n";
}
