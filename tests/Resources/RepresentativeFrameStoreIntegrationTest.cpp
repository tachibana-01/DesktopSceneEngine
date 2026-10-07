#include <DesktopSceneEngine/Resources/PreviewVideoDecoder.h>

#include "Resources/RepresentativeFrameCache.h"
#include "Resources/RepresentativeFramePng.h"
#include "Resources/RepresentativeFrameStore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{

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
            / ("DesktopSceneEngine-RepresentativeFrameStoreIntegrationTest-" + std::to_string(nonce));
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

std::vector<std::filesystem::path> findVideoFixtures()
{
    std::vector<std::filesystem::path> fixtures{};
    const std::filesystem::path root{DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root})
    {
        if (entry.is_regular_file() && entry.path().extension() == ".mp4")
        {
            fixtures.push_back(entry.path());
        }
    }

    return fixtures;
}

bool framesEqual(
    const DesktopSceneEngine::Resources::RepresentativeFrameRgba& left,
    const DesktopSceneEngine::Resources::RepresentativeFrameRgba& right
)
{
    return left.width == right.width
        && left.height == right.height
        && left.rowStride == right.rowStride
        && left.rgba == right.rgba;
}

}

int main()
{
    const std::vector<std::filesystem::path> fixtures = findVideoFixtures();
    if (!require(!fixtures.empty(), "at least one uncommitted local video fixture must exist"))
    {
        return 1;
    }

    TemporaryDirectory temporary{};
    DesktopSceneEngine::Resources::RepresentativeFrameStore store{temporary.path() / "media"};
    const auto generated = store.loadOrGenerate(fixtures.front(), 0);
    if (!require(generated.status == DesktopSceneEngine::Resources::RepresentativeFrameStoreStatus::Generated
            && !generated.persistenceFailed
            && generated.frame.width > 0 && generated.frame.height > 0
            && generated.frame.width <= 320 && generated.frame.height <= 320,
            "a real isolated PreviewVideoDecoder request must generate a bounded frame"))
    {
        return 1;
    }

    const auto source = DesktopSceneEngine::Resources::sourceIdentityForFile(fixtures.front());
    if (!require(source.has_value(), "the fixture must retain a source identity"))
    {
        return 1;
    }

    const DesktopSceneEngine::Resources::RepresentativeFrameCacheLayout layout{
        temporary.path() / "media",
        DesktopSceneEngine::Resources::makeSourceFingerprint(*source)};
    const auto persisted = DesktopSceneEngine::Resources::readRepresentativeFramePng(layout.framePath(0U));
    const auto diskHit = store.loadOrGenerate(fixtures.front(), 0);
    return require(persisted.status == DesktopSceneEngine::Resources::RepresentativeFramePngStatus::Read
                && framesEqual(generated.frame, persisted.frame),
               "the generated real frame must round-trip through B1 PNG storage")
        && require(diskHit.status == DesktopSceneEngine::Resources::RepresentativeFrameStoreStatus::RamHit,
            "the same real source/timestamp must become a RAM hot-cache hit")
        ? 0
        : 1;
}
