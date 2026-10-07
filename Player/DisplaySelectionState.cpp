#include "DisplaySelectionState.h"

#include <cassert>
#include <utility>

namespace DesktopSceneEngine::Player
{

DisplaySelectionState::DisplaySelectionState(DisplayId boundDisplayId)
    : boundDisplayId_(std::move(boundDisplayId))
{
    assert(boundDisplayId_.isValid());
}

void DisplaySelectionState::refresh(const Platform::DisplayEnumerationResult& result)
{
    enumerationStatus_ = result.status;
    snapshot_ = result.snapshot;
    updateAvailability();
}

const DisplayId& DisplaySelectionState::boundDisplayId() const noexcept
{
    return boundDisplayId_;
}

std::optional<Platform::DisplayDescriptor> DisplaySelectionState::boundDisplay() const
{
    if (enumerationStatus_ != Platform::DisplayEnumerationStatus::Success)
    {
        return std::nullopt;
    }

    return snapshot_.resolve(boundDisplayId_);
}

std::optional<DisplayIdentifyRequest> DisplaySelectionState::identifyRequest() const
{
    if (!boundDisplay().has_value())
    {
        return std::nullopt;
    }

    return DisplayIdentifyRequest{boundDisplayId_};
}

DisplaySelectionAvailability DisplaySelectionState::availability() const noexcept
{
    return availability_;
}

void DisplaySelectionState::updateAvailability()
{
    if (enumerationStatus_ != Platform::DisplayEnumerationStatus::Success)
    {
        availability_ = DisplaySelectionAvailability::EnumerationFailed;
        return;
    }

    availability_ = snapshot_.resolve(boundDisplayId_).has_value()
        ? DisplaySelectionAvailability::Available
        : DisplaySelectionAvailability::Unresolved;
}

}
