#include "ProjectJson.h"

#include <stdexcept>
#include <vector>

namespace DesktopSceneEngine
{
namespace Infrastructure
{
namespace Json
{
namespace
{

using ::DesktopSceneEngine::Domain::Crop;
using ::DesktopSceneEngine::Domain::EntityId;
using ::DesktopSceneEngine::Domain::Layout;
using ::DesktopSceneEngine::Domain::Library;
using ::DesktopSceneEngine::Domain::Media;
using ::DesktopSceneEngine::Domain::Playback;
using ::DesktopSceneEngine::Domain::Playlist;
using ::DesktopSceneEngine::Domain::Scene;

nlohmann::json toJson(const EntityId& value)
{
    return nlohmann::json{
        {"id", value.value()}
    };
}

nlohmann::json toJson(const Crop& value)
{
    return nlohmann::json{
        {"x", value.x},
        {"y", value.y},
        {"width", value.width},
        {"height", value.height}
    };
}

nlohmann::json toJson(const Layout& value)
{
    return nlohmann::json{
        {"positionX", value.positionX},
        {"positionY", value.positionY},
        {"scaleX", value.scaleX},
        {"scaleY", value.scaleY}
    };
}

nlohmann::json toJson(const Playback& value)
{
    return nlohmann::json{
        {"loop", value.loop},
        {"speed", value.speed},
        {"volume", value.volume}
    };
}

EntityId fromEntityId(const nlohmann::json& document)
{
    return EntityId{
        document.at("id").get<std::string>()
    };
}

Crop fromCrop(const nlohmann::json& document)
{
    return Crop{
        document.at("x").get<float>(),
        document.at("y").get<float>(),
        document.at("width").get<float>(),
        document.at("height").get<float>()
    };
}

Layout fromLayout(const nlohmann::json& document)
{
    return Layout{
        document.at("positionX").get<float>(),
        document.at("positionY").get<float>(),
        document.at("scaleX").get<float>(),
        document.at("scaleY").get<float>()
    };
}

Playback fromPlayback(const nlohmann::json& document)
{
    return Playback{
        document.at("loop").get<bool>(),
        document.at("speed").get<float>(),
        document.at("volume").get<float>()
    };
}

nlohmann::json toJson(const Media& value)
{
    return nlohmann::json{
        {"id", toJson(value.id)},
        {"name", value.name},
        {"resourceReference", value.resourceReference}
    };
}

Media fromMedia(const nlohmann::json& document)
{
    return Media{
        fromEntityId(document.at("id")),
        document.at("name").get<std::string>(),
        document.at("resourceReference").get<std::string>()
    };
}

nlohmann::json toJson(const Scene& value)
{
    return nlohmann::json{
        {"id", toJson(value.id)},
        {"mediaId", toJson(value.mediaId)},
        {"crop", toJson(value.crop)},
        {"layout", toJson(value.layout)},
        {"playback", toJson(value.playback)},
        {"duration", value.duration}
    };
}

Scene fromScene(const nlohmann::json& document)
{
    return Scene{
        fromEntityId(document.at("id")),
        fromEntityId(document.at("mediaId")),
        fromCrop(document.at("crop")),
        fromLayout(document.at("layout")),
        fromPlayback(document.at("playback")),
        document.value("duration", 0.0F)
    };
}

nlohmann::json toJson(const Playlist& value)
{
    nlohmann::json sceneIds = nlohmann::json::array();

    for (const EntityId& sceneId : value.sceneIds)
    {
        sceneIds.push_back(toJson(sceneId));
    }

    return nlohmann::json{
        {"id", toJson(value.id)},
        {"sceneIds", sceneIds}
    };
}

Playlist fromPlaylist(const nlohmann::json& document)
{
    if (!document.contains("sceneIds") || !document.at("sceneIds").is_array())
    {
        throw std::runtime_error("Playlist JSON sceneIds must be an array.");
    }

    std::vector<EntityId> sceneIds{};

    for (const nlohmann::json& sceneId : document.at("sceneIds"))
    {
        sceneIds.push_back(fromEntityId(sceneId));
    }

    return Playlist{
        fromEntityId(document.at("id")),
        sceneIds
    };
}

nlohmann::json toJson(const Library& value)
{
    nlohmann::json media = nlohmann::json::array();

    for (const Media& item : value.media)
    {
        media.push_back(toJson(item));
    }

    return nlohmann::json{
        {"media", media}
    };
}

Library fromLibrary(const nlohmann::json& document)
{
    if (!document.contains("media") || !document.at("media").is_array())
    {
        throw std::runtime_error("Library JSON media must be an array.");
    }

    Library library{};

    for (const nlohmann::json& media : document.at("media"))
    {
        library.media.push_back(fromMedia(media));
    }

    return library;
}

}

nlohmann::json toJson(const Domain::Project& project)
{
    nlohmann::json scenes = nlohmann::json::array();

    for (const Domain::Scene& scene : project.scenes)
    {
        scenes.push_back(toJson(scene));
    }

    nlohmann::json playlists = nlohmann::json::array();

    for (const Domain::Playlist& playlist : project.playlists)
    {
        playlists.push_back(toJson(playlist));
    }

    return nlohmann::json{
        {"version", "1.0"},
        {"library", toJson(project.library)},
        {"scenes", scenes},
        {"playlists", playlists}
    };
}

Domain::Project fromJson(const nlohmann::json& document)
{
    if (!document.is_object())
    {
        throw std::runtime_error("Project JSON root must be an object.");
    }

    if (!document.contains("version"))
    {
        throw std::runtime_error("Project JSON is missing the version field.");
    }

    const nlohmann::json& version = document.at("version");

    if (!version.is_string() || version.get<std::string>() != "1.0")
    {
        throw std::runtime_error("Unsupported Project JSON version.");
    }

    if (!document.contains("library") || !document.at("library").is_object())
    {
        throw std::runtime_error("Project JSON library must be an object.");
    }

    if (!document.contains("scenes") || !document.at("scenes").is_array())
    {
        throw std::runtime_error("Project JSON scenes must be an array.");
    }

    if (!document.contains("playlists") || !document.at("playlists").is_array())
    {
        throw std::runtime_error("Project JSON playlists must be an array.");
    }

    try
    {
        std::vector<Domain::Scene> scenes{};

        for (const nlohmann::json& scene : document.at("scenes"))
        {
            scenes.push_back(fromScene(scene));
        }

        std::vector<Domain::Playlist> playlists{};

        for (const nlohmann::json& playlist : document.at("playlists"))
        {
            playlists.push_back(fromPlaylist(playlist));
        }

        return Domain::Project{
            fromLibrary(document.at("library")),
            scenes,
            playlists
        };
    }
    catch (const nlohmann::json::exception& error)
    {
        throw std::runtime_error{
            std::string{"Invalid Project JSON: "} + error.what()
        };
    }
}

}
}
}
