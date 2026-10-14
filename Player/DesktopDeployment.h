#pragma once

namespace DesktopSceneEngine::Player
{
enum class DesktopDeploymentIntent { Deploy, Undeploy };
enum class DesktopDeploymentResult : unsigned char
{
    Completed, AlreadySatisfied, Rejected, Failure, Received, Uncertain, Unavailable
};

// Value projection only; MainWindow retains all intent and endpoint authority.
struct DesktopDeploymentProjection
{
    bool retained = false;
    bool canDeploy = false;
    bool canUndeploy = false;
    friend bool operator==(const DesktopDeploymentProjection&, const DesktopDeploymentProjection&) = default;
};
}
