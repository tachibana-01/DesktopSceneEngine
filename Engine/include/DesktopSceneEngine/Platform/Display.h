#pragma once

#include <DesktopSceneEngine/DisplayIdentity.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace DesktopSceneEngine::Platform
{

struct DisplayRefreshRate
{
    std::uint32_t numerator = 0U;
    std::uint32_t denominator = 0U;

    friend bool operator==(const DisplayRefreshRate&, const DisplayRefreshRate&) = default;
};

// A platform-neutral value snapshot of one active output display. Coordinates
// and dimensions use physical desktop pixels; they are never Qt logical pixels.
struct DisplayDescriptor
{
    DisplayId id{};
    std::string friendlyName{};
    std::string sourceDeviceName{};
    int desktopX = 0;
    int desktopY = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    bool primary = false;
    int displayNumber = 0;
    std::optional<DisplayRefreshRate> refreshRate{};
};

struct DisplaySnapshot
{
    std::vector<DisplayDescriptor> displays{};

    // Returns no descriptor for an unavailable or ambiguous identity. It does
    // not choose a first, primary, or geometrically similar fallback.
    [[nodiscard]] std::optional<DisplayDescriptor> resolve(const DisplayId& id) const;
};

enum class DisplayEnumerationStatus
{
    Success,
    ApiFailure,
    IncompletePathMetadata,
    DuplicateIdentity
};

struct DisplayEnumerationResult
{
    DisplayEnumerationStatus status = DisplayEnumerationStatus::ApiFailure;
    DisplaySnapshot snapshot{};
};

enum class DisplayIdentifyStatus
{
    Shown,
    InvalidRequest,
    Unresolved,
    ApiFailure
};

// Enumerates active Windows display paths into a value-only snapshot. Native
// display handles and CCD structures are confined to the implementation.
[[nodiscard]] DisplayEnumerationResult enumerateActiveDisplays();

// Re-resolves a current Windows display from a value identity before showing a
// temporary, input-transparent identification overlay. No native handle is
// retained by callers between enumeration and identification.
[[nodiscard]] DisplayIdentifyStatus identifyDisplay(
    const DisplayId& id,
    std::uint32_t durationMilliseconds
);

}
