// Compile this test alone: it includes the production implementation so the
// pixel proof exercises the exact rasterizer without capturing the desktop.
#include "../../Player/NativeTimelineHoverWindow.cpp"

#include <QApplication>
#include <iostream>

namespace
{
int checks{}, failures{};
void check(bool passed, const char* description)
{
    ++checks;
    if (!passed) { ++failures; std::cerr << "FAIL " << description << '\n'; }
}
constexpr wchar_t OwnerClass[] = L"DSE.Test.NativeTimelineHoverOwner";
LRESULT CALLBACK ownerProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}
HWND createOwner()
{
    WNDCLASSW definition{};
    definition.lpfnWndProc = ownerProcedure;
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpszClassName = OwnerClass;
    if (!RegisterClassW(&definition) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("test-owner-registration");
    const auto owner = CreateWindowExW(0, OwnerClass, L"Owned Hover Class Test",
        WS_OVERLAPPEDWINDOW, 120, 120, 700, 480, nullptr, nullptr, definition.hInstance, nullptr);
    if (!owner) throw std::runtime_error("test-owner-creation");
    ShowWindow(owner, SW_SHOWNOACTIVATE);
    return owner;
}
void pump() { QApplication::processEvents(); }
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    using namespace DesktopSceneEngine::Player;
    HWND owner{};
    try
    {
        owner = createOwner();
        pump();
        NativeTimelineHoverCard card;
        card.screenRect = QRect{180, 200, 216, 153};
        card.imageRect = QRect{8, 8, 200, 113};
        card.timeRect = QRect{8, 121, 200, 24};
        card.timestamp = QStringLiteral("01:23");
        card.statusText = QStringLiteral("Loading preview…");
        card.image = QImage{16, 9, QImage::Format_RGBA8888};
        card.image.fill(QColor{255, 0, 0});
        const auto raster = rasterizeHoverCard(card);
        check(!raster.isNull() && raster.size() == QSize{216, 153}, "canonical physical paint buffer");
        check(raster.pixelColor(108, 60) == QColor{255, 0, 0}, "RGBA source paints red without BGRA channel swap");
        check(raster.pixelColor(108, 0) == QColor{0x36, 0x40, 0x4d}, "accepted card border color");
        std::cout << "corner alpha [0,1,2,3,108] = "
            << qAlpha(raster.pixel(0, 0)) << ',' << qAlpha(raster.pixel(1, 0)) << ','
            << qAlpha(raster.pixel(2, 0)) << ',' << qAlpha(raster.pixel(3, 0)) << ','
            << qAlpha(raster.pixel(108, 0)) << '\n';
        // A 3px antialiased arc partially covers pixel(0,0), so it is not
        // mathematically transparent. Check its coverage gradient instead of
        // requiring zero alpha, and prove a square card cannot pass the check.
        const auto hasRoundedCorner = [](const QImage& pixels) {
            const auto a0 = qAlpha(pixels.pixel(0, 0));
            const auto a1 = qAlpha(pixels.pixel(1, 0));
            const auto a2 = qAlpha(pixels.pixel(2, 0));
            const auto a3 = qAlpha(pixels.pixel(3, 0));
            return a0 < a1 && a1 < a2 && a2 < a3 && a3 == 255;
        };
        check(hasRoundedCorner(raster), "accepted rounded corner has antialiased coverage into its opaque edge");
        QImage square{raster.size(), raster.format()};
        square.fill(QColor{0x36, 0x40, 0x4d});
        check(!hasRoundedCorner(square), "negative square-corner control cannot satisfy rounded-card proof");
        bool opaque = true, timeInk = false;
        for (int y = card.imageRect.top(); y <= card.timeRect.bottom(); ++y)
            for (int x = card.imageRect.left(); x <= card.imageRect.right(); ++x)
                opaque &= qAlpha(raster.pixel(x, y)) == 255;
        for (int y = card.timeRect.top(); y <= card.timeRect.bottom(); ++y)
            for (int x = card.timeRect.left(); x <= card.timeRect.right(); ++x)
                timeInk |= raster.pixelColor(x, y) != QColor{0x17, 0x1c, 0x23};
        check(opaque, "layered image/text content has valid opaque alpha");
        check(timeInk, "accepted formatted timestamp is actually painted");
        auto pending = card; pending.image = {};
        check(!rasterizeHoverCard(pending).isNull(), "pending state paints without an image");
        auto retained = card;
        retained.statusText = QStringLiteral("Loading preview…");
        const auto retainedPaint = rasterizeHoverCard(retained);
        check(retainedPaint == raster && retainedPaint.copy(card.imageRect).pixelColor(100, 50) == QColor{255,0,0},
            "pending plus retained image paints the same complete card; Loading never erases or overlays the ready image");
        auto invalid = card; invalid.screenRect.setWidth(4096);
        check(rasterizeHoverCard(invalid).isNull(), "unbounded card is rejected before allocation");
        invalid = card; invalid.imageRect.translate(-100, 0);
        check(rasterizeHoverCard(invalid).isNull(), "image outside card is rejected");

        HWND popup{};
        {
            NativeTimelineHoverWindow hover{reinterpret_cast<WId>(owner)};
            popup = static_cast<HWND>(hover.nativeHandle());
            check(IsWindow(popup), "exactly one popup HWND created");
            check(GetWindow(popup, GW_OWNER) == owner, "Session root is popup owner");
            check(GetAncestor(popup, GA_ROOT) == popup, "popup is a top-level window, not child");
            const auto style = GetWindowLongPtrW(popup, GWL_STYLE);
            const auto extended = GetWindowLongPtrW(popup, GWL_EXSTYLE);
            check((style & WS_POPUP) && !(style & WS_CHILD), "owned WS_POPUP semantics");
            check((extended & (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW))
                == (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW), "layered transparent no-activate tool styles");
            check(!(extended & WS_EX_TOPMOST), "no global topmost style");
            check(!IsWindowVisible(popup), "surface starts hidden");
            const auto foreground = GetForegroundWindow(), focus = GetFocus(), active = GetActiveWindow();
            check(hover.present(card).isEmpty(), "first real layered presentation succeeds");
            pump();
            check(IsWindowVisible(popup), "actual native popup is visible");
            RECT bounds{}; GetWindowRect(popup, &bounds);
            check(bounds.left == 180 && bounds.top == 200 && bounds.right - bounds.left == 216
                && bounds.bottom - bounds.top == 153, "supplied physical screen rectangle applied without another scale");
            check(GetForegroundWindow() == foreground && GetFocus() == focus && GetActiveWindow() == active,
                "presentation does not steal foreground, focus or activation");
            POINT middle{card.screenRect.center().x(), card.screenRect.center().y()};
            check(WindowFromPoint(middle) != popup, "layered transparent popup is excluded from mouse hit target");
            check(SendMessageW(popup, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE, "mouse activation is explicitly refused");
            const auto gdiObjects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            for (int move = 0; move < 50; ++move)
            {
                card.screenRect.moveLeft(180 + move);
                check(hover.present(card).isEmpty() && hover.nativeHandle() == popup, "rapid updates retain one HWND");
            }
            check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdiObjects,
                "rapid repaint releases its DIB, DC and selected-object resources");
            check(GetForegroundWindow() == foreground && GetFocus() == focus, "rapid move retains current input owner");
            hover.hide(); pump();
            check(!IsWindowVisible(popup) && IsWindow(popup), "hide preserves the same surface");
            check(hover.present(pending).isEmpty(), "same HWND can show pending content again");
            invalid = pending; invalid.timeRect.moveLeft(-50);
            check(!hover.present(invalid).isEmpty() && !IsWindowVisible(popup), "bad geometry fails explicitly and hides stale card");
            ShowWindow(owner, SW_HIDE); pump();
            check(!hover.present(pending).isEmpty() && !IsWindowVisible(popup), "hidden owner cannot show popup");
            ShowWindow(owner, SW_SHOWNOACTIVATE); pump();
            check(hover.present(pending).isEmpty(), "new authorized presentation works after owner restoration");
            ShowWindow(owner, SW_MINIMIZE); pump();
            check(!hover.present(pending).isEmpty() && !IsWindowVisible(popup), "minimized owner cannot show popup");
            ShowWindow(owner, SW_RESTORE); pump();
            check(hover.present(pending).isEmpty(), "new presentation works after minimize restoration");
        }
        check(!IsWindow(popup), "C++ teardown destroys popup exactly once");
        {
            NativeTimelineHoverWindow hover{reinterpret_cast<WId>(owner)};
            popup = static_cast<HWND>(hover.nativeHandle());
            check(hover.present(pending).isEmpty(), "second owned surface can present");
            DestroyWindow(owner); owner = nullptr; pump();
            check(!IsWindow(popup) && hover.nativeHandle() == nullptr, "OS owner destruction clears popup handle");
            check(!hover.present(pending).isEmpty(), "late presentation cannot resurrect destroyed popup");
        }
        check(!IsWindow(popup), "destructor after OS destruction does not recreate a window");
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "FAIL exception: " << error.what() << '\n';
    }
    if (owner) DestroyWindow(owner);
    std::cout << "NativeTimelineHoverWindow: " << checks - failures << '/' << checks << " PASS\n";
    return failures ? 1 : 0;
}
