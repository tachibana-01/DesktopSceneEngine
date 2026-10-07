#include <DesktopSceneEngine/Domain/AuthoredProject.h>

#include <iostream>
#include <string>
#include <type_traits>

namespace
{

using DesktopSceneEngine::Domain::AuthoredItem;
using DesktopSceneEngine::Domain::AuthoredMediaType;
using DesktopSceneEngine::Domain::AuthoredPlaylist;
using DesktopSceneEngine::Domain::AuthoredProject;
using DesktopSceneEngine::Domain::BackgroundMusicTrack;
using DesktopSceneEngine::Domain::ItemId;
using DesktopSceneEngine::Domain::MediaPresentationMode;
using DesktopSceneEngine::Domain::MusicPlaybackMode;
using DesktopSceneEngine::Domain::PlaybackSequenceMode;
using DesktopSceneEngine::Domain::PlaylistId;
using DesktopSceneEngine::Domain::TrackId;

constexpr const char* ValidUuid = "01234567-89ab-cdef-0123-456789abcdef";
constexpr const char* SecondValidUuid = "fedcba98-7654-3210-fedc-ba9876543210";

DesktopSceneEngine::Domain::DisplayOwner testDisplayOwner()
{
    return *DesktopSceneEngine::Domain::DisplayOwner::fromDisplayId(
        DesktopSceneEngine::DisplayId::fromTargetDevicePath(
            "\\\\?\\DISPLAY#AUTHORED_PROJECT_TEST#DISPLAY_1"));
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

bool verifyStrongAuthoredIds()
{
    static_assert(!std::is_same_v<PlaylistId, ItemId>);
    static_assert(!std::is_same_v<PlaylistId, TrackId>);
    static_assert(!std::is_same_v<ItemId, TrackId>);
    static_assert(!std::is_convertible_v<PlaylistId, ItemId>);
    static_assert(!std::is_convertible_v<PlaylistId, TrackId>);
    static_assert(!std::is_convertible_v<ItemId, TrackId>);

    const PlaylistId playlistId{std::string{ValidUuid}};
    const ItemId itemId{std::string{ValidUuid}};
    const TrackId trackId{std::string{ValidUuid}};

    return require(playlistId.isValid(), "canonical PlaylistId must be valid")
        && require(itemId.isValid(), "canonical ItemId must be valid")
        && require(trackId.isValid(), "canonical TrackId must be valid")
        && require(!PlaylistId{}.isValid() && !ItemId{}.isValid() && !TrackId{}.isValid(),
            "default authored IDs must remain invalid rather than being generated in Domain")
        && require(playlistId.value() == ValidUuid,
            "PlaylistId must preserve its canonical stored value")
        && require(PlaylistId{std::string{ValidUuid}} == playlistId,
            "same PlaylistId text must compare equal")
        && require(!PlaylistId{std::string{"01234567-89AB-cdef-0123-456789abcdef"}}.isValid(),
            "uppercase UUID text must be invalid")
        && require(!PlaylistId{std::string{"{01234567-89ab-cdef-0123-456789abcdef}"}}.isValid(),
            "braced UUID text must be invalid")
        && require(!PlaylistId{std::string{"0123456789ab-cdef-0123-456789abcdef"}}.isValid(),
            "UUID text with a missing hyphen must be invalid")
        && require(!PlaylistId{std::string{"01234567-89ab-cdef-0123-456789abcde"}}.isValid(),
            "short UUID text must be invalid")
        && require(!PlaylistId{std::string{"01234567-89ab-cdef-0123-456789abcdeg"}}.isValid(),
            "UUID text with a non-hex character must be invalid");
}

bool verifyDefaults()
{
    static_assert(!std::is_default_constructible_v<AuthoredProject>);
    const auto owner = testDisplayOwner();
    const AuthoredProject project{owner};
    const AuthoredPlaylist playlist{};
    const AuthoredItem item{};
    const BackgroundMusicTrack track{};

    return require(project.displayOwner == owner,
               "AuthoredProject must preserve its required DisplayOwner")
        && require(project.desktopCanvas.aspectRatio.numerator == 16
                && project.desktopCanvas.aspectRatio.denominator == 9,
               "AuthoredProject Canvas must default to 16:9")
        && require(project.playlistPlaybackMode == PlaybackSequenceMode::RepeatCurrent,
            "AuthoredProject playlist mode must default to Repeat Current")
        && require(project.playlists.empty(), "AuthoredProject must default to an empty playlist list")
        && require(playlist.enabled, "AuthoredPlaylist must default enabled")
        && require(playlist.masterVolume == 1.0,
            "AuthoredPlaylist master volume must default to 1.0")
        && require(playlist.itemPlaybackMode == PlaybackSequenceMode::ListLoop,
            "AuthoredPlaylist item mode must default to List Loop")
        && require(playlist.globalMusicQueue.enabled,
            "global BGM queue must default enabled as authored configuration")
        && require(playlist.globalMusicQueue.playbackMode == MusicPlaybackMode::ListLoop,
            "global BGM queue mode must default to List Loop")
        && require(playlist.globalMusicQueue.volume == 1.0,
            "global BGM queue volume must default to 1.0")
        && require(item.enabled, "AuthoredItem must default enabled")
        && require(item.mediaType == AuthoredMediaType::Image,
            "AuthoredItem media type must default to Image")
        && require(item.imageDurationSeconds == 10.0,
            "AuthoredItem image duration must default to 10 seconds")
        && require(item.videoPlaybackRate == 1.0,
            "AuthoredItem video rate must default to 1.0")
        && require(item.itemMediaVolume == 1.0,
            "AuthoredItem media volume must default to 1.0")
        && require(item.musicQueue.enabled,
            "item BGM queue must default enabled as authored configuration")
        && require(item.musicQueue.playbackMode == MusicPlaybackMode::ListLoop,
            "item BGM queue mode must default to List Loop")
        && require(item.musicQueue.volume == 1.0,
            "item BGM queue volume must default to 1.0")
        && require(track.volume == 1.0,
            "authored BGM track volume must default to 1.0")
        && require(item.presentation.crop.x == 0.0F
                && item.presentation.crop.y == 0.0F
                && item.presentation.crop.width == 1.0F
                && item.presentation.crop.height == 1.0F,
            "AuthoredItem presentation must default to a full crop")
        && require(item.presentation.layout.positionX == 0.0F
                && item.presentation.layout.positionY == 0.0F
                && item.presentation.layout.scaleX == 1.0F
                && item.presentation.layout.scaleY == 1.0F,
            "AuthoredItem presentation must default to origin at 1x scale")
        && require(item.presentation.mediaPresentation.mode == MediaPresentationMode::AutoFill,
            "AuthoredItem presentation must default to Auto Fill");
}

bool verifyOrderAndDuplicateSourceIdentity()
{
    const TrackId firstId{std::string{ValidUuid}};
    const TrackId secondId{std::string{SecondValidUuid}};
    AuthoredPlaylist playlist{};
    playlist.globalMusicQueue.tracks = {
        {firstId, "same.mp3", "First", 1.0},
        {secondId, "same.mp3", "Second", 0.5}};

    return require(playlist.globalMusicQueue.tracks.size() == 2,
               "duplicate BGM source paths must preserve both tracks")
        && require(playlist.globalMusicQueue.tracks[0].id == firstId
                && playlist.globalMusicQueue.tracks[1].id == secondId,
            "BGM queue vector order must be authored order")
        && require(playlist.globalMusicQueue.tracks[0].id
                != playlist.globalMusicQueue.tracks[1].id,
            "duplicate BGM source paths must retain distinct TrackIds")
        && require(playlist.globalMusicQueue.tracks[0].source
                == playlist.globalMusicQueue.tracks[1].source,
            "source equality must not collapse BGM track identity");
}

template <typename ProjectType>
concept HasSelectedPlaylistId = requires(ProjectType project)
{
    project.selectedPlaylistId;
};

template <typename ProjectType>
concept HasActivePlaylistId = requires(ProjectType project)
{
    project.activePlaylistId;
};

template <typename ProjectType>
concept HasDisplayTarget = requires(ProjectType project)
{
    project.displayTarget;
};

template <typename ProjectType>
concept HasPlaybackTime = requires(ProjectType project)
{
    project.playbackTime;
};

static_assert(!HasSelectedPlaylistId<AuthoredProject>);
static_assert(!HasActivePlaylistId<AuthoredProject>);
static_assert(!HasDisplayTarget<AuthoredProject>);
static_assert(!HasPlaybackTime<AuthoredProject>);

}

int main()
{
    const bool ids = verifyStrongAuthoredIds();
    const bool defaults = verifyDefaults();
    const bool order = verifyOrderAndDuplicateSourceIdentity();
    return ids && defaults && order ? 0 : 1;
}
