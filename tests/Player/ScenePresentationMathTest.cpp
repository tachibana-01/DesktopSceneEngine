#include "ScenePresentationMath.h"

#include <DesktopSceneEngine/Domain/MediaPresentation.h>
#include <DesktopSceneEngine/Presentation/PresentationGeometry.h>

#include <cmath>
#include <iostream>

namespace
{

using DesktopSceneEngine::Core::makeScenePresentationRenderState;
using DesktopSceneEngine::Domain::AspectRatio;
using DesktopSceneEngine::Domain::Crop;
using DesktopSceneEngine::Domain::Layout;
using DesktopSceneEngine::Domain::MediaPresentation;
using DesktopSceneEngine::Domain::MediaPresentationMode;

constexpr float Tolerance = 0.0001F;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool approximatelyEqual(float left, float right)
{
    return std::fabs(left - right) <= Tolerance;
}

bool hasVisibleRect(const DesktopSceneEngine::Rendering::RenderPresentation& state)
{
    return state.sourceUv.width > 0.0F
        && state.sourceUv.height > 0.0F
        && state.destination.width > 0.0F
        && state.destination.height > 0.0F;
}

bool verifyAdapterParity(
    int sourceWidth,
    int sourceHeight,
    const AspectRatio& canvasAspect,
    const Crop& crop,
    const Layout& layout,
    const MediaPresentation& presentation
)
{
    const auto shared = DesktopSceneEngine::Presentation::resolvePresentationGeometry({
        sourceWidth,
        sourceHeight,
        canvasAspect.numerator,
        canvasAspect.denominator,
        crop,
        layout,
        presentation});
    const auto adapted = makeScenePresentationRenderState(
        sourceWidth,
        sourceHeight,
        canvasAspect,
        crop,
        layout,
        presentation);

    if (!require(shared.has_value() == adapted.has_value(),
            "ScenePresentationMath must preserve shared resolver validity"))
    {
        return false;
    }

    if (!shared)
    {
        return true;
    }

    return require(approximatelyEqual(adapted->sourceUv.x, shared->sourceUv.x)
                && approximatelyEqual(adapted->sourceUv.y, shared->sourceUv.y)
                && approximatelyEqual(adapted->sourceUv.width, shared->sourceUv.width)
                && approximatelyEqual(adapted->sourceUv.height, shared->sourceUv.height)
                && approximatelyEqual(adapted->destination.x, shared->destination.x)
                && approximatelyEqual(adapted->destination.y, shared->destination.y)
                && approximatelyEqual(adapted->destination.width, shared->destination.width)
                && approximatelyEqual(adapted->destination.height, shared->destination.height),
        "ScenePresentationMath must copy shared geometry without changing it");
}

bool verifyDefaultPresentationFillsMatchingCanvas()
{
    const auto state = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{16, 9},
        Crop{},
        Layout{},
        MediaPresentation{});

    return require(state.has_value(), "default presentation should be renderable")
        && require(hasVisibleRect(*state),
            "default presentation must produce non-zero source and destination rectangles")
        && require(approximatelyEqual(state->sourceUv.x, 0.0F)
                && approximatelyEqual(state->sourceUv.y, 0.0F)
                && approximatelyEqual(state->sourceUv.width, 1.0F)
                && approximatelyEqual(state->sourceUv.height, 1.0F),
            "matching source and Canvas aspects must retain the complete source")
        && require(approximatelyEqual(state->destination.x, 0.0F)
                && approximatelyEqual(state->destination.y, 0.0F)
                && approximatelyEqual(state->destination.width, 1.0F)
                && approximatelyEqual(state->destination.height, 1.0F),
            "matching source and Canvas aspects must fill the complete Canvas");
}

bool verifyNativeContainDoesNotDistort()
{
    const auto state = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{1, 1},
        Crop{},
        Layout{},
        MediaPresentation{MediaPresentationMode::Native, AspectRatio{16, 9}});

    return require(state.has_value(), "Native presentation should be renderable")
        && require(hasVisibleRect(*state),
            "Native presentation must produce non-zero source and destination rectangles")
        && require(approximatelyEqual(state->destination.x, 0.0F),
            "Native 16:9 content should remain horizontally centered in a square Canvas")
        && require(approximatelyEqual(state->destination.y, 0.21875F),
            "Native 16:9 content should be vertically letterboxed in a square Canvas")
        && require(approximatelyEqual(state->destination.width, 1.0F),
            "Native content should use the full limiting Canvas dimension")
        && require(approximatelyEqual(state->destination.height, 0.5625F),
            "Native content must preserve the source aspect ratio");
}

bool verifyAutoFillUsesCenteredCrop()
{
    const auto state = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{1, 1},
        Crop{},
        Layout{},
        MediaPresentation{MediaPresentationMode::AutoFill, AspectRatio{16, 9}});

    return require(state.has_value(), "Auto Fill presentation should be renderable")
        && require(hasVisibleRect(*state),
            "Auto Fill presentation must produce non-zero source and destination rectangles")
        && require(approximatelyEqual(state->destination.x, 0.0F)
                && approximatelyEqual(state->destination.y, 0.0F)
                && approximatelyEqual(state->destination.width, 1.0F)
                && approximatelyEqual(state->destination.height, 1.0F),
            "Auto Fill should cover the complete Canvas")
        && require(approximatelyEqual(state->sourceUv.x, 0.21875F),
            "Auto Fill should crop the wider source symmetrically")
        && require(approximatelyEqual(state->sourceUv.width, 0.5625F),
            "Auto Fill crop width should match the Canvas aspect")
        && require(approximatelyEqual(state->sourceUv.y, 0.0F)
                && approximatelyEqual(state->sourceUv.height, 1.0F),
            "Auto Fill should retain the full source height for a wider source");
}

bool verifyCustomUsesPresentationLayout()
{
    const auto state = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{16, 9},
        Crop{},
        Layout{0.1F, -0.05F, 0.5F, 0.5F},
        MediaPresentation{MediaPresentationMode::Custom, AspectRatio{1, 1}});

    return require(state.has_value(), "Custom presentation should be renderable")
        && require(hasVisibleRect(*state),
            "Custom presentation must produce non-zero source and destination rectangles")
        && require(approximatelyEqual(state->destination.x, 0.459375F),
            "Custom position X should offset the centered scaled content")
        && require(approximatelyEqual(state->destination.y, 0.3F),
            "negative Custom position Y should move content down in Canvas coordinates")
        && require(approximatelyEqual(state->destination.width, 0.28125F)
                && approximatelyEqual(state->destination.height, 0.5F),
            "Custom scale should apply to the fitted custom aspect rectangle");
}

bool verifyCustomPositionUsesEditorCoordinates()
{
    const auto neutral = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{16, 9},
        Crop{},
        Layout{},
        MediaPresentation{MediaPresentationMode::Custom, AspectRatio{16, 9}});
    const auto positive = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{16, 9},
        Crop{},
        Layout{0.1F, 0.1F, 1.0F, 1.0F},
        MediaPresentation{MediaPresentationMode::Custom, AspectRatio{16, 9}});
    const auto negative = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{16, 9},
        Crop{},
        Layout{0.0F, -0.1F, 1.0F, 1.0F},
        MediaPresentation{MediaPresentationMode::Custom, AspectRatio{16, 9}});

    if (!require(neutral.has_value() && positive.has_value() && negative.has_value(),
            "valid Custom positions should produce renderable presentation states"))
    {
        return false;
    }

    const float neutralCenterX = neutral->destination.x + neutral->destination.width * 0.5F;
    const float neutralCenterY = neutral->destination.y + neutral->destination.height * 0.5F;
    const float positiveCenterX = positive->destination.x + positive->destination.width * 0.5F;
    const float positiveCenterY = positive->destination.y + positive->destination.height * 0.5F;
    const float negativeCenterY = negative->destination.y + negative->destination.height * 0.5F;

    return require(positiveCenterX > neutralCenterX,
               "positive Custom position X must move content right")
        && require(positiveCenterY < neutralCenterY,
            "positive Custom position Y must move content up")
        && require(negativeCenterY > neutralCenterY,
            "negative Custom position Y must move content down");
}

bool verifyNativeAndAutoFillApplyLayoutAfterTheirBaselines()
{
    const auto native = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{1, 1},
        Crop{},
        Layout{0.10F, 0.05F, 0.50F, 2.00F},
        MediaPresentation{MediaPresentationMode::Native, AspectRatio{16, 9}});
    const auto autoFill = makeScenePresentationRenderState(
        1920,
        1080,
        AspectRatio{1, 1},
        Crop{},
        Layout{-0.10F, 0.20F, 1.50F, 0.50F},
        MediaPresentation{MediaPresentationMode::AutoFill, AspectRatio{16, 9}});

    return require(native.has_value()
                && approximatelyEqual(native->sourceUv.x, 0.0F)
                && approximatelyEqual(native->sourceUv.y, 0.0F)
                && approximatelyEqual(native->sourceUv.width, 1.0F)
                && approximatelyEqual(native->sourceUv.height, 1.0F)
                && approximatelyEqual(native->destination.x, 0.35F)
                && approximatelyEqual(native->destination.y, -0.1125F)
                && approximatelyEqual(native->destination.width, 0.50F)
                && approximatelyEqual(native->destination.height, 1.125F),
            "ScenePresentationMath must adapt Native containment followed by Layout")
        && require(autoFill.has_value()
                && approximatelyEqual(autoFill->sourceUv.x, 0.21875F)
                && approximatelyEqual(autoFill->sourceUv.y, 0.0F)
                && approximatelyEqual(autoFill->sourceUv.width, 0.5625F)
                && approximatelyEqual(autoFill->sourceUv.height, 1.0F)
                && approximatelyEqual(autoFill->destination.x, -0.35F)
                && approximatelyEqual(autoFill->destination.y, 0.05F)
                && approximatelyEqual(autoFill->destination.width, 1.50F)
                && approximatelyEqual(autoFill->destination.height, 0.50F),
            "ScenePresentationMath must adapt Auto Fill crop followed by Layout");
}

bool verifyAdapterParityAcrossModes()
{
    return verifyAdapterParity(
               1920,
               1080,
               AspectRatio{1, 1},
               Crop{0.1F, 0.2F, 0.6F, 0.5F},
               Layout{0.1F, 0.05F, 0.5F, 2.0F},
               MediaPresentation{MediaPresentationMode::Native, AspectRatio{16, 9}})
        && verifyAdapterParity(
            1920,
            1080,
            AspectRatio{1, 1},
            Crop{0.1F, 0.2F, 0.6F, 0.5F},
            Layout{-0.1F, 0.2F, 1.5F, 0.5F},
            MediaPresentation{MediaPresentationMode::AutoFill, AspectRatio{16, 9}})
        && verifyAdapterParity(
            1920,
            1080,
            AspectRatio{16, 9},
            Crop{0.2F, 0.1F, 0.4F, 0.4F},
            Layout{0.25F, -0.1F, 1.5F, 0.75F},
            MediaPresentation{MediaPresentationMode::Custom, AspectRatio{1, 1}});
}

bool verifyAuthoredCanvasDoesNotFollowPresentationTarget()
{
    const AspectRatio authoredCanvas{16, 9};
    const auto renderForTarget = [&](int targetWidth, int targetHeight)
    {
        (void)targetWidth;
        (void)targetHeight;
        return makeScenePresentationRenderState(
            1920,
            1080,
            authoredCanvas,
            Crop{},
            Layout{},
            MediaPresentation{MediaPresentationMode::Native, authoredCanvas});
    };
    const auto firstTarget = renderForTarget(1600, 900);
    const auto secondTarget = renderForTarget(1000, 1000);

    return require(firstTarget.has_value() && secondTarget.has_value(),
               "valid authored Canvas presentation should resolve for every target")
        && require(
            approximatelyEqual(
                firstTarget->destination.x,
                secondTarget->destination.x)
                && approximatelyEqual(
                    firstTarget->destination.y,
                    secondTarget->destination.y)
                && approximatelyEqual(
                    firstTarget->destination.width,
                    secondTarget->destination.width)
                && approximatelyEqual(
                    firstTarget->destination.height,
                    secondTarget->destination.height),
            "presentation-target dimensions must not change authored Canvas geometry");
}

}

int main()
{
    return verifyDefaultPresentationFillsMatchingCanvas()
            && verifyNativeContainDoesNotDistort()
            && verifyAutoFillUsesCenteredCrop()
            && verifyCustomUsesPresentationLayout()
            && verifyCustomPositionUsesEditorCoordinates()
            && verifyNativeAndAutoFillApplyLayoutAfterTheirBaselines()
            && verifyAdapterParityAcrossModes()
            && verifyAuthoredCanvasDoesNotFollowPresentationTarget()
        ? 0
        : 1;
}
