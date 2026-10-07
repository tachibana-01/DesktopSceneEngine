#pragma once

#include <QWidget>

class QPaintEvent;
class QResizeEvent;

namespace DesktopSceneEngine::Player
{

class CanvasChromeFrame final : public QWidget
{
public:

    explicit CanvasChromeFrame(QWidget* parent = nullptr);

    void setCanvasSurface(QWidget* canvasSurface);

    [[nodiscard]] QWidget* canvasSurface() const noexcept;

    [[nodiscard]] QRect canvasRect() const noexcept;

    void setHighlighted(bool highlighted);

    [[nodiscard]] bool isHighlighted() const noexcept;

protected:

    void paintEvent(QPaintEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;

private:

    void updateCanvasSurfaceGeometry();

    QWidget* canvasSurface_ = nullptr;
    bool highlighted_ = false;
};

}
