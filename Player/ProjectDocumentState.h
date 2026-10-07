#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace DesktopSceneEngine::Player
{

struct ProjectDocumentState
{
    std::optional<std::filesystem::path> currentPath{};
    std::uint64_t cleanRevision = 0U;

    [[nodiscard]] bool isDirty(std::uint64_t authoredRevision) const noexcept
    {
        return cleanRevision != authoredRevision;
    }

    void markClean(std::uint64_t authoredRevision) noexcept
    {
        cleanRevision = authoredRevision;
    }
};

}
