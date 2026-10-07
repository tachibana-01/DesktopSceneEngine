#include "Infrastructure/Persistence/ProjectFile.h"

#include <DesktopSceneEngine/Domain/Project.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

using DesktopSceneEngine::Domain::EntityId;
using DesktopSceneEngine::Domain::Project;

class TemporaryPath
{
public:

    explicit TemporaryPath(std::filesystem::path path)
        : path_(std::move(path))
    {
    }

    ~TemporaryPath()
    {
        std::error_code error{};
        std::filesystem::remove(path_, error);

        if (error)
        {
            std::cerr
                << "Cleanup warning for '"
                << path_.string()
                << "': "
                << error.message()
                << std::endl;
        }
    }

    TemporaryPath(const TemporaryPath&) = delete;
    TemporaryPath& operator=(const TemporaryPath&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:

    std::filesystem::path path_{};
};

std::filesystem::path temporaryPath(const std::string& suffix)
{
    const auto timestamp = std::chrono::high_resolution_clock::now()
        .time_since_epoch()
        .count();

    return std::filesystem::temp_directory_path()
        / ("DesktopSceneEngine_ProjectFileTest_"
            + std::to_string(timestamp)
            + "_"
            + suffix);
}

Project makeProject()
{
    const DesktopSceneEngine::Domain::Media media{
        EntityId{std::string{"media-1"}},
        "Sample Media",
        "media/sample"
    };
    const DesktopSceneEngine::Domain::Scene scene{
        EntityId{std::string{"scene-1"}},
        media.id,
        DesktopSceneEngine::Domain::Crop{0.1F, 0.2F, 0.7F, 0.8F},
        DesktopSceneEngine::Domain::Layout{0.25F, 0.5F, 1.25F, 0.75F},
        DesktopSceneEngine::Domain::Playback{false, 1.5F, 0.65F},
        1.25F
    };
    const DesktopSceneEngine::Domain::Playlist playlist{
        EntityId{std::string{"playlist-1"}},
        {scene.id}
    };

    return Project{
        DesktopSceneEngine::Domain::Library{{media}},
        {scene},
        {playlist}
    };
}

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool nearlyEqual(float actual, float expected) noexcept
{
    constexpr float tolerance = 0.0001F;
    return std::fabs(actual - expected) <= tolerance;
}

template <typename Function>
bool expectRuntimeError(Function&& function, const char* message)
{
    try
    {
        function();
    }
    catch (const std::runtime_error&)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyPersistenceRoundTrip()
{
    TemporaryPath projectPath{temporaryPath("round_trip.json")};
    const Project original = makeProject();

    DesktopSceneEngine::Infrastructure::Persistence::saveProjectFile(
        original,
        projectPath.path()
    );

    if (!require(
            std::filesystem::exists(projectPath.path()),
            "Saved project file should exist"))
    {
        return false;
    }

    std::ifstream input{projectPath.path(), std::ios::binary};
    const std::string contents{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };

    if (!require(contents.find("\n") != std::string::npos, "JSON should be formatted across lines")
        || !require(contents.find("    \"library\"") != std::string::npos, "JSON should use four-space indentation"))
    {
        return false;
    }

    const nlohmann::json document = nlohmann::json::parse(contents);
    if (!require(document.at("version") == "1.0", "Saved JSON should contain version"))
    {
        return false;
    }

    const Project restored =
        DesktopSceneEngine::Infrastructure::Persistence::loadProjectFile(
            projectPath.path()
        );

    return require(restored.library.media.size() == 1, "Media count should survive")
        && require(restored.library.media[0].id.value() == "media-1", "Media ID should survive")
        && require(restored.scenes.size() == 1, "Scene count should survive")
        && require(restored.scenes[0].mediaId.value() == "media-1", "Scene media ID should survive")
        && require(nearlyEqual(restored.scenes[0].crop.x, 0.1F), "Crop x should survive")
        && require(nearlyEqual(restored.scenes[0].layout.positionX, 0.25F), "Layout position should survive")
        && require(!restored.scenes[0].playback.loop, "Playback loop should survive")
        && require(nearlyEqual(restored.scenes[0].playback.speed, 1.5F), "Playback speed should survive")
        && require(nearlyEqual(restored.scenes[0].playback.volume, 0.65F), "Playback volume should survive")
        && require(restored.playlists.size() == 1, "Playlist count should survive")
        && require(restored.playlists[0].sceneIds.size() == 1, "Playlist references should survive")
        && require(restored.playlists[0].sceneIds[0].value() == "scene-1", "Playlist reference should survive");
}

bool verifyErrorCases()
{
    TemporaryPath missingPath{temporaryPath("missing.json")};
    if (!expectRuntimeError(
            [&missingPath]()
            {
                (void)DesktopSceneEngine::Infrastructure::Persistence::loadProjectFile(
                    missingPath.path()
                );
            },
            "Missing file should be rejected"))
    {
        return false;
    }

    TemporaryPath projectPath{temporaryPath("errors.json")};

    {
        std::ofstream output{projectPath.path(), std::ios::binary | std::ios::trunc};
        output << "{ invalid json";
    }

    if (!expectRuntimeError(
            [&projectPath]()
            {
                (void)DesktopSceneEngine::Infrastructure::Persistence::loadProjectFile(
                    projectPath.path()
                );
            },
            "Malformed JSON should be rejected"))
    {
        return false;
    }

    {
        std::ofstream output{projectPath.path(), std::ios::binary | std::ios::trunc};
        output << R"({"version":"2.0","library":{"media":[]},"scenes":[],"playlists":[]})";
    }

    if (!expectRuntimeError(
            [&projectPath]()
            {
                (void)DesktopSceneEngine::Infrastructure::Persistence::loadProjectFile(
                    projectPath.path()
                );
            },
            "Unsupported version should be rejected"))
    {
        return false;
    }

    {
        std::ofstream output{projectPath.path(), std::ios::binary | std::ios::trunc};
        output << R"({"version":"1.0","library":{"media":{}},"scenes":[],"playlists":[]})";
    }

    if (!expectRuntimeError(
            [&projectPath]()
            {
                (void)DesktopSceneEngine::Infrastructure::Persistence::loadProjectFile(
                    projectPath.path()
                );
            },
            "Invalid nested structure should be rejected"))
    {
        return false;
    }

    TemporaryPath existingFile{temporaryPath("existing_file")};
    {
        std::ofstream output{existingFile.path(), std::ios::binary | std::ios::trunc};
        output << "not a directory";
    }

    const std::filesystem::path invalidWritePath =
        existingFile.path() / "child.json";

    return expectRuntimeError(
        [&invalidWritePath]()
        {
            DesktopSceneEngine::Infrastructure::Persistence::saveProjectFile(
                makeProject(),
                invalidWritePath
            );
        },
        "Invalid write path should be rejected");
}

}

int main()
{
    return verifyPersistenceRoundTrip() && verifyErrorCases() ? 0 : 1;
}
