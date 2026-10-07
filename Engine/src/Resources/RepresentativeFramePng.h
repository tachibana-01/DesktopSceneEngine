#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace DesktopSceneEngine::Resources
{

// An owned RGBA8 frame for representative-frame PNG storage. rgba contains at
// least rowStride * height bytes and each row begins with width * 4 RGBA bytes.
struct RepresentativeFrameRgba
{
    std::vector<std::uint8_t> rgba{};
    int width = 0;
    int height = 0;
    std::size_t rowStride = 0;
};

enum class RepresentativeFramePngStatus
{
    Written,
    ReusedExisting,
    Read,
    InvalidFrame,
    ComInitializationFailed,
    FileOpenFailed,
    EncodeFailed,
    FinalizeFailed,
    Missing,
    DecodeFailed
};

struct RepresentativeFramePngReadResult
{
    RepresentativeFramePngStatus status = RepresentativeFramePngStatus::DecodeFailed;
    RepresentativeFrameRgba frame{};
};

// Writes a normal PNG from an owned RGBA8 frame. A valid existing final PNG is
// reused. A missing or corrupt final artifact is written through a temporary
// sibling and finalized atomically within the supplied directory.
[[nodiscard]] RepresentativeFramePngStatus writeRepresentativeFramePng(
    const std::filesystem::path& finalPath,
    const RepresentativeFrameRgba& frame
) noexcept;

// Reads a PNG into tightly packed, top-to-bottom RGBA8 pixels. Missing and
// corrupt files return a failure status with a default-initialized frame.
[[nodiscard]] RepresentativeFramePngReadResult readRepresentativeFramePng(
    const std::filesystem::path& path
) noexcept;

}
