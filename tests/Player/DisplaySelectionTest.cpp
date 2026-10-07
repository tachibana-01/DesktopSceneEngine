#include "DisplaySelectionState.h"

#include <DesktopSceneEngine/Platform/Display.h>

#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace
{

using DesktopSceneEngine::Platform::DisplayDescriptor;
using DesktopSceneEngine::Platform::DisplayEnumerationResult;
using DesktopSceneEngine::Platform::DisplayEnumerationStatus;
using DesktopSceneEngine::DisplayId;
using DesktopSceneEngine::Platform::DisplaySnapshot;
using DesktopSceneEngine::Player::DisplaySelectionAvailability;
using DesktopSceneEngine::Player::DisplaySelectionState;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

DisplayDescriptor makeDisplay(
    DisplayId id,
    int displayNumber,
    int desktopX,
    bool primary
)
{
    return DisplayDescriptor{
        std::move(id),
        {},
        {},
        desktopX,
        0,
        1920,
        1080,
        primary,
        displayNumber,
        std::nullopt};
}

DisplayEnumerationResult successfulSnapshot(std::vector<DisplayDescriptor> displays)
{
    return DisplayEnumerationResult{
        DisplayEnumerationStatus::Success,
        DisplaySnapshot{std::move(displays)}};
}

bool verifySelectionPreservationAndIdentifyRouting()
{
    const DisplayId displayA = DisplayId::fromTargetDevicePath("display-a");
    const DisplayId displayB = DisplayId::fromTargetDevicePath("display-b");
    const DisplayId displayC = DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY3");

    DisplaySelectionState selection{displayB};
    selection.refresh(successfulSnapshot({
        makeDisplay(displayA, 1, 0, true),
        makeDisplay(displayB, 2, 1920, false),
        makeDisplay(displayC, 3, 3840, false)}));

    const auto startupRequest = selection.identifyRequest();
    if (!require(selection.availability() == DisplaySelectionAvailability::Available,
            "A bound display present in the first snapshot should be available")
        || !require(selection.boundDisplayId() == displayB,
            "Construction must establish the authoritative bound DisplayId")
        || !require(startupRequest.has_value() && startupRequest->displayId == displayB,
            "Identify routing must use the bound display identity"))
    {
        return false;
    }

    selection.refresh(successfulSnapshot({
        makeDisplay(displayC, 1, -1920, false),
        makeDisplay(displayA, 4, 0, true),
        makeDisplay(displayB, 7, 1920, false)}));
    const auto renumberedRequest = selection.identifyRequest();
    if (!require(selection.availability() == DisplaySelectionAvailability::Available,
            "A bound display that remains present must stay available after refresh")
        || !require(selection.boundDisplayId() == displayB,
            "Refresh must preserve the bound DisplayId when snapshot metadata changes")
        || !require(renumberedRequest.has_value() && renumberedRequest->displayId == displayB,
            "Identify routing must preserve the bound display identity after renumbering")
        || !require(selection.boundDisplay().has_value()
                && selection.boundDisplay()->displayNumber == 7,
            "Resolved descriptor metadata should come from the latest snapshot"))
    {
        return false;
    }

    selection.refresh(successfulSnapshot({
        makeDisplay(displayA, 1, 0, true),
        makeDisplay(displayC, 2, 1920, false)}));
    if (!require(selection.availability() == DisplaySelectionAvailability::Unresolved,
            "A missing bound display must become unresolved")
        || !require(selection.boundDisplayId() == displayB,
            "An unresolved session must retain its bound DisplayId")
        || !require(!selection.boundDisplay().has_value(),
            "An unresolved binding must not fabricate a current descriptor")
        || !require(!selection.identifyRequest().has_value(),
            "Identify must be unavailable for an unresolved bound display"))
    {
        return false;
    }

    selection.refresh(successfulSnapshot({
        makeDisplay(displayB, 5, -1920, false),
        makeDisplay(displayA, 1, 0, true)}));
    if (!require(selection.availability() == DisplaySelectionAvailability::Available,
            "The same bound display should become available when it reappears")
        || !require(selection.boundDisplayId() == displayB,
            "Reappearance must not change the session identity")
        || !require(selection.boundDisplay().has_value()
                && selection.boundDisplay()->displayNumber == 5,
            "Reappearance should expose the fresh descriptor for the bound identity"))
    {
        return false;
    }

    return true;
}

bool verifyNoPrimaryAndFailureStates()
{
    const DisplayId displayA = DisplayId::fromTargetDevicePath("display-a");
    const DisplayId displayB = DisplayId::fromTargetDevicePath("display-b");
    DisplaySelectionState selection{displayB};

    selection.refresh(successfulSnapshot({
        makeDisplay(displayA, 1, 0, true)}));
    if (!require(selection.availability() == DisplaySelectionAvailability::Unresolved,
            "A different primary display must not replace a missing bound display")
        || !require(selection.boundDisplayId() == displayB,
            "No-fallback refresh must preserve the bound identity")
        || !require(!selection.identifyRequest().has_value(),
            "Identify must remain unavailable when the bound display is unresolved"))
    {
        return false;
    }

    selection.refresh(DisplayEnumerationResult{
        DisplayEnumerationStatus::ApiFailure,
        {}});
    return require(selection.availability() == DisplaySelectionAvailability::EnumerationFailed,
            "Enumeration failure must remain distinct from a missing bound display")
        && require(selection.boundDisplayId() == displayB,
            "Enumeration failure must not replace the bound identity");
}

}

int main()
{
    return verifySelectionPreservationAndIdentifyRouting() && verifyNoPrimaryAndFailureStates()
        ? 0 : 1;
}
