#include "Resources/RepresentativeFrameCache.h"
#include "Resources/RepresentativeFramePng.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{

using DesktopSceneEngine::Resources::RepresentativeFramePngReadResult;
using DesktopSceneEngine::Resources::RepresentativeFramePngStatus;
using DesktopSceneEngine::Resources::RepresentativeFrameRgba;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

class TemporaryDirectory final
{
public:

    TemporaryDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("DesktopSceneEngine-RepresentativeFramePngTest-" + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:

    std::filesystem::path path_{};
};

RepresentativeFrameRgba makeFrame(int width, int height, std::size_t rowStride)
{
    RepresentativeFrameRgba frame{};
    frame.width = width;
    frame.height = height;
    frame.rowStride = rowStride;
    frame.rgba.resize(rowStride * static_cast<std::size_t>(height), 0xE7U);
    return frame;
}

void setPixel(
    RepresentativeFrameRgba& frame,
    int x,
    int y,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha
)
{
    const std::size_t offset = static_cast<std::size_t>(y) * frame.rowStride
        + static_cast<std::size_t>(x) * 4U;
    frame.rgba[offset] = red;
    frame.rgba[offset + 1U] = green;
    frame.rgba[offset + 2U] = blue;
    frame.rgba[offset + 3U] = alpha;
}

bool pixelsMatch(const RepresentativeFrameRgba& expected, const RepresentativeFrameRgba& actual)
{
    if (expected.width != actual.width || expected.height != actual.height)
    {
        return false;
    }

    for (int y = 0; y < expected.height; ++y)
    {
        for (int x = 0; x < expected.width; ++x)
        {
            const std::size_t expectedOffset = static_cast<std::size_t>(y) * expected.rowStride
                + static_cast<std::size_t>(x) * 4U;
            const std::size_t actualOffset = static_cast<std::size_t>(y) * actual.rowStride
                + static_cast<std::size_t>(x) * 4U;
            for (std::size_t channel = 0; channel < 4U; ++channel)
            {
                if (expected.rgba[expectedOffset + channel] != actual.rgba[actualOffset + channel])
                {
                    return false;
                }
            }
        }
    }

    return true;
}

bool hasTemporarySibling(const std::filesystem::path& finalPath)
{
    std::error_code error{};
    const std::string prefix = finalPath.filename().string() + ".tmp.";
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(
             finalPath.parent_path(),
             error))
    {
        if (error)
        {
            return false;
        }

        if (entry.path().filename().string().starts_with(prefix))
        {
            return true;
        }
    }

    return false;
}

bool verifyChannelOrderAlphaAndAtomicWrite()
{
    TemporaryDirectory temporary{};
    const DesktopSceneEngine::Resources::RepresentativeFrameCacheLayout layout{
        temporary.path() / "cache",
        {"test-source"}};
    const std::filesystem::path finalPath = layout.framePath(5000U);

    RepresentativeFrameRgba frame = makeFrame(2, 2, 8U);
    setPixel(frame, 0, 0, 255U, 0U, 0U, 255U);
    setPixel(frame, 1, 0, 0U, 255U, 0U, 255U);
    setPixel(frame, 0, 1, 0U, 0U, 255U, 255U);
    setPixel(frame, 1, 1, 255U, 255U, 255U, 64U);

    const RepresentativeFramePngStatus writeStatus =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(finalPath, frame);
    const RepresentativeFramePngReadResult readResult =
        DesktopSceneEngine::Resources::readRepresentativeFramePng(finalPath);

    return require(writeStatus == RepresentativeFramePngStatus::Written,
               "a valid RGBA frame must encode to a new PNG")
        && require(std::filesystem::is_regular_file(finalPath),
            "atomic frame write must create the final PNG")
        && require(!hasTemporarySibling(finalPath),
            "successful atomic frame write must leave no temporary sibling")
        && require(readResult.status == RepresentativeFramePngStatus::Read,
            "the written PNG must decode successfully")
        && require(readResult.frame.rowStride == 8U,
            "decoded 2x2 RGBA pixels must be tightly packed")
        && require(pixelsMatch(frame, readResult.frame),
            "RGBA channel order, alpha, and pixel positions must round-trip exactly");
}

bool verifyPortraitAndNonTightStride()
{
    TemporaryDirectory temporary{};
    const std::filesystem::path finalPath = temporary.path() / "portrait.png";
    RepresentativeFrameRgba frame = makeFrame(3, 5, 16U);

    for (int y = 0; y < frame.height; ++y)
    {
        for (int x = 0; x < frame.width; ++x)
        {
            setPixel(
                frame,
                x,
                y,
                static_cast<std::uint8_t>(20 + x),
                static_cast<std::uint8_t>(40 + y),
                static_cast<std::uint8_t>(60 + x + y),
                static_cast<std::uint8_t>(80 + y));
        }
    }

    const RepresentativeFramePngStatus writeStatus =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(finalPath, frame);
    const RepresentativeFramePngReadResult readResult =
        DesktopSceneEngine::Resources::readRepresentativeFramePng(finalPath);

    return require(writeStatus == RepresentativeFramePngStatus::Written,
               "a valid portrait frame with non-tight rows must encode")
        && require(readResult.status == RepresentativeFramePngStatus::Read,
            "a valid portrait PNG must decode")
        && require(readResult.frame.rowStride == 12U,
            "decoded portrait RGBA rows must normalize to tight packing")
        && require(pixelsMatch(frame, readResult.frame),
            "row-dependent portrait colors must preserve orientation and ignore source padding");
}

bool verifyInvalidInputAndTemporaryCleanup()
{
    TemporaryDirectory temporary{};
    const std::filesystem::path finalPath = temporary.path() / "invalid.png";
    RepresentativeFrameRgba invalid = makeFrame(2, 2, 8U);
    invalid.width = 0;

    const RepresentativeFramePngStatus zeroWidth =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(finalPath, invalid);
    invalid.width = -1;
    const RepresentativeFramePngStatus negativeWidth =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(finalPath, invalid);
    invalid.width = 2;
    invalid.height = 0;
    const RepresentativeFramePngStatus zeroHeight =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(finalPath, invalid);
    invalid.height = 2;
    invalid.rowStride = 7U;
    const RepresentativeFramePngStatus shortStride =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(finalPath, invalid);
    invalid.rowStride = 8U;
    invalid.rgba.resize(7U);
    const RepresentativeFramePngStatus shortStorage =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(finalPath, invalid);

    RepresentativeFrameRgba overflowing{};
    overflowing.width = (std::numeric_limits<int>::max)();
    overflowing.height = (std::numeric_limits<int>::max)();
    overflowing.rowStride = static_cast<std::size_t>(overflowing.width) * 4U;
    overflowing.rgba = {0U};
    const RepresentativeFramePngStatus overflow =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(finalPath, overflowing);

    return require(zeroWidth == RepresentativeFramePngStatus::InvalidFrame,
               "zero-width frames must be rejected")
        && require(negativeWidth == RepresentativeFramePngStatus::InvalidFrame,
            "negative-width frames must be rejected")
        && require(zeroHeight == RepresentativeFramePngStatus::InvalidFrame,
            "zero-height frames must be rejected")
        && require(shortStride == RepresentativeFramePngStatus::InvalidFrame,
            "rows shorter than width times RGBA bytes must be rejected")
        && require(shortStorage == RepresentativeFramePngStatus::InvalidFrame,
            "too-small RGBA backing storage must be rejected")
        && require(overflow == RepresentativeFramePngStatus::InvalidFrame,
            "overflowing frame dimensions must be rejected")
        && require(!std::filesystem::exists(finalPath),
            "invalid frame writes must not create a final PNG")
        && require(!hasTemporarySibling(finalPath),
            "invalid frame writes must not leave temporary PNG files");
}

bool verifyMissingCorruptAndExistingFileSemantics()
{
    TemporaryDirectory temporary{};
    const std::filesystem::path missingPath = temporary.path() / "missing.png";
    const RepresentativeFramePngReadResult missing =
        DesktopSceneEngine::Resources::readRepresentativeFramePng(missingPath);

    const std::filesystem::path corruptPath = temporary.path() / "corrupt.png";
    {
        std::ofstream corrupt{corruptPath, std::ios::binary};
        corrupt << "not a PNG";
    }
    const RepresentativeFramePngReadResult corrupt =
        DesktopSceneEngine::Resources::readRepresentativeFramePng(corruptPath);

    const std::filesystem::path existingPath = temporary.path() / "existing.png";
    RepresentativeFrameRgba initial = makeFrame(1, 1, 4U);
    setPixel(initial, 0, 0, 10U, 20U, 30U, 40U);
    RepresentativeFrameRgba replacement = makeFrame(1, 1, 4U);
    setPixel(replacement, 0, 0, 50U, 60U, 70U, 80U);
    const RepresentativeFramePngStatus firstWrite =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(existingPath, initial);
    const RepresentativeFramePngStatus secondWrite =
        DesktopSceneEngine::Resources::writeRepresentativeFramePng(existingPath, replacement);
    const RepresentativeFramePngReadResult existing =
        DesktopSceneEngine::Resources::readRepresentativeFramePng(existingPath);

    return require(missing.status == RepresentativeFramePngStatus::Missing,
               "a missing PNG must be reported as a clean cache miss")
        && require(missing.frame.rgba.empty(),
            "a missing PNG must not expose partial frame storage")
        && require(corrupt.status == RepresentativeFramePngStatus::DecodeFailed,
            "a corrupt PNG must fail cleanly")
        && require(corrupt.frame.rgba.empty(),
            "a corrupt PNG must not expose partial frame storage")
        && require(firstWrite == RepresentativeFramePngStatus::Written,
            "the first valid write must create the cache artifact")
        && require(secondWrite == RepresentativeFramePngStatus::ReusedExisting,
            "a valid existing cache artifact must be reused without rewrite")
        && require(existing.status == RepresentativeFramePngStatus::Read
                && pixelsMatch(initial, existing.frame),
            "reusing a valid cache artifact must preserve its original pixels");
}

}

int main()
{
    return verifyChannelOrderAlphaAndAtomicWrite()
            && verifyPortraitAndNonTightStride()
            && verifyInvalidInputAndTemporaryCleanup()
            && verifyMissingCorruptAndExistingFileSemantics()
        ? 0
        : 1;
}
