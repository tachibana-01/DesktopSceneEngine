#include "NativeTimelineHoverWindow.h"

#include <QColor>
#include <QDebug>
#include <QFont>
#include <QPainter>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace DesktopSceneEngine::Player
{
namespace
{
constexpr wchar_t HoverClassName[] = L"DSE.Production.TimelineHover";
constexpr int MaximumDimension = 2048; // Canonical card at the supported scale<=8.

bool validCard(const NativeTimelineHoverCard& card)
{
    const QRect local{QPoint{}, card.screenRect.size()};
    return !card.screenRect.isEmpty() && card.screenRect.width() <= MaximumDimension
        && card.screenRect.height() <= MaximumDimension
        && !card.imageRect.isEmpty() && !card.timeRect.isEmpty()
        && local.contains(card.imageRect) && local.contains(card.timeRect)
        && !card.imageRect.intersects(card.timeRect)
        && card.borderPixels >= 1 && card.borderPixels <= 16
        && card.fontPixels >= 1 && card.fontPixels <= 256
        && card.timestamp.size() <= 32 && card.statusText.size() <= 128;
}

QImage rasterizeHoverCard(const NativeTimelineHoverCard& card)
{
    if (!validCard(card)) return {};
    // ARGB32_Premultiplied is BGRA byte order on Windows. Card content is
    // opaque; only its accepted rounded outer corners remain transparent.
    QImage pixels{card.screenRect.size(), QImage::Format_ARGB32_Premultiplied};
    if (pixels.isNull()) return {};
    pixels.fill(Qt::transparent);
    QPainter painter{&pixels};
    if (!painter.isActive()) return {};
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    const qreal radius = card.fontPixels * .3; // Accepted 3px radius / 10px font.
    painter.setBrush(QColor{0x36, 0x40, 0x4d});
    painter.drawRoundedRect(QRectF{pixels.rect()}, radius, radius);
    const auto inner = QRectF{pixels.rect()}.adjusted(card.borderPixels, card.borderPixels,
        -card.borderPixels, -card.borderPixels);
    const auto innerRadius = (std::max)(qreal{0}, radius - card.borderPixels);
    painter.setBrush(QColor{0x17, 0x1c, 0x23});
    painter.drawRoundedRect(inner, innerRadius, innerRadius);
    painter.fillRect(card.imageRect, QColor{0x0b, 0x0f, 0x14});
    QFont font{QStringLiteral("Segoe UI")};
    font.setPixelSize(card.fontPixels);
    painter.setFont(font);
    if (card.image.isNull())
    {
        painter.setPen(QColor{0x8a, 0x97, 0xa8});
        painter.drawText(card.imageRect, Qt::AlignCenter | Qt::TextSingleLine,
            card.statusText);
    }
    else
    {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(card.imageRect, card.image);
    }
    painter.setPen(QColor{0xc8, 0xd1, 0xde});
    painter.drawText(card.timeRect, Qt::AlignCenter | Qt::TextSingleLine, card.timestamp);
    painter.end();
    return pixels;
}

// Own only the short-lived paint resources. The layered HWND/compositor keeps
// the last submitted pixels; this object creates no image/frame cache.
class PaintDib final
{
public:
    PaintDib(const QImage& pixels)
    {
        screen = GetDC(nullptr);
        if (!screen) return;
        memory = CreateCompatibleDC(screen);
        if (!memory) return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = pixels.width();
        info.bmiHeader.biHeight = -pixels.height();
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* target{};
        bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &target, nullptr, 0);
        if (!bitmap || !target) return;
        original = SelectObject(memory, bitmap);
        if (!original || original == HGDI_ERROR) return;
        const auto bytes = static_cast<std::size_t>(pixels.width()) * 4;
        for (int row = 0; row < pixels.height(); ++row)
            std::memcpy(static_cast<unsigned char*>(target) + row * bytes,
                pixels.constScanLine(row), bytes);
        ready = true;
    }
    ~PaintDib()
    {
        if (memory && original && original != HGDI_ERROR) SelectObject(memory, original);
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
    }
    PaintDib(const PaintDib&) = delete;
    PaintDib& operator=(const PaintDib&) = delete;
    HDC screen{}, memory{};
    HBITMAP bitmap{};
    HGDIOBJ original{};
    bool ready = false;
};
}

struct NativeTimelineHoverWindow::Impl
{
    HWND owner{}, window{};
    DWORD thread = GetCurrentThreadId();

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            if (!self) return FALSE;
            SetLastError(ERROR_SUCCESS);
            if (!SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self))
                && GetLastError() != ERROR_SUCCESS) return FALSE;
            self->window = window;
        }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            return 0;
        }
        if (message == WM_NCDESTROY && self)
        {
            // The OS can destroy an owned popup before the C++ owner is retired.
            // Clear its handle so the destructor cannot touch a reused HWND.
            self->window = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

NativeTimelineHoverWindow::NativeTimelineHoverWindow(WId owner) : impl_(std::make_unique<Impl>())
{
    impl_->owner = reinterpret_cast<HWND>(owner);
    DWORD process{};
    const auto ownerThread = GetWindowThreadProcessId(impl_->owner, &process);
    if (!IsWindow(impl_->owner) || GetAncestor(impl_->owner, GA_ROOT) != impl_->owner
        || process != GetCurrentProcessId() || ownerThread != impl_->thread)
        throw std::runtime_error("hover-popup-invalid-session-owner");

    WNDCLASSW definition{};
    definition.lpfnWndProc = Impl::procedure;
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    definition.lpszClassName = HoverClassName;
    if (!RegisterClassW(&definition) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("hover-popup-class-registration-failed");
    constexpr DWORD extended = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (!CreateWindowExW(extended, HoverClassName, L"Timeline Hover", WS_POPUP,
        0, 0, 0, 0, impl_->owner, nullptr, definition.hInstance, impl_.get()))
        throw std::runtime_error("hover-popup-window-creation-failed: " + std::to_string(GetLastError()));
    qInfo() << "[NativeTimelineHover] created" << impl_->window << "owner" << impl_->owner;
}

NativeTimelineHoverWindow::~NativeTimelineHoverWindow()
{
    if (impl_->window)
    {
        qInfo() << "[NativeTimelineHover] destroying" << impl_->window;
        if (!DestroyWindow(impl_->window))
            qWarning() << "[NativeTimelineHover] destroy failed" << GetLastError();
    }
}

QString NativeTimelineHoverWindow::present(const NativeTimelineHoverCard& card)
{
    if (GetCurrentThreadId() != impl_->thread) return QStringLiteral("hover-popup-wrong-thread");
    const auto fail = [this](const char* error) {
        hide();
        return QString::fromLatin1(error);
    };
    if (!impl_->window || !IsWindow(impl_->window) || !IsWindow(impl_->owner)
        || GetWindow(impl_->window, GW_OWNER) != impl_->owner)
        return fail("hover-popup-window-unavailable");
    if (!IsWindowVisible(impl_->owner) || IsIconic(impl_->owner))
        return fail("hover-popup-owner-not-visible");
    const auto pixels = rasterizeHoverCard(card);
    if (pixels.isNull()) return fail("hover-popup-invalid-card");
    const PaintDib paint{pixels};
    if (!paint.ready) return fail("hover-popup-paint-allocation-failed");
    POINT destination{card.screenRect.x(), card.screenRect.y()}, source{};
    SIZE size{card.screenRect.width(), card.screenRect.height()};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(impl_->window, paint.screen, &destination, &size,
        paint.memory, &source, 0, &blend, ULW_ALPHA))
        return fail("hover-popup-paint-failed");
    if (!SetWindowPos(impl_->window, HWND_TOP, card.screenRect.x(), card.screenRect.y(),
        card.screenRect.width(), card.screenRect.height(),
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW))
        return fail("hover-popup-placement-failed");
    return {};
}

void NativeTimelineHoverWindow::hide() noexcept
{
    if (impl_->window && GetCurrentThreadId() == impl_->thread)
        ShowWindow(impl_->window, SW_HIDE);
}

void* NativeTimelineHoverWindow::nativeHandle() const noexcept { return impl_->window; }
}
