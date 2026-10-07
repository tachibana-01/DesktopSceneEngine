#include "LauncherWindow.h"

#include <DesktopSceneEngine/DisplayIdentity.h>
#include <DesktopSceneEngine/Platform/Display.h>

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <optional>
#include <utility>
#include <vector>

class LauncherWindowTestAccess
{
public:

    static void apply(
        DesktopSceneEngine::Player::LauncherWindow& window,
        const DesktopSceneEngine::Platform::DisplayEnumerationResult& result
    )
    {
        window.applyDisplayEnumeration(result);
    }

    static void selectRow(DesktopSceneEngine::Player::LauncherWindow& window, int row)
    {
        window.displayList_->setCurrentRow(row);
    }

    static int rowCount(const DesktopSceneEngine::Player::LauncherWindow& window)
    {
        return window.displayList_->count();
    }

    static std::optional<DesktopSceneEngine::DisplayId> selectedDisplayId(
        const DesktopSceneEngine::Player::LauncherWindow& window
    )
    {
        return window.selectedDisplayId();
    }

    static bool identifyEnabled(const DesktopSceneEngine::Player::LauncherWindow& window)
    {
        return window.identifyButton_->isEnabled();
    }

    static bool startEnabled(const DesktopSceneEngine::Player::LauncherWindow& window)
    {
        return window.startSessionButton_->isEnabled();
    }

    static bool refreshEnabled(const DesktopSceneEngine::Player::LauncherWindow& window)
    {
        return window.refreshButton_->isEnabled();
    }

    static QString statusText(const DesktopSceneEngine::Player::LauncherWindow& window)
    {
        return window.statusLabel_->text();
    }
};

namespace
{

using DesktopSceneEngine::DisplayId;
using DesktopSceneEngine::Platform::DisplayDescriptor;
using DesktopSceneEngine::Platform::DisplayEnumerationResult;
using DesktopSceneEngine::Platform::DisplayEnumerationStatus;
using DesktopSceneEngine::Platform::DisplaySnapshot;
using DesktopSceneEngine::Player::LauncherWindow;

bool require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
}

struct LauncherFixture
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("launcher.ini"), QSettings::IniFormat};
    DesktopSceneEngine::Player::ApplicationPreferencesStore store{settings};
    DesktopSceneEngine::Player::ApplicationTranslationState translation;
    LauncherWindow window{translation, store};
};

DisplayDescriptor makeDisplay(
    DisplayId id,
    int displayNumber,
    bool primary,
    std::string friendlyName
)
{
    return DisplayDescriptor{
        std::move(id),
        std::move(friendlyName),
        {},
        displayNumber * 1920,
        0,
        1920,
        1080,
        primary,
        displayNumber,
        std::nullopt
    };
}

DisplayEnumerationResult successfulSnapshot(std::vector<DisplayDescriptor> displays)
{
    return DisplayEnumerationResult{
        DisplayEnumerationStatus::Success,
        DisplaySnapshot{std::move(displays)}
    };
}

bool testCatalogOrderAndEligibility()
{
    const DisplayId displayA = DisplayId::fromTargetDevicePath("display-a");
    const DisplayId weakDisplay = DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY2");
    const DisplayId displayB = DisplayId::fromTargetDevicePath("display-b");
    LauncherFixture fixture;
    auto& window = fixture.window;

    LauncherWindowTestAccess::apply(
        window,
        successfulSnapshot({
            makeDisplay(displayA, 7, false, "First"),
            makeDisplay(weakDisplay, 2, true, "Weak"),
            makeDisplay(displayB, 7, false, "Third")
        })
    );

    bool result = require(
        LauncherWindowTestAccess::rowCount(window) == 3,
        "The Launcher catalog must preserve every descriptor in snapshot order."
    );

    LauncherWindowTestAccess::selectRow(window, 1);
    result = require(
        LauncherWindowTestAccess::selectedDisplayId(window) == weakDisplay,
        "A weak catalog row must preserve its exact DisplayId."
    ) && result;
    result = require(
        LauncherWindowTestAccess::identifyEnabled(window)
            && !LauncherWindowTestAccess::startEnabled(window),
        "Weak identities must remain identifiable but ineligible to own a Session."
    ) && result;

    LauncherWindowTestAccess::selectRow(window, 2);
    result = require(
        LauncherWindowTestAccess::selectedDisplayId(window) == displayB,
        "Selection must use DisplayId rather than display number or list label."
    ) && result;
    result = require(
        LauncherWindowTestAccess::identifyEnabled(window)
            && LauncherWindowTestAccess::startEnabled(window),
        "A valid persistent identity must enable Identify and Start Session."
    ) && result;

    return result;
}

bool testRefreshPreservesExactIdentity()
{
    const DisplayId displayA = DisplayId::fromTargetDevicePath("display-a");
    const DisplayId displayB = DisplayId::fromTargetDevicePath("display-b");
    LauncherFixture fixture;
    auto& window = fixture.window;

    LauncherWindowTestAccess::apply(
        window,
        successfulSnapshot({
            makeDisplay(displayA, 1, true, "A"),
            makeDisplay(displayB, 2, false, "B")
        })
    );
    LauncherWindowTestAccess::selectRow(window, 1);

    LauncherWindowTestAccess::apply(
        window,
        successfulSnapshot({
            makeDisplay(displayB, 8, false, "B renamed"),
            makeDisplay(displayA, 3, true, "A renamed")
        })
    );
    bool result = require(
        LauncherWindowTestAccess::selectedDisplayId(window) == displayB,
        "Refresh must reselect by exact DisplayId after order and metadata change."
    );

    LauncherWindowTestAccess::apply(
        window,
        successfulSnapshot({makeDisplay(displayA, 1, true, "A")})
    );
    result = require(
        !LauncherWindowTestAccess::selectedDisplayId(window).has_value()
            && !LauncherWindowTestAccess::identifyEnabled(window)
            && !LauncherWindowTestAccess::startEnabled(window),
        "Refresh must clear selection and actions when the selected identity disappears."
    ) && result;

    return result;
}

bool testEnumerationFailureClearsCatalog()
{
    const DisplayId displayA = DisplayId::fromTargetDevicePath("display-a");
    LauncherFixture fixture;
    auto& window = fixture.window;
    LauncherWindowTestAccess::apply(
        window,
        successfulSnapshot({makeDisplay(displayA, 1, true, "A")})
    );
    LauncherWindowTestAccess::selectRow(window, 0);

    LauncherWindowTestAccess::apply(
        window,
        DisplayEnumerationResult{DisplayEnumerationStatus::ApiFailure, {}}
    );

    return require(
        LauncherWindowTestAccess::rowCount(window) == 0
            && !LauncherWindowTestAccess::selectedDisplayId(window).has_value(),
        "Enumeration failure must clear stale catalog rows and selection."
    ) && require(
        !LauncherWindowTestAccess::identifyEnabled(window)
            && !LauncherWindowTestAccess::startEnabled(window)
            && LauncherWindowTestAccess::refreshEnabled(window),
        "Enumeration failure must disable stale actions while leaving Refresh available."
    ) && require(
        !LauncherWindowTestAccess::statusText(window).isEmpty(),
        "Enumeration failure must present a diagnostic status."
    );
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    return testCatalogOrderAndEligibility()
        && testRefreshPreservesExactIdentity()
        && testEnumerationFailureClearsCatalog()
        ? 0
        : 1;
}
