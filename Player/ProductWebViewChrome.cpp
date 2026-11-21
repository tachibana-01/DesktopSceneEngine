#include "ProductWebViewChrome.h"
#include "ProductWindowHost.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <Windows.h>
#include <dcomp.h>
#include <wrl.h>
#include <WebView2.h>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace DesktopSceneEngine::Player
{
namespace
{
void checked(HRESULT result, const char* operation)
{
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + " HRESULT="
        + std::to_string(static_cast<unsigned long>(result)));
}

constexpr double CoordinateLimit = 100000;
double number(const QJsonObject& o, const char* key)
{
    return o.value(QLatin1String(key)).toDouble(-CoordinateLimit - 1);
}

struct CssGeometry
{
    QSizeF viewport;
    double dpr{};
    std::array<QRectF, 4> regions;
    bool suspended = false;
};

std::optional<CssGeometry> parseGeometry(const QJsonObject& packet, qsizetype separators)
{
    const auto viewport = packet.value("viewport").toObject();
    const auto regions = packet.value("regions").toObject();
    const bool emptyDrag = regions.value("drag").isNull();
    const bool suspended = emptyDrag && regions.value("minimize").isNull()
        && regions.value("maximize").isNull() && regions.value("close").isNull();
    if (packet.size() != 5 || viewport.size() != 3 || regions.size() != 4
        || separators != (suspended ? 12 : emptyDrag ? 24 : 28)) return {};
    CssGeometry result{{number(viewport, "width"), number(viewport, "height")}, number(viewport, "dpr"), {}, suspended};
    if (!(result.viewport.width() > 0 && result.viewport.width() <= CoordinateLimit
        && result.viewport.height() > 0 && result.viewport.height() <= CoordinateLimit
        && result.dpr >= .25 && result.dpr <= 8)) return {};
    // A DOM modal's platform backdrop makes the header inert, including its
    // native caption projections. Only a full, explicit suspension is valid.
    if (suspended) return result;
    constexpr std::array names{"drag", "minimize", "maximize", "close"};
    // innerWidth/innerHeight are integral CSS px. An edge can legitimately be
    // fractional up to the next CSS pixel, checked again in the raw viewport.
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (i == 0 && emptyDrag) continue;
        const auto rect = regions.value(QLatin1String(names[i])).toObject();
        if (rect.size() != 4) return {};
        const QRectF value(number(rect, "x"), number(rect, "y"), number(rect, "width"), number(rect, "height"));
        if (!(value.x() >= 0 && value.y() >= 0 && value.width() > 0 && value.height() > 0
            && value.right() <= result.viewport.width() + 1 && value.bottom() <= result.viewport.height() + 1)) return {};
        result.regions[i] = value;
    }
    return result;
}

std::optional<ProductChromeGeometry> physicalGeometry(const CssGeometry& geometry, HWND input, HWND root,
    ICoreWebView2Controller* controller)
{
    if (geometry.suspended) return {};
    ComPtr<ICoreWebView2Controller3> controller3;
    RECT bounds{}, client{};
    double scale{}, zoom{};
    if (!input || !root || GetAncestor(input, GA_ROOT) != root || !IsWindowVisible(input) || IsIconic(root)
        || !controller || FAILED(controller->get_Bounds(&bounds))
        || FAILED(controller->QueryInterface(IID_PPV_ARGS(&controller3)))
        || FAILED(controller3->get_RasterizationScale(&scale)) || FAILED(controller->get_ZoomFactor(&zoom))
        || !GetClientRect(input, &client) || !EqualRect(&bounds, &client)) return {};
    scale *= zoom;
    if (!(scale >= .25 && scale <= 8) || std::abs(scale - geometry.dpr) > .001
        || std::abs(std::round(geometry.viewport.width() * scale) - bounds.right) > 1
        || std::abs(std::round(geometry.viewport.height() * scale) - bounds.bottom) > 1) return {};
    POINT origin{};
    if (!ClientToScreen(input, &origin) || !ScreenToClient(root, &origin)) return {};
    std::array<QRect, 4> projected;
    const auto edge = [scale](double value) { return static_cast<int>(std::lround(value * scale)); };
    for (std::size_t i = 0; i < projected.size(); ++i)
    {
        const auto& value = geometry.regions[i];
        if (value.isEmpty()) continue;
        const auto left = edge(value.left()), top = edge(value.top());
        const auto right = edge(value.right()), bottom = edge(value.bottom());
        if (right <= left || bottom <= top || right > bounds.right || bottom > bounds.bottom) return {};
        projected[i] = QRect(origin.x + left, origin.y + top, right - left, bottom - top);
    }
    return ProductChromeGeometry{projected[0], projected[1], projected[2], projected[3]};
}

ProductChromeControl control(const QJsonValue& value)
{
    if (value == QJsonValue{"minimize"}) return ProductChromeControl::Minimize;
    if (value == QJsonValue{"maximize"}) return ProductChromeControl::Maximize;
    if (value == QJsonValue{"close"}) return ProductChromeControl::Close;
    return ProductChromeControl::None;
}

QJsonValue control(ProductChromeControl value)
{
    switch (value)
    {
    case ProductChromeControl::Minimize: return "minimize";
    case ProductChromeControl::Maximize: return "maximize";
    case ProductChromeControl::Close: return "close";
    default: return QJsonValue::Null;
    }
}
}

struct OpaqueWebViewVisual::Impl
{
    ComPtr<IDCompositionDevice> device;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
};

OpaqueWebViewVisual::OpaqueWebViewVisual(WId window) : impl_(std::make_unique<Impl>())
{
    auto& binding = *impl_;
    checked(DCompositionCreateDevice2(nullptr, IID_PPV_ARGS(&binding.device)), "create-webview-dcomp-device");
    checked(binding.device->CreateTargetForHwnd(reinterpret_cast<HWND>(window), TRUE, &binding.target), "create-webview-dcomp-target");
    checked(binding.device->CreateVisual(&binding.visual), "create-webview-dcomp-visual");
    checked(binding.target->SetRoot(binding.visual.Get()), "attach-webview-dcomp-root");
}

OpaqueWebViewVisual::~OpaqueWebViewVisual()
{
    impl_->target->SetRoot(nullptr);
    impl_->device->Commit();
}

IUnknown* OpaqueWebViewVisual::visual() const noexcept { return impl_->visual.Get(); }
long OpaqueWebViewVisual::commit() noexcept { return impl_->device->Commit(); }

void closeProductCompositionController(ICoreWebView2CompositionController* value) noexcept
{
    ComPtr<ICoreWebView2Controller> controller;
    if (value && SUCCEEDED(value->QueryInterface(IID_PPV_ARGS(&controller)))) controller->Close();
}

struct ProductWebViewChrome::State : std::enable_shared_from_this<State>
{
    QPointer<ProductWindowHost> frame;
    QPointer<QWidget> input;
    ComPtr<ICoreWebView2> web;
    QString generation;
    std::optional<CssGeometry> css;
    bool admitted = false;
    bool observing = false;
    bool pending = false;

    void announce()
    {
        if (pending || !input) return;
        pending = true;
        QTimer::singleShot(0, input, [weak = weak_from_this(), page = generation] {
            const auto state = weak.lock();
            if (!state || !state->observing || state->generation != page) return;
            state->pending = false;
            if (!state->frame || !state->web) return;
            const auto window = state->frame->productChromeState();
            const QJsonObject payload{{"active", window.active}, {"maximized", window.maximized},
                {"minimized", window.minimized}, {"hovered", control(window.hovered)}, {"pressed", control(window.pressed)}};
            const auto message = QString::fromUtf8(QJsonDocument(QJsonObject{{"kind", "shell-chrome-state"},
                {"version", 1}, {"generation", page}, {"state", payload}}).toJson(QJsonDocument::Compact));
            const auto target = state->web;
            target->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(message.utf16()));
        });
    }

    void retire()
    {
        if (!std::exchange(observing, false) || !frame) return;
        frame->setProductChromeStateSink({});
        if (frame) frame->clearProductChromeGeometry();
    }
};

ProductWebViewChrome::ProductWebViewChrome() : state_(std::make_shared<State>()) {}
ProductWebViewChrome::~ProductWebViewChrome() { state_->retire(); }

void ProductWebViewChrome::reset()
{
    const auto state = std::exchange(state_, std::make_shared<State>());
    state->retire();
}

bool ProductWebViewChrome::isModalSuspended(const QString& generation) const
{
    return state_->generation == generation && state_->css && state_->css->suspended;
}

void ProductWebViewChrome::refresh(QWidget* input, ICoreWebView2Controller* controller, ICoreWebView2* web,
    const QString& generation, bool productReady)
{
    auto* frame = input ? dynamic_cast<ProductWindowHost*>(input->window()) : nullptr;
    if (!frame || !controller || !web || generation.isEmpty() || !productReady) return;
    if (!state_->generation.isEmpty() && state_->generation != generation) reset();
    const auto state = state_;
    state->generation = generation;
    const ComPtr<ICoreWebView2Controller> currentController{controller};
    if (!state->observing)
    {
        state->frame = frame;
        state->input = input;
        state->web = web;
        state->observing = true;
        frame->setProductChromeStateSink([weak = state->weak_from_this()](ProductChromeState) {
            if (const auto current = weak.lock(); current && current->observing) current->announce();
        });
        state->announce();
    }
    if (!state->observing || !state->frame || !state->input) return;
    const auto css = state->css;
    const auto inputId = state->input->internalWinId();
    const auto frameId = state->frame->internalWinId();
    const auto proposed = css ? physicalGeometry(*css, reinterpret_cast<HWND>(inputId),
        reinterpret_cast<HWND>(frameId), currentController.Get()) : std::nullopt;
    if (state_ != state || !state->observing || !state->frame || !state->input
        || state->input->internalWinId() != inputId || state->frame->internalWinId() != frameId) return;
    // The platform can revoke admission during a change of HWND, size or DPI.
    // It deduplicates against the current native frame, not a browser cache.
    const bool admitted = proposed && state->frame->setProductChromeGeometry(inputId, *proposed);
    if (!admitted && std::exchange(state->admitted, false) && state->frame) state->frame->clearProductChromeGeometry();
    else if (admitted) state->admitted = true;
}

bool ProductWebViewChrome::receive(const QByteArray& packet, const QString& generation, QWidget* input,
    ICoreWebView2Controller* controller, ICoreWebView2* web, bool productReady)
{
    if (packet.size() > 4096) return false;
    const auto object = QJsonDocument::fromJson(packet).object();
    const auto kind = object.value("kind");
    const bool geometry = kind == QJsonValue{"shell-chrome-geometry"};
    if (!geometry && kind != QJsonValue{"shell-chrome-command"}) return false;
    if (generation.isEmpty() || object.value("generation") != QJsonValue{generation}
        || object.value("version") != QJsonValue{1}) return true;
    if (geometry)
    {
        if (const auto css = parseGeometry(object, packet.count(':')))
        {
            if (!state_->generation.isEmpty() && state_->generation != generation) reset();
            const auto state = state_;
            state->generation = generation;
            state->css = css;
            refresh(input, controller, web, generation, productReady);
            if (state == state_ && state->observing) state->announce();
        }
        return true;
    }
    const auto command = control(object.value("action"));
    if (object.size() == 4 && packet.count(':') == 4 && command != ProductChromeControl::None
        && productReady && state_->input && state_->observing && state_->generation == generation
        && state_->css && !state_->css->suspended)
    {
        QTimer::singleShot(0, state_->input, [weak = state_->weak_from_this(), page = generation, command] {
            const auto state = weak.lock();
            if (state && state->observing && state->generation == page && state->frame && state->input
                && state->css && !state->css->suspended
                && state->frame->isProductPresentationAvailable()) state->frame->requestProductChromeAction(command);
        });
    }
    return true;
}
}
