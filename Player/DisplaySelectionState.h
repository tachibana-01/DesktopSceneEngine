#pragma once

#include <DesktopSceneEngine/Platform/Display.h>

#include <optional>

namespace DesktopSceneEngine::Player
{

enum class DisplaySelectionAvailability
{
    Available,
    Unresolved,
    EnumerationFailed
};

struct DisplayIdentifyRequest
{
    DisplayId displayId{};
};

class DisplaySelectionState
{
public:

    explicit DisplaySelectionState(DisplayId boundDisplayId);

    void refresh(const Platform::DisplayEnumerationResult& result);

    [[nodiscard]] const DisplayId& boundDisplayId() const noexcept;

    [[nodiscard]] std::optional<Platform::DisplayDescriptor> boundDisplay() const;

    [[nodiscard]] std::optional<DisplayIdentifyRequest> identifyRequest() const;

    [[nodiscard]] DisplaySelectionAvailability availability() const noexcept;

private:

    void updateAvailability();

    Platform::DisplayEnumerationStatus enumerationStatus_
        = Platform::DisplayEnumerationStatus::ApiFailure;
    Platform::DisplaySnapshot snapshot_{};
    const DisplayId boundDisplayId_;
    DisplaySelectionAvailability availability_ = DisplaySelectionAvailability::EnumerationFailed;
};

}
