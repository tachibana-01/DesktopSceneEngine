#include "ActivePlaybackProjection.h"
#include "EffectiveVolume.h"
#include "MainWindowTestDisplay.h"
#include "TimelinePreviewSource.h"

#include "WorkspaceModel.h"

#include <cmath>
#include <iostream>

namespace
{

using DesktopSceneEngine::Player::ActivePlaybackProjection;
using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::WorkspaceMediaType;
using DesktopSceneEngine::Player::WorkspaceModel;
using DesktopSceneEngine::Player::WorkspaceItem;
using DesktopSceneEngine::Player::WorkspaceItemPresentation;
using DesktopSceneEngine::Player::WorkspacePlaylist;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyProjection()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto imageId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/landscape.png"),
        QStringLiteral("Landscape"));
    const auto videoId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/clip.mp4"),
        QStringLiteral("Clip"));
    if (!require(imageId.has_value() && videoId.has_value(),
            "The test Workspace items should be created")
        || !require(model.setImageDisplayDuration(*imageId, 12.5),
            "The image duration should be configurable")
        || !require(model.setVideoPlaybackRate(*videoId, 1.5),
            "The video rate should be configurable")
        || !require(model.setItemEnabled(*videoId, false),
            "The raw projection test should include a disabled authored Item"))
    {
        return false;
    }

    const auto projection = DesktopSceneEngine::Player::makeActivePlaybackProjection(
        model.authoredProject().playlists.front());
    if (!require(projection.has_value(), "A populated playlist should project")
        || !require(projection->sourcePlaylistId == playlistId,
            "The projection should retain its source playlist identity")
        || !require(projection->project.library.media.size() == 2U
                && projection->project.scenes.size() == 2U,
            "Each authored item, including disabled items, should produce one media value and one scene")
        || !require(projection->project.scenes[0].duration == 12.5F,
            "Image display duration should transfer to the projected scene")
        || !require(projection->project.scenes[0].playback.speed == 1.0F,
            "Projected images should use normal playback speed")
        || !require(projection->project.library.media[0].resourceReference
                == "C:/Media/landscape.png",
            "Image source paths should transfer unchanged")
        || !require(projection->project.library.media[0].name == "Landscape"
                && projection->project.library.media[1].name == "Clip",
            "Authored UTF-8 display aliases should transfer directly")
        || !require(projection->project.library.media[1].resourceReference
                == "C:/Media/clip.mp4",
            "Video source paths should transfer unchanged")
        || !require(projection->project.library.media[0].id.value()
                == "media-" + imageId->value.toString(QUuid::WithoutBraces).toStdString()
                && projection->project.scenes[1].id.value()
                == "scene-" + videoId->value.toString(QUuid::WithoutBraces).toStdString(),
            "Temporary Engine IDs should derive directly from canonical authored Item IDs")
        || !require(std::fabs(projection->project.scenes[1].playback.speed - 1.5F)
                < 0.001F,
            "Video playback rate should transfer to the projected scene")
        || !require(projection->project.scenes[0].crop.x == 0.0F
                && projection->project.scenes[0].crop.y == 0.0F
                && projection->project.scenes[0].crop.width == 1.0F
                && projection->project.scenes[0].crop.height == 1.0F
                && projection->project.scenes[0].layout.positionX == 0.0F
                && projection->project.scenes[0].layout.positionY == 0.0F
                && projection->project.scenes[0].layout.scaleX == 1.0F
                && projection->project.scenes[0].layout.scaleY == 1.0F,
            "Default item presentation should preserve the current default Scene values")
        || !require(projection->sceneIndexForItem(*imageId) == 0U
                && projection->sceneIndexForItem(*videoId) == 1U,
            "Item identifiers should map to projected scene indices in item order")
        || !require(projection->itemForSceneIndex(0U) == imageId
                && projection->itemForSceneIndex(1U) == videoId,
            "Projected scene indices should map back to stable item identifiers"))
    {
        return false;
    }

    return true;
}

bool verifyRejectedProjection()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    if (!require(
            !DesktopSceneEngine::Player::makeActivePlaybackProjection(
                *model.authoredPlaylist(model.selectedPlaylistId())).has_value(),
            "An empty playlist should not produce a bindable projection"))
    {
        return false;
    }

    DesktopSceneEngine::Domain::AuthoredPlaylist invalidPlaylist{};
    invalidPlaylist.id = DesktopSceneEngine::Domain::PlaylistId{
        "00000000-0000-0000-0000-000000000001"};
    invalidPlaylist.items.push_back(DesktopSceneEngine::Domain::AuthoredItem{
        DesktopSceneEngine::Domain::ItemId{
            "00000000-0000-0000-0000-000000000002"},
        "Broken",
        DesktopSceneEngine::Domain::AuthoredMediaType::Image,
        " \t\n"});
    return require(
        !DesktopSceneEngine::Player::makeActivePlaybackProjection(invalidPlaylist)
             .has_value(),
        "A playlist with an invalid item source should be rejected as a whole");
}

bool verifyPreparedRebuildCandidate()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/a.png"),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/b.mp4"),
        QStringLiteral("B"));
    const auto itemC = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/c.png"),
        QStringLiteral("C"));
    if (!require(itemA.has_value() && itemB.has_value() && itemC.has_value(),
            "The rebuild candidate test items should be created"))
    {
        return false;
    }

    const auto oldProjection = DesktopSceneEngine::Player::makeActivePlaybackProjection(
        *model.authoredPlaylist(playlistId));
    if (!require(oldProjection.has_value(),
            "The original projection should remain available before rebuilding"))
    {
        return false;
    }

    const auto itemD = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/d.png"),
        QStringLiteral("D"));
    const auto prepared = DesktopSceneEngine::Player::prepareActivePlaybackProjection(
        *model.authoredPlaylist(playlistId),
        model.authoredItem(*itemB)->id);
    if (!require(itemD.has_value() && prepared.has_value(),
            "A rebuilt candidate should resolve the preserved stable item")
        || !require(oldProjection->sceneToItem.size() == 3,
            "Building a candidate must not mutate the still-live old projection")
        || !require(prepared->projection.sceneToItem.size() == 4,
            "The candidate should include the updated playlist membership")
        || !require(prepared->targetSceneIndex == 1U
                && prepared->projection.itemForSceneIndex(
                    prepared->targetSceneIndex) == itemB,
            "The candidate target should resolve the preserved item by stable identity"))
    {
        return false;
    }

    DesktopSceneEngine::Domain::ItemId missingItem{};
    return require(
        !DesktopSceneEngine::Player::prepareActivePlaybackProjection(
             *model.authoredPlaylist(playlistId), missingItem).has_value(),
        "A candidate must reject an item identity that is absent from its mapping");
}

bool verifyActiveTimelinePreviewSource()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto selectedImageId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/selected.png"),
        QStringLiteral("Selected image"));
    const auto activeVideoId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/active.mp4"),
        QStringLiteral("Active video"));
    if (!require(selectedImageId.has_value() && activeVideoId.has_value(),
            "The active preview source test items should be created")
        || !require(model.selectItem(*selectedImageId),
            "The distinct selected item should be retained"))
    {
        return false;
    }

    const auto projection = DesktopSceneEngine::Player::makeActivePlaybackProjection(
        *model.authoredPlaylist(playlistId));
    const auto source = projection.has_value()
        ? DesktopSceneEngine::Player::timelinePreviewSourceForActiveScene(
            model,
            *projection,
            1U)
        : std::nullopt;
    return require(source.has_value()
            && source->itemId == *activeVideoId
            && source->source == QStringLiteral("C:/Media/active.mp4")
            && source->mediaType == WorkspaceMediaType::Video,
        "Timeline preview resolution must use the active projected scene rather than selection");
}

bool verifyActiveTimelinePreviewPresentation()
{
    using DesktopSceneEngine::Domain::AspectRatio;
    using DesktopSceneEngine::Domain::Crop;
    using DesktopSceneEngine::Domain::Layout;
    using DesktopSceneEngine::Domain::MediaPresentation;
    using DesktopSceneEngine::Domain::MediaPresentationMode;

    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto selectedItemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/selected.png"),
        QStringLiteral("Selected image"));
    const auto activeItemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/active.mp4"),
        QStringLiteral("Active video"));
    const WorkspaceItemPresentation selectedPresentation{
        Crop{0.0F, 0.0F, 1.0F, 1.0F},
        Layout{},
        MediaPresentation{MediaPresentationMode::Native, AspectRatio{1, 1}}};
    const WorkspaceItemPresentation activePresentation{
        Crop{0.2F, 0.1F, 0.6F, 0.8F},
        Layout{0.25F, -0.15F, 0.7F, 1.1F},
        MediaPresentation{MediaPresentationMode::Custom, AspectRatio{4, 3}}};
    if (!require(selectedItemId.has_value() && activeItemId.has_value(),
            "The active presentation test items should be created")
        || !require(model.selectItem(*selectedItemId),
            "The distinct selected item should remain selected")
        || !require(model.setItemPresentation(*selectedItemId, selectedPresentation)
                && model.setItemPresentation(*activeItemId, activePresentation),
            "The selected and active items should retain independently editable presentation"))
    {
        return false;
    }

    const auto projection = DesktopSceneEngine::Player::makeActivePlaybackProjection(
        *model.authoredPlaylist(playlistId));
    if (!require(projection.has_value() && projection->itemForSceneIndex(1U) == activeItemId,
            "The second projected scene should remain the active target independent of selection"))
    {
        return false;
    }

    const auto& activeScene = projection->project.scenes[1];
    return require(model.selectedItemId() == selectedItemId
                   && activeScene.crop.x == activePresentation.crop.x
                   && activeScene.crop.y == activePresentation.crop.y
                   && activeScene.crop.width == activePresentation.crop.width
                   && activeScene.crop.height == activePresentation.crop.height
                   && activeScene.layout.positionX == activePresentation.layout.positionX
                   && activeScene.layout.positionY == activePresentation.layout.positionY
                   && activeScene.layout.scaleX == activePresentation.layout.scaleX
                   && activeScene.layout.scaleY == activePresentation.layout.scaleY
                   && activeScene.presentation.mode
                        == activePresentation.mediaPresentation.mode
                   && activeScene.presentation.customAspect
                        == activePresentation.mediaPresentation.customAspect,
        "Active-scene presentation must remain independent from the selected item's presentation");
}

bool verifyItemPresentationProjectsToScene()
{
    DesktopSceneEngine::Domain::AuthoredPlaylist playlist{};
    playlist.id = DesktopSceneEngine::Domain::PlaylistId{
        "10000000-0000-0000-0000-000000000001"};
    const WorkspaceItemPresentation imagePresentation{
        DesktopSceneEngine::Domain::Crop{0.1F, 0.2F, 0.7F, 0.6F},
        DesktopSceneEngine::Domain::Layout{0.25F, -0.15F, 0.8F, 1.2F},
        DesktopSceneEngine::Domain::MediaPresentation{
            DesktopSceneEngine::Domain::MediaPresentationMode::Custom,
            {4, 3}}};
    const WorkspaceItemPresentation videoPresentation{
        DesktopSceneEngine::Domain::Crop{0.05F, 0.1F, 0.9F, 0.75F},
        DesktopSceneEngine::Domain::Layout{-0.3F, 0.4F, 1.1F, 0.9F},
        DesktopSceneEngine::Domain::MediaPresentation{
            DesktopSceneEngine::Domain::MediaPresentationMode::Native,
            {16, 9}}};
    const DesktopSceneEngine::Domain::ItemId imageId{
        "10000000-0000-0000-0000-000000000002"};
    const DesktopSceneEngine::Domain::ItemId videoId{
        "10000000-0000-0000-0000-000000000003"};

    playlist.items.push_back(DesktopSceneEngine::Domain::AuthoredItem{
        imageId,
        "Image",
        DesktopSceneEngine::Domain::AuthoredMediaType::Image,
        "C:/Media/image.png",
        10.0,
        1.0,
        imagePresentation});
    playlist.items.push_back(DesktopSceneEngine::Domain::AuthoredItem{
        videoId,
        "Video",
        DesktopSceneEngine::Domain::AuthoredMediaType::Video,
        "C:/Media/video.mp4",
        10.0,
        1.0,
        videoPresentation});

    const auto projection = DesktopSceneEngine::Player::makeActivePlaybackProjection(playlist);
    if (!require(projection.has_value() && projection->project.scenes.size() == 2U,
            "Each item with presentation state should still project to one scene"))
    {
        return false;
    }

    const auto& imageScene = projection->project.scenes[0];
    const auto& videoScene = projection->project.scenes[1];
    return require(
               imageScene.crop.x == imagePresentation.crop.x
                   && imageScene.crop.y == imagePresentation.crop.y
                   && imageScene.crop.width == imagePresentation.crop.width
                   && imageScene.crop.height == imagePresentation.crop.height
                   && imageScene.layout.positionX == imagePresentation.layout.positionX
                   && imageScene.layout.positionY == imagePresentation.layout.positionY
                   && imageScene.layout.scaleX == imagePresentation.layout.scaleX
                   && imageScene.layout.scaleY == imagePresentation.layout.scaleY
                   && imageScene.presentation.mode
                        == imagePresentation.mediaPresentation.mode
                   && imageScene.presentation.customAspect
                        == imagePresentation.mediaPresentation.customAspect,
               "Image presentation values must project into its matching scene")
        && require(
               videoScene.crop.x == videoPresentation.crop.x
                   && videoScene.crop.y == videoPresentation.crop.y
                   && videoScene.crop.width == videoPresentation.crop.width
                   && videoScene.crop.height == videoPresentation.crop.height
                   && videoScene.layout.positionX == videoPresentation.layout.positionX
                   && videoScene.layout.positionY == videoPresentation.layout.positionY
                   && videoScene.layout.scaleX == videoPresentation.layout.scaleX
                   && videoScene.layout.scaleY == videoPresentation.layout.scaleY
                   && videoScene.presentation.mode
                        == videoPresentation.mediaPresentation.mode
                   && videoScene.presentation.customAspect
                        == videoPresentation.mediaPresentation.customAspect,
               "Video presentation values must project into its matching scene");
}

bool verifyActiveItemIdentityForVolume()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto selectedVideoId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/selected.mp4"),
        QStringLiteral("Selected video"));
    const auto activeVideoId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/active.mp4"),
        QStringLiteral("Active video"));
    if (!require(selectedVideoId.has_value() && activeVideoId.has_value(),
            "The active-item volume test items should be created")
        || !require(model.setItemMediaVolume(*selectedVideoId, 0.2),
            "The selected video volume should be configurable")
        || !require(model.setItemMediaVolume(*activeVideoId, 0.8),
            "The active video volume should be configurable")
        || !require(model.selectItem(*selectedVideoId),
            "The selected item should remain distinct from the active scene"))
    {
        return false;
    }

    const auto projection = DesktopSceneEngine::Player::makeActivePlaybackProjection(
        *model.authoredPlaylist(playlistId));
    const auto firstItemId = projection.has_value()
        ? projection->itemForSceneIndex(0U)
        : std::nullopt;
    const auto activeItemId = projection.has_value()
        ? projection->itemForSceneIndex(1U)
        : std::nullopt;
    const auto* firstItem = firstItemId.has_value()
        ? model.item(*firstItemId)
        : nullptr;
    const auto* activeItem = activeItemId.has_value()
        ? model.item(*activeItemId)
        : nullptr;
    return require(projection.has_value() && activeItemId == activeVideoId,
            "The active scene should resolve through the projection, not selection")
        && require(model.selectedItemId() == selectedVideoId,
            "The selected item should remain Video A")
        && require(firstItem != nullptr
                && std::fabs(DesktopSceneEngine::Player::calculateEffectiveVolume(
                                 1.0,
                                 firstItem->itemMediaVolume)
                             - 0.2)
                    < 0.001,
            "The first active scene should apply Video A's volume")
        && require(activeItem != nullptr && std::fabs(activeItem->itemMediaVolume - 0.8) < 0.001,
            "The active scene should resolve Video B's item media volume")
        && require(activeItem != nullptr
                && std::fabs(DesktopSceneEngine::Player::calculateEffectiveVolume(
                                 1.0,
                                 activeItem->itemMediaVolume)
                             - 0.8)
                    < 0.001,
            "Switching to the second active scene should apply Video B's volume");
}

}

int main()
{
    return verifyProjection()
            && verifyRejectedProjection()
            && verifyPreparedRebuildCandidate()
            && verifyActiveTimelinePreviewSource()
            && verifyActiveTimelinePreviewPresentation()
            && verifyItemPresentationProjectsToScene()
            && verifyActiveItemIdentityForVolume()
        ? 0
        : 1;
}
