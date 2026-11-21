#pragma once

#include <DesktopSceneEngine/PresentationTarget.h>

#include <cstdint>
#include <memory>

struct IUnknown;

namespace DesktopSceneEngine
{

// Same-process content snapshot. The lease keeps its COM content alive without
// retaining Engine or its presentation owner. Access it on the presentation
// owner thread, including checks of eligibility and current publication.
class CompositionContentLease
{
public:

    struct State;

    CompositionContentLease() noexcept = default;
    ~CompositionContentLease();

    CompositionContentLease(const CompositionContentLease&) = delete;
    CompositionContentLease& operator=(const CompositionContentLease&) = delete;
    CompositionContentLease(CompositionContentLease&&) noexcept;
    CompositionContentLease& operator=(CompositionContentLease&&) noexcept;

    // Borrowed COM reference, valid for this lease's lifetime. A non-null
    // reference alone does not authorize presentation after invalidation.
    [[nodiscard]] IUnknown* content() const noexcept;

    // Immutable facts from the publication acquired by this lease.
    [[nodiscard]] PresentationExtent extent() const noexcept;
    [[nodiscard]] PresentationTargetId targetId() const noexcept;
    [[nodiscard]] std::uint64_t deviceGeneration() const noexcept;
    [[nodiscard]] std::uint64_t contentGeneration() const noexcept;

    // An older publication can remain eligible while a replacement is current.
    // Target/device retirement, allocation invalidation, and shutdown revoke
    // eligibility without releasing the lease's strong COM reference.
    [[nodiscard]] bool eligible() const noexcept;
    [[nodiscard]] bool isCurrent() const noexcept;

private:

    explicit CompositionContentLease(std::shared_ptr<State> state) noexcept;

    friend class Rendering::GraphicsBackend;

    std::shared_ptr<State> state_{};
};

}
