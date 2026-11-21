#pragma once

namespace DesktopSceneEngine::Player
{
enum class DesktopDeploymentIntent { Deploy, Undeploy };
enum class DesktopDeploymentResult : unsigned char
{
    Completed, AlreadySatisfied, Rejected, Failure, Received, Uncertain, Unavailable
};

// Value projection only. SessionApplication owns deployment intent.
struct DesktopDeploymentProjection
{
    bool retained = false;
    bool canDeploy = false;
    bool canUndeploy = false;
    bool commandPending = false;
    friend bool operator==(const DesktopDeploymentProjection&, const DesktopDeploymentProjection&) = default;
};
}
