#include <DesktopSceneEngine/DisplayIdentity.h>

#include <charconv>
#include <cstdint>
#include <limits>
#include <utility>

namespace
{

constexpr std::string_view CanonicalIdentityPrefix = "dse-display-id:v1:";
constexpr std::string_view TargetDevicePathToken = "target-device-path";
constexpr std::string_view SourceDeviceNameToken = "source-device-name";

[[nodiscard]] bool isContinuationByte(std::uint8_t value) noexcept
{
    return (value & 0xC0U) == 0x80U;
}

[[nodiscard]] bool isWellFormedUtf8(std::string_view value) noexcept
{
    if (value.empty()
        || value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())
        || value.find('\0') != std::string_view::npos)
    {
        return false;
    }

    std::size_t index = 0U;
    while (index < value.size())
    {
        const auto first = static_cast<std::uint8_t>(value[index]);
        if (first <= 0x7FU)
        {
            ++index;
            continue;
        }

        if (first >= 0xC2U && first <= 0xDFU)
        {
            if (index + 1U >= value.size()
                || !isContinuationByte(static_cast<std::uint8_t>(value[index + 1U])))
            {
                return false;
            }
            index += 2U;
            continue;
        }

        if (first >= 0xE0U && first <= 0xEFU)
        {
            if (index + 2U >= value.size())
            {
                return false;
            }

            const auto second = static_cast<std::uint8_t>(value[index + 1U]);
            const auto third = static_cast<std::uint8_t>(value[index + 2U]);
            const bool validSecond = first == 0xE0U
                ? second >= 0xA0U && second <= 0xBFU
                : first == 0xEDU
                    ? second >= 0x80U && second <= 0x9FU
                    : isContinuationByte(second);
            if (!validSecond || !isContinuationByte(third))
            {
                return false;
            }
            index += 3U;
            continue;
        }

        if (first >= 0xF0U && first <= 0xF4U)
        {
            if (index + 3U >= value.size())
            {
                return false;
            }

            const auto second = static_cast<std::uint8_t>(value[index + 1U]);
            const auto third = static_cast<std::uint8_t>(value[index + 2U]);
            const auto fourth = static_cast<std::uint8_t>(value[index + 3U]);
            const bool validSecond = first == 0xF0U
                ? second >= 0x90U && second <= 0xBFU
                : first == 0xF4U
                    ? second >= 0x80U && second <= 0x8FU
                    : isContinuationByte(second);
            if (!validSecond || !isContinuationByte(third)
                || !isContinuationByte(fourth))
            {
                return false;
            }
            index += 4U;
            continue;
        }

        return false;
    }

    return true;
}

[[nodiscard]] std::optional<std::size_t> parseCanonicalValueLength(
    std::string_view text
) noexcept
{
    if (text.empty() || (text.size() > 1U && text.front() == '0'))
    {
        return std::nullopt;
    }

    std::size_t length = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), length);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        return std::nullopt;
    }

    return length;
}

}

namespace DesktopSceneEngine
{

DisplayId::DisplayId(DisplayIdentityKind kind, std::string value)
    : kind_(kind),
      value_(std::move(value))
{
}

DisplayId DisplayId::fromTargetDevicePath(std::string targetDevicePath)
{
    return DisplayId{DisplayIdentityKind::TargetDevicePath, std::move(targetDevicePath)};
}

DisplayId DisplayId::fromSourceDeviceName(std::string sourceDeviceName)
{
    return DisplayId{DisplayIdentityKind::SourceDeviceName, std::move(sourceDeviceName)};
}

bool DisplayId::isValid() const noexcept
{
    return isWellFormedUtf8(value_);
}

DisplayIdentityKind DisplayId::kind() const noexcept
{
    return kind_;
}

bool DisplayId::isPersistentOwnerCapable() const noexcept
{
    // TargetDevicePath identifies the current Windows display-target route. It
    // is not an eternal physical-panel identity across arbitrary reconfiguration.
    return isValid() && kind_ == DisplayIdentityKind::TargetDevicePath;
}

std::optional<std::string> DisplayId::canonicalRepresentation() const
{
    if (!isValid())
    {
        return std::nullopt;
    }

    const std::string_view kindToken = kind_ == DisplayIdentityKind::TargetDevicePath
        ? TargetDevicePathToken
        : SourceDeviceNameToken;
    std::string representation{};
    representation.reserve(
        CanonicalIdentityPrefix.size() + kindToken.size() + 2U
        + std::to_string(value_.size()).size() + value_.size());
    representation.append(CanonicalIdentityPrefix);
    representation.append(kindToken);
    representation.push_back(':');
    representation.append(std::to_string(value_.size()));
    representation.push_back(':');
    representation.append(value_);
    return representation;
}

std::optional<DisplayId> DisplayId::parseCanonicalRepresentation(
    std::string_view representation
)
{
    if (!representation.starts_with(CanonicalIdentityPrefix))
    {
        return std::nullopt;
    }
    representation.remove_prefix(CanonicalIdentityPrefix.size());

    DisplayIdentityKind kind = DisplayIdentityKind::SourceDeviceName;
    if (representation.starts_with(TargetDevicePathToken)
        && representation.size() > TargetDevicePathToken.size()
        && representation[TargetDevicePathToken.size()] == ':')
    {
        kind = DisplayIdentityKind::TargetDevicePath;
        representation.remove_prefix(TargetDevicePathToken.size() + 1U);
    }
    else if (representation.starts_with(SourceDeviceNameToken)
        && representation.size() > SourceDeviceNameToken.size()
        && representation[SourceDeviceNameToken.size()] == ':')
    {
        representation.remove_prefix(SourceDeviceNameToken.size() + 1U);
    }
    else
    {
        return std::nullopt;
    }

    const std::size_t separator = representation.find(':');
    if (separator == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto declaredLength = parseCanonicalValueLength(
        representation.substr(0U, separator));
    const std::string_view value = representation.substr(separator + 1U);
    if (!declaredLength.has_value() || *declaredLength != value.size())
    {
        return std::nullopt;
    }

    DisplayId identity{kind, std::string{value}};
    return identity.isValid() ? std::optional<DisplayId>{std::move(identity)} : std::nullopt;
}

}
