#pragma once

#include <optional>

namespace DesktopSceneEngine::Platform::Windows
{

struct NativeClientSize
{
    int width = 0;
    int height = 0;

    friend bool operator==(const NativeClientSize&, const NativeClientSize&) = default;
};

// Queries realized native client pixels for a borrowed Windows target without
// exposing Win32 types to callers.
[[nodiscard]] std::optional<NativeClientSize> queryNativeWindowClientSize(
    void* nativeHandle
) noexcept;

}
