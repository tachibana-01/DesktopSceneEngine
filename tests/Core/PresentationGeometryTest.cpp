#include <DesktopSceneEngine/Presentation/PresentationGeometry.h>

#include <cmath>
#include <iostream>
#include <limits>

namespace
{

using DesktopSceneEngine::Domain::AspectRatio;
using DesktopSceneEngine::Domain::Crop;
using DesktopSceneEngine::Domain::Layout;
using DesktopSceneEngine::Domain::MediaPresentation;
using DesktopSceneEngine::Domain::MediaPresentationMode;
using DesktopSceneEngine::Presentation::NormalizedRectangle;
using DesktopSceneEngine::Presentation::PresentationGeometryInput;
using DesktopSceneEngine::Presentation::resolvePresentationGeometry;

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

bool matches(
    const NormalizedRectangle& rectangle,
    float x,
    float y,
    float width,
    float height
)
{
    return approximatelyEqual(rectangle.x, x)
        && approximatelyEqual(rectangle.y, y)
        && approximatelyEqual(rectangle.width, width)
        && approximatelyEqual(rectangle.height, height);
}

PresentationGeometryInput input(
    int sourceWidth,
    int sourceHeight,
    int canvasWidth,
    int canvasHeight,
    Crop crop = {},
    Layout layout = {},
    MediaPresentation presentation = {}
)
{
    return {
        sourceWidth,
        sourceHeight,
        canvasWidth,
        canvasHeight,
        crop,
        layout,
        presentation};
}

bool verifyNativeGeometry()
{
    const MediaPresentation native{MediaPresentationMode::Native, AspectRatio{16, 9}};
    const auto matching = resolvePresentationGeometry(input(
        1920,
        1080,
        1600,
        900,
        {},
        {},
        native));
    const auto portrait = resolvePresentationGeometry(input(
        1080,
        1920,
        1600,
        900,
        {},
        {},
        native));
    const auto landscape = resolvePresentationGeometry(input(
        1920,
        1080,
        900,
        1600,
        {},
        {},
        native));
    const Crop halfWidthCrop{0.0F, 0.0F, 0.5F, 1.0F};
    const auto cropped = resolvePresentationGeometry(input(
        1920,
        1080,
        1600,
        900,
        halfWidthCrop,
        {},
        native));
    const Crop asymmetricCrop{0.15F, 0.20F, 0.50F, 0.40F};
    const auto asymmetric = resolvePresentationGeometry(input(
        1920,
        1080,
        1600,
        900,
        asymmetricCrop,
        {},
        native));

    return require(matching.has_value(), "matching Native input should resolve")
        && require(matches(matching->sourceUv, 0.0F, 0.0F, 1.0F, 1.0F),
            "Native must retain the complete source when uncropped")
        && require(matches(matching->destination, 0.0F, 0.0F, 1.0F, 1.0F),
            "matching Native source and Canvas aspects must fill the Canvas")
        && require(portrait.has_value()
                && matches(portrait->destination, 0.341796875F, 0.0F, 0.31640625F, 1.0F),
            "portrait Native content must be centered in a landscape Canvas")
        && require(landscape.has_value()
                && matches(landscape->destination, 0.0F, 0.341796875F, 1.0F, 0.31640625F),
            "landscape Native content must be centered in a portrait Canvas")
        && require(cropped.has_value()
                && matches(cropped->sourceUv, 0.0F, 0.0F, 0.5F, 1.0F)
                && matches(cropped->destination, 0.25F, 0.0F, 0.5F, 1.0F),
            "Native containment must use the post-crop source aspect")
        && require(asymmetric.has_value()
                && matches(asymmetric->sourceUv,
                    asymmetricCrop.x,
                    asymmetricCrop.y,
                    asymmetricCrop.width,
                    asymmetricCrop.height),
            "Native source UV must preserve asymmetric top-left Crop coordinates");
}

bool verifyAutoFillGeometry()
{
    const MediaPresentation autoFill{MediaPresentationMode::AutoFill, AspectRatio{16, 9}};
    const auto landscapeToPortrait = resolvePresentationGeometry(input(
        1920,
        1080,
        1080,
        1920,
        {},
        {},
        autoFill));
    const auto portraitToLandscape = resolvePresentationGeometry(input(
        1080,
        1920,
        1920,
        1080,
        {},
        {},
        autoFill));
    const Crop asymmetricCrop{0.10F, 0.20F, 0.60F, 0.50F};
    const auto asymmetric = resolvePresentationGeometry(input(
        1920,
        1080,
        1920,
        1080,
        asymmetricCrop,
        {},
        autoFill));

    return require(landscapeToPortrait.has_value()
                && matches(landscapeToPortrait->sourceUv,
                    0.341796875F,
                    0.0F,
                    0.31640625F,
                    1.0F),
            "Auto Fill must centered-crop a landscape source for a portrait Canvas")
        && require(portraitToLandscape.has_value()
                && matches(portraitToLandscape->sourceUv,
                    0.0F,
                    0.341796875F,
                    1.0F,
                    0.31640625F),
            "Auto Fill must centered-crop a portrait source for a landscape Canvas")
        && require(landscapeToPortrait.has_value()
                && portraitToLandscape.has_value()
                && matches(landscapeToPortrait->destination, 0.0F, 0.0F, 1.0F, 1.0F)
                && matches(portraitToLandscape->destination, 0.0F, 0.0F, 1.0F, 1.0F),
            "Auto Fill destination must remain the full Canvas")
        && require(asymmetric.has_value()
                && matches(asymmetric->sourceUv, 0.15F, 0.20F, 0.50F, 0.50F)
                && matches(asymmetric->destination, 0.0F, 0.0F, 1.0F, 1.0F),
            "Auto Fill must further crop the requested asymmetric Crop without moving it vertically");
}

bool verifyNativeLayoutTransform()
{
    const MediaPresentation native{MediaPresentationMode::Native, AspectRatio{16, 9}};
    const auto transformed = resolvePresentationGeometry(input(
        1920,
        1080,
        1000,
        1000,
        {},
        Layout{0.10F, 0.05F, 0.50F, 2.00F},
        native));
    const auto scaled = resolvePresentationGeometry(input(
        1920,
        1080,
        1000,
        1000,
        {},
        Layout{0.0F, 0.0F, 0.50F, 0.50F},
        native));
    const auto positioned = resolvePresentationGeometry(input(
        1920,
        1080,
        1000,
        1000,
        {},
        Layout{0.10F, 0.05F, 1.0F, 1.0F},
        native));
    const auto cropped = resolvePresentationGeometry(input(
        1920,
        1080,
        1600,
        900,
        Crop{0.0F, 0.0F, 0.50F, 1.0F},
        Layout{0.10F, -0.10F, 1.20F, 0.80F},
        native));

    return require(transformed.has_value()
                && matches(transformed->sourceUv, 0.0F, 0.0F, 1.0F, 1.0F)
                && matches(transformed->destination, 0.35F, -0.1125F, 0.50F, 1.125F),
            "Native must apply non-uniform Layout scale and editor coordinates after containment")
        && require(scaled.has_value()
                && matches(scaled->destination, 0.25F, 0.359375F, 0.50F, 0.28125F),
            "Native Layout scale must resize the contained baseline around its center")
        && require(positioned.has_value()
                && matches(positioned->destination, 0.10F, 0.16875F, 1.0F, 0.5625F),
            "Native Layout position must translate the contained baseline without resizing it")
        && require(cropped.has_value()
                && matches(cropped->sourceUv, 0.0F, 0.0F, 0.50F, 1.0F)
                && matches(cropped->destination, 0.30F, 0.20F, 0.60F, 0.80F),
            "Native must preserve Crop UV while applying Layout only to its contained destination");
}

bool verifyAutoFillLayoutTransform()
{
    const MediaPresentation autoFill{MediaPresentationMode::AutoFill, AspectRatio{16, 9}};
    const auto transformed = resolvePresentationGeometry(input(
        1920,
        1080,
        1000,
        1000,
        {},
        Layout{-0.10F, 0.20F, 1.50F, 0.50F},
        autoFill));
    const auto scaled = resolvePresentationGeometry(input(
        1920,
        1080,
        1000,
        1000,
        {},
        Layout{0.0F, 0.0F, 0.50F, 0.50F},
        autoFill));
    const auto positioned = resolvePresentationGeometry(input(
        1920,
        1080,
        1000,
        1000,
        {},
        Layout{0.10F, 0.05F, 1.0F, 1.0F},
        autoFill));
    const auto cropped = resolvePresentationGeometry(input(
        1920,
        1080,
        1920,
        1080,
        Crop{0.10F, 0.20F, 0.60F, 0.50F},
        Layout{0.10F, -0.10F, 0.50F, 1.50F},
        autoFill));

    return require(transformed.has_value()
                && matches(transformed->sourceUv, 0.21875F, 0.0F, 0.5625F, 1.0F)
                && matches(transformed->destination, -0.35F, 0.05F, 1.50F, 0.50F),
            "Auto Fill must preserve its centered cover crop while applying Layout to destination")
        && require(scaled.has_value()
                && matches(scaled->destination, 0.25F, 0.25F, 0.50F, 0.50F),
            "Auto Fill Layout scale must resize the full-Canvas baseline around its center")
        && require(positioned.has_value()
                && matches(positioned->destination, 0.10F, -0.05F, 1.0F, 1.0F),
            "Auto Fill Layout position must translate the full-Canvas baseline without resizing it")
        && require(cropped.has_value()
                && matches(cropped->sourceUv, 0.15F, 0.20F, 0.50F, 0.50F)
                && matches(cropped->destination, 0.35F, -0.15F, 0.50F, 1.50F),
            "Auto Fill Layout position must translate destination rather than pan source crop");
}

bool verifyTransformedBaselinesRecomputeForCanvasAspect()
{
    const Layout layout{0.10F, -0.05F, 0.50F, 1.50F};
    const MediaPresentation native{MediaPresentationMode::Native, AspectRatio{16, 9}};
    const MediaPresentation autoFill{MediaPresentationMode::AutoFill, AspectRatio{16, 9}};
    const auto nativeMatching = resolvePresentationGeometry(input(
        1920, 1080, 1600, 900, {}, layout, native));
    const auto nativeSquare = resolvePresentationGeometry(input(
        1920, 1080, 1000, 1000, {}, layout, native));
    const auto autoFillMatching = resolvePresentationGeometry(input(
        1920, 1080, 1600, 900, {}, layout, autoFill));
    const auto autoFillSquare = resolvePresentationGeometry(input(
        1920, 1080, 1000, 1000, {}, layout, autoFill));

    return require(nativeMatching.has_value() && nativeSquare.has_value()
                && matches(nativeMatching->destination, 0.35F, -0.20F, 0.50F, 1.50F)
                && matches(nativeSquare->destination, 0.35F, 0.128125F, 0.50F, 0.84375F),
            "Native must recompute containment for each Canvas before applying unchanged Layout")
        && require(autoFillMatching.has_value() && autoFillSquare.has_value()
                && matches(autoFillMatching->sourceUv, 0.0F, 0.0F, 1.0F, 1.0F)
                && matches(autoFillSquare->sourceUv, 0.21875F, 0.0F, 0.5625F, 1.0F)
                && matches(autoFillMatching->destination, 0.35F, -0.20F, 0.50F, 1.50F)
                && matches(autoFillSquare->destination, 0.35F, -0.20F, 0.50F, 1.50F),
            "Auto Fill must recompute cover crop for each Canvas before applying unchanged Layout");
}

bool verifyCustomGeometry()
{
    const MediaPresentation custom{MediaPresentationMode::Custom, AspectRatio{1, 1}};
    const auto scaledAndPositioned = resolvePresentationGeometry(input(
        1920,
        1080,
        1600,
        900,
        {},
        Layout{0.10F, 0.20F, 2.0F, 0.5F},
        custom));
    const auto leftAndDown = resolvePresentationGeometry(input(
        1920,
        1080,
        1600,
        900,
        {},
        Layout{-0.10F, -0.20F, 1.0F, 1.0F},
        custom));
    const Crop crop{0.20F, 0.10F, 0.40F, 0.40F};
    const auto combined = resolvePresentationGeometry(input(
        1920,
        1080,
        1600,
        900,
        crop,
        Layout{0.25F, -0.10F, 1.5F, 0.75F},
        custom));

    return require(scaledAndPositioned.has_value()
                && matches(scaledAndPositioned->sourceUv, 0.21875F, 0.0F, 0.5625F, 1.0F)
                && matches(scaledAndPositioned->destination, 0.0375F, 0.05F, 1.125F, 0.5F),
            "Custom must crop to its aspect, scale independently, and preserve overflow")
        && require(leftAndDown.has_value()
                && matches(leftAndDown->destination, 0.11875F, 0.20F, 0.5625F, 1.0F),
            "negative Custom X/Y must move left/down in resolved Canvas coordinates")
        && require(scaledAndPositioned.has_value()
                && scaledAndPositioned->destination.y < 0.25F,
            "positive editor positionY must move Custom output up by reducing top-left Y")
        && require(combined.has_value()
                && matches(combined->sourceUv, 0.2875F, 0.10F, 0.225F, 0.40F)
                && matches(combined->destination, 0.328125F, 0.225F, 0.84375F, 0.75F),
            "Custom must apply Crop, aspect, scale, and editor coordinates in the established order");
}

bool verifyInvalidInputIsRejected()
{
    const MediaPresentation custom{MediaPresentationMode::Custom, AspectRatio{0, 1}};
    const float infinity = std::numeric_limits<float>::infinity();

    return require(!resolvePresentationGeometry(input(0, 1080, 1920, 1080)).has_value(),
               "nonpositive source width must be rejected")
        && require(!resolvePresentationGeometry(input(1920, 0, 1920, 1080)).has_value(),
            "nonpositive source height must be rejected")
        && require(!resolvePresentationGeometry(input(1920, 1080, 0, 1080)).has_value(),
            "nonpositive Canvas width must be rejected")
        && require(!resolvePresentationGeometry(input(1920, 1080, 1920, 0)).has_value(),
            "nonpositive Canvas height must be rejected")
        && require(!resolvePresentationGeometry(input(
                1920,
                1080,
                1920,
                1080,
                Crop{0.8F, 0.0F, 0.3F, 1.0F})).has_value(),
            "Crop outside normalized source bounds must be rejected")
        && require(!resolvePresentationGeometry(input(
                1920,
                1080,
                1920,
                1080,
                {},
                Layout{0.0F, 0.0F, 0.0F, 1.0F})).has_value(),
            "nonpositive Layout scale must be rejected")
        && require(!resolvePresentationGeometry(input(
                1920,
                1080,
                1920,
                1080,
                {},
                {},
                custom)).has_value(),
            "invalid Custom aspect must be rejected")
        && require(!resolvePresentationGeometry(input(
                1920,
                1080,
                1920,
                1080,
                Crop{infinity, 0.0F, 1.0F, 1.0F})).has_value(),
            "non-finite Crop values must be rejected")
        && require(!resolvePresentationGeometry(input(
                1920,
                1080,
                1920,
                1080,
                {},
                Layout{infinity, 0.0F, 1.0F, 1.0F})).has_value(),
            "non-finite Layout values must be rejected");
}

}

int main()
{
    const bool native = verifyNativeGeometry();
    const bool autoFill = verifyAutoFillGeometry();
    const bool nativeLayout = verifyNativeLayoutTransform();
    const bool autoFillLayout = verifyAutoFillLayoutTransform();
    const bool transformedCanvas = verifyTransformedBaselinesRecomputeForCanvasAspect();
    const bool custom = verifyCustomGeometry();
    const bool invalidInput = verifyInvalidInputIsRejected();
    return native && autoFill && nativeLayout && autoFillLayout && transformedCanvas
            && custom && invalidInput
        ? 0
        : 1;
}
