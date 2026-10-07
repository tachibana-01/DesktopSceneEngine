#include "CanvasChromeFrame.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QRegion>
#include <QScreen>
#include <QWidget>

#include <cmath>
#include <iostream>

namespace
{

constexpr QRgb HighlightRed = qRgb(0xF0, 0x44, 0x44);

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

QImage grabFrame(DesktopSceneEngine::Player::CanvasChromeFrame& frame)
{
    QApplication::processEvents();
    return frame.grab().toImage().convertToFormat(QImage::Format_RGB32);
}

bool isHighlightRed(const QImage& image, const QPoint& point)
{
    const qreal devicePixelRatio = image.devicePixelRatio();
    const QPoint devicePoint{
        static_cast<int>(std::floor((static_cast<qreal>(point.x()) + 0.5) * devicePixelRatio)),
        static_cast<int>(std::floor((static_cast<qreal>(point.y()) + 0.5) * devicePixelRatio))};
    return image.valid(devicePoint) && image.pixel(devicePoint) == HighlightRed;
}

std::size_t highlightRedPixelCount(const QImage& image)
{
    std::size_t count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if (row[x] == HighlightRed)
            {
                ++count;
            }
        }
    }
    return count;
}

QImage grabWidgetFromScreen(QWidget& widget)
{
    QScreen* screen = widget.screen();
    if (screen == nullptr)
    {
        return {};
    }

    return screen->grabWindow(widget.winId())
        .toImage()
        .convertToFormat(QImage::Format_RGB32);
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    QWidget host{};
    host.resize(320, 240);
    if (QScreen* screen = application.primaryScreen(); screen != nullptr)
    {
        host.move(screen->availableGeometry().topLeft() + QPoint{64, 64});
    }

    DesktopSceneEngine::Player::CanvasChromeFrame frame{&host};
    frame.setObjectName(QStringLiteral("previewCanvasFrame"));
    frame.setGeometry(0, 0, 101, 73);

    QWidget canvasSurface{&frame};
    canvasSurface.setAttribute(Qt::WA_NativeWindow);
    canvasSurface.setAttribute(Qt::WA_OpaquePaintEvent);
    canvasSurface.setAutoFillBackground(true);
    QPalette canvasPalette = canvasSurface.palette();
    canvasPalette.setColor(QPalette::Window, QColor{QStringLiteral("#080b0f")});
    canvasSurface.setPalette(canvasPalette);
    frame.setCanvasSurface(&canvasSurface);
    frame.setHighlighted(true);
    host.show();

    const QRect expectedCanvasRect{4, 4, 93, 65};
    if (!require(frame.canvasRect() == expectedCanvasRect,
            "the permanent four-pixel ring must surround the exact Canvas surface"))
    {
        return 1;
    }
    if (!require(canvasSurface.geometry() == expectedCanvasRect,
            "the Canvas child must fill the canonical Canvas rect exactly"))
    {
        return 1;
    }

    const QRegion ringRegion = QRegion{frame.rect()}.subtracted(QRegion{frame.canvasRect()});
    if (!require(ringRegion.intersected(QRegion{frame.canvasRect()}).isEmpty(),
            "the Qt chrome ring and Canvas surface must be spatially disjoint"))
    {
        return 1;
    }

    const QRect originalCanvasRect = frame.canvasRect();
    const QRect originalSurfaceGeometry = canvasSurface.geometry();
    const WId originalCanvasWindowId = canvasSurface.winId();
    const QImage highlighted = grabFrame(frame);
    const QImage highlightedScreen = grabWidgetFromScreen(frame);
    const QImage highlightedCanvasScreen = grabWidgetFromScreen(canvasSurface);
    const std::size_t highlightedScreenRedPixels = highlightRedPixelCount(highlightedScreen);

    if (!require(frame.canvasRect() == originalCanvasRect
            && canvasSurface.geometry() == originalSurfaceGeometry,
            "enabling Highlight must not alter Canvas geometry"))
    {
        return 1;
    }

    const QPoint top{frame.width() / 2, 1};
    const QPoint bottom{frame.width() / 2, frame.height() - 2};
    const QPoint left{1, frame.height() / 2};
    const QPoint right{frame.width() - 2, frame.height() / 2};
    const QPoint center = frame.canvasRect().center();

    if (!require(isHighlightRed(highlighted, top)
            && isHighlightRed(highlighted, bottom)
            && isHighlightRed(highlighted, left)
            && isHighlightRed(highlighted, right),
            "Highlight ON must paint all four reserved ring sides red"))
    {
        return 1;
    }
    if (!require(!isHighlightRed(highlighted, center),
            "the Qt chrome widget must never paint Highlight red into the Canvas center"))
    {
        return 1;
    }
    if (!require(!highlightedScreen.isNull() && highlightedScreenRedPixels > 0,
            "the physical screen capture must contain the enabled Qt-owned ring"))
    {
        return 1;
    }
    if (!require(!highlightedCanvasScreen.isNull()
            && highlightRedPixelCount(highlightedCanvasScreen) == 0,
            "the complete native Canvas surface must contain zero editor-red pixels"))
    {
        return 1;
    }

    frame.setHighlighted(false);
    const QImage normal = grabFrame(frame);
    const QImage normalScreen = grabWidgetFromScreen(frame);
    const std::size_t normalScreenRedPixels = highlightRedPixelCount(normalScreen);
    if (!require(frame.canvasRect() == originalCanvasRect
            && canvasSurface.geometry() == originalSurfaceGeometry
            && canvasSurface.winId() == originalCanvasWindowId,
            "disabling Highlight must not alter Canvas geometry"))
    {
        return 1;
    }
    if (!require(!isHighlightRed(normal, top)
            && !isHighlightRed(normal, bottom)
            && !isHighlightRed(normal, left)
            && !isHighlightRed(normal, right),
            "Highlight OFF must restore every sampled ring side to normal chrome"))
    {
        return 1;
    }
    if (!require(!normalScreen.isNull() && normalScreenRedPixels == 0,
            "Highlight OFF must leave zero #f04444 pixels in the physical screen capture"))
    {
        return 1;
    }

    frame.setHighlighted(true);
    frame.setHighlighted(false);
    frame.setHighlighted(true);
    frame.setHighlighted(false);
    if (!require(frame.canvasRect() == originalCanvasRect
            && canvasSurface.geometry() == originalSurfaceGeometry,
            "repeated Highlight toggles must remain paint-only"))
    {
        return 1;
    }

    frame.resize(207, 119);
    QApplication::processEvents();
    if (!require(frame.canvasRect() == QRect{4, 4, 199, 111}
            && canvasSurface.geometry() == frame.canvasRect(),
            "resizing must keep the child aligned with the canonical Canvas rect"))
    {
        return 1;
    }

    frame.resize(7, 5);
    QApplication::processEvents();
    if (!require(frame.canvasRect().width() >= 0 && frame.canvasRect().height() >= 0
            && canvasSurface.geometry() == frame.canvasRect(),
            "tiny geometry must not create a negative Canvas extent"))
    {
        return 1;
    }

    const QList<QWidget*> directChildren = frame.findChildren<QWidget*>(
        QString{},
        Qt::FindDirectChildrenOnly);
    if (!require(directChildren.size() == 1 && directChildren.front() == &canvasSurface,
            "Candidate E must not create additional native Highlight child widgets"))
    {
        return 1;
    }

    std::cout << "CanvasChromeFrame physical gate: DPR="
              << frame.devicePixelRatioF()
              << " onRed=" << highlightedScreenRedPixels
              << " offRed=" << normalScreenRedPixels << '\n';

    return require(frame.focusPolicy() == Qt::NoFocus,
        "CanvasChromeFrame must not take keyboard focus")
        ? 0
        : 1;
}
