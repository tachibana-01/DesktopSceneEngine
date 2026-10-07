#include "WorkspaceModel.h"
#include "MainWindowTestDisplay.h"

#include <array>
#include <iostream>
#include <limits>
#include <string>

namespace
{

using DesktopSceneEngine::Player::MediaLayerType;
using DesktopSceneEngine::Player::MusicPlaybackMode;
using DesktopSceneEngine::Player::PlaybackSequenceMode;
using DesktopSceneEngine::Player::DesktopCanvas;
using DesktopSceneEngine::Domain::AspectRatio;
using DesktopSceneEngine::Domain::MediaPresentation;
using DesktopSceneEngine::Domain::MediaPresentationMode;
using DesktopSceneEngine::Player::WorkspaceMediaType;
using DesktopSceneEngine::Player::WorkspaceModel;
using DesktopSceneEngine::Player::WorkspaceItemPresentation;
using DesktopSceneEngine::Player::isSupportedBackgroundMusicSource;
using DesktopSceneEngine::Player::mediaLayerTypeForSource;

constexpr const char* ReplacementPlaylistId = "11111111-1111-4111-8111-111111111111";
constexpr const char* ReplacementItemId = "22222222-2222-4222-8222-222222222222";
constexpr const char* ReplacementTrackId = "33333333-3333-4333-8333-333333333333";

template <typename T>
concept HasAuthoredMediaLayers = requires(T value)
{
    value.mediaLayers;
};

static_assert(!HasAuthoredMediaLayers<DesktopSceneEngine::Domain::AuthoredItem>,
    "Retired Extended Media Layers must remain outside the authored document");

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool hasPresentation(
    const WorkspaceItemPresentation& presentation,
    const DesktopSceneEngine::Domain::Crop& crop,
    const DesktopSceneEngine::Domain::Layout& layout
)
{
    return presentation.crop.x == crop.x
        && presentation.crop.y == crop.y
        && presentation.crop.width == crop.width
        && presentation.crop.height == crop.height
        && presentation.layout.positionX == layout.positionX
        && presentation.layout.positionY == layout.positionY
        && presentation.layout.scaleX == layout.scaleX
        && presentation.layout.scaleY == layout.scaleY
        && presentation.mediaPresentation.mode == MediaPresentationMode::Custom
        && presentation.mediaPresentation.customAspect == AspectRatio{4, 3};
}

std::string canonicalIdText(const QUuid& id)
{
    return id.toString(QUuid::WithoutBraces).toLower().toStdString();
}

bool verifyCanonicalAuthoredProjectOwnership()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto& authored = model.authoredProject();
    const auto initialPlaylistId = model.selectedPlaylistId();

    if (!require(&authored == &model.authoredProject(),
            "authoredProject() must expose the stable canonical document")
        || !require(authored.playlists.size() == 1U
                && authored.playlists.front().id.value()
                    == canonicalIdText(initialPlaylistId.value),
            "The initial Player playlist identity must map exactly to the canonical authored ID"))
    {
        return false;
    }

    const QString unicodePlaylistAlias = QString::fromUtf8("播放列表 测试 🎵");
    const QString unicodeItemAlias = QString::fromUtf8("视频 日本語 🎬");
    const QString unicodeSource = QString::fromUtf8("C:/媒体/日本語/视频.mp4");
    const auto playlistB = model.addPlaylist();
    const auto itemA = model.addItem(
        playlistB, WorkspaceMediaType::Image, QStringLiteral("C:/Media/image.png"));
    const auto itemB = model.addItem(
        playlistB, WorkspaceMediaType::Video, unicodeSource, unicodeItemAlias);
    const WorkspaceItemPresentation presentation{
        {0.1F, 0.2F, 0.7F, 0.6F},
        {0.25F, -0.25F, 1.2F, 0.8F},
        MediaPresentation{MediaPresentationMode::Custom, AspectRatio{4, 3}}};
    if (!require(itemA.has_value() && itemB.has_value(),
            "Canonical ownership testing requires two authored items")
        || !require(model.renamePlaylist(playlistB, unicodePlaylistAlias)
                && model.setPlaylistPlaybackMode(PlaybackSequenceMode::Random)
                && model.setPlaylistItemPlaybackMode(
                    playlistB, PlaybackSequenceMode::RepeatCurrent)
                && model.setPlaylistMasterVolume(playlistB, 0.6)
                && model.setItemPresentation(*itemB, presentation)
                && model.setVideoPlaybackRate(*itemB, 1.7)
                && model.setItemMediaVolume(*itemB, 0.4),
            "Existing WorkspaceModel mutations must accept the canonical ownership fixture")
        || !require(model.moveItem(*itemB, -1) && model.movePlaylist(playlistB, -1),
            "Authored vector order must remain mutable through existing move APIs"))
    {
        return false;
    }

    const QString duplicateTrackSource = QString::fromUtf8("C:/音乐/主题.m4a");
    const auto firstTrackId = model.addPlaylistMusicTrack(
        playlistB, duplicateTrackSource, QString::fromUtf8("主题 A"));
    const auto secondTrackId = model.addPlaylistMusicTrack(
        playlistB, duplicateTrackSource, QString::fromUtf8("主题 B"));
    if (!require(firstTrackId.has_value() && secondTrackId.has_value()
            && *firstTrackId != *secondTrackId,
            "Duplicate BGM paths must retain distinct stable TrackIds")
        || !require(model.setPlaylistMusicPlaybackMode(
                playlistB, MusicPlaybackMode::Shuffle)
                && model.setPlaylistMusicVolume(playlistB, 0.35)
                && model.setPlaylistMusicTrackVolume(playlistB, *secondTrackId, 0.75)
                && model.movePlaylistMusicTrack(playlistB, *secondTrackId, -1),
            "BGM configuration must remain editable through the existing facade"))
    {
        return false;
    }

    if (!require(authored.playlistPlaybackMode
                == DesktopSceneEngine::Domain::PlaybackSequenceMode::Random,
            "Project playback mode must be stored canonically")
        || !require(authored.playlists.front().id.value()
                == canonicalIdText(playlistB.value),
            "Playlist moves must immediately reorder the canonical vector")
        || !require(authored.playlists.front().displayAlias
                == unicodePlaylistAlias.toUtf8().toStdString(),
            "Playlist aliases must be stored as lossless UTF-8")
        || !require(authored.playlists.front().itemPlaybackMode
                == DesktopSceneEngine::Domain::PlaybackSequenceMode::RepeatCurrent
                && authored.playlists.front().masterVolume == 0.6,
            "Playlist authored settings must update the same canonical document")
        || !require(authored.playlists.front().items.front().id.value()
                == canonicalIdText(itemB->value)
                && authored.playlists.front().items.front().source
                    == unicodeSource.toUtf8().toStdString()
                && authored.playlists.front().items.front().displayAlias
                    == unicodeItemAlias.toUtf8().toStdString(),
            "Item order, identity, source, and Unicode alias must be canonical")
        || !require(authored.playlists.front().items.front().videoPlaybackRate == 1.7
                && authored.playlists.front().items.front().itemMediaVolume == 0.4
                && authored.playlists.front().items.front().presentation.crop.x
                    == presentation.crop.x,
            "Item playback and presentation values must update the canonical item"))
    {
        return false;
    }

    const auto& queue = authored.playlists.front().globalMusicQueue;
    if (!require(queue.enabled,
            "Authored BGM queues must retain the frozen enabled=true default")
        || !require(queue.playbackMode
                == DesktopSceneEngine::Domain::MusicPlaybackMode::Shuffle
                && queue.volume == 0.35 && queue.tracks.size() == 2U,
            "BGM queue mode, volume, and tracks must be canonical")
        || !require(queue.tracks.front().id.value()
                == canonicalIdText(secondTrackId->value)
                && queue.tracks.front().source
                    == duplicateTrackSource.toUtf8().toStdString()
                && queue.tracks.front().volume == 0.75,
            "Track moves, identity, source, and volume must update canonical order"))
    {
        return false;
    }

    const auto playlistCount = authored.playlists.size();
    const auto canonicalFrontId = authored.playlists.front().id.value();
    const auto canonicalFrontAlias = authored.playlists.front().displayAlias;
    return require(model.selectPlaylist(initialPlaylistId)
            && model.activatePlaylist(playlistB)
            && authored.playlists.size() == playlistCount
            && authored.playlists.front().id.value() == canonicalFrontId
            && authored.playlists.front().displayAlias == canonicalFrontAlias,
        "Selection and activation must remain transient and leave authored content unchanged");
}

bool verifyCanvasAndItemPresentationOwnership()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistA = model.selectedPlaylistId();
    const auto playlistB = model.addPlaylist();
    const auto imageItemId = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/image.png"),
        QStringLiteral("Image"));
    const auto videoItemId = model.addItem(
        playlistA,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/video.mp4"),
        QStringLiteral("Video"));
    if (!require(
            model.desktopCanvas().aspectRatio == AspectRatio{16, 9},
            "A new workspace must use the deterministic 16:9 Desktop Canvas default")
        || !require(imageItemId.has_value() && videoItemId.has_value(),
            "Canvas ownership testing requires image and video workspace items")
        || !require(
            model.item(*imageItemId)->presentation.mediaPresentation.mode
                == MediaPresentationMode::AutoFill
                && model.item(*videoItemId)->presentation.mediaPresentation.mode
                    == MediaPresentationMode::AutoFill,
            "New items must preserve the existing full-Canvas behavior through Auto Fill"))
    {
        return false;
    }

    const DesktopSceneEngine::Domain::Crop imageCrop{0.1F, 0.2F, 0.6F, 0.7F};
    const DesktopSceneEngine::Domain::Layout imageLayout{0.25F, -0.1F, 0.8F, 1.1F};
    const DesktopSceneEngine::Domain::Crop videoCrop{0.05F, 0.1F, 0.9F, 0.75F};
    const DesktopSceneEngine::Domain::Layout videoLayout{-0.2F, 0.3F, 1.25F, 0.9F};
    if (!require(
            model.setItemPresentation(*imageItemId, {
                imageCrop,
                imageLayout,
                MediaPresentation{MediaPresentationMode::Custom, AspectRatio{4, 3}}}),
            "Image item presentation should be editable through its stable identity")
        || !require(
            model.setItemPresentation(*videoItemId, {
                videoCrop,
                videoLayout,
                MediaPresentation{MediaPresentationMode::Custom, AspectRatio{4, 3}}}),
            "Video item presentation should be editable through its stable identity")
        || !require(!model.setItemPresentation(
                DesktopSceneEngine::Player::ItemId{}, {
                    imageCrop,
                    imageLayout,
                    MediaPresentation{MediaPresentationMode::Custom, AspectRatio{4, 3}}}),
            "A stale item identity must not create presentation state"))
    {
        return false;
    }

    model.setDesktopCanvas(DesktopCanvas{AspectRatio{16, 10}});
    if (!require(model.selectItem(*videoItemId)
            && model.setActiveItem(*imageItemId)
            && model.selectPlaylist(playlistB)
            && model.desktopCanvas().aspectRatio == AspectRatio{16, 10},
            "Canvas state must remain workspace-level through selection and playback changes"))
    {
        return false;
    }

    const auto* imageItem = model.item(*imageItemId);
    const auto* videoItem = model.item(*videoItemId);
    if (!require(imageItem != nullptr && videoItem != nullptr,
            "Presentation items should remain addressable after unrelated selection changes")
        || !require(imageItem->presentation.mediaPresentation.mode
                == MediaPresentationMode::Custom
                && videoItem->presentation.mediaPresentation.mode
                    == MediaPresentationMode::Custom,
            "Explicit per-item presentation modes must remain attached to their items")
        || !require(
            hasPresentation(imageItem->presentation, imageCrop, imageLayout),
            "Image presentation must remain attached to its stable item")
        || !require(
            hasPresentation(videoItem->presentation, videoCrop, videoLayout),
            "Video presentation must remain attached to its stable item")
        || !require(
            !hasPresentation(imageItem->presentation, videoCrop, videoLayout),
            "Item A presentation must not inherit Item B presentation"))
    {
        return false;
    }

    return require(model.removeItem(*videoItemId),
               "Removing an unrelated item should succeed")
        && require(
            model.desktopCanvas().aspectRatio == AspectRatio{16, 10},
            "Removing an item must not mutate workspace Desktop Canvas state")
        && require(
            model.item(*imageItemId) != nullptr
                && hasPresentation(model.item(*imageItemId)->presentation, imageCrop, imageLayout),
            "Removing another item must preserve the remaining item's presentation");
}

bool verifyPlaylistBehavior()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};

    if (!require(model.playlists().size() == 1, "A new workspace should contain one playlist"))
    {
        return false;
    }

    const auto playlistA = model.playlists().front().id;
    if (!require(model.playlists().front().displayAlias == QStringLiteral("Playlist A"),
            "The initial playlist should be named Playlist A")
        || !require(model.selectedPlaylistId() == playlistA,
            "The initial playlist should be selected")
        || !require(model.activePlaylistId() == playlistA,
            "The initial playlist should be active"))
    {
        return false;
    }

    DesktopSceneEngine::Player::PlaylistId playlistZ{};
    for (int index = 0; index < 25; ++index)
    {
        const auto id = model.addPlaylist();
        if (index == 24)
        {
            playlistZ = id;
        }
    }

    const auto playlistAA = model.addPlaylist();
    if (!require(model.playlist(playlistZ)->displayAlias == QStringLiteral("Playlist Z"),
            "Default playlist naming should reach Playlist Z")
        || !require(model.playlist(playlistAA)->displayAlias == QStringLiteral("Playlist AA"),
            "Default playlist naming should continue with Playlist AA"))
    {
        return false;
    }

    const auto playlistB = model.playlists().at(1).id;
    const auto playlistC = model.playlists().at(2).id;
    if (!require(model.setPlaylistMasterVolume(playlistA, 0.3),
            "Playlist A master volume should be configurable")
        || !require(model.setPlaylistMasterVolume(playlistB, 0.8),
            "Playlist B master volume should be configurable")
        || !require(model.setPlaylistMasterVolume(playlistB, 0.6),
            "Changing Playlist B master volume should succeed")
        || !require(model.playlist(playlistA)->masterVolume == 0.3,
            "Changing Playlist B master volume must not modify Playlist A")
        || !require(model.playlist(playlistB)->masterVolume == 0.6,
            "Playlist B should retain its independently edited master volume")
        || !require(model.selectPlaylist(playlistA)
                && model.playlist(model.selectedPlaylistId())->masterVolume == 0.3,
            "Selecting Playlist A should resolve its own stored master volume")
        || !require(model.selectPlaylist(playlistB)
                && model.playlist(model.selectedPlaylistId())->masterVolume == 0.6,
            "Selecting Playlist B should resolve its own stored master volume"))
    {
        return false;
    }

    if (!require(model.renamePlaylist(playlistB, QStringLiteral("Work")),
            "A non-empty playlist rename should succeed")
        || !require(model.playlist(playlistB)->id == playlistB,
            "Playlist identity should survive rename")
        || !require(model.renamePlaylist(playlistAA, QString::fromUtf8("\xE5\xAD\xA6\xE4\xB9\xA0")),
            "Playlist aliases should support Unicode")
        || !require(model.renamePlaylist(
                playlistZ,
                QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F Favorites \xE2\x9D\xA4\xEF\xB8\x8F")),
            "Playlist aliases should support emoji")
        || !require(!model.renamePlaylist(playlistB, QString{}),
            "Empty playlist aliases should be rejected")
        || !require(!model.renamePlaylist(playlistB, QStringLiteral("   \t")),
            "Whitespace-only playlist aliases should be rejected")
        || !require(model.renamePlaylist(playlistB, QStringLiteral("Playlist A")),
            "Duplicate playlist aliases should remain allowed"))
    {
        return false;
    }

    if (!require(model.removePlaylist(playlistC), "A non-final playlist should be removable"))
    {
        return false;
    }
    const auto playlistAB = model.addPlaylist();
    if (!require(model.playlist(playlistAB)->displayAlias == QStringLiteral("Playlist AB"),
            "Default aliases should not be recycled after deletion"))
    {
        return false;
    }

    WorkspaceModel selectionModel{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto selectionA = selectionModel.selectedPlaylistId();
    const auto selectionB = selectionModel.addPlaylist();
    const auto selectionC = selectionModel.addPlaylist();
    if (!require(selectionModel.selectPlaylist(selectionB), "Selecting a playlist should succeed")
        || !require(selectionModel.removePlaylist(selectionB),
            "Deleting a selected playlist should succeed")
        || !require(selectionModel.selectedPlaylistId() == selectionC,
            "Deleting a selected playlist should select its next neighbor")
        || !require(selectionModel.activePlaylistId() == selectionA,
            "Deleting an inactive playlist should preserve the active playlist"))
    {
        return false;
    }

    WorkspaceModel activationModel{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto activationB = activationModel.addPlaylist();
    const auto activationC = activationModel.addPlaylist();
    if (!require(activationModel.activatePlaylist(activationB), "Activating a playlist should succeed")
        || !require(activationModel.selectedPlaylistId() == activationB,
            "Activating a playlist should select it")
        || !require(activationModel.removePlaylist(activationB),
            "Deleting an active playlist should succeed")
        || !require(activationModel.selectedPlaylistId() == activationC,
            "Deleting the selected active playlist should select its next neighbor")
        || !require(activationModel.activePlaylistId() == activationC,
            "Deleting the active playlist should activate its next neighbor"))
    {
        return false;
    }

    WorkspaceModel finalPlaylistModel{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto finalPlaylistId = finalPlaylistModel.selectedPlaylistId();
    const auto finalItemId = finalPlaylistModel.addItem(
        finalPlaylistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/final-playlist.png"));
    const auto finalLayerId = finalItemId.has_value()
        ? finalPlaylistModel.addExternalMediaLayer(
            *finalItemId,
            QStringLiteral("C:/Media/final-playlist-overlay.png"))
        : std::nullopt;
    std::size_t finalDeleteNotificationCount = 0U;
    finalPlaylistModel.setAuthoredChangedCallback([&]
    {
        ++finalDeleteNotificationCount;
    });
    const std::uint64_t revisionBeforeFinalDelete = finalPlaylistModel.authoredRevision();
    if (!require(finalItemId.has_value() && finalLayerId.has_value(),
            "Final Playlist deletion testing requires authored and transient item state")
        || !require(finalPlaylistModel.removePlaylist(finalPlaylistId),
            "The final Playlist must be removable")
        || !require(finalPlaylistModel.authoredProject().playlists.empty()
                && finalPlaylistModel.playlists().isEmpty(),
            "Deleting the final Playlist must establish an empty project")
        || !require(!finalPlaylistModel.selectedPlaylistId().isValid()
                && !finalPlaylistModel.selectedItemId().has_value(),
            "Deleting the final Playlist must clear selected identities")
        || !require(!finalPlaylistModel.activePlaylistId().isValid()
                && !finalPlaylistModel.activeItemId().has_value(),
            "Deleting the final Playlist must clear active identities")
        || !require(finalPlaylistModel.mediaLayer(*finalLayerId) == nullptr,
            "Deleting the final Playlist must purge owned transient attachments")
        || !require(finalPlaylistModel.authoredRevision() == revisionBeforeFinalDelete + 1U
                && finalDeleteNotificationCount == 1U,
            "Deleting the final Playlist must revise and notify exactly once"))
    {
        return false;
    }

    const std::uint64_t revisionAfterFinalDelete = finalPlaylistModel.authoredRevision();
    finalDeleteNotificationCount = 0U;
    if (!require(!finalPlaylistModel.removePlaylist(finalPlaylistId)
                && finalPlaylistModel.authoredRevision() == revisionAfterFinalDelete
                && finalDeleteNotificationCount == 0U
                && finalPlaylistModel.authoredProject().playlists.empty(),
            "A stale Playlist delete after final deletion must be revision-neutral"))
    {
        return false;
    }

    const auto replacementPlaylistId = finalPlaylistModel.addPlaylist();
    const auto replacementItemId = finalPlaylistModel.addItem(
        replacementPlaylistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/after-empty.png"));
    const auto* replacementPlaylist = finalPlaylistModel.playlist(replacementPlaylistId);
    return require(replacementPlaylistId != finalPlaylistId
            && replacementPlaylist != nullptr
            && replacementPlaylist->displayAlias == QStringLiteral("Playlist B")
            && replacementPlaylist->globalMusicQueue.enabled
            && finalPlaylistModel.selectPlaylist(replacementPlaylistId)
            && replacementItemId.has_value(),
        "Adding after final deletion must create a fresh selectable Playlist B with defaults");
}

bool verifyVisualPlaybackSequenceConfiguration()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistA = model.selectedPlaylistId();
    const auto playlistB = model.addPlaylist();
    const auto playlistC = model.addPlaylist();
    const auto itemA = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/a.png"),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/b.png"),
        QStringLiteral("B"));

    if (!require(itemA.has_value() && itemB.has_value(),
            "Playback-mode testing requires stable Items in two Playlists")
        || !require(model.playlistPlaybackMode() == PlaybackSequenceMode::RepeatCurrent,
            "A new Workspace must preserve the RepeatCurrent Playlist playback default")
        || !require(model.playlistItemPlaybackMode(playlistA)
                    == PlaybackSequenceMode::ListLoop
                && model.playlistItemPlaybackMode(playlistB)
                    == PlaybackSequenceMode::ListLoop,
            "Every new Playlist must preserve the ListLoop Item playback default"))
    {
        return false;
    }

    if (!require(model.setPlaylistPlaybackMode(PlaybackSequenceMode::Random)
            && model.setPlaylistItemPlaybackMode(playlistA, PlaybackSequenceMode::Random)
            && model.setPlaylistItemPlaybackMode(playlistB, PlaybackSequenceMode::RepeatCurrent)
            && model.playlistPlaybackMode() == PlaybackSequenceMode::Random
            && model.playlistItemPlaybackMode(playlistA)
                == PlaybackSequenceMode::Random
            && model.playlistItemPlaybackMode(playlistB)
                == PlaybackSequenceMode::RepeatCurrent,
            "Playback modes must remain independently addressable by stable PlaylistId"))
    {
        return false;
    }

    if (!require(model.selectItem(*itemB)
            && model.setActiveItem(*itemA)
            && model.setPlaylistPlaybackMode(PlaybackSequenceMode::ListLoop)
            && model.setPlaylistItemPlaybackMode(playlistA, PlaybackSequenceMode::RepeatCurrent)
            && model.selectedPlaylistId() == playlistB
            && model.selectedItemId() == itemB
            && model.activePlaylistId() == playlistA
            && model.activeItemId() == itemA,
            "Playback-mode mutations must not alter selected or active identities"))
    {
        return false;
    }

    if (!require(model.removePlaylist(playlistB)
            && !model.playlistItemPlaybackMode(playlistB).has_value()
            && model.playlistItemPlaybackMode(playlistA)
                == PlaybackSequenceMode::RepeatCurrent
            && model.playlistItemPlaybackMode(playlistC)
                == PlaybackSequenceMode::ListLoop,
            "Deleting or selecting other Playlists must not mix Item playback modes between IDs"))
    {
        return false;
    }

    return require(
        !model.setPlaylistItemPlaybackMode(
            DesktopSceneEngine::Player::PlaylistId{},
            PlaybackSequenceMode::Random),
        "A stale PlaylistId must not create Item playback configuration");
}

bool verifyItemAndMediaLayerBehavior()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistA = model.selectedPlaylistId();
    const auto playlistB = model.addPlaylist();

    const auto imageId = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/landscape.png"),
        QString{});
    const auto videoId = model.addItem(
        playlistA,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/clip.mp4"),
        QStringLiteral("Video A"));
    const auto secondImageId = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/second.jpg"),
        QStringLiteral("Second"));
    const auto playlistBImageId = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/other.png"),
        QStringLiteral("Other"));

    if (!require(imageId.has_value(), "Adding an image item should succeed")
        || !require(videoId.has_value(), "Adding a video item should succeed")
        || !require(secondImageId.has_value(), "Adding another image item should succeed")
        || !require(playlistBImageId.has_value(), "Items should be addable to another playlist"))
    {
        return false;
    }

    const auto* playlist = model.playlist(playlistA);
    if (!require(playlist != nullptr && playlist->items.size() == 3,
            "Items should belong to their requested playlist")
        || !require(playlist->items.at(0).id == *imageId
                && playlist->items.at(1).id == *videoId
                && playlist->items.at(2).id == *secondImageId,
            "Playlist item order should be retained")
        || !require(model.playlist(playlistB)->items.at(0).id == *playlistBImageId,
            "Items should be retained by their owning playlist")
        || !require(model.item(*imageId)->displayAlias == QStringLiteral("landscape"),
            "An empty item alias should derive from the source filename"))
    {
        return false;
    }

    const QString originalSource = model.item(*videoId)->source;
    if (!require(model.renameItem(*videoId, QString::fromUtf8("\xE5\xA4\x9C\xE6\x99\xAF")),
            "Item aliases should support Unicode")
        || !require(model.renameItem(
                *videoId,
                QString::fromUtf8("\xF0\x9F\x8C\x83 \xE5\xA4\x9C\xE6\x99\xAF")),
            "Item aliases should support emoji")
        || !require(model.item(*videoId)->id == *videoId,
            "Item identity should survive rename")
        || !require(model.item(*videoId)->source == originalSource,
            "Item rename must not alter its source")
        || !require(!model.renameItem(*videoId, QString{}), "Empty item aliases should be rejected")
        || !require(!model.renameItem(*videoId, QStringLiteral(" \t ")),
            "Whitespace-only item aliases should be rejected"))
    {
        return false;
    }

    if (!require(model.selectItem(*videoId), "Selecting an item before a playlist change should succeed")
        || !require(model.selectPlaylist(playlistB), "Selecting another playlist should succeed")
        || !require(!model.selectedItemId().has_value(),
            "Selecting a playlist should clear item selection")
        || !require(model.setActiveItem(*imageId),
            "Playback synchronization should set an active item without changing selection")
        || !require(model.activePlaylistId() == playlistA
                && model.activeItemId() == imageId,
            "Setting the active item should retain its parent playlist and identity")
        || !require(model.selectedPlaylistId() == playlistB
                && !model.selectedItemId().has_value(),
            "Playback synchronization must preserve the user selection")
        || !require(model.selectItem(*playlistBImageId),
            "A separate user selection should be retained before stopping playback")
        || !require(model.setActiveItem(*imageId),
            "Playback should remain able to identify a different active item")
        || !require((model.clearActivePlayback(),
                !model.activePlaylistId().isValid()
                    && !model.activeItemId().has_value()),
            "Stopping playback should clear both active playback identities")
        || !require(model.selectedPlaylistId() == playlistB
                && model.selectedItemId() == playlistBImageId,
            "Clearing complete playback state must preserve the user selection")
        || !require((model.clearActivePlayback(),
                !model.activePlaylistId().isValid()
                    && !model.activeItemId().has_value()),
            "Clearing already-empty playback state should remain safe")
        || !require(model.selectItem(*imageId), "Selecting an item should succeed")
        || !require(model.selectedPlaylistId() == playlistA,
            "Selecting an item should select its parent playlist")
        || !require(model.selectedItemId() == imageId,
            "Selecting an item should record its stable item identity"))
    {
        return false;
    }

    if (!require(!model.setImageDisplayDuration(*imageId, 0.0),
            "Image display duration must be positive")
        || !require(model.setImageDisplayDuration(*imageId, 12.5),
            "Positive image display duration should be accepted")
        || !require(model.item(*imageId)->displayDurationSeconds == 12.5,
            "Image display duration should update")
        || !require(!model.setVideoPlaybackRate(*videoId, 0.49),
            "Video rate below 0.5x should be rejected")
        || !require(!model.setVideoPlaybackRate(*videoId, 2.01),
            "Video rate above 2.0x should be rejected")
        || !require(!model.setVideoPlaybackRate(*videoId, 0.55),
            "Video playback rate should use 0.1x increments")
        || !require(model.setVideoPlaybackRate(*videoId, 0.5),
            "The minimum supported video playback rate should be accepted")
        || !require(model.setVideoPlaybackRate(*videoId, 1.0),
            "The normal video playback rate should be accepted")
        || !require(model.setVideoPlaybackRate(*videoId, 2.0),
            "Video rate within the safe range should be accepted")
        || !require(model.item(*videoId)->playbackRate == 2.0,
            "Video playback rate should update"))
    {
        return false;
    }

    if (!require(model.setPlaylistMasterVolume(playlistA, 1.5),
            "Playlist master volume should be editable")
        || !require(model.playlist(playlistA)->masterVolume == 1.0,
            "Playlist master volume should clamp at one")
        || !require(model.setPlaylistMasterVolume(playlistA, -1.0),
            "Playlist master volume should support lower clamping")
        || !require(model.playlist(playlistA)->masterVolume == 0.0,
            "Playlist master volume should clamp at zero"))
    {
        return false;
    }

    if (!require(model.activateItem(*videoId), "Activating an item should succeed")
        || !require(model.activePlaylistId() == playlistA,
            "Activating an item should activate its parent playlist")
        || !require(model.activeItemId() == videoId,
            "Activating an item should record the active item")
        || !require(model.selectedPlaylistId() == playlistA && model.selectedItemId() == videoId,
            "Activating an item should also select it"))
    {
        return false;
    }

    const auto externalAudioId = model.addExternalMediaLayer(
        *videoId,
        QStringLiteral("C:/Audio/music.mp3"),
        QString::fromUtf8("\xE9\x9F\xB3\xE8\xBD\xA8 \xF0\x9F\x8E\xB5"));
    const auto externalVideoId = model.addExternalMediaLayer(
        *videoId,
        QString::fromUtf8("C:/\xE8\xA7\x86\xE9\xA2\x91/overlay.mkv"),
        QString::fromUtf8("\xE8\xA6\x86\xE7\x9B\x96\xE8\xA7\x86\xE9\xA2\x91"));
    const auto externalImageId = model.addExternalMediaLayer(
        *videoId,
        QStringLiteral("C:/Image/overlay.webp"),
        QString::fromUtf8("\xE3\x82\xAA\xE3\x83\xBC\xE3\x83\x90\xE3\x83\xBC\xE3\x83\xAC\xE3\x82\xA4\xE3\x83\xA4\xE3\x83\xBC"));
    if (!require(!externalAudioId.has_value(),
            "Audio sources must not be accepted as extended media layers")
        || !require(externalVideoId.has_value(), "An external video layer should be addable")
        || !require(externalImageId.has_value(), "An external image layer should be addable"))
    {
        return false;
    }

    const auto* externalVideoLayer = model.mediaLayer(*externalVideoId);
    const auto* externalImageLayer = model.mediaLayer(*externalImageId);
    if (!require(externalVideoLayer != nullptr && externalImageLayer != nullptr,
            "Added media layers should remain addressable by stable identity"))
    {
        return false;
    }

    if (!require(externalVideoLayer->type == MediaLayerType::Video,
            "External video should retain its media type")
        || !require(externalImageLayer->type == MediaLayerType::Image,
            "External image should retain its media type")
        || !require(externalVideoLayer->volume == std::optional<double>{1.0}
                && !externalImageLayer->volume.has_value(),
            "Video layers should retain volume metadata while image layers should not")
        || !require(*externalVideoId != *externalImageId,
            "Media layer IDs should remain unique")
        || !require(externalVideoLayer->source
                == QString::fromUtf8("C:/\xE8\xA7\x86\xE9\xA2\x91/overlay.mkv"),
            "Unicode media source paths should remain unchanged")
        || !require(!model.addExternalMediaLayer(*videoId, QStringLiteral("C:/Other/file.txt")).has_value(),
            "Unsupported media sources should be rejected by the model")
        || !require(model.setMediaLayerEnabled(*externalVideoId, false),
            "Video layer enabled state should be editable")
        || !require(!model.mediaLayer(*externalVideoId)->enabled,
            "Video layer enabled state should update")
        || !require(model.setMediaLayerVolume(*externalVideoId, 0.5),
            "Video layers should retain their volume metadata")
        || !require(model.mediaLayer(*externalVideoId)->volume == std::optional<double>{0.5},
            "Video layer volume should update")
        || !require(!model.setMediaLayerVolume(*externalImageId, 0.5),
            "Image layers should not expose audio-volume semantics")
        || !require(model.removeMediaLayer(*externalVideoId),
            "External video layers should be removable")
        || !require(model.removeMediaLayer(*externalImageId),
            "External image layers should be removable")
        || !require(!model.removeMediaLayer(*externalVideoId),
            "Removing a stale media layer identity should fail safely"))
    {
        return false;
    }

    const std::array<const char*, 12> audioExtensions{
        "mp3", "aac", "m4a", "flac", "wav", "ogg", "opus", "wma", "alac", "aiff", "aif", "ape"};
    for (const char* extension : audioExtensions)
    {
        if (!require(
                !mediaLayerTypeForSource(
                    QStringLiteral("C:/Audio/track.") + QString::fromLatin1(extension))
                     .has_value(),
                "Audio extensions must be rejected as extended media layers"))
        {
            return false;
        }
    }

    if (!require(mediaLayerTypeForSource(QStringLiteral("C:/Video/clip.m4v"))
                == MediaLayerType::Video,
            "Video extensions should be recognized")
        || !require(mediaLayerTypeForSource(QStringLiteral("C:/Image/overlay.gif"))
                == MediaLayerType::Image,
            "Image extensions should be recognized")
        || !require(!mediaLayerTypeForSource(QStringLiteral("C:/Other/file.txt")).has_value(),
            "Unsupported media extensions should be rejected"))
    {
        return false;
    }

    const auto itemOwnedLayerId = model.addExternalMediaLayer(
        *videoId, QStringLiteral("C:/Video/item-cleanup.mkv"));
    const auto playlistOwnedLayerId = model.addExternalMediaLayer(
        *playlistBImageId, QStringLiteral("C:/Image/playlist-cleanup.png"));
    if (!require(itemOwnedLayerId.has_value() && playlistOwnedLayerId.has_value(),
            "Transient cleanup testing requires Item- and Playlist-owned media layers"))
    {
        return false;
    }

    if (!require(model.removeItem(*videoId), "Removing an item should succeed")
        || !require(!model.activeItemId().has_value(),
            "Removing the active item should clear active item state")
        || !require(!model.selectedItemId().has_value(),
            "Removing the selected item should clear selected item state")
        || !require(model.item(*videoId) == nullptr,
            "Removed items should no longer be addressable by identity")
        || !require(model.mediaLayer(*itemOwnedLayerId) == nullptr,
            "Removing an Item must purge its transient media-layer attachments")
        || !require(model.removePlaylist(playlistB),
            "Removing a Playlist with transient media layers should succeed")
        || !require(model.mediaLayer(*playlistOwnedLayerId) == nullptr,
            "Removing a Playlist must purge attachments for every owned Item"))
    {
        return false;
    }

    return true;
}

bool verifyVideoItemMediaVolumeBehavior()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto videoAId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/video-a.mp4"),
        QStringLiteral("Video A"));
    const auto videoBId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/video-b.mp4"),
        QStringLiteral("Video B"));
    const auto imageId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/image.png"),
        QStringLiteral("Image"));
    if (!require(videoAId.has_value() && videoBId.has_value() && imageId.has_value(),
            "Video item media-volume testing requires two videos and one image"))
    {
        return false;
    }

    if (!require(model.item(*videoAId)->itemMediaVolume == 1.0
            && model.item(*videoBId)->itemMediaVolume == 1.0,
            "New video items should default their media volume to 1.0")
        || !require(model.setItemMediaVolume(*videoAId, -1.0),
            "Video item media volume should support lower clamping")
        || !require(model.item(*videoAId)->itemMediaVolume == 0.0,
            "Video item media volume should clamp at zero")
        || !require(model.setItemMediaVolume(*videoBId, 2.0),
            "Video item media volume should support upper clamping")
        || !require(model.item(*videoBId)->itemMediaVolume == 1.0,
            "Video item media volume should clamp at one")
        || !require(!model.setItemMediaVolume(
                *videoAId,
                std::numeric_limits<double>::quiet_NaN()),
            "Video item media volume should reject NaN")
        || !require(!model.setItemMediaVolume(
                *videoAId,
                std::numeric_limits<double>::infinity()),
            "Video item media volume should reject infinity")
        || !require(!model.setItemMediaVolume(*imageId, 0.5),
            "Image items should not expose embedded-audio volume semantics"))
    {
        return false;
    }

    if (!require(model.setItemMediaVolume(*videoAId, 0.25),
            "Video A media volume should be editable")
        || !require(model.setItemMediaVolume(*videoBId, 0.80),
            "Video B media volume should be editable")
        || !require(model.setActiveItem(*videoBId) && model.selectItem(*videoAId),
            "Selected and active video items should be independently addressable")
        || !require(model.activeItemId() == videoBId && model.selectedItemId() == videoAId,
            "Selecting Video A must not replace active Video B")
        || !require(model.item(*videoAId)->itemMediaVolume == 0.25
                && model.item(*videoBId)->itemMediaVolume == 0.80,
            "Video item media volumes should remain independent by item identity")
        || !require(model.selectItem(*videoBId)
                && model.item(*videoBId)->itemMediaVolume == 0.80
                && model.selectItem(*videoAId)
                && model.item(*videoAId)->itemMediaVolume == 0.25,
            "Selection changes should preserve each video item's media volume")
        || !require(model.removeItem(*imageId),
            "Removing an unrelated image should succeed")
        || !require(model.item(*videoAId)->itemMediaVolume == 0.25
                && model.item(*videoBId)->itemMediaVolume == 0.80,
            "Removing an unrelated item should preserve surviving video volumes"))
    {
        return false;
    }

    return true;
}

bool verifyAvailabilityAndOrdering()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistA = model.selectedPlaylistId();
    const auto playlistB = model.addPlaylist();
    const auto playlistC = model.addPlaylist();
    const auto itemA1 = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/a1.png"),
        QStringLiteral("A1"));
    const auto itemA2 = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/a2.png"),
        QStringLiteral("A2"));
    const auto itemA3 = model.addItem(
        playlistA,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/a3.mp4"),
        QStringLiteral("A3"));
    const auto itemB1 = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/b1.png"),
        QStringLiteral("B1"));
    if (!require(itemA1.has_value() && itemA2.has_value() && itemA3.has_value()
            && itemB1.has_value(),
            "Availability and ordering testing requires four workspace items"))
    {
        return false;
    }

    if (!require(model.playlistEnabled(playlistA) && model.playlistEnabled(playlistB)
            && model.playlistEnabled(playlistC),
            "New and existing playlists must default to enabled")
        || !require(model.itemEnabled(*itemA1) && model.itemEnabled(*itemA2)
                && model.itemEnabled(*itemA3) && model.itemEnabled(*itemB1),
            "New items must default to enabled")
        || !require(model.isItemPlayable(playlistA, *itemA1)
                && !model.isItemPlayable(playlistA, *itemB1),
            "Effective item playability must require parent containment")
        || !require(model.isPlaylistPlayable(playlistA) && model.isPlaylistPlayable(playlistB)
                && !model.isPlaylistPlayable(playlistC),
            "Playlist playability must require an enabled playlist with a playable item")
        || !require(!model.playlistEnabled(DesktopSceneEngine::Player::PlaylistId{})
                && !model.itemEnabled(DesktopSceneEngine::Player::ItemId{})
                && !model.isItemPlayable(playlistA, DesktopSceneEngine::Player::ItemId{})
                && !model.isItemPlayable(
                    DesktopSceneEngine::Player::PlaylistId{},
                    *itemA1)
                && !model.isPlaylistPlayable(DesktopSceneEngine::Player::PlaylistId{}),
            "Stale availability identities must fail safely"))
    {
        return false;
    }

    if (!require(model.setItemEnabled(*itemA2, false),
            "Disabling an item should change only its intrinsic state")
        || !require(model.playlistEnabled(playlistA) && model.itemEnabled(*itemA1)
                && !model.itemEnabled(*itemA2) && model.itemEnabled(*itemA3),
            "Disabling an item must leave its parent and siblings unchanged")
        || !require(model.isItemPlayable(playlistA, *itemA1)
                && !model.isItemPlayable(playlistA, *itemA2)
                && model.isItemPlayable(playlistA, *itemA3),
            "Effective item playability must reflect intrinsic item availability")
        || !require(model.setPlaylistEnabled(playlistA, false),
            "Disabling a playlist should succeed")
        || !require(!model.playlistEnabled(playlistA) && model.itemEnabled(*itemA1)
                && !model.itemEnabled(*itemA2) && model.itemEnabled(*itemA3),
            "Disabling a playlist must not rewrite child intrinsic availability")
        || !require(!model.isItemPlayable(playlistA, *itemA1)
                && !model.isItemPlayable(playlistA, *itemA2)
                && !model.isItemPlayable(playlistA, *itemA3)
                && !model.isPlaylistPlayable(playlistA)
                && !model.firstPlayableItem(playlistA).has_value(),
            "A disabled playlist must gate all child item playability")
        || !require(model.setPlaylistEnabled(playlistA, true)
                && model.isItemPlayable(playlistA, *itemA1)
                && !model.isItemPlayable(playlistA, *itemA2)
                && model.isItemPlayable(playlistA, *itemA3),
            "Re-enabling a playlist must restore effective playability from child state"))
    {
        return false;
    }

    if (!require(model.setItemEnabled(*itemA1, false)
                && model.setItemEnabled(*itemA2, true)
                && model.setItemEnabled(*itemA3, true),
            "First-playable testing should configure intrinsic item availability")
        || !require(model.firstPlayableItem(playlistA) == itemA2,
            "First playable item must follow current workspace order")
        || !require(model.setItemEnabled(*itemA2, false)
                && model.setItemEnabled(*itemA3, false)
                && !model.isPlaylistPlayable(playlistA)
                && !model.firstPlayableItem(playlistA).has_value(),
            "A playlist with all items disabled must not be playable")
        || !require(model.setItemEnabled(*itemA2, true)
                && model.setItemEnabled(*itemA3, true),
            "Items should be independently re-enabled for ordering tests")
        || !require(model.selectItem(*itemA3) && model.setActiveItem(*itemA2),
            "Selected and active item identities must be independently settable"))
    {
        return false;
    }

    if (!require(model.setPlaylistEnabled(playlistB, false)
                && model.movePlaylist(playlistB, -1),
            "A disabled middle playlist should move up")
        || !require(model.playlists().at(0).id == playlistB
                && model.playlists().at(1).id == playlistA
                && model.playlists().at(2).id == playlistC
                && !model.playlistEnabled(playlistB),
            "Playlist movement must preserve stable identity and intrinsic availability")
        || !require(model.movePlaylist(playlistB, 1)
                && model.playlists().at(0).id == playlistA
                && model.playlists().at(1).id == playlistB
                && model.playlists().at(2).id == playlistC,
            "Moving the playlist down should restore its original workspace order")
        || !require(!model.movePlaylist(playlistA, -1)
                && !model.movePlaylist(playlistC, 1)
                && !model.movePlaylist(playlistA, 0)
                && !model.movePlaylist(playlistA, 2)
                && !model.movePlaylist(DesktopSceneEngine::Player::PlaylistId{}, 1),
            "Playlist movement must reject boundaries, invalid directions, and stale identities")
        || !require(model.playlists().at(0).id == playlistA
                && model.playlists().at(1).id == playlistB
                && model.playlists().at(2).id == playlistC,
            "Rejected playlist moves must not change workspace order")
        || !require(model.selectedPlaylistId() == playlistA
                && model.activePlaylistId() == playlistA
                && model.selectedItemId() == itemA3
                && model.activeItemId() == itemA2,
            "Playlist movement must not replace selected or active stable identities"))
    {
        return false;
    }

    if (!require(model.moveItem(*itemA3, -1),
            "Moving an item up should swap it with its predecessor in its owning playlist")
        || !require(model.playlist(playlistA)->items.at(0).id == *itemA1
                && model.playlist(playlistA)->items.at(1).id == *itemA3
                && model.playlist(playlistA)->items.at(2).id == *itemA2
                && model.firstPlayableItem(playlistA) == itemA3,
            "Item movement must preserve identities and update first-playable order")
        || !require(model.moveItem(*itemA1, 1) && !model.itemEnabled(*itemA1),
            "A disabled item should remain disabled and movable within its owning playlist")
        || !require(!model.moveItem(*itemA2, 1)
                && !model.moveItem(*itemA2, 0)
                && !model.moveItem(*itemA2, -2)
                && !model.moveItem(DesktopSceneEngine::Player::ItemId{}, 1)
                && !model.moveItem(*itemB1, 1),
            "Item movement must reject boundaries, invalid directions, stale IDs, and cross-playlist moves")
        || !require(model.playlist(playlistA)->items.at(0).id == *itemA3
                && model.playlist(playlistA)->items.at(1).id == *itemA1
                && model.playlist(playlistA)->items.at(2).id == *itemA2,
            "Rejected item moves must not change their owning playlist order")
        || !require(model.selectedItemId() == itemA3 && model.activeItemId() == itemA2
                && model.item(*itemA3)->source == QStringLiteral("C:/Media/a3.mp4"),
            "Item movement must preserve selected/active identities and item media state"))
    {
        return false;
    }

    return true;
}

bool verifyBackgroundMusicConfiguration()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistA = model.selectedPlaylistId();
    const auto playlistB = model.addPlaylist();
    const auto imageItemId = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/image.png"),
        QStringLiteral("Image"));
    const auto videoItemId = model.addItem(
        playlistA,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/video.mp4"),
        QStringLiteral("Video"));
    const auto otherItemId = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/other.png"),
        QStringLiteral("Other"));
    const auto playlistBItemId = model.addItem(
        playlistB,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/other-video.mp4"),
        QStringLiteral("Other video"));
    if (!require(imageItemId.has_value() && videoItemId.has_value() && otherItemId.has_value()
            && playlistBItemId.has_value(),
            "Background music testing requires image and video items in two playlists"))
    {
        return false;
    }

    const auto* playlistAQueue = model.playlistMusicQueue(playlistA);
    const auto* imageQueue = model.itemMusicQueue(*imageItemId);
    const auto* videoQueue = model.itemMusicQueue(*videoItemId);
    if (!require(playlistAQueue != nullptr && imageQueue != nullptr && videoQueue != nullptr,
            "Playlist and both Item media types must expose background music queues")
        || !require(playlistAQueue->tracks.isEmpty()
                && playlistAQueue->playbackMode == MusicPlaybackMode::ListLoop
                && playlistAQueue->volume == 1.0,
            "New playlist music queues must default to empty List Loop at full volume")
        || !require(imageQueue->tracks.isEmpty()
                && imageQueue->playbackMode == MusicPlaybackMode::ListLoop
                && imageQueue->volume == 1.0
                && videoQueue->tracks.isEmpty()
                && videoQueue->playbackMode == MusicPlaybackMode::ListLoop
                && videoQueue->volume == 1.0,
            "New Image and Video Items must each default to an independent empty music queue"))
    {
        return false;
    }

    const std::array<const char*, 12> acceptedExtensions{
        "mp3", "aac", "m4a", "flac", "wav", "ogg", "opus", "wma", "alac", "aiff", "aif", "ape"};
    for (const char* extension : acceptedExtensions)
    {
        if (!require(
                isSupportedBackgroundMusicSource(
                    QStringLiteral("C:/Music/track.") + QString::fromLatin1(extension)),
                "Every configured background-music audio extension must be accepted")
            || !require(
                isSupportedBackgroundMusicSource(
                    QStringLiteral("C:/Music/track.")
                    + QString::fromLatin1(extension).toUpper()),
                "Background-music audio extension validation must be case-insensitive"))
        {
            return false;
        }
    }

    const std::array<const char*, 11> rejectedExtensions{
        "jpg", "png", "webp", "bmp", "mp4", "mkv", "webm", "avi", "mov", "exe", "txt"};
    for (const char* extension : rejectedExtensions)
    {
        if (!require(
                !isSupportedBackgroundMusicSource(
                    QStringLiteral("C:/Other/file.") + QString::fromLatin1(extension)),
                "Non-audio sources must be rejected by background-music configuration"))
        {
            return false;
        }
    }
    if (!require(!isSupportedBackgroundMusicSource(QStringLiteral("C:/Other/no-extension")),
            "Sources without an extension must be rejected")
        || !require(isSupportedBackgroundMusicSource(QStringLiteral("C:/Music/accepted.m4a")),
            "M4A must be accepted as a configured background-music source")
        || !require(!isSupportedBackgroundMusicSource(QStringLiteral("C:/Video/rejected.mp4")),
            "MP4 must not be accepted as a configured background-music source"))
    {
        return false;
    }

    const QString duplicateSource = QStringLiteral("C:/Music/ambient.mp3");
    const auto duplicateFirstId = model.addPlaylistMusicTrack(playlistA, duplicateSource);
    const auto duplicateSecondId = model.addPlaylistMusicTrack(playlistA, duplicateSource);
    const auto trackBId = model.addPlaylistMusicTrack(playlistA, QStringLiteral("C:/Music/b.flac"));
    const auto trackCId = model.addPlaylistMusicTrack(playlistA, QStringLiteral("C:/Music/c.ogg"));
    if (!require(duplicateFirstId.has_value() && duplicateSecondId.has_value()
            && trackBId.has_value() && trackCId.has_value(),
            "Supported playlist music sources must append with a new TrackId")
        || !require(*duplicateFirstId != *duplicateSecondId,
            "Duplicate source paths must receive independent TrackIds")
        || !require(model.playlistMusicQueue(playlistA)->tracks.size() == 4
                && model.playlistMusicQueue(playlistA)->tracks.at(0).sourcePath == duplicateSource
                && model.playlistMusicQueue(playlistA)->tracks.at(1).sourcePath == duplicateSource
                && model.playlistMusicQueue(playlistA)->tracks.at(0).displayAlias
                    == QStringLiteral("ambient"),
            "Duplicate paths must remain ordered entries with the current filename-derived alias convention")
        || !require(!model.addPlaylistMusicTrack(playlistA, QStringLiteral("C:/Video/clip.mp4"))
                        .has_value(),
            "Unsupported playlist sources must not create a track")
        || !require(!model.addPlaylistMusicTrack(
                        DesktopSceneEngine::Player::PlaylistId{}, duplicateSource)
                        .has_value(),
            "A stale playlist identity must not create a music track"))
    {
        return false;
    }

    if (!require(model.playlistMusicQueue(playlistA)->tracks.at(0).volume == 1.0
            && model.playlistMusicQueue(playlistA)->tracks.at(1).volume == 1.0,
            "New playlist music tracks must default to full independent track volume"))
    {
        return false;
    }

    if (!require(model.setPlaylistMusicTrackVolume(playlistA, *duplicateFirstId, 0.2)
            && model.setPlaylistMusicTrackVolume(playlistA, *duplicateSecondId, 0.8)
            && model.playlistMusicQueue(playlistA)->tracks.at(0).volume == 0.2
            && model.playlistMusicQueue(playlistA)->tracks.at(1).volume == 0.8
            && model.playlistMusicQueue(playlistA)->tracks.at(2).volume == 1.0
            && model.playlistMusicQueue(playlistA)->volume == 1.0,
            "Duplicate source paths must retain independent TrackId-scoped volumes without changing queue volume")
        || !require(model.setPlaylistMusicTrackVolume(playlistA, *trackBId, -1.0)
                && model.playlistMusicQueue(playlistA)->tracks.at(2).volume == 0.0
                && model.setPlaylistMusicTrackVolume(playlistA, *trackBId, 2.0)
                && model.playlistMusicQueue(playlistA)->tracks.at(2).volume == 1.0
                && !model.setPlaylistMusicTrackVolume(
                    playlistA,
                    *trackBId,
                    std::numeric_limits<double>::quiet_NaN())
                && !model.setPlaylistMusicTrackVolume(
                    playlistA,
                    *trackBId,
                    std::numeric_limits<double>::infinity())
                && !model.setPlaylistMusicTrackVolume(
                    playlistA,
                    *trackBId,
                    -std::numeric_limits<double>::infinity()),
            "Playlist track volume must clamp finite values and reject non-finite values")
        || !require(!model.setPlaylistMusicTrackVolume(
                playlistA,
                DesktopSceneEngine::Player::TrackId{},
                0.5),
            "A stale playlist TrackId must not mutate a music-track volume"))
    {
        return false;
    }

    if (!require(model.movePlaylistMusicTrack(playlistA, *trackBId, -1),
            "Moving a playlist music track up should swap it with its predecessor")
        || !require(model.playlistMusicQueue(playlistA)->tracks.at(0).id == *duplicateFirstId
                && model.playlistMusicQueue(playlistA)->tracks.at(1).id == *trackBId
                && model.playlistMusicQueue(playlistA)->tracks.at(2).id == *duplicateSecondId
                && model.playlistMusicQueue(playlistA)->tracks.at(2).volume == 0.8,
            "Playlist track order must follow TrackId rather than source-path equality")
        || !require(model.movePlaylistMusicTrack(playlistA, *trackBId, 1),
            "Moving the same playlist music track down should restore its original order")
        || !require(model.movePlaylistMusicTrack(playlistA, *duplicateSecondId, -1)
                && model.playlistMusicQueue(playlistA)->tracks.at(0).id == *duplicateSecondId
                && model.playlistMusicQueue(playlistA)->tracks.at(1).id == *duplicateFirstId
                && model.movePlaylistMusicTrack(playlistA, *duplicateSecondId, 1),
            "Duplicate source paths must reorder by their independent TrackIds")
        || !require(!model.movePlaylistMusicTrack(playlistA, *duplicateFirstId, -1)
                && !model.movePlaylistMusicTrack(playlistA, *trackCId, 1),
            "First-up and last-down playlist music moves must fail safely")
        || !require(model.removePlaylistMusicTrack(playlistA, *duplicateFirstId),
            "Removing a playlist music track by TrackId should succeed")
        || !require(model.playlistMusicQueue(playlistA)->tracks.size() == 3
                && model.playlistMusicQueue(playlistA)->tracks.at(0).id == *duplicateSecondId
                && model.playlistMusicQueue(playlistA)->tracks.at(0).volume == 0.8,
            "Removing one duplicate source must preserve the other TrackId and order")
        || !require(!model.removePlaylistMusicTrack(playlistA, *duplicateFirstId),
            "Removing a stale TrackId must fail safely"))
    {
        return false;
    }

    if (!require(model.setPlaylistMusicPlaybackMode(
                playlistA, MusicPlaybackMode::CurrentSingleLoop)
            && model.setPlaylistMusicVolume(playlistA, -1.0)
            && model.playlistMusicQueue(playlistA)->playbackMode
                == MusicPlaybackMode::CurrentSingleLoop
            && model.playlistMusicQueue(playlistA)->volume == 0.0
            && model.setPlaylistMusicVolume(playlistA, 2.0)
            && model.playlistMusicQueue(playlistA)->volume == 1.0
            && !model.setPlaylistMusicVolume(
                playlistA, std::numeric_limits<double>::quiet_NaN()),
            "Playlist music mode and finite volume updates must be independent and clamp safely")
        || !require(model.addPlaylistMusicTrack(playlistB, QStringLiteral("C:/Music/b.mp3"))
                        .has_value()
                && model.playlistMusicQueue(playlistB)->tracks.size() == 1
                && model.playlistMusicQueue(playlistA)->tracks.size() == 3,
            "Playlist global music queues must remain isolated by PlaylistId"))
    {
        return false;
    }

    const auto imageTrackId = model.addItemMusicTrack(*imageItemId, QStringLiteral("C:/Music/image.wav"));
    const auto videoTrackId = model.addItemMusicTrack(*videoItemId, QStringLiteral("C:/Music/video.aac"));
    const auto otherTrackId = model.addItemMusicTrack(*otherItemId, QStringLiteral("C:/Music/other.opus"));
    const auto playlistBTrackId = model.addItemMusicTrack(
        *playlistBItemId, QStringLiteral("C:/Music/playlist-b.wma"));
    if (!require(imageTrackId.has_value() && videoTrackId.has_value() && otherTrackId.has_value()
            && playlistBTrackId.has_value(),
            "Image and Video Items in independent playlists must accept supported music tracks")
        || !require(model.itemMusicQueue(*imageItemId)->tracks.size() == 1
                && model.itemMusicQueue(*videoItemId)->tracks.size() == 1
                && model.itemMusicQueue(*otherItemId)->tracks.size() == 1
                && model.playlistMusicQueue(playlistA)->tracks.size() == 3,
            "Every Item music queue must remain independent from other Items and playlist music")
        || !require(model.setItemMusicPlaybackMode(*videoItemId, MusicPlaybackMode::Shuffle)
                && model.setItemMusicVolume(*videoItemId, 0.25)
                && model.itemMusicQueue(*videoItemId)->playbackMode == MusicPlaybackMode::Shuffle
                && model.itemMusicQueue(*videoItemId)->volume == 0.25
                && model.itemMusicQueue(*imageItemId)->volume == 1.0,
            "Item music mode and volume must remain isolated by ItemId")
        || !require(!model.addItemMusicTrack(*imageItemId, QStringLiteral("C:/Video/clip.mp4"))
                        .has_value()
                && !model.addItemMusicTrack(
                        DesktopSceneEngine::Player::ItemId{}, duplicateSource)
                        .has_value(),
            "Unsupported sources and stale ItemIds must not create item music tracks"))
    {
        return false;
    }

    if (!require(model.itemMusicQueue(*imageItemId)->tracks.at(0).volume == 1.0
            && model.itemMusicQueue(*videoItemId)->tracks.at(0).volume == 1.0
            && model.setItemMusicTrackVolume(*videoItemId, *videoTrackId, 0.4)
            && model.itemMusicQueue(*videoItemId)->tracks.at(0).volume == 0.4
            && model.itemMusicQueue(*imageItemId)->tracks.at(0).volume == 1.0
            && model.playlistMusicQueue(playlistA)->tracks.at(0).volume == 0.8
            && model.itemMusicQueue(*videoItemId)->volume == 0.25,
            "Item TrackId-scoped volume must default independently and leave Item and Playlist queue volume unchanged")
        || !require(model.setItemMusicTrackVolume(*videoItemId, *videoTrackId, -1.0)
                && model.itemMusicQueue(*videoItemId)->tracks.at(0).volume == 0.0
                && model.setItemMusicTrackVolume(*videoItemId, *videoTrackId, 2.0)
                && model.itemMusicQueue(*videoItemId)->tracks.at(0).volume == 1.0
                && !model.setItemMusicTrackVolume(
                    *videoItemId,
                    *videoTrackId,
                    std::numeric_limits<double>::quiet_NaN())
                && !model.setItemMusicTrackVolume(
                    *videoItemId,
                    *videoTrackId,
                    std::numeric_limits<double>::infinity())
                && !model.setItemMusicTrackVolume(
                    DesktopSceneEngine::Player::ItemId{},
                    *videoTrackId,
                    0.5),
            "Item track volume must clamp finite values and reject non-finite or stale identity updates"))
    {
        return false;
    }

    if (!require(model.removeItemMusicTrack(*videoItemId, *videoTrackId),
            "Removing an Item music track by TrackId should succeed")
        || !require(model.itemMusicQueue(*videoItemId)->tracks.isEmpty()
                && model.itemMusicQueue(*imageItemId)->tracks.size() == 1,
            "Removing one Item music track must not affect another Item queue")
        || !require(!model.removeItemMusicTrack(*videoItemId, *videoTrackId),
            "Removing a stale Item TrackId must fail safely")
        || !require(model.removeItem(*otherItemId)
                && model.itemMusicQueue(*otherItemId) == nullptr
                && model.itemMusicQueue(*imageItemId) != nullptr,
            "Removing an Item must destroy only its owned music configuration")
        || !require(model.removePlaylist(playlistA)
                && model.playlistMusicQueue(playlistA) == nullptr
                && model.itemMusicQueue(*imageItemId) == nullptr
                && model.playlistMusicQueue(playlistB) != nullptr
                && model.itemMusicQueue(*playlistBItemId) != nullptr,
            "Removing a Playlist must destroy its global and Item music configuration only"))
    {
        return false;
    }

    return true;
}

DesktopSceneEngine::Domain::AuthoredPlaylist makeReplacementPlaylist(
    const char* id,
    const char* alias)
{
    DesktopSceneEngine::Domain::AuthoredPlaylist playlist{};
    playlist.id = DesktopSceneEngine::Domain::PlaylistId{std::string{id}};
    playlist.displayAlias = alias;
    return playlist;
}

bool verifyBackgroundMusicEnabledContract()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistA = model.selectedPlaylistId();
    const auto playlistB = model.addPlaylist();
    const auto itemA = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/enabled-a.png"));
    const auto itemA2 = model.addItem(
        playlistA,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/enabled-a2.mp4"));
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/enabled-b.png"));
    if (!require(itemA.has_value() && itemA2.has_value() && itemB.has_value(),
            "BGM Enabled testing requires Items in multiple Playlists"))
    {
        return false;
    }

    if (!require(model.playlistMusicQueue(playlistA)->enabled
                && model.playlistMusicQueue(playlistB)->enabled
                && model.itemMusicQueue(*itemA)->enabled
                && model.itemMusicQueue(*itemA2)->enabled
                && model.itemMusicQueue(*itemB)->enabled,
            "New Playlist and Item BGM compatibility queues must default Enabled to true")
        || !require(model.authoredPlaylist(playlistA)->globalMusicQueue.enabled
                && model.authoredPlaylist(playlistB)->globalMusicQueue.enabled
                && model.authoredItem(*itemA)->musicQueue.enabled
                && model.authoredItem(*itemA2)->musicQueue.enabled
                && model.authoredItem(*itemB)->musicQueue.enabled,
            "New authored Playlist and Item BGM queues must default Enabled to true"))
    {
        return false;
    }

    std::size_t notificationCount = 0U;
    model.setAuthoredChangedCallback([&notificationCount]
    {
        ++notificationCount;
    });
    std::uint64_t revision = model.authoredRevision();

    if (!require(model.setPlaylistMusicEnabled(playlistA, true)
                && model.authoredRevision() == revision
                && notificationCount == 0U,
            "Setting Playlist BGM Enabled to its default true value must be a semantic no-op")
        || !require(model.setPlaylistMusicEnabled(playlistA, false)
                && model.authoredRevision() == ++revision
                && notificationCount == 1U
                && !model.playlistMusicQueue(playlistA)->enabled
                && !model.authoredPlaylist(playlistA)->globalMusicQueue.enabled,
            "Disabling Playlist BGM must update authored and compatibility state exactly once")
        || !require(model.itemMusicQueue(*itemA)->enabled
                && model.itemMusicQueue(*itemA2)->enabled
                && model.itemMusicQueue(*itemB)->enabled
                && model.playlistMusicQueue(playlistB)->enabled,
            "Disabling one Playlist BGM queue must not change Item or peer Playlist queues")
        || !require(model.setPlaylistMusicEnabled(playlistA, false)
                && model.authoredRevision() == revision
                && notificationCount == 1U,
            "Setting Playlist BGM Enabled to the same value must be a semantic no-op")
        || !require(model.setPlaylistMusicEnabled(playlistA, true)
                && model.authoredRevision() == ++revision
                && notificationCount == 2U
                && model.playlistMusicQueue(playlistA)->enabled,
            "Re-enabling Playlist BGM must update state exactly once"))
    {
        return false;
    }

    if (!require(model.setItemMusicEnabled(*itemA, true)
                && model.authoredRevision() == revision
                && notificationCount == 2U,
            "Setting Item BGM Enabled to its default true value must be a semantic no-op")
        || !require(model.setItemMusicEnabled(*itemA, false)
                && model.authoredRevision() == ++revision
                && notificationCount == 3U
                && !model.itemMusicQueue(*itemA)->enabled
                && !model.authoredItem(*itemA)->musicQueue.enabled,
            "Disabling Item BGM must update authored and compatibility state exactly once")
        || !require(model.playlistMusicQueue(playlistA)->enabled
                && model.playlistMusicQueue(playlistB)->enabled
                && model.itemMusicQueue(*itemA2)->enabled
                && model.itemMusicQueue(*itemB)->enabled,
            "Disabling one Item BGM queue must not change Playlist or peer Item queues")
        || !require(model.setItemMusicEnabled(*itemA, false)
                && model.authoredRevision() == revision
                && notificationCount == 3U,
            "Setting Item BGM Enabled to the same value must be a semantic no-op")
        || !require(model.setItemMusicEnabled(*itemA, true)
                && model.authoredRevision() == ++revision
                && notificationCount == 4U
                && model.itemMusicQueue(*itemA)->enabled,
            "Re-enabling Item BGM must update state exactly once"))
    {
        return false;
    }

    if (!require(!model.setPlaylistMusicEnabled(
                    DesktopSceneEngine::Player::PlaylistId{},
                    false)
                && !model.setItemMusicEnabled(
                    DesktopSceneEngine::Player::ItemId{},
                    false)
                && model.authoredRevision() == revision
                && notificationCount == 4U,
            "Invalid BGM owner IDs must fail without revision or callback changes"))
    {
        return false;
    }

    const auto staleItem = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/stale-enabled.png"));
    if (!require(staleItem.has_value() && model.removeItem(*staleItem),
            "Stale Item BGM testing requires a removed Item"))
    {
        return false;
    }
    revision = model.authoredRevision();
    notificationCount = 0U;
    if (!require(!model.setItemMusicEnabled(*staleItem, false)
                && model.authoredRevision() == revision
                && notificationCount == 0U,
            "A stale Item ID must not mutate BGM Enabled state"))
    {
        return false;
    }

    const auto stalePlaylist = model.addPlaylist();
    if (!require(model.removePlaylist(stalePlaylist),
            "Stale Playlist BGM testing requires a removed Playlist"))
    {
        return false;
    }
    revision = model.authoredRevision();
    notificationCount = 0U;
    if (!require(!model.setPlaylistMusicEnabled(stalePlaylist, false)
                && model.authoredRevision() == revision
                && notificationCount == 0U,
            "A stale Playlist ID must not mutate BGM Enabled state"))
    {
        return false;
    }

    model.setAuthoredChangedCallback({});
    DesktopSceneEngine::Domain::AuthoredProject replacement{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    auto replacementPlaylist = makeReplacementPlaylist(
        ReplacementPlaylistId,
        "Replacement Enabled Playlist");
    replacementPlaylist.globalMusicQueue.enabled = false;
    DesktopSceneEngine::Domain::AuthoredItem replacementItem{};
    replacementItem.id = DesktopSceneEngine::Domain::ItemId{
        std::string{ReplacementItemId}};
    replacementItem.mediaType = DesktopSceneEngine::Domain::AuthoredMediaType::Image;
    replacementItem.source = "C:/Media/replacement-enabled.png";
    replacementItem.musicQueue.enabled = false;
    replacementPlaylist.items.push_back(std::move(replacementItem));
    replacement.playlists.push_back(std::move(replacementPlaylist));
    model.replaceAuthoredProject(std::move(replacement));

    const auto replacementPlaylistId = model.playlists().front().id;
    const auto replacementItemId = model.playlists().front().items.front().id;
    if (!require(!model.playlistMusicQueue(replacementPlaylistId)->enabled
                && !model.itemMusicQueue(replacementItemId)->enabled,
            "Whole-project replacement must preserve disabled BGM in compatibility queues"))
    {
        return false;
    }

    model.resetAuthoredProject();
    const auto resetPlaylistId = model.addPlaylist();
    const auto resetItemId = model.addItem(
        resetPlaylistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/reset-enabled.mp4"));
    return require(resetItemId.has_value()
            && model.playlistMusicQueue(resetPlaylistId)->enabled
            && model.itemMusicQueue(*resetItemId)->enabled
            && model.authoredPlaylist(resetPlaylistId)->globalMusicQueue.enabled
            && model.authoredItem(*resetItemId)->musicQueue.enabled,
        "Reset and new authored BGM queues must restore the Enabled=true default");
}

bool verifyAuthoredProjectReplacementAndAliasOrdinal()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto oldPlaylistId = model.selectedPlaylistId();
    const auto oldItemId = model.addItem(
        oldPlaylistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/old.mp4"),
        QStringLiteral("Old"));
    if (!require(oldItemId.has_value(),
            "Replacement testing requires an old transient Item"))
    {
        return false;
    }

    const auto oldLayerId = model.addExternalMediaLayer(
        *oldItemId,
        QStringLiteral("C:/Media/overlay.png"));
    if (!require(oldLayerId.has_value() && model.activateItem(*oldItemId),
            "Replacement testing requires transient selection, playback, and media state"))
    {
        return false;
    }

    DesktopSceneEngine::Domain::AuthoredProject replacement{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    replacement.desktopCanvas.aspectRatio = {21, 9};
    replacement.playlistPlaybackMode =
        DesktopSceneEngine::Domain::PlaybackSequenceMode::Random;

    auto richPlaylist = makeReplacementPlaylist(
        ReplacementPlaylistId,
        "Playlist A");
    richPlaylist.enabled = false;
    richPlaylist.masterVolume = 0.4;
    richPlaylist.itemPlaybackMode =
        DesktopSceneEngine::Domain::PlaybackSequenceMode::RepeatCurrent;
    richPlaylist.globalMusicQueue.playbackMode =
        DesktopSceneEngine::Domain::MusicPlaybackMode::Shuffle;
    richPlaylist.globalMusicQueue.volume = 0.6;
    richPlaylist.globalMusicQueue.tracks.push_back({
        DesktopSceneEngine::Domain::TrackId{std::string{ReplacementTrackId}},
        "C:/Music/replacement.m4a",
        "Replacement Track",
        0.3});

    DesktopSceneEngine::Domain::AuthoredItem item{};
    item.id = DesktopSceneEngine::Domain::ItemId{std::string{ReplacementItemId}};
    item.displayAlias = "Replacement Item";
    item.mediaType = DesktopSceneEngine::Domain::AuthoredMediaType::Video;
    item.source = "C:/Media/replacement.mp4";
    item.videoPlaybackRate = 1.7;
    item.itemMediaVolume = 0.25;
    item.presentation.crop = {0.1F, 0.2F, 0.7F, 0.6F};
    item.presentation.layout = {0.25F, -0.5F, 1.2F, 0.8F};
    item.presentation.mediaPresentation = {
        MediaPresentationMode::Custom,
        AspectRatio{4, 3}};
    richPlaylist.items.push_back(item);
    replacement.playlists.push_back(std::move(richPlaylist));
    replacement.playlists.push_back(makeReplacementPlaylist(
        "44444444-4444-4444-8444-444444444444", "Playlist Z"));
    replacement.playlists.push_back(makeReplacementPlaylist(
        "55555555-5555-4555-8555-555555555555", "Playlist AA"));
    replacement.playlists.push_back(makeReplacementPlaylist(
        "66666666-6666-4666-8666-666666666666", "Playlist AZ"));
    replacement.playlists.push_back(makeReplacementPlaylist(
        "77777777-7777-4777-8777-777777777777", "Playlist BA"));
    replacement.playlists.push_back(makeReplacementPlaylist(
        "88888888-8888-4888-8888-888888888888", "Custom"));
    replacement.playlists.push_back(makeReplacementPlaylist(
        "99999999-9999-4999-8999-999999999999", "Playlist a"));
    replacement.playlists.push_back(makeReplacementPlaylist(
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "Playlist AA copy"));

    const std::uint64_t revisionBefore = model.authoredRevision();
    model.replaceAuthoredProject(std::move(replacement));

    const auto& authored = model.authoredProject();
    if (!require(model.authoredRevision() == revisionBefore + 1U,
            "Whole-project replacement must advance authored revision exactly once")
        || !require(authored.playlists.size() == 8U && model.playlists().size() == 8,
            "Replacement must rebuild the compatibility Playlist projection")
        || !require(authored.playlists.front().id.value() == ReplacementPlaylistId
                && authored.playlists.front().items.front().id.value() == ReplacementItemId
                && authored.playlists.front().globalMusicQueue.tracks.front().id.value()
                    == ReplacementTrackId,
            "Replacement must preserve loaded Playlist, Item, and Track identities exactly")
        || !require(model.playlists().front().displayAlias == QStringLiteral("Playlist A")
                && !model.playlists().front().enabled
                && model.playlists().front().masterVolume == 0.4
                && model.playlists().front().items.front().playbackRate == 1.7,
            "Replacement must preserve authored semantics in the compatibility view")
        || !require(!model.selectedPlaylistId().isValid()
                && !model.selectedItemId().has_value()
                && !model.activePlaylistId().isValid()
                && !model.activeItemId().has_value(),
            "Replacement must clear all transient selected and active identities")
        || !require(model.mediaLayer(*oldLayerId) == nullptr,
            "Replacement must clear transient media-layer attachments"))
    {
        return false;
    }

    const auto nextPlaylistId = model.addPlaylist();
    return require(
        model.playlist(nextPlaylistId)->displayAlias == QStringLiteral("Playlist BB"),
        "Replacement must recalculate aliases from exact A/Z/AA/AZ/BA default grammar only");
}

bool verifyEmptyReset()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto itemId = model.addItem(
        model.selectedPlaylistId(),
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/reset.png"));
    const auto layerId = itemId.has_value()
        ? model.addExternalMediaLayer(*itemId, QStringLiteral("C:/Media/reset-overlay.png"))
        : std::nullopt;
    if (!require(itemId.has_value() && layerId.has_value(),
            "Reset testing requires authored and transient state"))
    {
        return false;
    }

    const std::uint64_t revisionBefore = model.authoredRevision();
    const auto ownerBefore = model.authoredProject().displayOwner;
    model.resetAuthoredProject();
    if (!require(model.authoredRevision() == revisionBefore + 1U,
            "Empty reset must advance authored revision exactly once")
        || !require(model.authoredProject().playlists.empty() && model.playlists().isEmpty(),
            "Empty reset must establish an empty AuthoredProject without a Playlist")
        || !require(model.authoredProject().displayOwner == ownerBefore,
            "Empty reset must preserve the current AuthoredProject owner")
        || !require(!model.selectedPlaylistId().isValid()
                && !model.selectedItemId().has_value()
                && !model.activePlaylistId().isValid()
                && !model.activeItemId().has_value(),
            "Empty reset must clear selection and active identity")
        || !require(model.mediaLayer(*layerId) == nullptr,
            "Empty reset must clear transient media-layer attachments"))
    {
        return false;
    }

    const auto playlistId = model.addPlaylist();
    return require(
        model.playlist(playlistId)->displayAlias == QStringLiteral("Playlist A"),
        "The next default alias after empty reset must be Playlist A");
}

bool verifyAuthoredRevisionSemantics()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    std::uint64_t revision = model.authoredRevision();

    model.setDesktopCanvas(DesktopCanvas{AspectRatio{4, 3}});
    if (!require(model.authoredRevision() == ++revision,
            "A project-level Canvas change must advance revision"))
    {
        return false;
    }
    model.setDesktopCanvas(DesktopCanvas{AspectRatio{4, 3}});
    if (!require(model.authoredRevision() == revision,
            "Setting the same Canvas must be a semantic no-op")
        || !require(model.renamePlaylist(playlistId, QStringLiteral("Playlist A"))
                && model.authoredRevision() == revision,
            "Renaming to the same alias must succeed without dirtying")
        || !require(model.setPlaylistMasterVolume(playlistId, 2.0)
                && model.authoredRevision() == revision,
            "A clamped Playlist volume equal to the stored value must not dirty"))
    {
        return false;
    }

    if (!require(model.setPlaylistEnabled(playlistId, false)
                && model.authoredRevision() == ++revision,
            "A Playlist setter change must advance revision")
        || !require(model.setPlaylistEnabled(playlistId, false)
                && model.authoredRevision() == revision,
            "An identical Playlist setter must not advance revision"))
    {
        return false;
    }

    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/revision.png"));
    if (!require(itemId.has_value() && model.authoredRevision() == ++revision,
            "A structural Item add must advance revision once")
        || !require(model.setImageDisplayDuration(*itemId, 12.0)
                && model.authoredRevision() == ++revision,
            "An Item setter change must advance revision")
        || !require(model.setImageDisplayDuration(*itemId, 12.0)
                && model.authoredRevision() == revision,
            "An identical Item setter must not advance revision"))
    {
        return false;
    }

    const WorkspaceItemPresentation presentation{
        {0.1F, 0.2F, 0.7F, 0.6F},
        {0.25F, -0.25F, 1.2F, 0.8F},
        MediaPresentation{MediaPresentationMode::Custom, AspectRatio{4, 3}}};
    if (!require(model.setItemPresentation(*itemId, presentation)
                && model.authoredRevision() == ++revision,
            "A presentation change must advance revision")
        || !require(model.setItemPresentation(*itemId, presentation)
                && model.authoredRevision() == revision,
            "An identical presentation must not advance revision"))
    {
        return false;
    }

    const auto trackId = model.addItemMusicTrack(
        *itemId,
        QStringLiteral("C:/Music/revision.m4a"));
    if (!require(trackId.has_value() && model.authoredRevision() == ++revision,
            "A structural BGM Track add must advance revision once")
        || !require(model.setItemMusicTrackVolume(*itemId, *trackId, 0.5)
                && model.authoredRevision() == ++revision,
            "A BGM setter change must advance revision")
        || !require(model.setItemMusicTrackVolume(*itemId, *trackId, 0.5)
                && model.authoredRevision() == revision,
            "An identical BGM setter must not advance revision"))
    {
        return false;
    }

    if (!require(!model.renamePlaylist(
                    DesktopSceneEngine::Player::PlaylistId{},
                    QStringLiteral("Missing"))
                && model.authoredRevision() == revision,
            "A failed authored mutation must not advance revision")
        || !require(!model.moveItem(*itemId, -1)
                && model.authoredRevision() == revision,
            "A failed boundary move must not advance revision"))
    {
        return false;
    }

    const auto layerId = model.addExternalMediaLayer(
        *itemId,
        QStringLiteral("C:/Media/transient-overlay.png"));
    if (!require(model.selectPlaylist(playlistId)
                && model.selectItem(*itemId)
                && model.setActiveItem(*itemId)
                && layerId.has_value(),
            "Transient revision testing requires valid operations"))
    {
        return false;
    }
    model.clearActivePlayback();
    return require(model.authoredRevision() == revision,
        "Selection, active playback, and media-layer attachments must not dirty authored state");
}

bool verifyAuthoredChangeNotification()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    std::size_t notificationCount = 0U;
    std::uint64_t observedRevision = 0U;
    model.setAuthoredChangedCallback([&]
    {
        ++notificationCount;
        observedRevision = model.authoredRevision();
    });

    const std::uint64_t initialRevision = model.authoredRevision();
    model.setDesktopCanvas(DesktopCanvas{AspectRatio{4, 3}});
    if (!require(notificationCount == 1U
            && observedRevision == initialRevision + 1U,
            "An authored mutation must notify once after its revision advances"))
    {
        return false;
    }

    model.setDesktopCanvas(DesktopCanvas{AspectRatio{4, 3}});
    if (!require(notificationCount == 1U,
            "A semantic no-op must not emit authored-change notification"))
    {
        return false;
    }

    if (!require(!model.renamePlaylist(
                    DesktopSceneEngine::Player::PlaylistId{},
                    QStringLiteral("Missing"))
            && notificationCount == 1U,
            "A failed authored mutation must not emit notification"))
    {
        return false;
    }

    const auto playlistId = model.selectedPlaylistId();
    if (!require(model.selectPlaylist(playlistId)
            && notificationCount == 1U,
            "A transient selection change must not emit authored notification"))
    {
        return false;
    }

    const auto trackId = model.addPlaylistMusicTrack(
        playlistId,
        QStringLiteral("C:/Music/notification.m4a"));
    if (!require(trackId.has_value() && notificationCount == 2U,
            "A BGM authored mutation must use the centralized notification"))
    {
        return false;
    }

    model.replaceAuthoredProject(DesktopSceneEngine::Domain::AuthoredProject{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()});
    if (!require(notificationCount == 3U,
            "Whole-project replacement must emit exactly one notification"))
    {
        return false;
    }

    model.setAuthoredChangedCallback({});
    (void)model.addPlaylist();
    return require(notificationCount == 3U,
        "Clearing the callback must prevent calls through a stale owner");
}

}

int main()
{
    return verifyCanonicalAuthoredProjectOwnership()
            && verifyPlaylistBehavior()
            && verifyVisualPlaybackSequenceConfiguration()
            && verifyItemAndMediaLayerBehavior()
            && verifyCanvasAndItemPresentationOwnership()
            && verifyVideoItemMediaVolumeBehavior()
            && verifyAvailabilityAndOrdering()
            && verifyBackgroundMusicConfiguration()
            && verifyBackgroundMusicEnabledContract()
            && verifyAuthoredProjectReplacementAndAliasOrdinal()
            && verifyEmptyReset()
            && verifyAuthoredRevisionSemantics()
            && verifyAuthoredChangeNotification()
        ? 0
        : 1;
}
