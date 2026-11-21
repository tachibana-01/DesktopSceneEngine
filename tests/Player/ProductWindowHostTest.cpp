#include "ProductWindowHost.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QPushButton>
#include <QtCore/qt_windows.h>

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace DesktopSceneEngine::Player;

void require(bool condition, const char* failure)
{
    if (!condition) throw std::runtime_error(failure);
}

class OwnedRoleWindow final : public ProductWindowHost
{
public:
    using ProductWindowHost::ProductWindowHost;
    bool closes = false;
    unsigned closeRequests = 0;
    unsigned systemCloseMessages = 0;
    unsigned nativeCloseMessages = 0;

protected:
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override
    {
        const auto& native = *static_cast<const MSG*>(message);
        if (native.message == WM_SYSCOMMAND && (native.wParam & 0xfff0) == SC_CLOSE) ++systemCloseMessages;
        if (native.message == WM_CLOSE) ++nativeCloseMessages;
        return ProductWindowHost::nativeEvent(type, message, result);
    }

    void closeEvent(QCloseEvent* event) override
    {
        ++closeRequests;
        if (!closes) event->ignore();
        else ProductWindowHost::closeEvent(event);
    }
};

LRESULT hit(HWND target, HWND root, const QPoint& rootPoint)
{
    POINT screen{rootPoint.x(), rootPoint.y()};
    require(ClientToScreen(root, &screen), "cannot convert root hit-test point to screen pixels");
    return SendMessageW(target, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y));
}

void deliverNativeClose()
{
    // SC_CLOSE delegates to WM_CLOSE in the native queue. QPA dispatches
    // the resulting QWindow close event separately on the next event turn.
    QApplication::processEvents();
    QApplication::processEvents();
}

ProductChromeGeometry geometryFor(HWND root)
{
    RECT bounds{};
    require(GetClientRect(root, &bounds), "role client bounds unavailable");
    const auto dpi = GetDpiForWindow(root);
    const auto dip = [dpi](int value) { return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI); };
    const auto width = dip(46);
    const auto button = [&](int index) {
        return QRect{static_cast<int>(bounds.right) - width * index, 0, width, dip(42)};
    };
    return {{dip(80), 0, dip(260), dip(42)}, button(3), button(2), button(1)};
}

void verifyNativeRegions(OwnedRoleWindow& window, HWND root, HWND host, HWND viewport)
{
    const auto geometry = geometryFor(root);
    require(window.setProductChromeGeometry(reinterpret_cast<WId>(host), geometry), "full-client chrome geometry must be accepted");
    const std::array checks{
        std::pair{geometry.drag.center(), HTCAPTION},
        std::pair{geometry.minimize.center(), HTMINBUTTON},
        std::pair{geometry.maximize.center(), HTMAXBUTTON},
        std::pair{geometry.close.center(), HTCLOSE},
    };
    for (const auto& [point, expected] : checks)
    {
        require(hit(root, root, point) == expected, "top-level root must own native caption hit and Snap semantics");
        require(hit(host, root, point) == HTTRANSPARENT && hit(viewport, root, point) == HTTRANSPARENT,
            "each owned composition-host ancestor must yield native chrome hit testing to the root");
    }
    RECT client{};
    GetClientRect(root, &client);
    const int right = client.right - 2, bottom = client.bottom - 2;
    const std::array borders{
        std::pair{QPoint{1, 1}, HTTOPLEFT}, std::pair{QPoint{right / 2, 1}, HTTOP},
        std::pair{QPoint{right, 1}, HTTOPRIGHT}, std::pair{QPoint{right, bottom / 2}, HTRIGHT},
        std::pair{QPoint{right, bottom}, HTBOTTOMRIGHT}, std::pair{QPoint{right / 2, bottom}, HTBOTTOM},
        std::pair{QPoint{1, bottom}, HTBOTTOMLEFT}, std::pair{QPoint{1, bottom / 2}, HTLEFT},
    };
    for (const auto& [point, expected] : borders)
    {
        require(hit(root, root, point) == expected, "restored native resize hit must cover all eight directions");
        require(hit(host, root, point) == HTTRANSPARENT && hit(viewport, root, point) == HTTRANSPARENT,
            "child surfaces must yield only the restored edges to the root");
    }
    const QPoint interactive{geometry.drag.left() / 2, geometry.drag.center().y()};
    require(hit(root, root, interactive) == HTCLIENT && hit(host, root, interactive) == HTCLIENT,
        "interactive regions in the same row must remain client input");
    require(hit(root, root, {geometry.drag.center().x(), geometry.drag.bottom() + 25}) == HTCLIENT,
        "content below the caption is not a drag surface");

    auto overlapping = geometry;
    overlapping.drag = geometry.maximize;
    require(!window.setProductChromeGeometry(reinterpret_cast<WId>(host), overlapping), "overlapping geometry must be rejected");
    require(!window.setProductChromeGeometry(reinterpret_cast<WId>(GetDesktopWindow()), geometry),
        "external window geometry must be rejected");

    window.clearProductChromeGeometry();
    require(hit(root, root, geometry.maximize.center()) == HTCLIENT, "retired page geometry must not own caption hits");
    require(hit(host, root, {right, bottom}) == HTTRANSPARENT, "resize must remain native while a page is unbound");
    require(window.setProductChromeGeometry(reinterpret_cast<WId>(host), geometry), "a fresh page may re-admit chrome");

    window.resize(window.width() + 38, window.height() + 24);
    QApplication::processEvents();
    require(hit(root, root, geometry.drag.center()) == HTCLIENT, "resize must invalidate the old caption projection");
    require(window.setProductChromeGeometry(reinterpret_cast<WId>(host), geometryFor(root)), "resized chrome must be re-admissible");
}

void verifyProductRole(ProductWindowRole role)
{
    HWND root = nullptr;
    {
        OwnedRoleWindow window(role);
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        window.setWindowTitle(QStringLiteral("DesktopSceneEngine — integrated chrome native check"));
        window.resize(820, 560);
        window.show();
        QApplication::processEvents();
        root = reinterpret_cast<HWND>(window.winId());
        auto* viewport = window.productViewport();
        require(IsWindowVisible(root) && !window.productFrameFailed(), "native role must start with a visible frame");
        require(viewport && !viewport->isVisible(), "startup fallback owns the body before content");
        require(reinterpret_cast<INT_PTR>(GetPropW(root, L"DSE.ProductWindowRole")) == static_cast<INT_PTR>(role),
            "native role marker differs from the selected role");
        const auto style = GetWindowLongPtrW(root, GWL_STYLE);
        constexpr LONG_PTR contract = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        require((style & contract) == contract, "full client must retain the Windows frame, resize and system styles");
        RECT frame{};
        POINT client{};
        require(GetWindowRect(root, &frame) && ClientToScreen(root, &client), "cannot inspect native frame");
        require(client.y == frame.top && client.x == frame.left, "restored native role must have no legacy title inset");

        auto* startup = window.findChild<QLabel*>(QStringLiteral("dseProductStartupStatus"));
        auto* fallbackClose = window.findChild<QPushButton*>(QStringLiteral("dseProductFallbackClose"));
        require(startup && startup->isVisible() && !startup->text().isEmpty(), "startup fallback must be visible before content");
        require(fallbackClose && fallbackClose->isVisible() && fallbackClose->isEnabled(), "startup fallback must be closable");
        auto* content = new QWidget(viewport);
        content->setAttribute(Qt::WA_NativeWindow);
        window.setProductContent(content);
        require(startup->isVisible() && fallbackClose->isVisible() && !content->isVisible(),
            "installed content does not replace the closable fallback before readiness");
        window.setProductPresentationStage(ProductPresentationStage::Ready);
        require(viewport->isVisible() && content->isVisible() && !startup->isVisible() && !fallbackClose->isVisible(),
            "ready shows only the installed presentation");
        const HWND viewportHandle = reinterpret_cast<HWND>(viewport->winId());
        const HWND contentHandle = reinterpret_cast<HWND>(content->winId());
        require(viewportHandle != root && GetAncestor(contentHandle, GA_ROOT) == root,
            "product viewport must be a native descendant of the role HWND");
        require(viewport->mapTo(&window, QPoint(0, 0)) == QPoint(0, 0),
            "the role's presentation must begin at the native client origin");
        RECT nativeRootClient{}, nativeViewportClient{};
        POINT viewportOrigin{};
        require(GetClientRect(root, &nativeRootClient) && GetClientRect(viewportHandle, &nativeViewportClient)
                && ClientToScreen(viewportHandle, &viewportOrigin), "native role and viewport extents must be observable");
        require(viewportOrigin.x == client.x && viewportOrigin.y == client.y && EqualRect(&nativeViewportClient, &nativeRootClient),
            "viewport must fill the whole root client with no second band");

        verifyNativeRegions(window, root, contentHandle, viewportHandle);

        window.requestProductChromeAction(ProductChromeControl::Maximize);
        QApplication::processEvents();
        require(IsZoomed(root) && window.productChromeState().maximized, "maximize must use the HWND state");
        require(hit(root, root, {1, 1}) == HTCLIENT, "a maximized window has no native resize edge");
        window.requestProductChromeAction(ProductChromeControl::Maximize);
        QApplication::processEvents();
        require(!IsZoomed(root) && !window.productChromeState().maximized, "maximize while zoomed must restore the same HWND");

        window.setProductPresentationStage(ProductPresentationStage::Failed, QStringLiteral("Owned startup failure"));
        require(startup->isVisible() && !content->isVisible() && startup->text() == QStringLiteral("Owned startup failure")
                && fallbackClose->isVisible(), "explicit failure must be visible and closable");
        fallbackClose->click();
        deliverNativeClose();
        require(window.systemCloseMessages == 1 && window.nativeCloseMessages == 1 && window.closeRequests == 1
                && IsWindow(root) && IsWindowVisible(root),
            "fallback Close honors the role's normal refusal");
        window.setProductPresentationStage(ProductPresentationStage::Ready);
        require(content->isVisible() && !startup->isVisible(), "a later ready generation dismisses stale failure");

        window.closes = true;
        window.requestProductChromeAction(ProductChromeControl::Close);
        deliverNativeClose();
        require(window.systemCloseMessages == 2 && window.nativeCloseMessages == 2 && window.closeRequests == 2
                && !IsWindowVisible(root), "accepted native Close retires the visible role");
    }
    require(!IsWindow(root), "destroyed role leaves no orphan HWND");
}
}

int main(int argc, char** argv)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try
    {
        verifyProductRole(ProductWindowRole::Launcher);
        verifyProductRole(ProductWindowRole::Session);
        verifyProductRole(ProductWindowRole::BackgroundMusic);
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL " << failure.what() << '\n';
        return 1;
    }
    std::cout << "PASS native role, full-client frame, composition HWND hits, resize/maximize and Close policy\n";
}
