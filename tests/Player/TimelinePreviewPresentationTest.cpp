#include "TimelinePreviewPresentation.h"

#include <DesktopSceneEngine/Domain/AspectRatio.h>

#include <QColor>
#include <QImage>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

DesktopSceneEngine::Player::TimelinePreviewSource makeSource(
    DesktopSceneEngine::Player::WorkspaceMediaType mediaType
)
{
    return {
        DesktopSceneEngine::Player::ItemId{QUuid::createUuid()},
        mediaType == DesktopSceneEngine::Player::WorkspaceMediaType::Image
            ? QStringLiteral("still.png")
            : QStringLiteral("clip.mp4"),
        mediaType};
}

DesktopSceneEngine::Player::TimelinePreviewDisplayedFrame::Identity makeRequest(
    const DesktopSceneEngine::Player::TimelinePreviewSource& source,
    float timeSeconds,
    std::uint64_t generation,
    std::uint64_t serial
)
{
    return {source, timeSeconds, generation, serial};
}

constexpr QColor canvasBackground{26, 26, 26};

bool isColorNear(const QColor& actual, const QColor& expected, int tolerance = 3)
{
    return std::abs(actual.red() - expected.red()) <= tolerance
        && std::abs(actual.green() - expected.green()) <= tolerance
        && std::abs(actual.blue() - expected.blue()) <= tolerance
        && std::abs(actual.alpha() - expected.alpha()) <= tolerance;
}

DesktopSceneEngine::Domain::Scene makeScene(
    DesktopSceneEngine::Domain::MediaPresentationMode mode,
    DesktopSceneEngine::Domain::Crop crop = {},
    DesktopSceneEngine::Domain::Layout layout = {},
    DesktopSceneEngine::Domain::AspectRatio customAspect = {1, 1}
)
{
    DesktopSceneEngine::Domain::Scene scene{};
    scene.crop = crop;
    scene.layout = layout;
    scene.presentation = {mode, customAspect};
    return scene;
}

QImage verticalBands(const QSize& size, const QColor& left, const QColor& center,
    const QColor& right)
{
    QImage image{size, QImage::Format_RGBA8888};
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            image.setPixelColor(
                x,
                y,
                x < image.width() / 3 ? left
                    : (x < image.width() * 2 / 3 ? center : right));
        }
    }
    return image;
}

QImage horizontalBands(const QSize& size, const QColor& top, const QColor& center,
    const QColor& bottom)
{
    QImage image{size, QImage::Format_RGBA8888};
    for (int y = 0; y < image.height(); ++y)
    {
        const QColor& color = y < image.height() / 3 ? top
            : (y < image.height() * 2 / 3 ? center : bottom);
        for (int x = 0; x < image.width(); ++x)
        {
            image.setPixelColor(x, y, color);
        }
    }
    return image;
}

bool verifyCanvasSizes()
{
    using DesktopSceneEngine::Player::timelinePreviewPopupCanvasSize;
    using DesktopSceneEngine::Domain::AspectRatio;

    return require(
               timelinePreviewPopupCanvasSize(AspectRatio{16, 9}) == QSize{224, 126},
               "A 16:9 Desktop Canvas should produce a 16:9 popup canvas")
        && require(
               timelinePreviewPopupCanvasSize(AspectRatio{16, 10}) == QSize{224, 140},
               "A 16:10 Desktop Canvas should produce a 16:10 popup canvas")
        && require(
               timelinePreviewPopupCanvasSize(AspectRatio{9, 16}) == QSize{79, 140},
               "A portrait Desktop Canvas should retain its aspect in the popup")
        && require(
               timelinePreviewPopupCanvasSize(AspectRatio{10, 7}) == QSize{200, 140},
               "Any valid logical Canvas aspect should fit the popup bounds");
}

bool verifyDefaultAutoFillPresentation()
{
    QImage source{QSize{2, 4}, QImage::Format_RGBA8888};
    source.fill(Qt::black);
    for (int x = 0; x < source.width(); ++x)
    {
        source.setPixelColor(x, 0, Qt::red);
        source.setPixelColor(x, 1, Qt::green);
        source.setPixelColor(x, 2, Qt::green);
        source.setPixelColor(x, 3, Qt::blue);
    }

    const QImage composed = DesktopSceneEngine::Player::composeTimelineCanvasPreview(
        source,
        DesktopSceneEngine::Domain::Scene{},
        QSize{4, 2});
    return require(composed.size() == QSize{4, 2},
               "The composed frame must fill the requested canvas dimensions")
        && require(composed.pixelColor(0, 0) == QColor{Qt::green}
                && composed.pixelColor(3, 0) == QColor{Qt::green}
                && composed.pixelColor(0, 1) == QColor{Qt::green}
                && composed.pixelColor(3, 1) == QColor{Qt::green},
            "Default Auto Fill must center-crop the source instead of stretching it");
}

bool verifyNativePresentation()
{
    using DesktopSceneEngine::Domain::MediaPresentationMode;
    using DesktopSceneEngine::Player::composeTimelineCanvasPreview;

    QImage portrait{QSize{40, 80}, QImage::Format_RGBA8888};
    portrait.fill(Qt::red);
    const QImage portraitOnLandscape = composeTimelineCanvasPreview(
        portrait,
        makeScene(MediaPresentationMode::Native),
        QSize{160, 90});

    QImage landscape{QSize{80, 40}, QImage::Format_RGBA8888};
    landscape.fill(Qt::green);
    const QImage landscapeOnPortrait = composeTimelineCanvasPreview(
        landscape,
        makeScene(MediaPresentationMode::Native),
        QSize{90, 160});

    return require(!portraitOnLandscape.isNull()
                   && isColorNear(portraitOnLandscape.pixelColor(10, 45), canvasBackground)
                   && isColorNear(portraitOnLandscape.pixelColor(80, 45), QColor{Qt::red})
                   && isColorNear(portraitOnLandscape.pixelColor(150, 45), canvasBackground),
               "Native portrait media must remain centered with Canvas side regions visible")
        && require(!landscapeOnPortrait.isNull()
                   && isColorNear(landscapeOnPortrait.pixelColor(45, 20), canvasBackground)
                   && isColorNear(landscapeOnPortrait.pixelColor(45, 80), QColor{Qt::green})
                   && isColorNear(landscapeOnPortrait.pixelColor(45, 145), canvasBackground),
            "Native landscape media must remain centered with Canvas top and bottom regions visible");
}

bool verifyAutoFillPresentation()
{
    using DesktopSceneEngine::Domain::MediaPresentationMode;
    using DesktopSceneEngine::Player::composeTimelineCanvasPreview;

    const QImage landscape = verticalBands(
        QSize{120, 60}, QColor{Qt::red}, QColor{Qt::green}, QColor{Qt::blue});
    const QImage landscapeOnPortrait = composeTimelineCanvasPreview(
        landscape,
        makeScene(MediaPresentationMode::AutoFill),
        QSize{60, 120});
    const QImage portrait = horizontalBands(
        QSize{60, 120}, QColor{Qt::red}, QColor{Qt::green}, QColor{Qt::blue});
    const QImage portraitOnLandscape = composeTimelineCanvasPreview(
        portrait,
        makeScene(MediaPresentationMode::AutoFill),
        QSize{120, 60});

    return require(!landscapeOnPortrait.isNull()
                   && isColorNear(landscapeOnPortrait.pixelColor(5, 5), QColor{Qt::green})
                   && isColorNear(landscapeOnPortrait.pixelColor(30, 60), QColor{Qt::green})
                   && isColorNear(landscapeOnPortrait.pixelColor(55, 115), QColor{Qt::green}),
               "Auto Fill must center-crop landscape source content for a portrait Canvas")
        && require(!portraitOnLandscape.isNull()
                   && isColorNear(portraitOnLandscape.pixelColor(5, 5), QColor{Qt::green})
                   && isColorNear(portraitOnLandscape.pixelColor(60, 30), QColor{Qt::green})
                   && isColorNear(portraitOnLandscape.pixelColor(115, 55), QColor{Qt::green}),
            "Auto Fill must center-crop portrait source content for a landscape Canvas");
}

bool verifyCropAndCustomPresentation()
{
    using DesktopSceneEngine::Domain::Crop;
    using DesktopSceneEngine::Domain::Layout;
    using DesktopSceneEngine::Domain::MediaPresentationMode;
    using DesktopSceneEngine::Player::composeTimelineCanvasPreview;

    const QImage source = verticalBands(
        QSize{300, 100}, QColor{Qt::red}, QColor{Qt::green}, QColor{Qt::blue});
    const QImage cropped = composeTimelineCanvasPreview(
        source,
        makeScene(MediaPresentationMode::AutoFill, Crop{1.0F / 3.0F, 0.0F, 2.0F / 3.0F, 1.0F}),
        QSize{200, 100});

    QImage solid{QSize{100, 100}, QImage::Format_RGBA8888};
    solid.fill(Qt::yellow);
    const QImage movedRight = composeTimelineCanvasPreview(
        solid,
        makeScene(MediaPresentationMode::Custom, {}, Layout{0.25F, 0.0F, 0.5F, 0.5F}),
        QSize{100, 100});
    const QImage movedLeft = composeTimelineCanvasPreview(
        solid,
        makeScene(MediaPresentationMode::Custom, {}, Layout{-0.25F, 0.0F, 0.5F, 0.5F}),
        QSize{100, 100});
    const QImage movedUp = composeTimelineCanvasPreview(
        solid,
        makeScene(MediaPresentationMode::Custom, {}, Layout{0.0F, 0.25F, 0.5F, 0.5F}),
        QSize{100, 100});
    const QImage movedDown = composeTimelineCanvasPreview(
        solid,
        makeScene(MediaPresentationMode::Custom, {}, Layout{0.0F, -0.25F, 0.5F, 0.5F}),
        QSize{100, 100});
    const QImage scaledDown = composeTimelineCanvasPreview(
        solid,
        makeScene(MediaPresentationMode::Custom, {}, Layout{0.0F, 0.0F, 0.5F, 0.5F}),
        QSize{100, 100});
    QImage enlargedSource{QSize{100, 100}, QImage::Format_RGBA8888};
    for (int y = 0; y < enlargedSource.height(); ++y)
    {
        for (int x = 0; x < enlargedSource.width(); ++x)
        {
            enlargedSource.setPixelColor(
                x,
                y,
                x < 25 ? QColor{Qt::red} : (x < 75 ? QColor{Qt::green} : QColor{Qt::blue}));
        }
    }
    const QImage scaledUp = composeTimelineCanvasPreview(
        enlargedSource,
        makeScene(MediaPresentationMode::Custom, {}, Layout{0.0F, 0.0F, 2.0F, 2.0F}),
        QSize{100, 100});

    return require(!cropped.isNull()
                   && isColorNear(cropped.pixelColor(10, 50), QColor{Qt::green})
                   && isColorNear(cropped.pixelColor(190, 50), QColor{Qt::blue}),
               "Asymmetric Crop must map the resolved source UV rectangle into QPainter pixels")
        && require(isColorNear(movedRight.pixelColor(10, 50), canvasBackground)
                   && isColorNear(movedRight.pixelColor(75, 50), QColor{Qt::yellow})
                   && isColorNear(movedLeft.pixelColor(10, 50), QColor{Qt::yellow})
                   && isColorNear(movedLeft.pixelColor(75, 50), canvasBackground),
            "Custom X must consume resolved Canvas X coordinates without reinterpretation")
        && require(isColorNear(movedUp.pixelColor(50, 10), QColor{Qt::yellow})
                   && isColorNear(movedUp.pixelColor(50, 75), canvasBackground)
                   && isColorNear(movedDown.pixelColor(50, 10), canvasBackground)
                   && isColorNear(movedDown.pixelColor(50, 75), QColor{Qt::yellow}),
            "Custom editor-positive Y must render upward through the shared resolver")
        && require(isColorNear(scaledDown.pixelColor(10, 10), canvasBackground)
                   && isColorNear(scaledDown.pixelColor(50, 50), QColor{Qt::yellow})
                   && isColorNear(scaledUp.pixelColor(5, 50), QColor{Qt::green})
                   && isColorNear(scaledUp.pixelColor(95, 50), QColor{Qt::green}),
            "Custom scale must expose Canvas background when reduced and clip enlarged media without destination clamping");
}

bool verifyNativeAndAutoFillLayoutPresentation()
{
    using DesktopSceneEngine::Domain::Layout;
    using DesktopSceneEngine::Domain::MediaPresentationMode;
    using DesktopSceneEngine::Player::composeTimelineCanvasPreview;

    QImage solid{QSize{100, 100}, QImage::Format_RGBA8888};
    solid.fill(Qt::yellow);
    const QImage native = composeTimelineCanvasPreview(
        solid,
        makeScene(MediaPresentationMode::Native, {}, Layout{0.25F, 0.0F, 0.50F, 0.50F}),
        QSize{100, 100});

    const QImage bands = verticalBands(
        QSize{300, 100}, QColor{Qt::red}, QColor{Qt::green}, QColor{Qt::blue});
    const QImage autoFill = composeTimelineCanvasPreview(
        bands,
        makeScene(MediaPresentationMode::AutoFill, {}, Layout{0.25F, 0.0F, 0.50F, 0.50F}),
        QSize{100, 100});

    return require(!native.isNull()
                   && isColorNear(native.pixelColor(10, 50), canvasBackground)
                   && isColorNear(native.pixelColor(75, 50), QColor{Qt::yellow}),
            "Timeline Native preview must apply Layout after its containment baseline")
        && require(!autoFill.isNull()
                   && isColorNear(autoFill.pixelColor(10, 50), canvasBackground)
                   && isColorNear(autoFill.pixelColor(75, 50), QColor{Qt::green})
                   && isColorNear(autoFill.pixelColor(75, 10), canvasBackground),
            "Timeline Auto Fill preview must retain centered cover crop while translating destination");
}

bool verifyCanvasAndRequestIsolation()
{
    using DesktopSceneEngine::Domain::MediaPresentationMode;
    using DesktopSceneEngine::Player::TimelinePreviewImageIdentity;
    using DesktopSceneEngine::Player::WorkspaceMediaType;
    using DesktopSceneEngine::Player::composeTimelineCanvasPreview;
    using DesktopSceneEngine::Player::decideTimelinePreviewImageRequest;

    const QImage source = verticalBands(
        QSize{120, 60}, QColor{Qt::red}, QColor{Qt::green}, QColor{Qt::blue});
    const QImage sourceCopy = source.copy();
    const QImage landscapeCanvas = composeTimelineCanvasPreview(
        source,
        makeScene(MediaPresentationMode::Native),
        QSize{160, 90});
    const QImage portraitCanvas = composeTimelineCanvasPreview(
        source,
        makeScene(MediaPresentationMode::Native),
        QSize{90, 160});
    const QImage autoFill = composeTimelineCanvasPreview(
        source,
        makeScene(MediaPresentationMode::AutoFill),
        QSize{160, 90});
    const auto sourceIdentity = makeSource(WorkspaceMediaType::Video);
    const TimelinePreviewImageIdentity representative{sourceIdentity, 10'000};
    const auto noNewRequest = decideTimelinePreviewImageRequest(
        representative,
        representative,
        std::nullopt);

    return require(!landscapeCanvas.isNull() && !portraitCanvas.isNull()
                   && landscapeCanvas.size() == QSize{160, 90}
                   && portraitCanvas.size() == QSize{90, 160}
                   && source == sourceCopy,
               "Canvas recomposition must change only the owned output Canvas, not the raw source frame")
        && require(!autoFill.isNull()
                   && isColorNear(landscapeCanvas.pixelColor(80, 0), canvasBackground)
                   && !isColorNear(autoFill.pixelColor(80, 0), canvasBackground),
            "Presentation recomposition must produce a new Canvas image from the retained raw frame")
        && require(!noNewRequest.shouldSubmit && noNewRequest.retainDisplayedImage,
            "Presentation and Canvas recomposition must preserve representative identity and require no new source request");
}

bool verifyImageVideoCompositionParity()
{
    using DesktopSceneEngine::Domain::MediaPresentationMode;
    using DesktopSceneEngine::Player::composeTimelineCanvasPreview;

    const QImage imageRepresentative = verticalBands(
        QSize{120, 60}, QColor{Qt::red}, QColor{Qt::green}, QColor{Qt::blue});
    const QImage videoRepresentative = imageRepresentative.copy();
    const auto customScene = makeScene(MediaPresentationMode::Custom, {},
        DesktopSceneEngine::Domain::Layout{0.15F, -0.10F, 0.8F, 0.8F});
    const auto nativeScene = makeScene(MediaPresentationMode::Native, {},
        DesktopSceneEngine::Domain::Layout{0.15F, -0.10F, 0.8F, 1.2F});
    const auto autoFillScene = makeScene(MediaPresentationMode::AutoFill, {},
        DesktopSceneEngine::Domain::Layout{-0.15F, 0.10F, 1.2F, 0.8F});
    const QImage imageCustomCanvas = composeTimelineCanvasPreview(
        imageRepresentative, customScene, QSize{160, 90});
    const QImage videoCustomCanvas = composeTimelineCanvasPreview(
        videoRepresentative, customScene, QSize{160, 90});
    const QImage imageNativeCanvas = composeTimelineCanvasPreview(
        imageRepresentative, nativeScene, QSize{160, 90});
    const QImage videoNativeCanvas = composeTimelineCanvasPreview(
        videoRepresentative, nativeScene, QSize{160, 90});
    const QImage imageAutoFillCanvas = composeTimelineCanvasPreview(
        imageRepresentative, autoFillScene, QSize{160, 90});
    const QImage videoAutoFillCanvas = composeTimelineCanvasPreview(
        videoRepresentative, autoFillScene, QSize{160, 90});

    return require(!imageCustomCanvas.isNull() && imageCustomCanvas == videoCustomCanvas,
               "Image and video representative QImages must share Custom Canvas composition")
        && require(!imageNativeCanvas.isNull() && imageNativeCanvas == videoNativeCanvas,
            "Image and video representative QImages must share transformed Native composition")
        && require(!imageAutoFillCanvas.isNull() && imageAutoFillCanvas == videoAutoFillCanvas,
            "Image and video representative QImages must share transformed Auto Fill composition");
}

bool verifyDisplayedFrameValidity()
{
    using DesktopSceneEngine::Player::TimelinePreviewDisplayedFrame;
    using DesktopSceneEngine::Player::WorkspaceMediaType;
    using DesktopSceneEngine::Player::isTimelinePreviewFrameCurrent;

    const auto videoSource = makeSource(WorkspaceMediaType::Video);
    const auto imageSource = makeSource(WorkspaceMediaType::Image);
    auto videoRequestA = makeRequest(videoSource, 10.0F, 3U, 10U);
    auto videoRequestB = makeRequest(videoSource, 11.0F, 3U, 11U);
    auto videoSameBucketRequest = makeRequest(videoSource, 14.0F, 3U, 12U);
    videoRequestA.representativeTimestampMilliseconds = 10'000;
    videoRequestB.representativeTimestampMilliseconds = 15'000;
    videoSameBucketRequest.representativeTimestampMilliseconds = 10'000;
    const auto imageRequestA = makeRequest(imageSource, 1.0F, 4U, 20U);
    const auto imageRequestB = makeRequest(imageSource, 5.0F, 4U, 21U);

    const TimelinePreviewDisplayedFrame videoFrame{videoRequestA, 7U};
    const TimelinePreviewDisplayedFrame imageFrame{imageRequestA, 7U};

    return require(isTimelinePreviewFrameCurrent(videoFrame, videoRequestA, 7U),
               "A video frame remains valid for its exact request identity")
        && require(isTimelinePreviewFrameCurrent(videoFrame, videoSameBucketRequest, 7U),
            "A video frame remains valid across raw hover times in its representative bucket")
        && require(!isTimelinePreviewFrameCurrent(videoFrame, videoRequestB, 7U),
            "A video frame must not remain visible for a newer representative bucket")
        && require(isTimelinePreviewFrameCurrent(imageFrame, imageRequestB, 7U),
            "A static image remains valid across synthetic timeline times")
        && require(!isTimelinePreviewFrameCurrent(imageFrame, imageRequestB, 8U),
            "A presentation generation change requires recomposition before reuse");
}

bool verifyRepresentativeRequestCoalescing()
{
    using DesktopSceneEngine::Player::TimelinePreviewImageIdentity;
    using DesktopSceneEngine::Player::WorkspaceMediaType;
    using DesktopSceneEngine::Player::decideTimelinePreviewImageRequest;

    const auto sourceA = makeSource(WorkspaceMediaType::Video);
    const auto sourceB = makeSource(WorkspaceMediaType::Video);
    const TimelinePreviewImageIdentity frameA{sourceA, 10'000};
    const TimelinePreviewImageIdentity frameB{sourceA, 20'000};
    const TimelinePreviewImageIdentity frameC{sourceA, 30'000};
    const TimelinePreviewImageIdentity sourceBFrame{sourceB, 10'000};

    const auto displayedSameBucket = decideTimelinePreviewImageRequest(
        frameA,
        frameA,
        std::nullopt);
    const auto pendingSameBucket = decideTimelinePreviewImageRequest(
        frameA,
        std::nullopt,
        frameA);
    const auto crossBucket = decideTimelinePreviewImageRequest(
        frameB,
        frameA,
        std::nullopt);
    const auto latestWins = decideTimelinePreviewImageRequest(
        frameC,
        frameA,
        frameB);
    const auto returnToDisplayed = decideTimelinePreviewImageRequest(
        frameA,
        frameA,
        frameB);
    const auto sourceSwitch = decideTimelinePreviewImageRequest(
        sourceBFrame,
        frameA,
        std::nullopt);

    return require(!displayedSameBucket.shouldSubmit
                       && !displayedSameBucket.shouldCancelPending
                       && displayedSameBucket.retainDisplayedImage,
               "A displayed representative frame must coalesce same-bucket hover moves")
        && require(!pendingSameBucket.shouldSubmit
                       && !pendingSameBucket.shouldCancelPending
                       && !pendingSameBucket.retainDisplayedImage,
            "A pending representative frame must not be cancelled or resubmitted in its bucket")
        && require(crossBucket.shouldSubmit
                       && !crossBucket.shouldCancelPending
                       && crossBucket.retainDisplayedImage,
            "A cross-bucket request must retain the last valid frame while the replacement is pending")
        && require(latestWins.shouldSubmit
                       && latestWins.shouldCancelPending
                       && latestWins.retainDisplayedImage,
            "A newer bucket must supersede the pending bucket without clearing the displayed frame")
        && require(!returnToDisplayed.shouldSubmit
                       && returnToDisplayed.shouldCancelPending
                       && returnToDisplayed.retainDisplayedImage,
            "Returning to the displayed bucket must cancel an obsolete pending request")
        && require(sourceSwitch.shouldSubmit
                       && !sourceSwitch.retainDisplayedImage,
            "A previous source frame must not be retained as the new source preview");
}

}

int main()
{
    return verifyCanvasSizes() && verifyDefaultAutoFillPresentation()
            && verifyNativePresentation()
            && verifyAutoFillPresentation()
            && verifyCropAndCustomPresentation()
            && verifyNativeAndAutoFillLayoutPresentation()
            && verifyCanvasAndRequestIsolation()
            && verifyImageVideoCompositionParity()
            && verifyDisplayedFrameValidity()
            && verifyRepresentativeRequestCoalescing()
        ? 0
        : 1;
}
