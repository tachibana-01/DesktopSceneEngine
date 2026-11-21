#pragma once

#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>

class QLabel;
class QStackedLayout;

namespace DesktopSceneEngine::Player
{
enum class ProductWindowRole { Launcher = 1, Session, BackgroundMusic };
enum class ProductPresentationStage { Starting, OpeningWebView, Navigating, AwaitingProduct, Ready, Failed, Retired };
enum class ProductChromeControl { None, Minimize, Maximize, Close };
struct ProductChromeGeometry { QRect drag, minimize, maximize, close; };
struct ProductChromeState
{
    bool active{}, maximized{}, minimized{};
    ProductChromeControl hovered{}, pressed{};
};

// The role fills the client area and paints its own chrome. The top-level HWND
// owns window state and performs Windows non-client hit testing.
class ProductWindowHost : public QWidget
{
public:
    explicit ProductWindowHost(ProductWindowRole role, QWidget* parent = nullptr);
    ~ProductWindowHost() override;

    [[nodiscard]] QWidget* productViewport() const noexcept { return viewport_; }
    [[nodiscard]] bool productFrameFailed() const noexcept { return frameFailed_; }
    [[nodiscard]] bool isProductPresentationAvailable() const noexcept { return stage_ == ProductPresentationStage::Ready; }
    void setProductContent(QWidget* content);
    void setProductPresentationStage(ProductPresentationStage stage, const QString& detail = {});
    bool setProductChromeGeometry(WId inputHost, const ProductChromeGeometry& rootPhysical);
    void clearProductChromeGeometry();
    [[nodiscard]] ProductChromeState productChromeState() const;
    void setProductChromeStateSink(std::function<void(ProductChromeState)> sink);
    void requestProductChromeAction(ProductChromeControl control);
    [[nodiscard]] bool hasProductCaptionCapture() const;
    [[nodiscard]] bool isProductChromeClientPoint(const QPoint& physicalScreenPoint) const;

protected:
    bool event(QEvent* event) override;
    void setRecoverableLauncherPageFailure();

private:
    struct NativeChrome;
    void attachWindowFrame();
    void detachWindowFrame() noexcept;
    void updatePresentationStage(ProductPresentationStage stage, const QString& detail, bool keepFailedPage);
    void showPresentationBody();
    void updateStartupText(const QString& detail);

    const ProductWindowRole role_;
    std::unique_ptr<NativeChrome> chrome_;
    QWidget* viewport_ = nullptr;
    QWidget* fallback_ = nullptr;
    QLabel* startup_ = nullptr;
    QStackedLayout* contentLayout_ = nullptr;
    QStackedLayout* bodyLayout_ = nullptr;
    QPointer<QWidget> content_;
    WId frameWindow_ = 0;
    bool frameFailed_ = false;
    bool hasPresentedContent_ = false;
    bool keepFailedPage_ = false;
    ProductPresentationStage stage_ = ProductPresentationStage::Starting;
};
}
