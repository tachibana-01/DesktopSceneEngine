#include "CanvasChromeFrame.h"

#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QRegion>
#include <QResizeEvent>

#include <algorithm>

namespace
{

constexpr int CanvasBoundaryRingThickness = 4;
const QColor CanvasBoundaryHighlightColor{QStringLiteral("#f04444")};
const QColor CanvasBoundaryNormalColor{QStringLiteral("#4a5562")};

}

namespace DesktopSceneEngine::Player
{

CanvasChromeFrame::CanvasChromeFrame(QWidget* parent)
    : QWidget(parent)
{
    setContentsMargins(
        CanvasBoundaryRingThickness,
        CanvasBoundaryRingThickness,
        CanvasBoundaryRingThickness,
        CanvasBoundaryRingThickness);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
}

void CanvasChromeFrame::setCanvasSurface(QWidget* canvasSurface)
{
    if (canvasSurface_ == canvasSurface)
    {
        return;
    }

    if (canvasSurface_ != nullptr)
    {
        canvasSurface_->hide();
    }

    canvasSurface_ = canvasSurface;
    if (canvasSurface_ == nullptr)
    {
        update();
        return;
    }

    canvasSurface_->setParent(this);
    updateCanvasSurfaceGeometry();
    canvasSurface_->show();
}

QWidget* CanvasChromeFrame::canvasSurface() const noexcept
{
    return canvasSurface_;
}

QRect CanvasChromeFrame::canvasRect() const noexcept
{
    const int horizontalInset = std::min(
        CanvasBoundaryRingThickness,
        std::max(width(), 0) / 2);
    const int verticalInset = std::min(
        CanvasBoundaryRingThickness,
        std::max(height(), 0) / 2);

    return {
        horizontalInset,
        verticalInset,
        std::max(width() - (2 * horizontalInset), 0),
        std::max(height() - (2 * verticalInset), 0)};
}

void CanvasChromeFrame::setHighlighted(bool highlighted)
{
    if (highlighted_ == highlighted)
    {
        return;
    }

    highlighted_ = highlighted;
    update();
}

bool CanvasChromeFrame::isHighlighted() const noexcept
{
    return highlighted_;
}

void CanvasChromeFrame::paintEvent(QPaintEvent* event)
{
    (void)event;
    QRegion ringRegion{rect()};
    const QRect surfaceRect = canvasRect();
    if (!surfaceRect.isEmpty())
    {
        ringRegion = ringRegion.subtracted(QRegion{surfaceRect});
    }

    QPainter painter{this};
    painter.setClipRegion(ringRegion);
    painter.fillRect(
        rect(),
        highlighted_ ? CanvasBoundaryHighlightColor : CanvasBoundaryNormalColor);
}

void CanvasChromeFrame::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCanvasSurfaceGeometry();
}

void CanvasChromeFrame::updateCanvasSurfaceGeometry()
{
    if (canvasSurface_ != nullptr && canvasSurface_->geometry() != canvasRect())
    {
        canvasSurface_->setGeometry(canvasRect());
    }
}

}
