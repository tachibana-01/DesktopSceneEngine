#pragma once

#include <string>
#include <utility>

namespace DesktopSceneEngine
{
namespace Domain
{

// Represents a stable identifier used for domain references.
class EntityId
{
public:

    EntityId() = default;

    explicit EntityId(std::string value)
        : value_(std::move(value))
    {
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return !value_.empty();
    }

    [[nodiscard]] const std::string& value() const noexcept
    {
        return value_;
    }

    bool operator==(const EntityId&) const = default;

private:

    std::string value_{};
};

}
}
