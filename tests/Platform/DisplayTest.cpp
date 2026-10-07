#include <DesktopSceneEngine/DisplayIdentity.h>
#include <DesktopSceneEngine/Platform/Display.h>

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using DesktopSceneEngine::Platform::DisplayDescriptor;
using DesktopSceneEngine::Platform::DisplayEnumerationStatus;
using DesktopSceneEngine::DisplayIdentityKind;
using DesktopSceneEngine::DisplayId;
using DesktopSceneEngine::Platform::DisplayRefreshRate;
using DesktopSceneEngine::Platform::DisplaySnapshot;
using DesktopSceneEngine::Platform::enumerateActiveDisplays;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

DisplayDescriptor makeDescriptor(
    DisplayId id,
    int number,
    int x,
    int y,
    int width,
    int height,
    bool primary
)
{
    return DisplayDescriptor{
        std::move(id),
        "Display",
        "DISPLAY",
        x,
        y,
        width,
        height,
        primary,
        number,
        std::nullopt};
}

bool verifyIdentityAndResolution()
{
    const DisplayId targetA = DisplayId::fromTargetDevicePath("target-path-a");
    const DisplayId targetB = DisplayId::fromTargetDevicePath("target-path-b");
    const DisplayId fallbackA = DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY1");
    const DisplayId fallbackB = DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY2");

    if (!require(targetA.isValid(), "A target-path identity should be valid")
        || !require(fallbackA.isValid(), "A source-device fallback should be valid")
        || !require(targetA == DisplayId::fromTargetDevicePath("target-path-a"),
            "Equal target paths should compare equal")
        || !require(targetA != targetB, "Different target paths should remain distinct")
        || !require(targetA < targetB, "Display IDs should provide a deterministic ordering")
        || !require(fallbackA != fallbackB, "Different source-device fallbacks should remain distinct")
        || !require(targetA != fallbackA,
            "A fallback identity must not impersonate a stable target-path identity"))
    {
        return false;
    }

    const DisplayDescriptor descriptorA = makeDescriptor(targetA, 1, 0, 0, 1920, 1080, true);
    const DisplayDescriptor descriptorB = makeDescriptor(targetB, 2, -1080, 0, 1080, 1920, false);
    const DisplayDescriptor descriptorC = makeDescriptor(fallbackA, 3, 1920, 0, 1920, 1080, false);
    const DisplaySnapshot snapshot{{descriptorA, descriptorB, descriptorC}};

    const auto resolved = snapshot.resolve(targetB);
    if (!require(resolved.has_value(), "A present display ID should resolve")
        || !require(resolved->id == targetB,
            "Resolution must distinguish equal-resolution displays by identity")
        || !require(resolved->desktopX == -1080 && resolved->pixelWidth == 1080
                && resolved->pixelHeight == 1920,
            "Descriptors must preserve negative origins and portrait dimensions")
        || !require(!snapshot.resolve(DisplayId::fromTargetDevicePath("missing")).has_value(),
            "A missing display must remain unresolved"))
    {
        return false;
    }

    const auto fallbackResolved = snapshot.resolve(fallbackA);
    DisplayDescriptor renumbered = descriptorB;
    renumbered.friendlyName = "Renamed Display";
    renumbered.sourceDeviceName = "RENAMED-SOURCE";
    renumbered.desktopX = 800;
    renumbered.desktopY = -600;
    renumbered.pixelWidth = 3840;
    renumbered.pixelHeight = 2160;
    renumbered.primary = true;
    renumbered.displayNumber = 17;
    renumbered.refreshRate = DisplayRefreshRate{144U, 1U};
    const DisplaySnapshot renumberedSnapshot{{renumbered}};
    if (!require(fallbackResolved.has_value() && fallbackResolved->id == fallbackA,
            "A source-device fallback identity should re-resolve exactly")
        || !require(renumberedSnapshot.resolve(targetB).has_value(),
            "Snapshot metadata must not participate in identity resolution"))
    {
        return false;
    }

    const DisplaySnapshot duplicateSnapshot{{descriptorA, descriptorA}};
    return require(!duplicateSnapshot.resolve(targetA).has_value(),
        "Duplicate IDs must remain unresolved rather than selecting an arbitrary display");
}

bool verifyCanonicalIdentityContract()
{
    const DisplayId target = DisplayId::fromTargetDevicePath("identity-value");
    const DisplayId fallback = DisplayId::fromSourceDeviceName("identity-value");

    const auto targetCanonical = target.canonicalRepresentation();
    const auto fallbackCanonical = fallback.canonicalRepresentation();
    if (!require(targetCanonical.has_value(),
            "A valid target identity should have a canonical representation")
        || !require(fallbackCanonical.has_value(),
            "A valid fallback identity should have a canonical representation")
        || !require(*targetCanonical
                == "dse-display-id:v1:target-device-path:14:identity-value",
            "Target identity canonicalization should be deterministic")
        || !require(*fallbackCanonical
                == "dse-display-id:v1:source-device-name:14:identity-value",
            "Fallback identity canonicalization should preserve provenance")
        || !require(target != fallback,
            "Equal text with different provenance must remain different identities")
        || !require(target.isPersistentOwnerCapable(),
            "A target-device-path identity should be owner-capable")
        || !require(!fallback.isPersistentOwnerCapable(),
            "A source-device-name identity should remain runtime-only"))
    {
        return false;
    }

    const auto parsedTarget = DisplayId::parseCanonicalRepresentation(*targetCanonical);
    const auto parsedFallback = DisplayId::parseCanonicalRepresentation(*fallbackCanonical);
    if (!require(parsedTarget == std::optional<DisplayId>{target},
            "A target identity should round-trip through its canonical representation")
        || !require(parsedFallback == std::optional<DisplayId>{fallback},
            "A fallback identity should round-trip through its canonical representation"))
    {
        return false;
    }

    const std::string utf8Value = "\\\\?\\DISPLAY#"
        "\xE6\x98\xBE\xE7\xA4\xBA"
        ":route\\with:punctuation";
    const DisplayId utf8Identity = DisplayId::fromTargetDevicePath(utf8Value);
    const auto utf8Canonical = utf8Identity.canonicalRepresentation();
    if (!require(utf8Identity.isValid() && utf8Canonical.has_value(),
            "Valid UTF-8 and punctuation should remain representable")
        || !require(
            DisplayId::parseCanonicalRepresentation(*utf8Canonical)
                == std::optional<DisplayId>{utf8Identity},
            "Canonical parsing must preserve UTF-8 and punctuation exactly"))
    {
        return false;
    }

    const std::string multibyteValue{"\xE6\x98\xBE", 3U};
    const DisplayId multibyteIdentity = DisplayId::fromTargetDevicePath(multibyteValue);
    const auto multibyteCanonical = multibyteIdentity.canonicalRepresentation();
    if (!require(
            multibyteCanonical
                == std::optional<std::string>{
                    std::string{"dse-display-id:v1:target-device-path:3:"}
                    + multibyteValue},
            "Canonical length must count multibyte UTF-8 bytes exactly"))
    {
        return false;
    }

    const std::vector<std::string> invalidRepresentations{
        "",
        "dse-display-id:v2:target-device-path:1:x",
        "dse-display-id:v1::1:x",
        "dse-display-id:v1:unknown-kind:1:x",
        "dse-display-id:v1:target-device-path::x",
        "dse-display-id:v1:target-device-path:0:",
        "dse-display-id:v1:target-device-path:01:x",
        "dse-display-id:v1:target-device-path:2:x",
        "dse-display-id:v1:target-device-path:1:xtrailing",
        std::string{"dse-display-id:v1:target-device-path:2:"}
            + std::string{"\xC3\x28", 2U}};
    for (const std::string& representation : invalidRepresentations)
    {
        if (!require(
                !DisplayId::parseCanonicalRepresentation(representation).has_value(),
                "Malformed canonical identity input must be rejected"))
        {
            return false;
        }
    }

    const DisplayId invalidUtf8 = DisplayId::fromTargetDevicePath(
        std::string{"\xC3\x28", 2U});
    const std::vector<std::string> invalidUtf8Values{
        std::string{"\xC0\xAF", 2U},
        std::string{"\xE0\x80\x80", 3U},
        std::string{"\xED\xA0\x80", 3U},
        std::string{"\xF4\x90\x80\x80", 4U},
        std::string{"\xF5\x80\x80\x80", 4U},
        std::string{"\xE2\x82", 2U}};
    for (const std::string& invalidValue : invalidUtf8Values)
    {
        const DisplayId invalidIdentity = DisplayId::fromTargetDevicePath(invalidValue);
        if (!require(
                !invalidIdentity.isValid()
                    && !invalidIdentity.canonicalRepresentation().has_value(),
                "Strict UTF-8 validation must reject malformed scalar encodings"))
        {
            return false;
        }
    }

    const std::vector<std::string> validUtf8BoundaryValues{
        std::string{"\xC2\x80", 2U},
        std::string{"\xE0\xA0\x80", 3U},
        std::string{"\xED\x9F\xBF", 3U},
        std::string{"\xF0\x90\x80\x80", 4U},
        std::string{"\xF4\x8F\xBF\xBF", 4U}};
    for (const std::string& validValue : validUtf8BoundaryValues)
    {
        const DisplayId validIdentity = DisplayId::fromTargetDevicePath(validValue);
        const auto canonical = validIdentity.canonicalRepresentation();
        if (!require(
                validIdentity.isValid() && canonical.has_value()
                    && DisplayId::parseCanonicalRepresentation(*canonical)
                        == std::optional<DisplayId>{validIdentity},
                "Strict UTF-8 validation must accept valid scalar boundary encodings"))
        {
            return false;
        }
    }

    const DisplayId embeddedNull = DisplayId::fromTargetDevicePath(
        std::string{"a\0b", 3U});
    const DisplayId emptyTargetPath = DisplayId::fromTargetDevicePath({});
    return require(!invalidUtf8.isValid(),
               "Invalid UTF-8 must not form a valid display identity")
        && require(!invalidUtf8.canonicalRepresentation().has_value(),
            "Invalid identities must not produce canonical representations")
        && require(!embeddedNull.isValid()
                && !embeddedNull.canonicalRepresentation().has_value(),
            "Embedded NUL must not form or serialize a valid display identity")
        && require(!emptyTargetPath.isValid()
                && !emptyTargetPath.isPersistentOwnerCapable(),
            "An empty target path must not become a persistent owner identity")
        && require(!DisplayId{}.isPersistentOwnerCapable(),
            "An invalid identity must not be owner-capable");
}

bool verifyLiveWindowsEnumeration()
{
    const auto result = enumerateActiveDisplays();
    if (!require(result.status == DisplayEnumerationStatus::Success,
            "Live Windows display enumeration should succeed"))
    {
        return false;
    }

    int primaryCount = 0;
    std::vector<DisplayId> identities{};
    int expectedNumber = 1;
    for (const DisplayDescriptor& descriptor : result.snapshot.displays)
    {
        if (!require(descriptor.id.isValid(), "Every live descriptor needs a valid identity")
            || !require(descriptor.pixelWidth > 0 && descriptor.pixelHeight > 0,
                "Every live descriptor needs positive physical dimensions")
            || !require(descriptor.displayNumber == expectedNumber,
                "Display numbering should be contiguous within one snapshot")
            || !require(result.snapshot.resolve(descriptor.id).has_value(),
                "Every unique live identity should re-resolve in the same snapshot"))
        {
            return false;
        }

        for (const DisplayId& previous : identities)
        {
            if (!require(previous != descriptor.id,
                    "Live enumeration must not silently expose duplicate identities"))
            {
                return false;
            }
        }

        identities.push_back(descriptor.id);
        primaryCount += descriptor.primary ? 1 : 0;
        std::cout << "Display " << descriptor.displayNumber
                  << ": " << descriptor.pixelWidth << 'x' << descriptor.pixelHeight
                  << " at (" << descriptor.desktopX << ',' << descriptor.desktopY << ')'
                  << (descriptor.primary ? " primary" : "")
                  << " id-kind="
                  << (descriptor.id.kind() == DisplayIdentityKind::TargetDevicePath
                          ? "target-device-path" : "source-device-name");
        if (!descriptor.friendlyName.empty())
        {
            std::cout << " friendly=\"" << descriptor.friendlyName << '\"';
        }
        if (descriptor.refreshRate.has_value())
        {
            std::cout << " refresh=" << descriptor.refreshRate->numerator
                      << '/' << descriptor.refreshRate->denominator;
        }
        std::cout << std::endl;
        ++expectedNumber;
    }

    return require(result.snapshot.displays.empty() || primaryCount == 1,
        "A non-empty Windows snapshot should report exactly one primary display");
}

}

int main()
{
    return verifyIdentityAndResolution()
            && verifyCanonicalIdentityContract()
            && verifyLiveWindowsEnumeration()
        ? 0 : 1;
}
