#pragma once

#include <QImage>
#include <QRect>
#include <QString>
#include <QtGui/qwindowdefs.h>
#include <memory>

namespace DesktopSceneEngine::Player
{
// Host-resolved physical geometry only. Image/time rectangles are relative to
// screenRect. This value carries no playback, decode, page or cache authority.
struct NativeTimelineHoverCard
{
    QRect screenRect, imageRect, timeRect;
    int borderPixels = 1;
    int fontPixels = 10;
    QString timestamp, statusText;
    QImage image;
};

// One GUI-thread, Session-owned popup HWND. The Host supplies already-authorized
// content/geometry and revokes visibility on page/owner lifecycle changes.
class NativeTimelineHoverWindow final
{
public:
    explicit NativeTimelineHoverWindow(WId owner);
    ~NativeTimelineHoverWindow();
    NativeTimelineHoverWindow(const NativeTimelineHoverWindow&) = delete;
    NativeTimelineHoverWindow& operator=(const NativeTimelineHoverWindow&) = delete;

    [[nodiscard]] QString present(const NativeTimelineHoverCard& card);
    void hide() noexcept;
    [[nodiscard]] void* nativeHandle() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
