#include "AuthoredProjectJson.h"

#include "AuthoredProjectValidation.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace DesktopSceneEngine::Infrastructure::Json
{
namespace
{

[[noreturn]] void failJson(const std::string& path, const char* message)
{
    throw std::runtime_error{
        "Invalid Authored Project JSON at " + path + ": " + message};
}

std::string childPath(const std::string& parent, const char* child)
{
    return parent.empty() ? std::string{child} : parent + "." + child;
}

const nlohmann::json& requireObject(
    const nlohmann::json& value,
    const std::string& path)
{
    if (!value.is_object())
    {
        failJson(path, "must be an object.");
    }

    return value;
}

const nlohmann::json& requireArray(
    const nlohmann::json& value,
    const std::string& path)
{
    if (!value.is_array())
    {
        failJson(path, "must be an array.");
    }

    return value;
}

const nlohmann::json& requireField(
    const nlohmann::json& object,
    const char* field,
    const std::string& parentPath)
{
    requireObject(object, parentPath.empty() ? "root" : parentPath);
    if (!object.contains(field))
    {
        failJson(childPath(parentPath, field), "is required.");
    }

    return object.at(field);
}

std::string readString(
    const nlohmann::json& value,
    const std::string& path)
{
    if (!value.is_string())
    {
        failJson(path, "must be a string.");
    }

    return value.get<std::string>();
}

bool readBoolean(const nlohmann::json& value, const std::string& path)
{
    if (!value.is_boolean())
    {
        failJson(path, "must be a boolean.");
    }

    return value.get<bool>();
}

double readDouble(const nlohmann::json& value, const std::string& path)
{
    if (!value.is_number())
    {
        failJson(path, "must be a number.");
    }

    return value.get<double>();
}

float readFloat(const nlohmann::json& value, const std::string& path)
{
    const double number = readDouble(value, path);
    constexpr double FloatMaximum =
        static_cast<double>(std::numeric_limits<float>::max());
    if (number < -FloatMaximum || number > FloatMaximum)
    {
        failJson(path, "is outside the supported floating-point range.");
    }

    return static_cast<float>(number);
}

int readPositiveInt(const nlohmann::json& value, const std::string& path)
{
    if (!value.is_number_integer() && !value.is_number_unsigned())
    {
        failJson(path, "must be an integer.");
    }

    if (value.is_number_unsigned())
    {
        const auto number = value.get<unsigned long long>();
        if (number == 0ULL
            || number > static_cast<unsigned long long>(
                std::numeric_limits<int>::max()))
        {
            failJson(path, "must be positive and representable as int.");
        }

        return static_cast<int>(number);
    }

    const auto number = value.get<long long>();
    if (number <= 0LL
        || number > static_cast<long long>(std::numeric_limits<int>::max()))
    {
        failJson(path, "must be positive and representable as int.");
    }

    return static_cast<int>(number);
}

const char* playbackSequenceModeText(Domain::PlaybackSequenceMode mode)
{
    switch (mode)
    {
    case Domain::PlaybackSequenceMode::ListLoop:
        return "listLoop";
    case Domain::PlaybackSequenceMode::Random:
        return "random";
    case Domain::PlaybackSequenceMode::RepeatCurrent:
        return "repeatCurrent";
    }

    throw std::runtime_error{"Invalid authored playback sequence mode."};
}

Domain::PlaybackSequenceMode readPlaybackSequenceMode(
    const nlohmann::json& value,
    const std::string& path)
{
    const std::string text = readString(value, path);
    if (text == "listLoop")
    {
        return Domain::PlaybackSequenceMode::ListLoop;
    }
    if (text == "random")
    {
        return Domain::PlaybackSequenceMode::Random;
    }
    if (text == "repeatCurrent")
    {
        return Domain::PlaybackSequenceMode::RepeatCurrent;
    }

    failJson(path, "contains an unknown playback sequence mode.");
}

const char* mediaTypeText(Domain::AuthoredMediaType mediaType)
{
    switch (mediaType)
    {
    case Domain::AuthoredMediaType::Image:
        return "image";
    case Domain::AuthoredMediaType::Video:
        return "video";
    }

    throw std::runtime_error{"Invalid authored media type."};
}

Domain::AuthoredMediaType readMediaType(
    const nlohmann::json& value,
    const std::string& path)
{
    const std::string text = readString(value, path);
    if (text == "image")
    {
        return Domain::AuthoredMediaType::Image;
    }
    if (text == "video")
    {
        return Domain::AuthoredMediaType::Video;
    }

    failJson(path, "contains an unknown media type.");
}

const char* presentationModeText(Domain::MediaPresentationMode mode)
{
    switch (mode)
    {
    case Domain::MediaPresentationMode::Native:
        return "native";
    case Domain::MediaPresentationMode::AutoFill:
        return "autoFill";
    case Domain::MediaPresentationMode::Custom:
        return "custom";
    }

    throw std::runtime_error{"Invalid authored media presentation mode."};
}

Domain::MediaPresentationMode readPresentationMode(
    const nlohmann::json& value,
    const std::string& path)
{
    const std::string text = readString(value, path);
    if (text == "native")
    {
        return Domain::MediaPresentationMode::Native;
    }
    if (text == "autoFill")
    {
        return Domain::MediaPresentationMode::AutoFill;
    }
    if (text == "custom")
    {
        return Domain::MediaPresentationMode::Custom;
    }

    failJson(path, "contains an unknown presentation mode.");
}

const char* musicPlaybackModeText(Domain::MusicPlaybackMode mode)
{
    switch (mode)
    {
    case Domain::MusicPlaybackMode::CurrentSingleLoop:
        return "currentSingleLoop";
    case Domain::MusicPlaybackMode::ListLoop:
        return "listLoop";
    case Domain::MusicPlaybackMode::Shuffle:
        return "shuffle";
    }

    throw std::runtime_error{"Invalid authored music playback mode."};
}

Domain::MusicPlaybackMode readMusicPlaybackMode(
    const nlohmann::json& value,
    const std::string& path)
{
    const std::string text = readString(value, path);
    if (text == "currentSingleLoop")
    {
        return Domain::MusicPlaybackMode::CurrentSingleLoop;
    }
    if (text == "listLoop")
    {
        return Domain::MusicPlaybackMode::ListLoop;
    }
    if (text == "shuffle")
    {
        return Domain::MusicPlaybackMode::Shuffle;
    }

    failJson(path, "contains an unknown music playback mode.");
}

nlohmann::json toJson(const Domain::AspectRatio& aspectRatio)
{
    return nlohmann::json{
        {"numerator", aspectRatio.numerator},
        {"denominator", aspectRatio.denominator}};
}

Domain::AspectRatio readAspectRatio(
    const nlohmann::json& document,
    const std::string& path)
{
    requireObject(document, path);
    return Domain::AspectRatio{
        readPositiveInt(
            requireField(document, "numerator", path),
            childPath(path, "numerator")),
        readPositiveInt(
            requireField(document, "denominator", path),
            childPath(path, "denominator"))};
}

nlohmann::json toJson(const Domain::BackgroundMusicTrack& track)
{
    return nlohmann::json{
        {"id", track.id.value()},
        {"source", track.source},
        {"displayAlias", track.displayAlias},
        {"volume", track.volume}};
}

Domain::BackgroundMusicTrack readTrack(
    const nlohmann::json& document,
    const std::string& path)
{
    requireObject(document, path);
    return Domain::BackgroundMusicTrack{
        Domain::TrackId{readString(
            requireField(document, "id", path),
            childPath(path, "id"))},
        readString(
            requireField(document, "source", path),
            childPath(path, "source")),
        readString(
            requireField(document, "displayAlias", path),
            childPath(path, "displayAlias")),
        readDouble(
            requireField(document, "volume", path),
            childPath(path, "volume"))};
}

nlohmann::json toJson(const Domain::BackgroundMusicQueue& queue)
{
    nlohmann::json tracks = nlohmann::json::array();
    for (const Domain::BackgroundMusicTrack& track : queue.tracks)
    {
        tracks.push_back(toJson(track));
    }

    return nlohmann::json{
        {"enabled", queue.enabled},
        {"mode", musicPlaybackModeText(queue.playbackMode)},
        {"volume", queue.volume},
        {"tracks", tracks}};
}

Domain::BackgroundMusicQueue readQueue(
    const nlohmann::json& document,
    const std::string& path)
{
    requireObject(document, path);
    Domain::BackgroundMusicQueue queue{};
    if (document.contains("enabled"))
    {
        queue.enabled = readBoolean(
            document.at("enabled"),
            childPath(path, "enabled"));
    }

    queue.playbackMode = readMusicPlaybackMode(
        requireField(document, "mode", path),
        childPath(path, "mode"));
    queue.volume = readDouble(
        requireField(document, "volume", path),
        childPath(path, "volume"));

    const nlohmann::json& tracks = requireArray(
        requireField(document, "tracks", path),
        childPath(path, "tracks"));
    queue.tracks.reserve(tracks.size());
    for (std::size_t index = 0U; index < tracks.size(); ++index)
    {
        queue.tracks.push_back(readTrack(
            tracks[index],
            childPath(path, "tracks") + "[" + std::to_string(index) + "]"));
    }

    return queue;
}

nlohmann::json toJson(const Domain::AuthoredItemPresentation& presentation)
{
    return nlohmann::json{
        {"crop", {
            {"x", presentation.crop.x},
            {"y", presentation.crop.y},
            {"width", presentation.crop.width},
            {"height", presentation.crop.height}}},
        {"layout", {
            {"positionX", presentation.layout.positionX},
            {"positionY", presentation.layout.positionY},
            {"scaleX", presentation.layout.scaleX},
            {"scaleY", presentation.layout.scaleY}}},
        {"mediaPresentation", {
            {"mode", presentationModeText(
                presentation.mediaPresentation.mode)},
            {"customAspect", toJson(
                presentation.mediaPresentation.customAspect)}}}};
}

Domain::AuthoredItemPresentation readPresentation(
    const nlohmann::json& document,
    const std::string& path)
{
    requireObject(document, path);
    const std::string cropPath = childPath(path, "crop");
    const nlohmann::json& crop = requireObject(
        requireField(document, "crop", path), cropPath);
    const std::string layoutPath = childPath(path, "layout");
    const nlohmann::json& layout = requireObject(
        requireField(document, "layout", path), layoutPath);
    const std::string mediaPresentationPath =
        childPath(path, "mediaPresentation");
    const nlohmann::json& mediaPresentation = requireObject(
        requireField(document, "mediaPresentation", path),
        mediaPresentationPath);

    return Domain::AuthoredItemPresentation{
        Domain::Crop{
            readFloat(requireField(crop, "x", cropPath), childPath(cropPath, "x")),
            readFloat(requireField(crop, "y", cropPath), childPath(cropPath, "y")),
            readFloat(requireField(crop, "width", cropPath), childPath(cropPath, "width")),
            readFloat(requireField(crop, "height", cropPath), childPath(cropPath, "height"))},
        Domain::Layout{
            readFloat(requireField(layout, "positionX", layoutPath), childPath(layoutPath, "positionX")),
            readFloat(requireField(layout, "positionY", layoutPath), childPath(layoutPath, "positionY")),
            readFloat(requireField(layout, "scaleX", layoutPath), childPath(layoutPath, "scaleX")),
            readFloat(requireField(layout, "scaleY", layoutPath), childPath(layoutPath, "scaleY"))},
        Domain::MediaPresentation{
            readPresentationMode(
                requireField(mediaPresentation, "mode", mediaPresentationPath),
                childPath(mediaPresentationPath, "mode")),
            readAspectRatio(
                requireField(mediaPresentation, "customAspect", mediaPresentationPath),
                childPath(mediaPresentationPath, "customAspect"))}};
}

nlohmann::json toJson(const Domain::AuthoredItem& item)
{
    return nlohmann::json{
        {"id", item.id.value()},
        {"displayAlias", item.displayAlias},
        {"enabled", item.enabled},
        {"mediaType", mediaTypeText(item.mediaType)},
        {"source", item.source},
        {"imageDurationSeconds", item.imageDurationSeconds},
        {"videoPlaybackRate", item.videoPlaybackRate},
        {"presentation", toJson(item.presentation)},
        {"itemMediaVolume", item.itemMediaVolume},
        {"backgroundMusic", toJson(item.musicQueue)}};
}

Domain::AuthoredItem readItem(
    const nlohmann::json& document,
    const std::string& path)
{
    requireObject(document, path);
    Domain::AuthoredItem item{};
    item.id = Domain::ItemId{readString(
        requireField(document, "id", path), childPath(path, "id"))};
    item.displayAlias = readString(
        requireField(document, "displayAlias", path),
        childPath(path, "displayAlias"));
    item.enabled = readBoolean(
        requireField(document, "enabled", path), childPath(path, "enabled"));
    item.mediaType = readMediaType(
        requireField(document, "mediaType", path),
        childPath(path, "mediaType"));
    item.source = readString(
        requireField(document, "source", path), childPath(path, "source"));
    item.imageDurationSeconds = readDouble(
        requireField(document, "imageDurationSeconds", path),
        childPath(path, "imageDurationSeconds"));
    item.videoPlaybackRate = readDouble(
        requireField(document, "videoPlaybackRate", path),
        childPath(path, "videoPlaybackRate"));
    item.presentation = readPresentation(
        requireField(document, "presentation", path),
        childPath(path, "presentation"));
    item.itemMediaVolume = readDouble(
        requireField(document, "itemMediaVolume", path),
        childPath(path, "itemMediaVolume"));
    item.musicQueue = readQueue(
        requireField(document, "backgroundMusic", path),
        childPath(path, "backgroundMusic"));
    return item;
}

nlohmann::json toJson(const Domain::AuthoredPlaylist& playlist)
{
    nlohmann::json items = nlohmann::json::array();
    for (const Domain::AuthoredItem& item : playlist.items)
    {
        items.push_back(toJson(item));
    }

    return nlohmann::json{
        {"id", playlist.id.value()},
        {"displayAlias", playlist.displayAlias},
        {"enabled", playlist.enabled},
        {"masterVolume", playlist.masterVolume},
        {"itemPlaybackMode", playbackSequenceModeText(
            playlist.itemPlaybackMode)},
        {"backgroundMusic", toJson(playlist.globalMusicQueue)},
        {"items", items}};
}

Domain::AuthoredPlaylist readPlaylist(
    const nlohmann::json& document,
    const std::string& path)
{
    requireObject(document, path);
    Domain::AuthoredPlaylist playlist{};
    playlist.id = Domain::PlaylistId{readString(
        requireField(document, "id", path), childPath(path, "id"))};
    playlist.displayAlias = readString(
        requireField(document, "displayAlias", path),
        childPath(path, "displayAlias"));
    playlist.enabled = readBoolean(
        requireField(document, "enabled", path), childPath(path, "enabled"));
    playlist.masterVolume = readDouble(
        requireField(document, "masterVolume", path),
        childPath(path, "masterVolume"));
    playlist.itemPlaybackMode = readPlaybackSequenceMode(
        requireField(document, "itemPlaybackMode", path),
        childPath(path, "itemPlaybackMode"));
    playlist.globalMusicQueue = readQueue(
        requireField(document, "backgroundMusic", path),
        childPath(path, "backgroundMusic"));

    const nlohmann::json& items = requireArray(
        requireField(document, "items", path), childPath(path, "items"));
    playlist.items.reserve(items.size());
    for (std::size_t index = 0U; index < items.size(); ++index)
    {
        playlist.items.push_back(readItem(
            items[index],
            childPath(path, "items") + "[" + std::to_string(index) + "]"));
    }

    return playlist;
}

}

nlohmann::json toAuthoredProjectJson(const Domain::AuthoredProject& project)
{
    validateAuthoredProject(project);

    nlohmann::json playlists = nlohmann::json::array();
    for (const Domain::AuthoredPlaylist& playlist : project.playlists)
    {
        playlists.push_back(toJson(playlist));
    }

    return nlohmann::json{
        {"type", AuthoredProjectDocumentType},
        {"version", AuthoredProjectDocumentVersion},
        {"displayOwner", *project.displayOwner.displayId().canonicalRepresentation()},
        {"desktopCanvas", {
            {"aspectRatio", toJson(project.desktopCanvas.aspectRatio)}}},
        {"playlistPlaybackMode", playbackSequenceModeText(
            project.playlistPlaybackMode)},
        {"playlists", playlists}};
}

Domain::AuthoredProject fromAuthoredProjectJson(
    const nlohmann::json& document)
{
    try
    {
        requireObject(document, "root");
        const std::string type = readString(
            requireField(document, "type", ""), "type");
        if (type != AuthoredProjectDocumentType)
        {
            failJson("type", "must be DesktopSceneEngineProject.");
        }

        const std::string version = readString(
            requireField(document, "version", ""), "version");
        if (version != AuthoredProjectDocumentVersion)
        {
            failJson("version", "must be 3.0.");
        }

        const std::string displayOwnerText = readString(
            requireField(document, "displayOwner", ""), "displayOwner");
        const auto displayId = DisplayId::parseCanonicalRepresentation(
            displayOwnerText);
        if (!displayId.has_value())
        {
            failJson(
                "displayOwner",
                "must be a canonical DisplayId representation.");
        }
        auto displayOwner = Domain::DisplayOwner::fromDisplayId(*displayId);
        if (!displayOwner.has_value())
        {
            failJson(
                "displayOwner",
                "must identify a persistent-owner-capable display.");
        }

        const std::string canvasPath = "desktopCanvas";
        const nlohmann::json& canvas = requireObject(
            requireField(document, "desktopCanvas", ""), canvasPath);

        Domain::AuthoredProject project{std::move(*displayOwner)};
        project.desktopCanvas.aspectRatio = readAspectRatio(
            requireField(canvas, "aspectRatio", canvasPath),
            childPath(canvasPath, "aspectRatio"));
        project.playlistPlaybackMode = readPlaybackSequenceMode(
            requireField(document, "playlistPlaybackMode", ""),
            "playlistPlaybackMode");

        const nlohmann::json& playlists = requireArray(
            requireField(document, "playlists", ""), "playlists");
        project.playlists.reserve(playlists.size());
        for (std::size_t index = 0U; index < playlists.size(); ++index)
        {
            project.playlists.push_back(readPlaylist(
                playlists[index],
                "playlists[" + std::to_string(index) + "]"));
        }

        validateAuthoredProject(project);
        return project;
    }
    catch (const nlohmann::json::exception& error)
    {
        throw std::runtime_error{
            std::string{"Invalid Authored Project JSON: "} + error.what()};
    }
}

}
