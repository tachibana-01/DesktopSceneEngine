#include "Infrastructure/Json/AuthoredProjectJson.h"
#include "Infrastructure/Json/AuthoredProjectValidation.h"

#include <DesktopSceneEngine/Domain/AuthoredProject.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{

using DesktopSceneEngine::Domain::AspectRatio;
using DesktopSceneEngine::Domain::AuthoredItem;
using DesktopSceneEngine::Domain::AuthoredMediaType;
using DesktopSceneEngine::Domain::AuthoredPlaylist;
using DesktopSceneEngine::Domain::AuthoredProject;
using DesktopSceneEngine::Domain::BackgroundMusicQueue;
using DesktopSceneEngine::Domain::BackgroundMusicTrack;
using DesktopSceneEngine::Domain::ItemId;
using DesktopSceneEngine::Domain::MediaPresentationMode;
using DesktopSceneEngine::Domain::MusicPlaybackMode;
using DesktopSceneEngine::Domain::PlaybackSequenceMode;
using DesktopSceneEngine::Domain::PlaylistId;
using DesktopSceneEngine::Domain::TrackId;

constexpr const char* PlaylistOneId = "11111111-1111-1111-1111-111111111111";
constexpr const char* PlaylistTwoId = "22222222-2222-2222-2222-222222222222";
constexpr const char* ImageItemId = "33333333-3333-3333-3333-333333333333";
constexpr const char* VideoItemId = "44444444-4444-4444-4444-444444444444";
constexpr const char* CustomItemId = "55555555-5555-5555-5555-555555555555";
constexpr const char* TrackOneId = "66666666-6666-6666-6666-666666666666";
constexpr const char* TrackTwoId = "77777777-7777-7777-7777-777777777777";
constexpr const char* TrackThreeId = "88888888-8888-8888-8888-888888888888";

DesktopSceneEngine::Domain::DisplayOwner testDisplayOwner()
{
    return *DesktopSceneEngine::Domain::DisplayOwner::fromDisplayId(
        DesktopSceneEngine::DisplayId::fromTargetDevicePath(
            "\\\\?\\DISPLAY#AUTHORED_JSON_TEST#DISPLAY_1"));
}

template <std::size_t Size>
std::string utf8(const char8_t (&value)[Size])
{
    return std::string{
        reinterpret_cast<const char*>(value),
        Size - 1U};
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

template <typename Function>
bool expectRuntimeErrorContaining(
    Function&& function,
    const char* expectedText,
    const char* message)
{
    try
    {
        function();
    }
    catch (const std::runtime_error& error)
    {
        if (std::string{error.what()}.find(expectedText) != std::string::npos)
        {
            return true;
        }

        std::cerr << "Test failed: " << message
                  << " (actual error: " << error.what() << ')' << std::endl;
        return false;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

template <typename Mutation>
bool expectMutationRejected(
    const nlohmann::json& valid,
    Mutation&& mutation,
    const char* message)
{
    nlohmann::json invalid = valid;
    mutation(invalid);
    return expectRuntimeError(
        [&invalid]()
        {
            (void)DesktopSceneEngine::Infrastructure::Json::
                fromAuthoredProjectJson(invalid);
        },
        message);
}

bool nearlyEqual(double actual, double expected) noexcept
{
    constexpr double Tolerance = 0.000001;
    return std::fabs(actual - expected) <= Tolerance;
}

bool sameAspect(const AspectRatio& left, const AspectRatio& right) noexcept
{
    return left.numerator == right.numerator
        && left.denominator == right.denominator;
}

bool sameTrack(
    const BackgroundMusicTrack& left,
    const BackgroundMusicTrack& right) noexcept
{
    return left.id == right.id
        && left.source == right.source
        && left.displayAlias == right.displayAlias
        && nearlyEqual(left.volume, right.volume);
}

bool sameQueue(
    const BackgroundMusicQueue& left,
    const BackgroundMusicQueue& right) noexcept
{
    if (left.enabled != right.enabled
        || left.playbackMode != right.playbackMode
        || !nearlyEqual(left.volume, right.volume)
        || left.tracks.size() != right.tracks.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < left.tracks.size(); ++index)
    {
        if (!sameTrack(left.tracks[index], right.tracks[index]))
        {
            return false;
        }
    }

    return true;
}

bool sameItem(const AuthoredItem& left, const AuthoredItem& right) noexcept
{
    return left.id == right.id
        && left.displayAlias == right.displayAlias
        && left.mediaType == right.mediaType
        && left.source == right.source
        && nearlyEqual(left.imageDurationSeconds, right.imageDurationSeconds)
        && nearlyEqual(left.videoPlaybackRate, right.videoPlaybackRate)
        && nearlyEqual(left.presentation.crop.x, right.presentation.crop.x)
        && nearlyEqual(left.presentation.crop.y, right.presentation.crop.y)
        && nearlyEqual(left.presentation.crop.width, right.presentation.crop.width)
        && nearlyEqual(left.presentation.crop.height, right.presentation.crop.height)
        && nearlyEqual(left.presentation.layout.positionX, right.presentation.layout.positionX)
        && nearlyEqual(left.presentation.layout.positionY, right.presentation.layout.positionY)
        && nearlyEqual(left.presentation.layout.scaleX, right.presentation.layout.scaleX)
        && nearlyEqual(left.presentation.layout.scaleY, right.presentation.layout.scaleY)
        && left.presentation.mediaPresentation.mode
            == right.presentation.mediaPresentation.mode
        && sameAspect(
            left.presentation.mediaPresentation.customAspect,
            right.presentation.mediaPresentation.customAspect)
        && nearlyEqual(left.itemMediaVolume, right.itemMediaVolume)
        && sameQueue(left.musicQueue, right.musicQueue)
        && left.enabled == right.enabled;
}

bool samePlaylist(
    const AuthoredPlaylist& left,
    const AuthoredPlaylist& right) noexcept
{
    if (left.id != right.id
        || left.displayAlias != right.displayAlias
        || !nearlyEqual(left.masterVolume, right.masterVolume)
        || !sameQueue(left.globalMusicQueue, right.globalMusicQueue)
        || left.itemPlaybackMode != right.itemPlaybackMode
        || left.enabled != right.enabled
        || left.items.size() != right.items.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < left.items.size(); ++index)
    {
        if (!sameItem(left.items[index], right.items[index]))
        {
            return false;
        }
    }

    return true;
}

bool sameProject(
    const AuthoredProject& left,
    const AuthoredProject& right) noexcept
{
    if (left.displayOwner != right.displayOwner
        || !sameAspect(left.desktopCanvas.aspectRatio, right.desktopCanvas.aspectRatio)
        || left.playlistPlaybackMode != right.playlistPlaybackMode
        || left.playlists.size() != right.playlists.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < left.playlists.size(); ++index)
    {
        if (!samePlaylist(left.playlists[index], right.playlists[index]))
        {
            return false;
        }
    }

    return true;
}

AuthoredProject makeRichProject()
{
    BackgroundMusicQueue playlistMusic{};
    playlistMusic.enabled = false;
    playlistMusic.playbackMode = MusicPlaybackMode::CurrentSingleLoop;
    playlistMusic.volume = 0.65;
    playlistMusic.tracks = {
        {TrackId{std::string{TrackOneId}},
            utf8(u8"C:/\u97f3\u4e50/\u540c\u4e00\u9996\u6b4c\U0001f600.m4a"),
            utf8(u8"\u80cc\u666f\u97f3\u697d\u4e00"),
            0.75},
        {TrackId{std::string{TrackTwoId}},
            utf8(u8"C:/\u97f3\u4e50/\u540c\u4e00\u9996\u6b4c\U0001f600.m4a"),
            utf8(u8"\u80cc\u666f\u97f3\u697d\u4e8c"),
            0.45}};

    AuthoredItem image{};
    image.id = ItemId{std::string{ImageItemId}};
    image.displayAlias = utf8(u8"\u6668\u66e6\u58c1\u7eb8\U0001f600");
    image.mediaType = AuthoredMediaType::Image;
    image.source = utf8(
        u8"C:/\u5a92\u4f53/\u671d\u65e5_\u65e5\u672c\u8a9e\U0001f600.png");
    image.imageDurationSeconds = 17.5;
    image.videoPlaybackRate = 1.1;
    image.presentation.crop = {0.1F, 0.2F, 0.7F, 0.6F};
    image.presentation.layout = {0.25F, -0.35F, 1.2F, 0.8F};
    image.presentation.mediaPresentation.mode = MediaPresentationMode::Native;
    image.presentation.mediaPresentation.customAspect = {4, 3};
    image.itemMediaVolume = 0.8;
    image.musicQueue.playbackMode = MusicPlaybackMode::Shuffle;
    image.musicQueue.volume = 0.4;
    image.musicQueue.tracks = {
        {TrackId{std::string{TrackThreeId}},
            utf8(u8"C:/\u97f3\u58f0/\u9805\u76ee.ogg"),
            utf8(u8"\u9805\u76ee\u97f3\u697d"),
            0.55}};

    AuthoredItem video{};
    video.id = ItemId{std::string{VideoItemId}};
    video.displayAlias = utf8(u8"\u591c\u666f\u30d3\u30c7\u30aa");
    video.mediaType = AuthoredMediaType::Video;
    video.source = utf8(u8"C:/\u52d5\u753b/\u591c\u666f.mp4");
    video.imageDurationSeconds = 12.25;
    video.videoPlaybackRate = 1.7;
    video.presentation.mediaPresentation.mode = MediaPresentationMode::AutoFill;
    video.itemMediaVolume = 0.35;
    video.enabled = false;

    AuthoredPlaylist first{};
    first.id = PlaylistId{std::string{PlaylistOneId}};
    first.displayAlias = utf8(u8"\u4e3b\u64ad\u653e\u5217\u8868");
    first.items = {image, video};
    first.masterVolume = 0.7;
    first.globalMusicQueue = playlistMusic;
    first.itemPlaybackMode = PlaybackSequenceMode::Random;

    AuthoredItem custom{};
    custom.id = ItemId{std::string{CustomItemId}};
    custom.displayAlias = "Custom";
    custom.source = "C:/media/custom.webp";
    custom.presentation.mediaPresentation.mode = MediaPresentationMode::Custom;
    custom.presentation.mediaPresentation.customAspect = {21, 9};

    AuthoredPlaylist second{};
    second.id = PlaylistId{std::string{PlaylistTwoId}};
    second.displayAlias = "Secondary";
    second.items = {custom};
    second.masterVolume = 0.25;
    second.globalMusicQueue.playbackMode = MusicPlaybackMode::Shuffle;
    second.itemPlaybackMode = PlaybackSequenceMode::RepeatCurrent;
    second.enabled = false;

    AuthoredProject project{testDisplayOwner()};
    project.desktopCanvas.aspectRatio = {32, 18};
    project.playlistPlaybackMode = PlaybackSequenceMode::Random;
    project.playlists = {first, second};
    return project;
}

bool verifyRichRoundTrip()
{
    const AuthoredProject original = makeRichProject();
    const nlohmann::json document =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(original);
    const AuthoredProject restored =
        DesktopSceneEngine::Infrastructure::Json::fromAuthoredProjectJson(document);

    return require(document.at("type") == "DesktopSceneEngineProject",
               "v3 root type must be explicit")
        && require(document.at("version") == "3.0",
            "v3 root version must be 3.0")
        && require(document.at("displayOwner")
                == *original.displayOwner.displayId().canonicalRepresentation(),
            "v3 writer must emit the canonical DisplayOwner identity")
        && require(document.at("desktopCanvas").at("aspectRatio").at("numerator") == 32,
            "Canvas numerator must preserve its exact authored value")
        && require(document.at("playlists").at(0).at("backgroundMusic").contains("enabled"),
            "writer must emit BGM enabled")
        && require(document.at("playlistPlaybackMode") == "random",
            "project playback mode must use its stable string")
        && require(document.at("playlists").at(0).at("itemPlaybackMode") == "random"
                && document.at("playlists").at(1).at("itemPlaybackMode") == "repeatCurrent",
            "playlist playback modes must use stable strings")
        && require(document.at("playlists").at(0).at("items").at(0).at("mediaType") == "image"
                && document.at("playlists").at(0).at("items").at(1).at("mediaType") == "video",
            "media types must use stable strings")
        && require(document.at("playlists").at(0).at("items").at(0)
                .at("presentation").at("mediaPresentation").at("mode") == "native"
                && document.at("playlists").at(0).at("items").at(1)
                .at("presentation").at("mediaPresentation").at("mode") == "autoFill"
                && document.at("playlists").at(1).at("items").at(0)
                .at("presentation").at("mediaPresentation").at("mode") == "custom",
            "presentation modes must use stable strings")
        && require(document.at("playlists").at(0).at("backgroundMusic").at("mode")
                == "currentSingleLoop"
                && document.at("playlists").at(0).at("items").at(0)
                .at("backgroundMusic").at("mode") == "shuffle"
                && document.at("playlists").at(0).at("items").at(1)
                .at("backgroundMusic").at("mode") == "listLoop",
            "BGM modes must use stable strings")
        && require(document.at("playlists").at(0).at("items").at(0)
                .contains("imageDurationSeconds")
                && document.at("playlists").at(0).at("items").at(0)
                .contains("videoPlaybackRate")
                && document.at("playlists").at(0).at("items").at(0)
                .contains("itemMediaVolume"),
            "writer must emit every authored Item numeric field")
        && require(document.at("playlists").at(0).at("items").at(0)
                .at("presentation").at("mediaPresentation").contains("customAspect"),
            "writer must emit customAspect for every presentation mode")
        && require(sameProject(original, restored),
            "rich AuthoredProject must survive JSON v3 round-trip")
        && require(restored.playlists[0].id.value() == PlaylistOneId,
            "PlaylistId must remain exact")
        && require(restored.playlists[0].items[0].id.value() == ImageItemId,
            "ItemId must remain exact")
        && require(restored.playlists[0].globalMusicQueue.tracks[0].id.value() == TrackOneId,
            "TrackId must remain exact")
        && require(restored.playlists[0].globalMusicQueue.tracks[0].source
                == restored.playlists[0].globalMusicQueue.tracks[1].source,
            "duplicate BGM source paths must survive")
        && require(restored.playlists[0].globalMusicQueue.tracks[0].id
                != restored.playlists[0].globalMusicQueue.tracks[1].id,
            "duplicate BGM paths must retain distinct TrackIds");
}

bool verifyBackgroundMusicEnabledAndUnknownFields()
{
    const AuthoredProject original = makeRichProject();
    nlohmann::json document =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(original);

    if (!require(!document.at("playlists").at(0).at("backgroundMusic").at("enabled").get<bool>(),
            "writer must preserve BGM enabled=false")
        || !require(document.at("playlists").at(0).at("items").at(0)
                .at("backgroundMusic").at("enabled").get<bool>(),
            "writer must preserve BGM enabled=true"))
    {
        return false;
    }

    document["playlists"][0]["backgroundMusic"].erase("enabled");
    document["futureRootField"] = nlohmann::json{{"value", 42}};
    document["playlists"][0]["items"][0]["futureItemField"] = "ignored";
    const AuthoredProject restored =
        DesktopSceneEngine::Infrastructure::Json::fromAuthoredProjectJson(document);

    return require(restored.playlists[0].globalMusicQueue.enabled,
               "missing BGM enabled must default true")
        && require(restored.playlists[0].items[0].displayAlias
                == original.playlists[0].items[0].displayAlias,
            "unknown fields must not disturb known values");
}

bool verifyUnicodeAndDuplicatePaths()
{
    const AuthoredProject original = makeRichProject();
    const AuthoredProject restored =
        DesktopSceneEngine::Infrastructure::Json::fromAuthoredProjectJson(
            DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(original));

    return require(restored.playlists[0].displayAlias
                == utf8(u8"\u4e3b\u64ad\u653e\u5217\u8868"),
               "Chinese alias must round-trip exactly")
        && require(restored.playlists[0].items[1].displayAlias
                == utf8(u8"\u591c\u666f\u30d3\u30c7\u30aa"),
            "Japanese alias must round-trip exactly")
        && require(restored.playlists[0].items[0].source.find(
                utf8(u8"\U0001f600")) != std::string::npos,
            "emoji in source path must round-trip exactly")
        && require(restored.playlists[0].globalMusicQueue.tracks.size() == 2U,
            "duplicate BGM paths must retain both tracks")
        && require(restored.playlists[0].globalMusicQueue.tracks[0].source
                == restored.playlists[0].globalMusicQueue.tracks[1].source,
            "duplicate BGM path text must remain equal")
        && require(restored.playlists[0].globalMusicQueue.tracks[0].id.value()
                == TrackOneId
                && restored.playlists[0].globalMusicQueue.tracks[1].id.value()
                == TrackTwoId,
            "duplicate BGM paths must preserve order and distinct TrackIds");
}

bool verifyInvalidUuidsAndUtf8()
{
    const nlohmann::json valid =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(
            makeRichProject());
    bool validResult = true;

    validResult = expectMutationRejected(
        valid,
        [](nlohmann::json& document)
        {
            document["playlists"][0]["id"] =
                "11111111-1111-1111-1111-11111111111A";
        },
        "reader must reject uppercase UUID text") && validResult;
    validResult = expectMutationRejected(
        valid,
        [](nlohmann::json& document)
        {
            document["playlists"][0]["id"] =
                "{11111111-1111-1111-1111-111111111111}";
        },
        "reader must reject braced UUID text") && validResult;
    validResult = expectMutationRejected(
        valid,
        [](nlohmann::json& document)
        {
            document["playlists"][0]["items"][0]["id"] =
                "33333333-3333-3333-3333-33333333333";
        },
        "reader must reject short UUID text") && validResult;
    validResult = expectMutationRejected(
        valid,
        [](nlohmann::json& document)
        {
            document["playlists"][0]["backgroundMusic"]["tracks"][0]["id"] =
                "66666666-6666-6666-6666-66666666666g";
        },
        "reader must reject non-hex UUID text") && validResult;

    AuthoredProject invalidProject = makeRichProject();
    invalidProject.playlists[0].id = PlaylistId{
        std::string{"11111111-1111-1111-1111-11111111111A"}};
    validResult = expectRuntimeError(
        [&invalidProject]()
        {
            (void)DesktopSceneEngine::Infrastructure::Json::
                toAuthoredProjectJson(invalidProject);
        },
        "writer must reject an invalid in-memory UUID") && validResult;

    invalidProject = makeRichProject();
    invalidProject.playlists[0].displayAlias = std::string{"\xC3\x28", 2U};
    validResult = expectRuntimeError(
        [&invalidProject]()
        {
            (void)DesktopSceneEngine::Infrastructure::Json::
                toAuthoredProjectJson(invalidProject);
        },
        "writer must reject malformed UTF-8") && validResult;

    nlohmann::json invalidUtf8 = valid;
    invalidUtf8["playlists"][0]["items"][0]["source"] =
        std::string{"\xF0\x28\x8C\x28", 4U};
    validResult = expectRuntimeError(
        [&invalidUtf8]()
        {
            (void)DesktopSceneEngine::Infrastructure::Json::
                fromAuthoredProjectJson(invalidUtf8);
        },
        "reader must reject malformed UTF-8") && validResult;

    return validResult;
}

bool verifyDuplicateIds()
{
    const nlohmann::json valid =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(
            makeRichProject());
    bool validResult = true;

    validResult = expectMutationRejected(
        valid,
        [](nlohmann::json& document)
        {
            document["playlists"][1]["id"] = document["playlists"][0]["id"];
        },
        "duplicate PlaylistId must be rejected") && validResult;
    validResult = expectMutationRejected(
        valid,
        [](nlohmann::json& document)
        {
            document["playlists"][1]["items"][0]["id"] =
                document["playlists"][0]["items"][0]["id"];
        },
        "duplicate ItemId across Playlists must be rejected") && validResult;
    validResult = expectMutationRejected(
        valid,
        [](nlohmann::json& document)
        {
            document["playlists"][0]["items"][0]["backgroundMusic"]
                ["tracks"][0]["id"] = document["playlists"][0]
                ["backgroundMusic"]["tracks"][0]["id"];
        },
        "duplicate TrackId across queues must be rejected") && validResult;

    nlohmann::json crossTypeReuse = valid;
    crossTypeReuse["playlists"][0]["items"][0]["id"] =
        crossTypeReuse["playlists"][0]["id"];
    validResult = require(
        DesktopSceneEngine::Infrastructure::Json::fromAuthoredProjectJson(
            crossTypeReuse).playlists[0].items[0].id.value() == PlaylistOneId,
        "same UUID text must remain legal across different strong ID types")
        && validResult;

    return validResult;
}

bool verifyInvalidEnums()
{
    const nlohmann::json valid =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(
            makeRichProject());
    bool validResult = true;

    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlistPlaybackMode"] = "unsupported"; },
        "unknown project sequence mode must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["itemPlaybackMode"] = "unsupported"; },
        "unknown Item sequence mode must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["items"][0]["mediaType"] = "audio"; },
        "unknown media type must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["backgroundMusic"]["mode"] = "unsupported"; },
        "unknown BGM mode must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        {
            document["playlists"][0]["items"][0]["presentation"]
                ["mediaPresentation"]["mode"] = "unsupported";
        },
        "unknown presentation mode must be rejected") && validResult;

    AuthoredProject invalidProject = makeRichProject();
    invalidProject.playlistPlaybackMode =
        static_cast<PlaybackSequenceMode>(999);
    validResult = expectRuntimeError(
        [&invalidProject]()
        {
            DesktopSceneEngine::Infrastructure::Json::
                validateAuthoredProject(invalidProject);
        },
        "validation API must reject invalid in-memory enum values") && validResult;
    return validResult;
}

bool verifyInvalidNumbers()
{
    const nlohmann::json valid =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(
            makeRichProject());
    bool validResult = true;

    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["masterVolume"] = -0.01; },
        "negative Playlist volume must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["items"][0]["itemMediaVolume"] = 1.01; },
        "Item volume above one must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["backgroundMusic"]["volume"] = 1.01; },
        "queue volume above one must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["backgroundMusic"]["tracks"][0]["volume"] = -0.01; },
        "negative Track volume must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["items"][0]["imageDurationSeconds"] = 0.0; },
        "non-positive image duration must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["items"][1]["videoPlaybackRate"] = 0.49; },
        "video rate below 0.5 must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["items"][1]["videoPlaybackRate"] = 2.01; },
        "video rate above 2.0 must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        {
            document["playlists"][0]["items"][0]["presentation"]
                ["crop"]["width"] = 0.95;
        },
        "crop extending beyond one must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        {
            document["playlists"][0]["items"][0]["presentation"]
                ["layout"]["scaleY"] = 0.0;
        },
        "non-positive layout scale must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["desktopCanvas"]["aspectRatio"]["numerator"] = 0; },
        "non-positive Canvas aspect must be rejected") && validResult;

    AuthoredProject invalidProject = makeRichProject();
    invalidProject.playlists[0].masterVolume =
        std::numeric_limits<double>::quiet_NaN();
    validResult = expectRuntimeError(
        [&invalidProject]()
        {
            (void)DesktopSceneEngine::Infrastructure::Json::
                toAuthoredProjectJson(invalidProject);
        },
        "writer must reject NaN before JSON construction") && validResult;

    invalidProject = makeRichProject();
    invalidProject.playlists[0].items[0].videoPlaybackRate =
        std::numeric_limits<double>::infinity();
    validResult = expectRuntimeError(
        [&invalidProject]()
        {
            (void)DesktopSceneEngine::Infrastructure::Json::
                toAuthoredProjectJson(invalidProject);
        },
        "writer must reject infinity before JSON construction") && validResult;

    invalidProject = makeRichProject();
    invalidProject.playlists[0].items[0].itemMediaVolume = 1.01;
    validResult = expectRuntimeError(
        [&invalidProject]()
        {
            (void)DesktopSceneEngine::Infrastructure::Json::
                toAuthoredProjectJson(invalidProject);
        },
        "writer must reject out-of-range authored values") && validResult;

    return validResult;
}

bool verifyRequiredFieldsAndStructure()
{
    const nlohmann::json valid =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(
            makeRichProject());
    bool validResult = true;

    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document.erase("desktopCanvas"); },
        "desktopCanvas must be required") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document.erase("playlistPlaybackMode"); },
        "playlistPlaybackMode must be required") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0].erase("id"); },
        "Playlist id must be required") && validResult;
    validResult = expectRuntimeErrorContaining(
        [&valid]()
        {
            nlohmann::json invalid = valid;
            invalid["playlists"][0]["items"][0].erase("source");
            (void)DesktopSceneEngine::Infrastructure::Json::
                fromAuthoredProjectJson(invalid);
        },
        "playlists[0].items[0].source",
        "missing Item source error must include its JSON path") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["items"][0].erase("presentation"); },
        "Item presentation must be required") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["backgroundMusic"].erase("mode"); },
        "BGM mode must be required") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        {
            document["playlists"][0]["backgroundMusic"]["tracks"][0]
                .erase("id");
        },
        "Track id must be required") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"] = nlohmann::json::object(); },
        "Playlists must be an array") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["playlists"][0]["backgroundMusic"]["enabled"] = "true"; },
        "present BGM enabled must be a boolean") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["desktopCanvas"]["aspectRatio"]["numerator"] = 16.0; },
        "Canvas aspect values must be JSON integers") && validResult;
    return validResult;
}

bool verifyTypeAndVersion()
{
    const nlohmann::json valid =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(
            makeRichProject());
    bool validResult = true;

    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["type"] = "AnotherDocument"; },
        "wrong root type must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document.erase("type"); },
        "missing root type must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["version"] = "1.0"; },
        "v1 must not be accepted by the authored v3 parser") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["version"] = "4.0"; },
        "future versions must not be accepted by the authored v3 parser") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document.erase("version"); },
        "missing version must be rejected") && validResult;
    return validResult;
}

bool verifyDisplayOwnerContract()
{
    const nlohmann::json valid =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(
            makeRichProject());
    bool validResult = true;

    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document.erase("displayOwner"); },
        "displayOwner must be required") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["displayOwner"] = 42; },
        "displayOwner must be a string") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        { document["displayOwner"] = "not-a-canonical-display-id"; },
        "malformed displayOwner must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        {
            document["displayOwner"] =
                *DesktopSceneEngine::DisplayId::fromSourceDeviceName(
                    "\\\\.\\DISPLAY-WEAK")
                    .canonicalRepresentation();
        },
        "SourceDeviceName displayOwner must be rejected") && validResult;
    validResult = expectMutationRejected(valid, [](nlohmann::json& document)
        {
            document["displayOwner"] =
                "dse-display-id:v2:target-device-path:4:test";
        },
        "unknown canonical display identity versions must be rejected") && validResult;

    return validResult;
}

}

int main()
{
    const bool roundTrip = verifyRichRoundTrip();
    const bool enabledAndUnknown = verifyBackgroundMusicEnabledAndUnknownFields();
    const bool unicodeAndPaths = verifyUnicodeAndDuplicatePaths();
    const bool invalidUuidsAndUtf8 = verifyInvalidUuidsAndUtf8();
    const bool duplicateIds = verifyDuplicateIds();
    const bool invalidEnums = verifyInvalidEnums();
    const bool invalidNumbers = verifyInvalidNumbers();
    const bool requiredFields = verifyRequiredFieldsAndStructure();
    const bool typeAndVersion = verifyTypeAndVersion();
    const bool displayOwner = verifyDisplayOwnerContract();
    return roundTrip
            && enabledAndUnknown
            && unicodeAndPaths
            && invalidUuidsAndUtf8
            && duplicateIds
            && invalidEnums
            && invalidNumbers
            && requiredFields
            && typeAndVersion
            && displayOwner
        ? 0
        : 1;
}
