#include "HostedCompositionContent.h"

#include <utility>

namespace DesktopSceneEngine
{

CompositionContentLease::CompositionContentLease(
    std::shared_ptr<State> state) noexcept
    : state_(std::move(state))
{
}

CompositionContentLease::~CompositionContentLease() = default;
CompositionContentLease::CompositionContentLease(
    CompositionContentLease&&) noexcept = default;
CompositionContentLease& CompositionContentLease::operator=(
    CompositionContentLease&&) noexcept = default;

IUnknown* CompositionContentLease::content() const noexcept
{
    return state_ ? state_->content.Get() : nullptr;
}

PresentationExtent CompositionContentLease::extent() const noexcept
{
    return state_ ? state_->extent : PresentationExtent{};
}

PresentationTargetId CompositionContentLease::targetId() const noexcept
{
    return state_ ? state_->targetId : PresentationTargetId{};
}

std::uint64_t CompositionContentLease::deviceGeneration() const noexcept
{
    return state_ ? state_->deviceGeneration : 0U;
}

std::uint64_t CompositionContentLease::contentGeneration() const noexcept
{
    return state_ ? state_->contentGeneration : 0U;
}

bool CompositionContentLease::eligible() const noexcept
{
    return state_ && state_->content && state_->eligible && state_->lifetime
        && state_->lifetime->active
        && state_->deviceGeneration == state_->lifetime->deviceGeneration;
}

bool CompositionContentLease::isCurrent() const noexcept
{
    return eligible() && state_->current;
}

}
