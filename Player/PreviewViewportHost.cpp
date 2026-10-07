#include "PreviewViewportHost.h"

#include "CanvasPresentationGeometry.h"

#include <QResizeEvent>

PreviewViewportHost::PreviewViewportHost(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet(QStringLiteral("background-color: #11161d;"));
}

void PreviewViewportHost::setCanvasFrame(QWidget* canvasFrame)
{
    if (canvasFrame_ == canvasFrame)
    {
        return;
    }

    canvasFrame_ = canvasFrame;
    if (canvasFrame_ == nullptr)
    {
        return;
    }

    canvasFrame_->setParent(this);
    updateCanvasFrameGeometry();
}

void PreviewViewportHost::setCanvasAspectRatio(
    const DesktopSceneEngine::Domain::AspectRatio& aspectRatio)
{
    if (!aspectRatio.isValid() || canvasAspectRatio_ == aspectRatio)
    {
        return;
    }

    canvasAspectRatio_ = aspectRatio;
    updateCanvasFrameGeometry();
}

void PreviewViewportHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCanvasFrameGeometry();
}

void PreviewViewportHost::updateCanvasFrameGeometry()
{
    if (canvasFrame_ == nullptr)
    {
        return;
    }

    const QMargins frameMargins = canvasFrame_->contentsMargins();
    const QSize contentSize{
        size().width() - frameMargins.left() - frameMargins.right(),
        size().height() - frameMargins.top() - frameMargins.bottom()};
    const QRect contentGeometry = DesktopSceneEngine::Player::fitCanvasInsidePreviewViewport(
        contentSize,
        canvasAspectRatio_);
    if (contentGeometry.isEmpty())
    {
        canvasFrame_->hide();
        return;
    }

    const QRect geometry = contentGeometry.translated(
        frameMargins.left(),
        frameMargins.top())
                               .adjusted(
                                   -frameMargins.left(),
                                   -frameMargins.top(),
                                   frameMargins.right(),
                                   frameMargins.bottom());

    if (canvasFrame_->geometry() != geometry)
    {
        canvasFrame_->setGeometry(geometry);
    }
    canvasFrame_->show();
}
