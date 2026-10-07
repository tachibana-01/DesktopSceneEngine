#include "Infrastructure/Json/ProjectJson.h"

#include <DesktopSceneEngine/Domain/Project.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

using DesktopSceneEngine::Domain::EntityId;
using DesktopSceneEngine::Domain::Project;

Project makeProject()
{
    const DesktopSceneEngine::Domain::Media media{
        EntityId{std::string{"media-1"}},
        "Sample Media",
        "media/sample"
    };

    const DesktopSceneEngine::Domain::Crop crop{
        0.1F,
        0.2F,
        0.7F,
        0.8F
    };
    const DesktopSceneEngine::Domain::Layout layout{
        0.25F,
        0.5F,
        1.25F,
        0.75F
    };
    const DesktopSceneEngine::Domain::Playback playback{
        false,
        1.5F,
        0.65F
    };
    const DesktopSceneEngine::Domain::Scene scene{
        EntityId{std::string{"scene-1"}},
        media.id,
        crop,
        layout,
        playback,
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

bool verifyRoundTrip()
{
    const Project original = makeProject();
    const nlohmann::json document =
        DesktopSceneEngine::Infrastructure::Json::toJson(original);

    if (!require(document.at("version") == "1.0", "Version should be 1.0"))
    {
        return false;
    }

    const Project restored =
        DesktopSceneEngine::Infrastructure::Json::fromJson(document);

    return require(restored.library.media.size() == 1, "Media count should survive")
        && require(restored.library.media[0].id.value() == "media-1", "Media ID should survive")
        && require(restored.library.media[0].name == "Sample Media", "Media name should survive")
        && require(restored.library.media[0].resourceReference == "media/sample", "Media reference should survive")
        && require(restored.scenes.size() == 1, "Scene count should survive")
        && require(restored.scenes[0].id.value() == "scene-1", "Scene ID should survive")
        && require(restored.scenes[0].mediaId.value() == "media-1", "Scene media ID should survive")
        && require(nearlyEqual(restored.scenes[0].crop.x, 0.1F), "Crop x should survive")
        && require(nearlyEqual(restored.scenes[0].crop.height, 0.8F), "Crop height should survive")
        && require(nearlyEqual(restored.scenes[0].layout.positionX, 0.25F), "Layout position should survive")
        && require(nearlyEqual(restored.scenes[0].layout.scaleY, 0.75F), "Layout scale should survive")
        && require(!restored.scenes[0].playback.loop, "Playback loop should survive")
        && require(nearlyEqual(restored.scenes[0].playback.speed, 1.5F), "Playback speed should survive")
        && require(nearlyEqual(restored.scenes[0].playback.volume, 0.65F), "Playback volume should survive")
        && require(nearlyEqual(restored.scenes[0].duration, 1.25F), "Scene duration should survive")
        && require(restored.playlists.size() == 1, "Playlist count should survive")
        && require(restored.playlists[0].id.value() == "playlist-1", "Playlist ID should survive")
        && require(restored.playlists[0].sceneIds.size() == 1, "Playlist references should survive")
        && require(restored.playlists[0].sceneIds[0].value() == "scene-1", "Playlist order should survive");
}

bool verifyInvalidDocuments()
{
    const Project original = makeProject();
    const nlohmann::json valid =
        DesktopSceneEngine::Infrastructure::Json::toJson(original);

    nlohmann::json unsupportedVersion = valid;
    unsupportedVersion["version"] = "2.0";
    if (!expectRuntimeError(
            [&unsupportedVersion]()
            {
                (void)DesktopSceneEngine::Infrastructure::Json::fromJson(unsupportedVersion);
            },
            "Unsupported version should be rejected"))
    {
        return false;
    }

    nlohmann::json missingVersion = valid;
    missingVersion.erase("version");
    if (!expectRuntimeError(
            [&missingVersion]()
            {
                (void)DesktopSceneEngine::Infrastructure::Json::fromJson(missingVersion);
            },
            "Missing version should be rejected"))
    {
        return false;
    }

    nlohmann::json missingScenes = valid;
    missingScenes.erase("scenes");
    if (!expectRuntimeError(
            [&missingScenes]()
            {
                (void)DesktopSceneEngine::Infrastructure::Json::fromJson(missingScenes);
            },
            "Missing scenes should be rejected"))
    {
        return false;
    }

    nlohmann::json invalidMedia = valid;
    invalidMedia["library"]["media"] = nlohmann::json::object();
    if (!expectRuntimeError(
            [&invalidMedia]()
            {
                (void)DesktopSceneEngine::Infrastructure::Json::fromJson(invalidMedia);
            },
            "Invalid media structure should be rejected"))
    {
        return false;
    }

    nlohmann::json invalidScene = valid;
    invalidScene["scenes"][0]["crop"] = "invalid";
    if (!expectRuntimeError(
            [&invalidScene]()
            {
                (void)DesktopSceneEngine::Infrastructure::Json::fromJson(invalidScene);
            },
            "Invalid nested scene structure should be rejected"))
    {
        return false;
    }

    nlohmann::json malformedStructure = valid;
    malformedStructure["playlists"] = nlohmann::json::array({nlohmann::json::array()});
    return expectRuntimeError(
        [&malformedStructure]()
        {
            (void)DesktopSceneEngine::Infrastructure::Json::fromJson(malformedStructure);
        },
        "Malformed nested structure should be rejected");
}

}

int main()
{
    return verifyRoundTrip() && verifyInvalidDocuments() ? 0 : 1;
}
