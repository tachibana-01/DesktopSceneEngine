#pragma once

#include <DesktopSceneEngine/Domain/AspectRatio.h>

#include <QWidget>

class PreviewViewportHost final : public QWidget
{
public:

    explicit PreviewViewportHost(QWidget* parent = nullptr);

    void setCanvasFrame(QWidget* canvasFrame);

    void setCanvasAspectRatio(
        const DesktopSceneEngine::Domain::AspectRatio& aspectRatio);

protected:

    void resizeEvent(QResizeEvent* event) override;

private:

    void updateCanvasFrameGeometry();

    QWidget* canvasFrame_ = nullptr;
    DesktopSceneEngine::Domain::AspectRatio canvasAspectRatio_{};
};
