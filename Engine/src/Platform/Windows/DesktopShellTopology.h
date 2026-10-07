#pragma once

#include <DesktopSceneEngine/Platform/Display.h>

#include <cstdint>
#include <limits>
#include <optional>

namespace DesktopSceneEngine::Platform::Windows
{

// Borrowed, freshly observed facts. No native window lifetime is owned here.
struct DesktopShellFacts
{
    std::uintptr_t progman = 0, defView = 0, icons = 0, worker = 0;
    std::uintptr_t defViewParent = 0, iconsParent = 0, workerParent = 0;
    bool classesAndProcessMatch = false;
    bool defViewLayered = false;
    bool defViewAboveWorker = false;
};

enum class DesktopShellKind { SupportedRaised, Unsupported, Incomplete };

inline bool canPrepareRaisedDesktop(const DesktopShellFacts& facts) noexcept
{
    return facts.progman != 0 && facts.defView != 0 && facts.icons != 0
        && facts.progman != facts.defView && facts.defView != facts.icons
        && facts.progman != facts.icons && facts.classesAndProcessMatch
        && facts.defViewLayered && facts.defViewParent == facts.progman
        && facts.iconsParent == facts.defView && facts.worker == 0;
}

inline DesktopShellKind classifyDesktopShell(const DesktopShellFacts& facts) noexcept
{
    if (!facts.progman || !facts.defView || !facts.icons || !facts.worker)
        return DesktopShellKind::Incomplete;
    if (!facts.classesAndProcessMatch || !facts.defViewLayered || !facts.defViewAboveWorker
        || facts.defViewParent != facts.progman || facts.iconsParent != facts.defView
        || facts.workerParent != facts.progman
        || facts.progman == facts.defView || facts.progman == facts.icons
        || facts.progman == facts.worker || facts.defView == facts.icons
        || facts.defView == facts.worker || facts.icons == facts.worker)
        return DesktopShellKind::Unsupported;
    return DesktopShellKind::SupportedRaised;
}

struct DesktopPlacement
{
    int x = 0, y = 0, width = 0, height = 0;
    friend bool operator==(const DesktopPlacement&, const DesktopPlacement&) = default;
};

// Physical screen pixels -> non-mirrored parent-client pixels. Widen before
// arithmetic so malformed metadata cannot overflow a Win32 placement call.
inline std::optional<DesktopPlacement> deriveDesktopPlacement(
    const DisplayDescriptor& display, int parentScreenX, int parentScreenY) noexcept
{
    if (display.pixelWidth <= 0 || display.pixelHeight <= 0) return std::nullopt;
    const std::int64_t x = std::int64_t(display.desktopX) - parentScreenX;
    const std::int64_t y = std::int64_t(display.desktopY) - parentScreenY;
    const auto fits = [](std::int64_t value)
    { return value >= (std::numeric_limits<int>::min)() && value <= (std::numeric_limits<int>::max)(); };
    if (!fits(x) || !fits(y) || !fits(x + display.pixelWidth) || !fits(y + display.pixelHeight)
        || !fits(std::int64_t(display.desktopX) + display.pixelWidth)
        || !fits(std::int64_t(display.desktopY) + display.pixelHeight)) return std::nullopt;
    return DesktopPlacement{int(x), int(y), display.pixelWidth, display.pixelHeight};
}

}
