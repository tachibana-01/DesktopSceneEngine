#pragma once

#include "WorkspaceModel.h"

#include <DesktopSceneEngine/Domain/AuthoredProject.h>
#include <DesktopSceneEngine/Domain/Project.h>

#include <cstddef>
#include <optional>

namespace DesktopSceneEngine::Player
{

// Owns the temporary Domain sequence currently borrowed by Engine together with
// the stable Workspace item mapping for that sequence.
struct ActivePlaybackProjection
{
    Domain::Project project{};
    PlaylistId sourcePlaylistId{};
    QVector<ItemId> sceneToItem{};

    [[nodiscard]] std::optional<std::size_t> sceneIndexForItem(
        ItemId itemId
    ) const noexcept;

    [[nodiscard]] std::optional<ItemId> itemForSceneIndex(
        std::size_t sceneIndex
    ) const noexcept;
};

// A candidate projection together with the scene selected by a stable
// Workspace item identity. The caller still owns when and where the
// projection is stored before Engine binds its Project.
struct PreparedActivePlaybackProjection
{
    ActivePlaybackProjection projection{};
    std::size_t targetSceneIndex = 0U;
};

// Builds a complete, ordered playback sequence. An empty or invalid playlist
// is rejected rather than producing a partial project.
[[nodiscard]] std::optional<ActivePlaybackProjection> makeActivePlaybackProjection(
    const Domain::AuthoredPlaylist& playlist
);

[[nodiscard]] std::optional<PreparedActivePlaybackProjection>
prepareActivePlaybackProjection(
    const Domain::AuthoredPlaylist& playlist,
    const Domain::ItemId& targetItemId
);

// Selects the normal Next-equivalent target after removing the current item.
// The old index remains the successor position when it still exists; otherwise
// the sequence wraps to its first remaining item.
[[nodiscard]] std::optional<std::size_t> successorIndexAfterRemovingActiveItem(
    std::size_t previousActiveIndex,
    std::size_t remainingItemCount
) noexcept;

}
