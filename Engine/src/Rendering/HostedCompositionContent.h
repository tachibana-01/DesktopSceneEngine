#pragma once

#include <DesktopSceneEngine/HostedCompositionContent.h>

#include <unknwn.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace DesktopSceneEngine::Rendering::Detail
{

// Shared only with held leases. It owns neither target resources nor publication.
struct HostedTargetLifetime
{
    bool active = true;
    std::uint64_t deviceGeneration = 0U;
};

}

namespace DesktopSceneEngine
{

struct CompositionContentLease::State
{
    Microsoft::WRL::ComPtr<IUnknown> content{};
    PresentationExtent extent{};
    PresentationTargetId targetId{};
    std::uint64_t deviceGeneration = 0U;
    std::uint64_t contentGeneration = 0U;
    bool eligible = true;
    bool current = true;
    std::shared_ptr<Rendering::Detail::HostedTargetLifetime> lifetime{};
};

}

namespace DesktopSceneEngine::Rendering::Detail
{

struct HostedTargetState
{
    std::shared_ptr<HostedTargetLifetime> lifetime
        = std::make_shared<HostedTargetLifetime>();
    std::uint64_t nextContentGeneration = 1U;
    std::shared_ptr<CompositionContentLease::State> published{};
};

}
