#include "ProductWindowHost.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QStyleHints>
#include <QVariant>
#include <QVBoxLayout>

#include <QtCore/qt_windows.h>
#include <CommCtrl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace DesktopSceneEngine::Player
{
namespace
{
constexpr UINT_PTR FrameSubclass = 1;
constexpr auto RoleProperty = L"DSE.ProductWindowRole";
constexpr COLORREF BgmBodyColor = RGB(11, 12, 14);

bool highContrast() noexcept
{
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

QString stageName(ProductPresentationStage stage)
{
    switch (stage)
    {
    case ProductPresentationStage::Starting: return QStringLiteral("starting");
    case ProductPresentationStage::OpeningWebView: return QStringLiteral("opening-webview");
    case ProductPresentationStage::Navigating: return QStringLiteral("navigating");
    case ProductPresentationStage::AwaitingProduct: return QStringLiteral("awaiting-product");
    case ProductPresentationStage::Ready: return QStringLiteral("ready");
    case ProductPresentationStage::Failed: return QStringLiteral("failed");
    case ProductPresentationStage::Retired: return QStringLiteral("retired");
    }
    return {};
}

Qt::WindowFlags productWindowFlags()
{
    // Ask Qt to size its QWidget in the full client area, with no Qt caption.
    // attachWindowFrame installs the actual Win32 frame and system styles.
    return Qt::Window | Qt::FramelessWindowHint;
}

void applyTitleTheme(HWND hwnd)
{
    const BOOL dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark && !highContrast();
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

bool eraseBgmFrame(HWND hwnd, HDC target)
{
    RECT bounds{};
    if (!target || !GetClientRect(hwnd, &bounds)) return false;
    const auto previous = SetDCBrushColor(target, highContrast() ? GetSysColor(COLOR_WINDOW) : BgmBodyColor);
    const auto erased = FillRect(target, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (previous != CLR_INVALID) SetDCBrushColor(target, previous);
    return erased != FALSE;
}

ProductChromeControl controlFromHit(LRESULT hit) noexcept
{
    switch (hit)
    {
    case HTMINBUTTON: return ProductChromeControl::Minimize;
    case HTMAXBUTTON: return ProductChromeControl::Maximize;
    case HTCLOSE: return ProductChromeControl::Close;
    default: return ProductChromeControl::None;
    }
}

QRect clientRect(HWND window) noexcept
{
    RECT rect{};
    if (!GetClientRect(window, &rect)) return {};
    return {0, 0, static_cast<int>(rect.right), static_cast<int>(rect.bottom)};
}

QPoint messagePoint(LPARAM lp) noexcept { return {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; }

bool sameGeometry(const ProductChromeGeometry& a, const ProductChromeGeometry& b) noexcept
{
    return a.drag == b.drag && a.minimize == b.minimize && a.maximize == b.maximize && a.close == b.close;
}

bool validGeometry(const ProductChromeGeometry& geometry, const QRect& client, int dpi) noexcept
{
    const int laneHeight = MulDiv(128, dpi, USER_DEFAULT_SCREEN_DPI);
    const std::array rects{geometry.minimize, geometry.maximize, geometry.close, geometry.drag};
    for (std::size_t i = 0; i < rects.size(); ++i)
    {
        const auto& rect = rects[i];
        if (i == 3 && rect.isEmpty()) continue;
        if (rect.isEmpty() || !client.contains(rect) || rect.bottom() >= laneHeight) return false;
        for (std::size_t previous = 0; previous < i; ++previous)
            if (rect.intersects(rects[previous])) return false;
    }
    return true;
}
}

struct ProductWindowHost::NativeChrome
{
    explicit NativeChrome(ProductWindowHost& window) : owner(window) {}

    ProductWindowHost& owner;
    HWND root = nullptr;
    std::vector<HWND> inputChain;
    std::optional<ProductChromeGeometry> geometry;
    QRect admittedClient;
    UINT admittedDpi = 0;
    ProductChromeControl hovered = ProductChromeControl::None;
    ProductChromeControl pressed = ProductChromeControl::None;
    bool notifyPending = false;
    std::function<void(ProductChromeState)> sink;

    [[nodiscard]] ProductChromeState state() const noexcept
    {
        const bool exists = root && IsWindow(root);
        return {exists && GetForegroundWindow() == root, exists && IsZoomed(root), exists && IsIconic(root),
            hovered, pressed == hovered ? pressed : ProductChromeControl::None};
    }

    void publish()
    {
        if (!sink || notifyPending) return;
        notifyPending = true;
        QMetaObject::invokeMethod(&owner, [this] {
            notifyPending = false;
            // The sink may replace itself or retire its presentation.
            const auto notify = sink;
            if (notify) notify(state());
        }, Qt::QueuedConnection);
    }

    void setHovered(ProductChromeControl next)
    {
        if (hovered == next) return;
        hovered = next;
        publish();
    }

    [[nodiscard]] bool hasCapture() const noexcept
    {
        return root && pressed != ProductChromeControl::None && GetCapture() == root;
    }

    void cancelPointer()
    {
        const bool release = hasCapture();
        pressed = ProductChromeControl::None;
        setHovered(ProductChromeControl::None);
        if (release) ReleaseCapture();
        publish();
    }

    void clearGeometry()
    {
        geometry.reset();
        admittedClient = {};
        admittedDpi = 0;
        cancelPointer();
    }

    void unbindChildren() noexcept
    {
        for (const auto child : inputChain) RemoveWindowSubclass(child, inputProc, FrameSubclass);
        inputChain.clear();
    }

    bool setGeometry(HWND host, const ProductChromeGeometry& next)
    {
        if (!root || !host || IsIconic(root)) return false;
        const auto client = clientRect(root);
        const auto dpi = GetDpiForWindow(root);
        if (!dpi || !validGeometry(next, client, dpi)) return false;

        std::vector<HWND> chain;
        if (GetAncestor(host, GA_ROOT) != root) return false;
        for (auto child = host; child != root; child = GetAncestor(child, GA_PARENT))
        {
            DWORD process{};
            if (GetWindowThreadProcessId(child, &process) != GetCurrentThreadId() || process != GetCurrentProcessId())
                return false;
            chain.push_back(child);
        }
        // Windowed WebViews would intercept this route in a foreign child.
        // Only the role's own composited, same-GUI-thread host chain participates.
        if (chain != inputChain)
        {
            clearGeometry();
            unbindChildren();
            for (const auto child : chain)
            {
                if (!SetWindowSubclass(child, inputProc, FrameSubclass, reinterpret_cast<DWORD_PTR>(this)))
                {
                    unbindChildren();
                    return false;
                }
                inputChain.push_back(child);
            }
        }
        if (geometry && sameGeometry(*geometry, next) && admittedClient == client && admittedDpi == dpi) return true;
        cancelPointer();
        geometry = next;
        admittedClient = client;
        admittedDpi = dpi;
        return true;
    }

    [[nodiscard]] LRESULT hitTest(const QPoint& physicalScreenPoint) const noexcept
    {
        if (!root || !IsWindowEnabled(root) || IsIconic(root)) return HTCLIENT;
        POINT local{physicalScreenPoint.x(), physicalScreenPoint.y()};
        if (!ScreenToClient(root, &local)) return HTNOWHERE;
        const QPoint point(local.x, local.y);
        const auto client = clientRect(root);
        if (!client.contains(point)) return HTNOWHERE;

        const auto dpi = GetDpiForWindow(root);
        if (!IsZoomed(root) && (GetWindowLongPtrW(root, GWL_STYLE) & WS_THICKFRAME))
        {
            const auto padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const auto borderX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + padding;
            const auto borderY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + padding;
            const bool left = point.x() < borderX, right = point.x() >= client.width() - borderX;
            const bool top = point.y() < borderY, bottom = point.y() >= client.height() - borderY;
            if (top) return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
            if (bottom) return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
        }
        if (!geometry || client != admittedClient || dpi != admittedDpi) return HTCLIENT;
        if (geometry->minimize.contains(point)) return HTMINBUTTON;
        if (geometry->maximize.contains(point)) return HTMAXBUTTON;
        if (geometry->close.contains(point)) return HTCLOSE;
        return geometry->drag.contains(point) ? HTCAPTION : HTCLIENT;
    }

    void requestAction(ProductChromeControl control)
    {
        if (!root || !IsWindowEnabled(root) || !IsWindowVisible(root) || IsIconic(root)) return;
        // Resolve maximize/restore when invoked, using the actual HWND state.
        WPARAM command{};
        switch (control)
        {
        case ProductChromeControl::Minimize: command = SC_MINIMIZE; break;
        case ProductChromeControl::Maximize: command = IsZoomed(root) ? SC_RESTORE : SC_MAXIMIZE; break;
        case ProductChromeControl::Close: command = SC_CLOSE; break;
        case ProductChromeControl::None: return;
        }
        PostMessageW(root, WM_SYSCOMMAND, command, 0);
    }

    [[nodiscard]] ProductChromeControl capturedPoint(LPARAM lp) const noexcept
    {
        const auto local = messagePoint(lp);
        POINT point{local.x(), local.y()};
        if (!ClientToScreen(root, &point)) return ProductChromeControl::None;
        return controlFromHit(hitTest({point.x, point.y}));
    }

    static LRESULT CALLBACK inputProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR context)
    {
        auto& self = *reinterpret_cast<NativeChrome*>(context);
        if (message == WM_NCHITTEST)
        {
            const auto hit = self.hitTest(messagePoint(lp));
            if (hit != HTNOWHERE && hit != HTCLIENT) return HTTRANSPARENT;
        }
        if (message == WM_NCDESTROY)
        {
            self.unbindChildren();
            self.clearGeometry();
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }

    static LRESULT CALLBACK frameProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR context)
    {
        auto& self = *reinterpret_cast<NativeChrome*>(context);
        switch (message)
        {
        case WM_NCCALCSIZE:
        {
            // Keep the restored HWND full-client. Maximized UI stays inside the
            // actual monitor work area without planting a second title strip.
            auto& proposed = wp ? reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0] : *reinterpret_cast<RECT*>(lp);
            if (IsZoomed(hwnd))
            {
                MONITORINFO monitor{sizeof(monitor)};
                RECT contained{};
                if (GetMonitorInfoW(MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST), &monitor)
                    && IntersectRect(&contained, &proposed, &monitor.rcWork)) proposed = contained;
            }
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            const auto result = DefSubclassProc(hwnd, message, wp, lp);
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            {
                auto& placement = *reinterpret_cast<MINMAXINFO*>(lp);
                placement.ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top};
                placement.ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top};
            }
            return result;
        }
        case WM_NCHITTEST:
            return self.hitTest(messagePoint(lp));
        case WM_ERASEBKGND:
            if (self.owner.role_ == ProductWindowRole::BackgroundMusic && eraseBgmFrame(hwnd, reinterpret_cast<HDC>(wp)))
                return TRUE;
            break;
        case WM_SIZE:
            if (self.geometry && (IsIconic(hwnd) || clientRect(hwnd) != self.admittedClient)) self.clearGeometry();
            self.publish();
            break;
        case WM_DPICHANGED:
            self.clearGeometry();
            applyTitleTheme(hwnd);
            break;
        case WM_THEMECHANGED: case WM_DWMCOMPOSITIONCHANGED:
            applyTitleTheme(hwnd);
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) self.cancelPointer();
            self.publish();
            break;
        case WM_ENABLE: case WM_SHOWWINDOW:
            if (!wp) self.cancelPointer();
            self.publish();
            break;
        case WM_NCMOUSEMOVE:
        {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
            TrackMouseEvent(&tracking);
            self.setHovered(controlFromHit(wp));
            break;
        }
        case WM_NCMOUSELEAVE:
            self.setHovered(ProductChromeControl::None);
            break;
        case WM_NCLBUTTONDOWN: case WM_NCLBUTTONDBLCLK:
            if (const auto button = controlFromHit(wp); button != ProductChromeControl::None)
            {
                if (self.hitTest(messagePoint(lp)) == static_cast<LRESULT>(wp) && !GetCapture())
                {
                    SetCapture(hwnd);
                    if (GetCapture() == hwnd)
                    {
                        self.pressed = button;
                        self.setHovered(button);
                        self.publish();
                    }
                }
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (self.hasCapture())
            {
                self.setHovered(self.capturedPoint(lp));
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (self.hasCapture())
            {
                const auto release = self.capturedPoint(lp);
                const auto action = self.pressed;
                self.cancelPointer();
                self.setHovered(release);
                if (release == action) self.requestAction(action);
                return 0;
            }
            break;
        case WM_CANCELMODE: case WM_CAPTURECHANGED:
            self.cancelPointer();
            break;
        case WM_NCDESTROY:
            self.unbindChildren();
            self.clearGeometry();
            self.root = nullptr;
            self.owner.frameWindow_ = 0;
            RemovePropW(hwnd, RoleProperty);
            RemoveWindowSubclass(hwnd, frameProc, FrameSubclass);
            break;
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }
};

ProductWindowHost::ProductWindowHost(ProductWindowRole role, QWidget* parent)
    : QWidget(parent, productWindowFlags()), role_(role), chrome_(std::make_unique<NativeChrome>(*this))
{
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* body = new QWidget(this);
    layout->addWidget(body);
    bodyLayout_ = new QStackedLayout(body);
    bodyLayout_->setContentsMargins(0, 0, 0, 0);
    viewport_ = new QWidget(body);
    viewport_->setObjectName(QStringLiteral("dseProductViewport"));
    viewport_->setAttribute(Qt::WA_NativeWindow);
    bodyLayout_->addWidget(viewport_);

    contentLayout_ = new QStackedLayout(viewport_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    fallback_ = new QWidget(body);
    auto* fallbackLayout = new QVBoxLayout(fallback_);
    fallbackLayout->addStretch();
    startup_ = new QLabel(fallback_);
    startup_->setObjectName(QStringLiteral("dseProductStartupStatus"));
    startup_->setTextFormat(Qt::PlainText);
    startup_->setAlignment(Qt::AlignCenter);
    startup_->setWordWrap(true);
    startup_->setMargin(24);
    fallbackLayout->addWidget(startup_);
    auto* closeAction = new QPushButton(QCoreApplication::translate("ProductWindowHost", "Close"), fallback_);
    closeAction->setObjectName(QStringLiteral("dseProductFallbackClose"));
    connect(closeAction, &QPushButton::clicked, this, [this] {
        if (frameWindow_) requestProductChromeAction(ProductChromeControl::Close);
        else close();
    });
    fallbackLayout->addWidget(closeAction, 0, Qt::AlignHCenter);
    fallbackLayout->addStretch();
    bodyLayout_->addWidget(fallback_);

    QPalette palette = viewport_->palette();
    if (!highContrast())
    {
        palette.setColor(QPalette::Window, role_ == ProductWindowRole::BackgroundMusic
            ? QColor{GetRValue(BgmBodyColor), GetGValue(BgmBodyColor), GetBValue(BgmBodyColor)} : QColor{22, 22, 27});
        palette.setColor(QPalette::WindowText, QColor{224, 225, 231});
        if (role_ == ProductWindowRole::BackgroundMusic) setPalette(palette);
    }
    if (role_ == ProductWindowRole::BackgroundMusic)
    {
        // Paint the top-level and native child on their first exposed frame.
        setAutoFillBackground(true);
        viewport_->setAutoFillBackground(true);
    }
    body->setPalette(palette);
    body->setAutoFillBackground(true);
    setProductPresentationStage(ProductPresentationStage::Starting);
}

ProductWindowHost::~ProductWindowHost()
{
    chrome_->sink = {};
    detachWindowFrame();
}

void ProductWindowHost::setProductContent(QWidget* content)
{
    if (!contentLayout_ || content_ == content) return;
    clearProductChromeGeometry();
    hasPresentedContent_ = false;
    keepFailedPage_ = false;
    if (content_)
    {
        content_->hide();
        contentLayout_->removeWidget(content_);
    }
    content_ = content;
    if (content_)
    {
        contentLayout_->addWidget(content_);
        contentLayout_->setCurrentWidget(content_);
    }
    showPresentationBody();
}

void ProductWindowHost::setProductPresentationStage(ProductPresentationStage stage, const QString& detail)
{
    updatePresentationStage(stage, detail, false);
}

void ProductWindowHost::setRecoverableLauncherPageFailure()
{
    if (role_ == ProductWindowRole::Launcher)
        updatePresentationStage(ProductPresentationStage::Failed, {}, true);
}

void ProductWindowHost::updatePresentationStage(ProductPresentationStage stage, const QString& detail, bool keepFailedPage)
{
    if (stage_ == ProductPresentationStage::Retired || frameFailed_) return;
    stage_ = stage;
    keepFailedPage_ = keepFailedPage;
    setProperty("dseProductPresentation", stageName(stage_));
    if (stage == ProductPresentationStage::Retired || stage == ProductPresentationStage::Navigating
        || stage == ProductPresentationStage::Failed) clearProductChromeGeometry();
    updateStartupText(detail);
    showPresentationBody();
}

void ProductWindowHost::showPresentationBody()
{
    if (stage_ == ProductPresentationStage::Retired) return;
    hasPresentedContent_ = content_ && (hasPresentedContent_ || stage_ == ProductPresentationStage::Ready || keepFailedPage_);
    const bool showPage = hasPresentedContent_ && (stage_ != ProductPresentationStage::Failed || keepFailedPage_);
    bodyLayout_->setCurrentWidget(showPage ? viewport_ : fallback_);
}

bool ProductWindowHost::setProductChromeGeometry(WId inputHost, const ProductChromeGeometry& rootPhysical)
{
    return !frameFailed_ && stage_ != ProductPresentationStage::Failed && stage_ != ProductPresentationStage::Retired
        && chrome_->setGeometry(reinterpret_cast<HWND>(inputHost), rootPhysical);
}

void ProductWindowHost::clearProductChromeGeometry() { chrome_->clearGeometry(); }
ProductChromeState ProductWindowHost::productChromeState() const { return chrome_->state(); }

void ProductWindowHost::setProductChromeStateSink(std::function<void(ProductChromeState)> sink)
{
    chrome_->sink = std::move(sink);
    chrome_->publish();
}

void ProductWindowHost::requestProductChromeAction(ProductChromeControl control) { chrome_->requestAction(control); }
bool ProductWindowHost::hasProductCaptionCapture() const { return chrome_->hasCapture(); }

bool ProductWindowHost::isProductChromeClientPoint(const QPoint& physicalScreenPoint) const
{
    return chrome_->hitTest(physicalScreenPoint) == HTCLIENT;
}

void ProductWindowHost::updateStartupText(const QString& detail)
{
    if (!startup_) return;
    if (!detail.isEmpty()) startup_->setText(detail);
    else if (stage_ == ProductPresentationStage::Failed)
        startup_->setText(QCoreApplication::translate("ProductWindowHost", "Presentation unavailable."));
    else
        startup_->setText(QCoreApplication::translate("ProductWindowHost", "Connecting…"));
}

void ProductWindowHost::attachWindowFrame()
{
    if (frameFailed_) return;
    const WId id = internalWinId();
    if (!id || id == frameWindow_) return;
    detachWindowFrame();
    const auto hwnd = reinterpret_cast<HWND>(id);
    if (!SetWindowSubclass(hwnd, NativeChrome::frameProc, FrameSubclass, reinterpret_cast<DWORD_PTR>(chrome_.get())))
    {
        setProductPresentationStage(ProductPresentationStage::Failed,
            QCoreApplication::translate("ProductWindowHost", "The native window frame could not be initialized."));
        frameFailed_ = true;
        return;
    }
    frameWindow_ = id;
    chrome_->root = hwnd;
    SetPropW(hwnd, RoleProperty, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(role_)));
    const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_OVERLAPPEDWINDOW);
    applyTitleTheme(hwnd);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    chrome_->publish();
}

void ProductWindowHost::detachWindowFrame() noexcept
{
    chrome_->unbindChildren();
    chrome_->clearGeometry();
    if (auto* hwnd = reinterpret_cast<HWND>(frameWindow_); hwnd && IsWindow(hwnd))
    {
        RemovePropW(hwnd, RoleProperty);
        RemoveWindowSubclass(hwnd, NativeChrome::frameProc, FrameSubclass);
    }
    chrome_->root = nullptr;
    frameWindow_ = 0;
}

bool ProductWindowHost::event(QEvent* event)
{
    const auto type = event->type();
    const bool result = QWidget::event(event);
    if (type == QEvent::WinIdChange || type == QEvent::Show) attachWindowFrame();
    return result;
}
}
