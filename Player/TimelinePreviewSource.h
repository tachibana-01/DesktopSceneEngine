#pragma once

#include "ActivePlaybackProjection.h"

#include <cstddef>
#include <optional>

namespace DesktopSceneEngine::Player
{

struct TimelinePreviewSource
{
    ItemId itemId{};
    QString source{};
    WorkspaceMediaType mediaType = WorkspaceMediaType::Image;

    friend bool operator==(const TimelinePreviewSource&, const TimelinePreviewSource&) = default;
};

[[nodiscard]] inline std::optional<TimelinePreviewSource>
timelinePreviewSourceForActiveScene(
    const WorkspaceModel& workspaceModel,
    const ActivePlaybackProjection& projection,
    std::size_t sceneIndex
)
{
    const auto itemId = projection.itemForSceneIndex(sceneIndex);
    if (!itemId.has_value())
    {
        return std::nullopt;
    }

    const WorkspaceItem* item = workspaceModel.item(*itemId);
    if (item == nullptr || item->source.isEmpty())
    {
        return std::nullopt;
    }

    return TimelinePreviewSource{item->id, item->source, item->mediaType};
}

}
