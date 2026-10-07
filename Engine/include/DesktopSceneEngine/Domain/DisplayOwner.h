#pragma once

#include <DesktopSceneEngine/DisplayIdentity.h>

#include <optional>
#include <utility>

namespace DesktopSceneEngine::Domain
{

class DisplayOwner final
{
public:

    [[nodiscard]] static std::optional<DisplayOwner> fromDisplayId(
        DisplayId displayId)
    {
        if (!displayId.isPersistentOwnerCapable())
        {
            return std::nullopt;
        }

        return DisplayOwner{std::move(displayId)};
    }

    [[nodiscard]] const DisplayId& displayId() const noexcept
    {
        return displayId_;
    }

    friend bool operator==(const DisplayOwner&, const DisplayOwner&) = default;

private:

    explicit DisplayOwner(DisplayId displayId)
        : displayId_(std::move(displayId))
    {
    }

    DisplayId displayId_;
};

}
