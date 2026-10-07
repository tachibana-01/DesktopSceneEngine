#include "ActivePlaybackProjection.h"
#include "MainWindowTestDisplay.h"
#include "WorkspaceModel.h"

#include <DesktopSceneEngine/Engine.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{

using DesktopSceneEngine::Engine;
using DesktopSceneEngine::Player::ActivePlaybackProjection;
using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::WorkspaceMediaType;
using DesktopSceneEngine::Player::WorkspaceModel;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool nearlyEqual(float left, float right, float tolerance = 0.02F)
{
    return std::fabs(left - right) <= tolerance;
}

std::vector<std::filesystem::path> filesWithExtensions(
    const std::filesystem::path& directory,
    const std::vector<std::string>& extensions
)
{
    std::vector<std::filesystem::path> files{};
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::string extension = entry.path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        if (std::find(extensions.begin(), extensions.end(), extension)
            != extensions.end())
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::string utf8Path(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

bool verifyFinalStorageRebuild()
{
    const std::filesystem::path resourceRoot{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    const auto images = filesWithExtensions(
        resourceRoot / "Images", {".jpg", ".jpeg", ".png"});
    const auto videos = filesWithExtensions(resourceRoot / "Videos", {".mp4"});
    if (!require(!images.empty() && !videos.empty(),
            "The rebuild test requires one image and one MP4 resource"))
    {
        return false;
    }

    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QString::fromStdString(utf8Path(videos.front())),
        QStringLiteral("B"));
    if (!require(itemA.has_value() && itemB.has_value(),
            "The original playlist should contain two valid items")
        || !require(model.setVideoPlaybackRate(*itemB, 1.5),
            "The active video should use a non-default configured speed"))
    {
        return false;
    }

    std::optional<ActivePlaybackProjection> activeProjection =
        DesktopSceneEngine::Player::makeActivePlaybackProjection(
            *model.authoredPlaylist(playlistId));
    if (!require(activeProjection.has_value(),
            "The original playlist should produce a bindable projection"))
    {
        return false;
    }

    const auto activeSceneIndex = activeProjection->sceneIndexForItem(*itemB);
    if (!require(activeSceneIndex.has_value(),
            "The stable active item should resolve in the original projection"))
    {
        return false;
    }

    Engine engine{};
    engine.initialize();
    if (!require(engine.bindProject(
            activeProjection->project,
            activeProjection->project.scenes[*activeSceneIndex].id),
            "Engine should bind the original projection from its stored location")
        || !require(engine.seek(0.25F),
            "The active video should seek before rebuilding")
        || !require(engine.playbackSpeed() == 1.5F,
            "The original binding should configure the active video speed"))
    {
        engine.shutdown();
        return false;
    }

    engine.setVolume(0.65F);
    engine.toggleMute();
    engine.togglePause();
    const float oldTime = engine.playbackTime();
    const float oldSpeed = engine.playbackSpeed();
    const float oldVolume = engine.volume();
    const bool oldMute = engine.isMuted();
    const bool oldPaused = engine.isPaused();
    if (!require(oldPaused, "The source playback should be paused before rebuilding"))
    {
        engine.shutdown();
        return false;
    }

    if (!require(model.selectItem(*itemA),
            "A non-active item should remain selected while playback is active")
        || !require(model.setActiveItem(*itemB),
            "The currently playing item should be tracked independently from selection"))
    {
        engine.shutdown();
        return false;
    }

    const auto itemC = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("C"));
    const auto itemD = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("D"));
    const auto itemE = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("E"));
    if (!require(itemC.has_value() && itemD.has_value() && itemE.has_value(),
            "The active playlist batch should append every new item before rebuilding")
        || !require(model.selectedItemId() == itemA,
            "Adding to the active playlist must preserve the user item selection")
        || !require(model.activeItemId() == itemB,
            "Adding to the active playlist must preserve the active item identity"))
    {
        engine.shutdown();
        return false;
    }

    {
        auto prepared = DesktopSceneEngine::Player::prepareActivePlaybackProjection(
            *model.authoredPlaylist(playlistId),
            model.authoredItem(*itemB)->id);
        if (!require(prepared.has_value(),
                "The rebuilt candidate should preserve the active stable item")
            || !require(activeProjection->sceneToItem.size() == 2,
                "Candidate creation must leave the still-bound old projection unchanged")
            || !require(prepared->projection.sceneToItem.size() == 5,
                "One rebuild after a batch must contain every appended item"))
        {
            engine.shutdown();
            return false;
        }

        auto oldProjection = std::move(activeProjection);
        engine.detachProject();
        activeProjection = std::move(prepared->projection);
        const auto rebuiltSceneIndex = activeProjection->sceneIndexForItem(*itemB);
        if (!require(rebuiltSceneIndex.has_value(),
                "The active item must resolve after moving the candidate into final storage")
            || !require(engine.bindProject(
                activeProjection->project,
                activeProjection->project.scenes[*rebuiltSceneIndex].id),
                "Engine must bind only the candidate Project in final storage"))
        {
            engine.shutdown();
            return false;
        }

        if (oldPaused)
        {
            engine.togglePause();
        }
        if (!require(engine.isPaused(),
                "Paused rebuild must establish pause before speed and seek restoration"))
        {
            engine.shutdown();
            return false;
        }

        engine.setPlaybackSpeed(oldSpeed);
        if (!require(engine.isPaused(),
                "Paused speed restoration must not resume playback before seek"))
        {
            engine.shutdown();
            return false;
        }
        if (!require(engine.seek(oldTime),
                "The rebuilt projection should restore same-item media time"))
        {
            engine.shutdown();
            return false;
        }
        if (!require(engine.isPaused(),
                "Paused seek restoration must remain paused after audio repositioning"))
        {
            engine.shutdown();
            return false;
        }
        // oldProjection remains alive until after the new binding and state
        // restoration are complete.
    }

    const auto rebuiltSceneIndex = activeProjection->sceneIndexForItem(*itemB);
    if (!require(rebuiltSceneIndex.has_value()
            && engine.activeSceneIndex() == *rebuiltSceneIndex,
            "The Engine should remain active on item B in the rebuilt projection")
        || !require(nearlyEqual(engine.playbackTime(), oldTime),
            "The rebuilt projection should restore the actual media time")
        || !require(nearlyEqual(engine.playbackSpeed(), oldSpeed)
                && engine.isPaused() == oldPaused,
            "The rebuilt projection should preserve speed and pause state")
        || !require(nearlyEqual(engine.volume(), oldVolume)
                && engine.isMuted() == oldMute,
            "Detach and rebind should preserve output volume and mute"))
    {
        engine.shutdown();
        return false;
    }

    if (!require(engine.switchToNextScene(),
            "Next should operate after the local candidate is destroyed")
        || !require(activeProjection->itemForSceneIndex(engine.activeSceneIndex())
                == itemC,
            "Next should enter the first batch-added item")
        || !require(engine.switchToNextScene(),
            "The rebuilt projection should advance through the batch")
        || !require(activeProjection->itemForSceneIndex(engine.activeSceneIndex())
                == itemD,
            "The second batch-added item should participate in the active sequence")
        || !require(engine.switchToNextScene(),
            "The rebuilt projection should retain every batch-added item")
        || !require(activeProjection->itemForSceneIndex(engine.activeSceneIndex())
                == itemE,
            "The final batch-added item should participate in the active sequence")
        || !require(engine.switchToNextScene(),
            "The rebuilt projection should retain its original wrap behavior")
        || !require(activeProjection->itemForSceneIndex(engine.activeSceneIndex())
                == itemA,
            "The batch-expanded sequence should wrap to its original first item"))
    {
        engine.shutdown();
        return false;
    }

    engine.shutdown();
    return true;
}

bool verifyNonActivePlaylistAddLeavesPlaybackUntouched()
{
    const std::filesystem::path resourceRoot{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    const auto images = filesWithExtensions(
        resourceRoot / "Images", {".jpg", ".jpeg", ".png"});
    if (!require(!images.empty(),
            "The non-active add test requires one image resource"))
    {
        return false;
    }

    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto activePlaylistId = model.selectedPlaylistId();
    const auto inactivePlaylistId = model.addPlaylist();
    const auto itemA = model.addItem(
        activePlaylistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        activePlaylistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("B"));
    if (!require(itemA.has_value() && itemB.has_value(),
            "The active playlist should contain two playable items")
        || !require(model.setActiveItem(*itemB),
            "The second item should become active without changing selection"))
    {
        return false;
    }

    std::optional<ActivePlaybackProjection> activeProjection =
        DesktopSceneEngine::Player::makeActivePlaybackProjection(
            *model.authoredPlaylist(activePlaylistId));
    if (!require(activeProjection.has_value(),
            "The active playlist should produce a bindable projection"))
    {
        return false;
    }

    const auto activeSceneIndex = activeProjection->sceneIndexForItem(*itemB);
    if (!require(activeSceneIndex.has_value(),
            "The active stable item should resolve before a non-active add"))
    {
        return false;
    }

    Engine engine{};
    engine.initialize();
    if (!require(engine.bindProject(
            activeProjection->project,
            activeProjection->project.scenes[*activeSceneIndex].id),
            "Engine should bind the active playlist projection")
        || !require(engine.seek(0.5F),
            "The active image should seek before a non-active playlist mutation"))
    {
        engine.shutdown();
        return false;
    }

    const float playbackTime = engine.playbackTime();
    if (!require(model.selectPlaylist(inactivePlaylistId),
            "The user should be able to select a non-active playlist")
        || !require(activeProjection->sourcePlaylistId != inactivePlaylistId,
            "A non-active target must not be treated as the active playback playlist"))
    {
        engine.shutdown();
        return false;
    }

    const auto itemC = model.addItem(
        inactivePlaylistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("C"));
    const auto itemD = model.addItem(
        inactivePlaylistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("D"));
    if (!require(itemC.has_value() && itemD.has_value(),
            "The non-active playlist mutation should commit to the workspace")
        || !require(activeProjection->sceneToItem.size() == 2
                && engine.sceneCount() == 2U,
            "Adding to a non-active playlist must not rebuild the active Engine projection")
        || !require(nearlyEqual(engine.playbackTime(), playbackTime),
            "Adding to a non-active playlist must not disturb active playback time")
        || !require(model.selectedPlaylistId() == inactivePlaylistId
                && !model.selectedItemId().has_value(),
            "Adding to a non-active playlist must preserve its user selection")
        || !require(model.activeItemId() == itemB,
            "Adding to a non-active playlist must preserve the active item"))
    {
        engine.shutdown();
        return false;
    }

    engine.shutdown();
    return true;
}

bool verifySuccessorIndexAfterCurrentItemRemoval()
{
    using DesktopSceneEngine::Player::successorIndexAfterRemovingActiveItem;

    return require(
               successorIndexAfterRemovingActiveItem(2U, 3U) == std::optional<std::size_t>{2U},
               "Removing a middle active item should target the item now at its old index")
        && require(
               successorIndexAfterRemovingActiveItem(2U, 2U) == std::optional<std::size_t>{0U},
               "Removing the final active item should wrap to the first remaining item")
        && require(
               successorIndexAfterRemovingActiveItem(0U, 2U) == std::optional<std::size_t>{0U},
               "Removing the first active item should target the new first item")
        && require(
               !successorIndexAfterRemovingActiveItem(0U, 0U).has_value(),
               "Removing the only active item should produce no successor");
}

bool verifyNonCurrentActivePlaylistDeleteRebuild()
{
    const std::filesystem::path resourceRoot{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    const auto images = filesWithExtensions(
        resourceRoot / "Images", {".jpg", ".jpeg", ".png"});
    const auto videos = filesWithExtensions(resourceRoot / "Videos", {".mp4"});
    if (!require(!images.empty() && !videos.empty(),
            "The non-current delete test requires one image and one video resource"))
    {
        return false;
    }

    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("B"));
    const auto itemC = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QString::fromStdString(utf8Path(videos.front())),
        QStringLiteral("C"));
    const auto itemD = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("D"));
    if (!require(itemA.has_value() && itemB.has_value() && itemC.has_value()
                && itemD.has_value(),
            "The delete test playlist should contain four items")
        || !require(model.setVideoPlaybackRate(*itemC, 1.5),
            "The preserved active video should have a non-default speed"))
    {
        return false;
    }

    std::optional<ActivePlaybackProjection> activeProjection =
        DesktopSceneEngine::Player::makeActivePlaybackProjection(
            *model.authoredPlaylist(playlistId));
    const auto activeSceneIndex = activeProjection.has_value()
        ? activeProjection->sceneIndexForItem(*itemC)
        : std::nullopt;
    if (!require(activeSceneIndex.has_value(),
            "The active stable item should resolve before deleting another item"))
    {
        return false;
    }

    Engine engine{};
    engine.initialize();
    if (!require(engine.bindProject(
            activeProjection->project,
            activeProjection->project.scenes[*activeSceneIndex].id),
            "The original delete projection should bind")
        || !require(engine.seek(0.25F),
            "The active video should seek before deleting another item"))
    {
        engine.shutdown();
        return false;
    }

    engine.setVolume(0.65F);
    engine.toggleMute();
    engine.togglePause();
    const float oldTime = engine.playbackTime();
    const float oldSpeed = engine.playbackSpeed();
    const float oldVolume = engine.volume();
    const bool oldMute = engine.isMuted();
    if (!require(model.selectItem(*itemA),
            "A non-active item should remain selected before deletion")
        || !require(model.setActiveItem(*itemC),
            "The active item should remain independent from user selection")
        || !require(model.removeItem(*itemB),
            "Removing a non-current item should update the workspace"))
    {
        engine.shutdown();
        return false;
    }

    auto prepared = DesktopSceneEngine::Player::prepareActivePlaybackProjection(
        *model.authoredPlaylist(playlistId),
        model.authoredItem(*itemC)->id);
    if (!require(prepared.has_value(),
            "The updated projection should preserve the surviving current item")
        || !require(prepared->projection.sceneToItem.size() == 3
                && prepared->projection.sceneToItem.at(0) == *itemA
                && prepared->projection.sceneToItem.at(1) == *itemC
                && prepared->projection.sceneToItem.at(2) == *itemD,
            "The rebuilt projection should remove only the deleted non-current item"))
    {
        engine.shutdown();
        return false;
    }

    {
        auto oldProjection = std::move(activeProjection);
        engine.detachProject();
        activeProjection = std::move(prepared->projection);
        const auto rebuiltSceneIndex = activeProjection->sceneIndexForItem(*itemC);
        if (!require(rebuiltSceneIndex.has_value()
                && engine.bindProject(
                    activeProjection->project,
                    activeProjection->project.scenes[*rebuiltSceneIndex].id),
                "The rebuilt non-current delete projection should bind"))
        {
            engine.shutdown();
            return false;
        }

        engine.togglePause();
        engine.setPlaybackSpeed(oldSpeed);
        if (!require(engine.seek(oldTime),
                "The surviving current item should restore its media time"))
        {
            engine.shutdown();
            return false;
        }
    }

    if (!require(activeProjection->itemForSceneIndex(engine.activeSceneIndex()) == itemC,
            "Deleting another item must keep the same active stable item")
        || !require(nearlyEqual(engine.playbackTime(), oldTime)
                && nearlyEqual(engine.playbackSpeed(), oldSpeed)
                && engine.isPaused(),
            "Non-current deletion must preserve time, speed, and paused state")
        || !require(nearlyEqual(engine.volume(), oldVolume) && engine.isMuted() == oldMute,
            "Non-current deletion must preserve output volume and mute")
        || !require(model.selectedItemId() == itemA && model.activeItemId() == itemC,
            "Non-current deletion must preserve selected and active identities separately")
        || !require(engine.switchToNextScene()
                && activeProjection->itemForSceneIndex(engine.activeSceneIndex()) == itemD,
            "Next after non-current deletion should use the rebuilt sequence")
        || !require(
            (engine.update(10.1F),
                activeProjection->itemForSceneIndex(engine.activeSceneIndex()) == itemA),
            "Automatic transition after non-current deletion should use the rebuilt sequence"))
    {
        engine.shutdown();
        return false;
    }

    engine.shutdown();
    return true;
}

bool verifyCurrentActivePlaylistDeleteStartsSuccessor()
{
    const std::filesystem::path resourceRoot{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    const auto images = filesWithExtensions(
        resourceRoot / "Images", {".jpg", ".jpeg", ".png"});
    const auto videos = filesWithExtensions(resourceRoot / "Videos", {".mp4"});
    if (!require(!images.empty() && !videos.empty(),
            "The current-item delete test requires one image and one video resource"))
    {
        return false;
    }

    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("B"));
    const auto itemC = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QString::fromStdString(utf8Path(videos.front())),
        QStringLiteral("C"));
    const auto itemD = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QString::fromStdString(utf8Path(videos.front())),
        QStringLiteral("D"));
    if (!require(itemA.has_value() && itemB.has_value() && itemC.has_value()
                && itemD.has_value(),
            "The current-item delete playlist should contain four items")
        || !require(model.setVideoPlaybackRate(*itemD, 1.5),
            "The successor should carry its own configured playback speed"))
    {
        return false;
    }

    std::optional<ActivePlaybackProjection> activeProjection =
        DesktopSceneEngine::Player::makeActivePlaybackProjection(
            *model.authoredPlaylist(playlistId));
    const auto oldActiveIndex = activeProjection.has_value()
        ? activeProjection->sceneIndexForItem(*itemC)
        : std::nullopt;
    if (!require(oldActiveIndex == std::optional<std::size_t>{2U},
            "The third item should be the current deletion target"))
    {
        return false;
    }

    Engine engine{};
    engine.initialize();
    if (!require(engine.bindProject(
            activeProjection->project,
            activeProjection->project.scenes[*oldActiveIndex].id),
            "The current deletion source projection should bind"))
    {
        engine.shutdown();
        return false;
    }

    engine.setVolume(0.65F);
    engine.toggleMute();
    engine.togglePause();
    const float oldVolume = engine.volume();
    const bool oldMute = engine.isMuted();
    if (!require(model.selectItem(*itemA),
            "A non-active item should remain selected before current deletion")
        || !require(model.setActiveItem(*itemC),
            "The current item should be active before deletion")
        || !require(model.removeItem(*itemC),
            "Removing the current item should update the workspace"))
    {
        engine.shutdown();
        return false;
    }

    const auto* updatedPlaylist = model.playlist(playlistId);
    std::optional<std::size_t> successorIndex{};
    if (updatedPlaylist != nullptr)
    {
        successorIndex = DesktopSceneEngine::Player::successorIndexAfterRemovingActiveItem(
            *oldActiveIndex,
            static_cast<std::size_t>(updatedPlaylist->items.size()));
    }
    if (!require(successorIndex == std::optional<std::size_t>{2U},
            "Deleting the middle current item should select its logical successor")
        || !require(updatedPlaylist != nullptr
                && updatedPlaylist->items[static_cast<qsizetype>(*successorIndex)].id == *itemD,
            "The successor should be the item now occupying the deleted index"))
    {
        engine.shutdown();
        return false;
    }

    auto prepared = DesktopSceneEngine::Player::prepareActivePlaybackProjection(
        *model.authoredPlaylist(playlistId),
        model.authoredItem(
            updatedPlaylist->items[static_cast<qsizetype>(*successorIndex)].id)->id);
    if (!require(prepared.has_value(),
            "The current-item delete successor should produce a bindable projection")
        || !require(prepared->projection.sceneToItem.size() == 3
                && prepared->projection.sceneToItem.at(0) == *itemA
                && prepared->projection.sceneToItem.at(1) == *itemB
                && prepared->projection.sceneToItem.at(2) == *itemD,
            "The rebuilt sequence should exclude the deleted current item"))
    {
        engine.shutdown();
        return false;
    }

    {
        auto oldProjection = std::move(activeProjection);
        engine.detachProject();
        activeProjection = std::move(prepared->projection);
        if (!require(engine.bindProject(
                activeProjection->project,
                activeProjection->project.scenes[prepared->targetSceneIndex].id),
                "The current-item successor projection should bind"))
        {
            engine.shutdown();
            return false;
        }
    }

    if (!require(activeProjection->itemForSceneIndex(engine.activeSceneIndex()) == itemD,
            "Deleting the current item must activate its logical successor")
        || !require(nearlyEqual(engine.playbackTime(), 0.0F)
                && nearlyEqual(engine.playbackSpeed(), 1.5F)
                && !engine.isPaused(),
            "The successor should start normally with its own configured rate")
        || !require(nearlyEqual(engine.volume(), oldVolume) && engine.isMuted() == oldMute,
            "Current-item deletion must preserve output volume and mute")
        || !require(model.selectedItemId() == itemA,
            "Current-item deletion must not hijack an unrelated user selection")
        || !require(model.setActiveItem(*itemD) && model.activeItemId() == itemD,
            "Current-item deletion should update the active marker to its successor"))
    {
        engine.shutdown();
        return false;
    }

    engine.shutdown();
    return true;
}

bool verifyNonActivePlaylistDeleteLeavesPlaybackUntouched()
{
    const std::filesystem::path resourceRoot{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    const auto images = filesWithExtensions(
        resourceRoot / "Images", {".jpg", ".jpeg", ".png"});
    if (!require(!images.empty(),
            "The non-active delete test requires one image resource"))
    {
        return false;
    }

    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto activePlaylistId = model.selectedPlaylistId();
    const auto inactivePlaylistId = model.addPlaylist();
    const auto itemA = model.addItem(
        activePlaylistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        activePlaylistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("B"));
    const auto itemX = model.addItem(
        inactivePlaylistId,
        WorkspaceMediaType::Image,
        QString::fromStdString(utf8Path(images.front())),
        QStringLiteral("X"));
    if (!require(itemA.has_value() && itemB.has_value() && itemX.has_value(),
            "The active and inactive playlists should contain their requested items"))
    {
        return false;
    }

    std::optional<ActivePlaybackProjection> activeProjection =
        DesktopSceneEngine::Player::makeActivePlaybackProjection(
            *model.authoredPlaylist(activePlaylistId));
    const auto activeSceneIndex = activeProjection.has_value()
        ? activeProjection->sceneIndexForItem(*itemA)
        : std::nullopt;
    if (!require(activeSceneIndex.has_value(),
            "The active playback item should resolve before a non-active delete"))
    {
        return false;
    }

    Engine engine{};
    engine.initialize();
    if (!require(engine.bindProject(
            activeProjection->project,
            activeProjection->project.scenes[*activeSceneIndex].id),
            "The active playlist projection should bind before a non-active delete")
        || !require(engine.seek(0.5F),
            "The active image should seek before the unrelated deletion"))
    {
        engine.shutdown();
        return false;
    }

    const float playbackTime = engine.playbackTime();
    if (!require(model.setActiveItem(*itemA),
            "The active marker should identify the currently playing item")
        || !require(model.selectItem(*itemX),
            "The user should be able to select an item in another playlist")
        || !require(model.removeItem(*itemX),
            "Deleting an item in a non-active playlist should update only that playlist"))
    {
        engine.shutdown();
        return false;
    }

    if (!require(activeProjection->sourcePlaylistId == activePlaylistId
                && activeProjection->sceneToItem.size() == 2
                && engine.sceneCount() == 2U,
            "A non-active playlist deletion must not replace the active Engine projection")
        || !require(nearlyEqual(engine.playbackTime(), playbackTime),
            "A non-active playlist deletion must not disturb active playback time")
        || !require(model.activeItemId() == itemA
                && model.selectedPlaylistId() == inactivePlaylistId
                && !model.selectedItemId().has_value(),
            "Non-active deletion must preserve active playback while applying normal selection invalidation"))
    {
        engine.shutdown();
        return false;
    }

    engine.shutdown();
    return true;
}

}

int main()
{
    return verifyFinalStorageRebuild()
            && verifyNonActivePlaylistAddLeavesPlaybackUntouched()
            && verifySuccessorIndexAfterCurrentItemRemoval()
            && verifyNonCurrentActivePlaylistDeleteRebuild()
            && verifyCurrentActivePlaylistDeleteStartsSuccessor()
            && verifyNonActivePlaylistDeleteLeavesPlaybackUntouched()
        ? 0
        : 1;
}
