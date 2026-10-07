#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace DesktopSceneEngine
{

enum class DisplayIdentityKind
{
    TargetDevicePath,
    SourceDeviceName
};

class DisplayId
{
public:

    DisplayId() = default;

    [[nodiscard]] static DisplayId fromTargetDevicePath(std::string targetDevicePath);

    [[nodiscard]] static DisplayId fromSourceDeviceName(std::string sourceDeviceName);

    [[nodiscard]] bool isValid() const noexcept;

    [[nodiscard]] DisplayIdentityKind kind() const noexcept;

    [[nodiscard]] bool isPersistentOwnerCapable() const noexcept;

    [[nodiscard]] std::optional<std::string> canonicalRepresentation() const;

    [[nodiscard]] static std::optional<DisplayId> parseCanonicalRepresentation(
        std::string_view representation
    );

    friend bool operator==(const DisplayId&, const DisplayId&) = default;
    friend auto operator<=>(const DisplayId&, const DisplayId&) = default;

private:

    DisplayId(DisplayIdentityKind kind, std::string value);

    DisplayIdentityKind kind_ = DisplayIdentityKind::SourceDeviceName;
    std::string value_{};
};

}
