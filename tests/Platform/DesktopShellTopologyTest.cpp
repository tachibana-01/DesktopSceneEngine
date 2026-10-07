#include "Platform/Windows/DesktopShellTopology.h"

#include <iostream>
#include <limits>

using namespace DesktopSceneEngine::Platform::Windows;

int main()
{
    bool ok = true;
    const auto check = [&](bool value, const char* label)
    { if (!value) { std::cerr << "Test failed: " << label << '\n'; ok = false; } };
    const DesktopShellFacts raised{1, 2, 3, 4, 1, 2, 1, true, true, true};
    check(classifyDesktopShell(raised) == DesktopShellKind::SupportedRaised, "supported raised topology");
    for (int missing = 0; missing < 4; ++missing)
    {
        auto facts = raised;
        if (missing == 0) facts.progman = 0;
        if (missing == 1) facts.defView = 0;
        if (missing == 2) facts.icons = 0;
        if (missing == 3) facts.worker = 0;
        check(classifyDesktopShell(facts) == DesktopShellKind::Incomplete, "missing required window");
    }
    for (int wrong = 0; wrong < 6; ++wrong)
    {
        auto facts = raised;
        if (wrong == 0) facts.defViewParent = 4;
        if (wrong == 1) facts.iconsParent = 1;
        if (wrong == 2) facts.workerParent = 0;
        if (wrong == 3) facts.defViewLayered = false;
        if (wrong == 4) facts.classesAndProcessMatch = false;
        if (wrong == 5) facts.defViewAboveWorker = false;
        check(classifyDesktopShell(facts) == DesktopShellKind::Unsupported, "classic/wrong relationships/classes/order");
    }
    auto malformed = raised;
    malformed.worker = malformed.defView;
    check(classifyDesktopShell(malformed) == DesktopShellKind::Unsupported, "aliased shell handles");
    auto unprepared = raised;
    unprepared.worker = 0;
    check(canPrepareRaisedDesktop(unprepared), "only incomplete raised shape may need bounded preparation");
    unprepared.defViewLayered = false;
    check(!canPrepareRaisedDesktop(unprepared), "classic topology cannot trigger strategy fallback");

    using DesktopSceneEngine::Platform::DisplayDescriptor;
    for (const auto region : {DesktopPlacement{0, 0, 1920, 1080},
            DesktopPlacement{1920, 300, 2560, 1600}, DesktopPlacement{-2560, 0, 2560, 1600},
            DesktopPlacement{0, -1600, 2560, 1600}})
    {
        DisplayDescriptor display;
        display.desktopX = region.x; display.desktopY = region.y;
        display.pixelWidth = region.width; display.pixelHeight = region.height;
        check(deriveDesktopPlacement(display, 0, 0) == region, "physical screen coordinates preserved");
        check(deriveDesktopPlacement(display, -310, 120)
            == DesktopPlacement{region.x + 310, region.y - 120, region.width, region.height},
            "nonzero parent-client origin and exact dimensions");
    }
    DisplayDescriptor invalid;
    check(!deriveDesktopPlacement(invalid, 0, 0), "zero geometry rejected");
    invalid.pixelWidth = 100; invalid.pixelHeight = 100;
    invalid.desktopX = (std::numeric_limits<int>::max)();
    check(!deriveDesktopPlacement(invalid, 0, 0), "screen edge overflow rejected");
    invalid.desktopX = (std::numeric_limits<int>::min)();
    check(!deriveDesktopPlacement(invalid, 100, 0), "parent conversion overflow rejected");
    return ok ? 0 : 1;
}
