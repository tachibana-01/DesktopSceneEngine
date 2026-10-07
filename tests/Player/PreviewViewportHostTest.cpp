#include "CanvasChromeFrame.h"
#include "CanvasPresentationGeometry.h"
#include "PreviewViewportHost.h"

#include <QApplication>
#include <QWidget>

#include <cstdlib>
#include <iostream>

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

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    using DesktopSceneEngine::Domain::AspectRatio;
    using DesktopSceneEngine::Player::fitCanvasInsidePreviewViewport;

    if (!require(
            fitCanvasInsidePreviewViewport(
                QSize{3000, 1000},
                AspectRatio{16, 9})
                == QRect{611, 0, 1777, 1000},
            "A 16:9 Canvas should be vertically maximized and centered in a wide viewport"))
    {
        return 1;
    }

    if (!require(
            fitCanvasInsidePreviewViewport(
                QSize{1000, 1000},
                AspectRatio{16, 9})
                == QRect{0, 219, 1000, 562},
            "A 16:9 Canvas should be horizontally maximized and centered in a square viewport"))
    {
        return 1;
    }

    if (!require(
            fitCanvasInsidePreviewViewport(
                QSize{1600, 900},
                AspectRatio{16, 9})
                == QRect{0, 0, 1600, 900},
            "A matching 16:9 viewport should be filled exactly"))
    {
        return 1;
    }

    if (!require(
            fitCanvasInsidePreviewViewport(
                QSize{1600, 1000},
                AspectRatio{16, 10})
                == QRect{0, 0, 1600, 1000},
            "A matching 16:10 viewport should be filled exactly"))
    {
        return 1;
    }

    if (!require(
            fitCanvasInsidePreviewViewport(
                QSize{1600, 900},
                AspectRatio{16, 10})
                == QRect{80, 0, 1440, 900},
            "A 16:10 Canvas should be centered in a 16:9-like viewport"))
    {
        return 1;
    }

    if (!require(
            fitCanvasInsidePreviewViewport(
                QSize{0, 100},
                AspectRatio{16, 9})
                .isEmpty()
                && fitCanvasInsidePreviewViewport(
                    QSize{100, 0},
                    AspectRatio{16, 10})
                    .isEmpty()
                && fitCanvasInsidePreviewViewport(
                    QSize{1, 1},
                    AspectRatio{16, 9})
                    == QRect{0, 0, 1, 0},
            "Zero and tiny viewports should produce bounded, safe Canvas geometry"))
    {
        return 1;
    }

    PreviewViewportHost host{};
    QWidget canvasFrame{};
    host.resize(1000, 1000);
    host.setCanvasFrame(&canvasFrame);
    host.setCanvasAspectRatio(AspectRatio{16, 9});
    host.show();
    host.resize(1000, 1000);
    application.processEvents();
    if (!require(
            canvasFrame.geometry() == QRect{0, 219, 1000, 562},
            "The host should apply the Canvas presentation rectangle to its Canvas frame"))
    {
        return 1;
    }

    host.setCanvasAspectRatio(AspectRatio{16, 10});
    if (!require(
            canvasFrame.geometry() == QRect{0, 187, 1000, 625},
            "Changing only the Desktop Canvas aspect should update the Canvas frame"))
    {
        return 1;
    }

    host.resize(3000, 1000);
    if (!require(
            canvasFrame.geometry() == QRect{700, 0, 1600, 1000},
            "Host resize should re-fit the Canvas without changing its aspect mode"))
    {
        return 1;
    }

    PreviewViewportHost framedHost{};
    DesktopSceneEngine::Player::CanvasChromeFrame framedCanvas{};
    QWidget renderedCanvas{&framedCanvas};
    framedCanvas.setCanvasSurface(&renderedCanvas);
    framedCanvas.setObjectName(QStringLiteral("previewCanvasFrame"));
    framedHost.resize(1600, 900);
    framedHost.setCanvasFrame(&framedCanvas);
    framedHost.show();
    framedHost.resize(1600, 900);
    application.processEvents();

    const QSize renderedCanvasSize = framedCanvas.canvasRect().size();
    if (!require(
            renderedCanvasSize.width() > 0 && renderedCanvasSize.height() > 0
                && std::abs(renderedCanvasSize.width() * 9
                            - renderedCanvasSize.height() * 16)
                    <= 8
                && renderedCanvas.geometry() == framedCanvas.canvasRect(),
            "The permanent chrome ring must preserve the exact Canvas child aspect"))
    {
        return 1;
    }

    return 0;
}
