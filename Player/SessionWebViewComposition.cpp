#include "SessionWebViewComposition.h"
#include <QDebug>
#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

using Microsoft::WRL::ComPtr;
namespace DesktopSceneEngine::Player
{
namespace
{
void checked(HRESULT hr, const char* operation)
{
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) + " HRESULT="
        + std::to_string(static_cast<unsigned long>(hr)));
}
QString number(std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }
QString identity(const void* value) { return number(reinterpret_cast<quintptr>(value)); }
QJsonObject rectangle(const QRect& value)
{
    return {{"x", value.x()}, {"y", value.y()}, {"width", value.width()}, {"height", value.height()}};
}
bool contains(const RECT& client, const QRect& value) noexcept
{
    return value.width() > 0 && value.height() > 0 && value.x() >= 0 && value.y() >= 0
        && static_cast<qint64>(value.x()) + value.width() <= client.right
        && static_cast<qint64>(value.y()) + value.height() <= client.bottom;
}
}

struct SessionWebViewComposition::State
{
    HWND root{}, topLevel{}, webHost{};
    DWORD thread = GetCurrentThreadId();
    bool closed = false, healthy = true, hasAccepted = false, matteReady = false, webViewAttached = false;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> tree, matte, previewClip, preview;
    ComPtr<IDCompositionSurface> matteSurface;
    ComPtr<IDCompositionSurfaceFactory> matteFactory;
    CompositionContentLease content;
    QRect localRect, rootRect, matteRect;
    QString page, acceptedPage, acceptedRequest, reason = QStringLiteral("composition-content-not-ready");
    std::uint64_t next{}, active{}, revision{}, acceptedBinding{}, matteGeneration{};
    quintptr engineDevice{}, matteDevice{}, webViewVisual{};
    unsigned attachments{}, detachments{}, contentBindings{}, contentReleases{}, geometryCommits{}, failures{}, rollbackFailures{};
    HRESULT lastError = S_OK, lastHwndQuery = E_PENDING;
    const char* lastOperation = "none";

    void error(HRESULT hr, const char* operation) noexcept
    {
        if (FAILED(hr)) { ++failures; lastError = hr; lastOperation = operation; }
    }
    bool live() const noexcept
    {
        return !closed && GetCurrentThreadId() == thread && IsWindow(root)
            && GetAncestor(root, GA_ROOT) == topLevel;
    }
    bool ownsHost(HWND host) const noexcept
    {
        return IsWindow(host) && (host == root || IsChild(root, host));
    }
    bool current(WId host, const QString& value) const noexcept
    {
        return live() && healthy && active && webHost == reinterpret_cast<HWND>(host)
            && ownsHost(webHost) && !value.isEmpty() && page == value;
    }
    bool valid() const noexcept
    {
        return current(reinterpret_cast<WId>(webHost), page) && hasAccepted && acceptedBinding == active
            && acceptedPage == page && content.eligible() && matteReady;
    }
    QJsonValue accepted() const
    {
        if (!valid()) return QJsonValue::Null;
        return QJsonObject{{"revision", number(revision)}, {"page", acceptedPage},
            {"binding", number(acceptedBinding)}, {"requestId", acceptedRequest}, {"rect", rectangle(rootRect)},
            {"contentGeneration", number(content.contentGeneration())}, {"deviceGeneration", number(content.deviceGeneration())}};
    }
    HRESULT backingGeometry(const QRect& value) noexcept
    {
        return matte->SetTransform(D2D_MATRIX_3X2_F{float(value.width()), 0, 0, float(value.height()), 0, 0});
    }
    HRESULT backingGeometry() noexcept
    {
        RECT client{};
        if (!GetClientRect(root, &client)) return E_FAIL;
        if (client.right <= 0 || client.bottom <= 0) return S_OK;
        const QRect candidate(0, 0, client.right, client.bottom);
        const HRESULT hr = backingGeometry(candidate);
        if (SUCCEEDED(hr)) matteRect = candidate;
        return hr;
    }
    HRESULT geometry(const QRect& value, const CompositionContentLease& lease, bool open) noexcept
    {
        HRESULT hr = previewClip->SetOffsetX(float(value.x()));
        if (SUCCEEDED(hr)) hr = previewClip->SetOffsetY(float(value.y()));
        if (SUCCEEDED(hr)) hr = previewClip->SetClip(D2D_RECT_F{0, 0, open ? float(value.width()) : 0,
            open ? float(value.height()) : 0});
        if (FAILED(hr) || !open) return hr;
        const auto extent = lease.extent();
        if (!lease.eligible() || extent.width <= 0 || extent.height <= 0) return E_PENDING;
        // React supplies the aperture. Host only fits the existing buffer uniformly.
        const double scale = std::min(double(value.width()) / extent.width, double(value.height()) / extent.height);
        return preview->SetTransform(D2D_MATRIX_3X2_F{float(scale), 0, 0, float(scale),
            float((value.width() - extent.width * scale) / 2), float((value.height() - extent.height * scale) / 2)});
    }
    void retire(const QString& value) noexcept
    {
        hasAccepted = false; acceptedBinding = 0; acceptedPage.clear(); acceptedRequest.clear(); reason = value;
        if (closed) return;
        if (GetCurrentThreadId() != thread) { error(RPC_E_WRONG_THREAD, "retire-preview-thread"); return; }
        HRESULT hr = previewClip->SetClip(D2D_RECT_F{0, 0, 0, 0});
        if (SUCCEEDED(hr)) hr = composition->Commit();
        error(hr, "retire-preview-clip-or-commit");
        if (FAILED(hr)) healthy = false;
    }
    HRESULT clear() noexcept
    {
        if (closed || GetCurrentThreadId() != thread) return closed ? E_ABORT : RPC_E_WRONG_THREAD;
        retire(QStringLiteral("content-retired"));
        HRESULT hr = preview->SetContent(nullptr);
        const HRESULT backing = matte->SetContent(nullptr);
        error(hr, "clear-preview-content"); error(backing, "clear-backing-content");
        if (SUCCEEDED(hr)) hr = backing;
        const HRESULT commit = composition->Commit(); error(commit, "clear-content-commit");
        if (SUCCEEDED(hr)) hr = commit;
        if (FAILED(hr)) { healthy = false; return hr; }
        if (content.content()) ++contentReleases;
        content = {}; matteSurface.Reset(); matteFactory.Reset(); matteReady = false;
        matteGeneration = 0; engineDevice = matteDevice = 0;
        return S_OK;
    }
    bool rollback(const QRect& oldRect, bool oldOpen, IUnknown* oldContent) noexcept
    {
        HRESULT hr = preview->SetContent(oldContent);
        const HRESULT placement = geometry(oldRect, content, oldOpen && content.eligible());
        error(hr, "rollback-content"); error(placement, "rollback-placement");
        if (SUCCEEDED(hr)) hr = placement;
        const HRESULT commit = composition->Commit(); error(commit, "rollback-commit");
        if (SUCCEEDED(hr)) hr = commit;
        if (FAILED(hr)) { ++rollbackFailures; healthy = false; retire(QStringLiteral("composition-rollback-failed")); }
        return SUCCEEDED(hr);
    }
    HRESULT ensureMatte(ID3D11Device* borrowed, std::uint64_t generation)
    {
        if (matteReady && matteGeneration == generation && matteDevice == reinterpret_cast<quintptr>(borrowed)) return S_OK;
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        HRESULT hr = borrowed->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
        if (FAILED(hr) || !options.ClearView) return FAILED(hr) ? hr : E_NOTIMPL;
        ComPtr<IDXGIDevice> dxgi;
        hr = borrowed->QueryInterface(IID_PPV_ARGS(&dxgi)); if (FAILED(hr)) return hr;
        ComPtr<IDCompositionDevice2> factoryDevice;
        hr = composition.As(&factoryDevice); if (FAILED(hr)) return hr;
        ComPtr<IDCompositionSurfaceFactory> factory;
        hr = factoryDevice->CreateSurfaceFactory(dxgi.Get(), &factory); if (FAILED(hr)) return hr;
        ComPtr<IDCompositionSurface> surface;
        hr = factory->CreateSurface(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_IGNORE, &surface);
        if (FAILED(hr)) return hr;
        ComPtr<IDXGISurface> drawing; POINT offset{};
        hr = surface->BeginDraw(nullptr, IID_PPV_ARGS(&drawing), &offset); if (FAILED(hr)) return hr;
        ComPtr<IDXGISurface2> drawing2; ComPtr<ID3D11Texture2D> texture; UINT subresource = 0;
        hr = drawing.As(&drawing2);
        if (SUCCEEDED(hr)) hr = drawing2->GetResource(IID_PPV_ARGS(&texture), &subresource);
        ComPtr<ID3D11DeviceContext> context; ComPtr<ID3D11DeviceContext1> context1; ComPtr<ID3D11RenderTargetView> view;
        if (SUCCEEDED(hr)) { borrowed->GetImmediateContext(&context); hr = context.As(&context1); }
        if (SUCCEEDED(hr))
        {
            D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
            D3D11_RENDER_TARGET_VIEW_DESC vd{}; vd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            const UINT mip = desc.MipLevels ? subresource % desc.MipLevels : 0;
            const UINT width = std::max(1U, desc.Width >> std::min(mip, 31U));
            const UINT height = std::max(1U, desc.Height >> std::min(mip, 31U));
            if (!desc.MipLevels || subresource >= desc.MipLevels * desc.ArraySize || desc.Format != vd.Format
                || offset.x < 0 || offset.y < 0 || unsigned(offset.x) >= width || unsigned(offset.y) >= height) hr = E_UNEXPECTED;
            else if (desc.ArraySize > 1)
            {
                vd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY; vd.Texture2DArray.MipSlice = mip;
                vd.Texture2DArray.FirstArraySlice = subresource / desc.MipLevels; vd.Texture2DArray.ArraySize = 1;
            }
            else { vd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; vd.Texture2D.MipSlice = mip; }
            if (SUCCEEDED(hr)) hr = borrowed->CreateRenderTargetView(texture.Get(), &vd, &view);
        }
        if (SUCCEEDED(hr))
        {
            // BeginDraw may return an atlas. Touch only the allocated 1x1 pixel.
            const float color[]{17.0F / 255, 22.0F / 255, 29.0F / 255, 1};
            const D3D11_RECT pixel{offset.x, offset.y, offset.x + 1, offset.y + 1};
            context1->ClearView(view.Get(), color, &pixel, 1);
            hr = borrowed->GetDeviceRemovedReason();
        }
        view.Reset(); texture.Reset(); drawing2.Reset(); drawing.Reset(); context1.Reset(); context.Reset();
        const HRESULT end = surface->EndDraw(); error(end, "backing-EndDraw");
        if (SUCCEEDED(hr)) hr = end;
        if (FAILED(hr)) return hr;
        const QRect oldRect = matteRect;
        hr = matte->SetContent(surface.Get());
        if (SUCCEEDED(hr)) hr = backingGeometry();
        if (SUCCEEDED(hr)) hr = composition->Commit();
        if (FAILED(hr))
        {
            const HRESULT restore = matte->SetContent(matteSurface.Get()); error(restore, "backing-rollback-content");
            const HRESULT transform = backingGeometry(oldRect); error(transform, "backing-rollback-transform");
            const HRESULT commit = composition->Commit(); error(commit, "backing-rollback-commit");
            if (FAILED(restore) || FAILED(transform) || FAILED(commit)) { ++rollbackFailures; healthy = false; }
            matteRect = oldRect; return hr;
        }
        matteSurface = std::move(surface); matteFactory = std::move(factory); matteReady = true;
        matteGeneration = generation; matteDevice = reinterpret_cast<quintptr>(borrowed);
        return S_OK;
    }
};

struct SessionWebViewComposition::Binding::Visual { ComPtr<IDCompositionVisual> value; };

SessionWebViewComposition::SessionWebViewComposition(WId sessionRoot) : state_(std::make_shared<State>())
{
    auto& s = *state_; s.root = reinterpret_cast<HWND>(sessionRoot);
    s.topLevel = GetAncestor(s.root, GA_ROOT);
    if (!s.live()) throw std::runtime_error("composition-session-root-unavailable");
    checked(DCompositionCreateDevice2(nullptr, IID_PPV_ARGS(&s.composition)), "composition-create-model-c");
    checked(s.composition->CreateTargetForHwnd(s.root, TRUE, &s.target), "composition-common-root-target");
    checked(s.composition->CreateVisual(&s.tree), "composition-persistent-root");
    checked(s.composition->CreateVisual(&s.matte), "composition-ui-backing");
    checked(s.composition->CreateVisual(&s.previewClip), "composition-preview-clip");
    checked(s.composition->CreateVisual(&s.preview), "composition-preview");
    checked(s.previewClip->SetClip(D2D_RECT_F{0, 0, 0, 0}), "composition-preview-initially-closed");
    checked(s.previewClip->AddVisual(s.preview.Get(), FALSE, nullptr), "composition-preview-child");
    checked(s.tree->AddVisual(s.matte.Get(), FALSE, nullptr), "composition-backing-child");
    checked(s.tree->AddVisual(s.previewClip.Get(), TRUE, s.matte.Get()), "composition-preview-above-backing");
    checked(s.target->SetRoot(s.tree.Get()), "composition-persistent-root-attach");
    checked(s.composition->Commit(), "composition-persistent-root-commit");
    qInfo() << "[SessionComposition] root target created" << s.root;
}

SessionWebViewComposition::~SessionWebViewComposition()
{
    auto& s = *state_;
    s.error(s.clear(), "retire-content");
    s.closed = true; s.active = 0; s.page.clear();
    if (s.target) s.error(s.target->SetRoot(nullptr), "retire-root");
    if (s.composition) s.error(s.composition->Commit(), "retire-root-commit");
    // Release the graph before the held lease even when a failed device cannot
    // accept cleanup submissions. Binding retains only a weak State reference.
    s.target.Reset(); s.tree.Reset(); s.previewClip.Reset(); s.preview.Reset(); s.matte.Reset();
    s.matteSurface.Reset(); s.matteFactory.Reset(); s.composition.Reset(); s.content = {};
    qInfo() << "[SessionComposition] root target retired attachments" << s.attachments << "detachments" << s.detachments;
}

std::unique_ptr<SessionWebViewComposition::Binding> SessionWebViewComposition::attach(WId host)
{
    auto& s = *state_;
    if (!s.live() || !s.healthy || s.active || !s.ownsHost(reinterpret_cast<HWND>(host)))
        throw std::runtime_error("composition-attachment-owner-conflict");
    auto visual = std::make_unique<Binding::Visual>();
    checked(s.composition->CreateVisual(&visual->value), "composition-webview-visual");
    checked(s.tree->AddVisual(visual->value.Get(), TRUE, s.previewClip.Get()), "composition-webview-child");
    const auto id = ++s.next; s.active = id; s.webHost = reinterpret_cast<HWND>(host); ++s.attachments;
    s.webViewAttached = true; s.webViewVisual = reinterpret_cast<quintptr>(visual->value.Get());
    auto binding = std::unique_ptr<Binding>(new Binding(state_, std::move(visual), id));
    checked(binding->layout(host), "composition-host-layout");
    return binding;
}

void SessionWebViewComposition::beginPage(WId host, const QString& page)
{
    auto& s = *state_;
    if (!s.live() || !s.active || s.webHost != reinterpret_cast<HWND>(host) || page.isEmpty()) return;
    if (s.page != page) { s.retire(QStringLiteral("page-generation-retired")); s.page = page; }
}
void SessionWebViewComposition::retirePreview(const QString& reason) noexcept { state_->retire(reason); }
long SessionWebViewComposition::clearPreviewContent() noexcept { return state_->clear(); }

QString SessionWebViewComposition::placePreview(const QRect& physicalRect, WId host,
    const QString& page, const QString& requestId)
{
    auto& s = *state_;
    if (!s.current(host, page)) return QStringLiteral("composition-page-or-binding-retired");
    if (requestId.isEmpty()) return QStringLiteral("composition-request-id-required");
    RECT client{}, rootClient{}; POINT origin{}; SetLastError(ERROR_SUCCESS);
    if (!GetClientRect(s.webHost, &client) || !GetClientRect(s.root, &rootClient)
        || (!MapWindowPoints(s.webHost, s.root, &origin, 1) && GetLastError() != ERROR_SUCCESS))
        return QStringLiteral("composition-coordinate-unavailable");
    if (!contains(client, physicalRect)) return QStringLiteral("composition-rect-outside-current-host");
    const auto x = qint64(physicalRect.x()) + origin.x, y = qint64(physicalRect.y()) + origin.y;
    if (x < 0 || y < 0 || x + physicalRect.width() > rootClient.right || y + physicalRect.height() > rootClient.bottom)
        return QStringLiteral("composition-rect-outside-current-root");
    const QRect candidate(int(x), int(y), physicalRect.width(), physicalRect.height());
    if (!s.content.eligible() || !s.matteReady)
    {
        if (s.content.content() && !s.content.eligible()) s.retire(QStringLiteral("content-lifecycle-invalid"));
        return QStringLiteral("composition-content-not-ready");
    }
    if (s.valid() && s.rootRect == candidate) return {}; // A transport UUID is not a new presentation.
    HRESULT hr = s.geometry(candidate, s.content, true);
    if (SUCCEEDED(hr)) hr = s.composition->Commit();
    if (FAILED(hr))
    {
        s.error(hr, "preview-placement-or-commit"); s.rollback(s.rootRect, s.valid(), s.content.content());
        return QStringLiteral("composition-preview-placement-failed");
    }
    s.localRect = physicalRect; s.rootRect = candidate; s.hasAccepted = true;
    s.acceptedPage = page; s.acceptedBinding = s.active; s.acceptedRequest = requestId;
    ++s.revision; ++s.geometryCommits; s.reason.clear();
    return {};
}

void SessionWebViewComposition::syncContent(CompositionContentLease lease)
{
    auto& s = *state_;
    if (!s.live() || !s.healthy) return;
    if (!lease.eligible())
    {
        // An empty acquisition says only that no new publication is ready.
        // A retained eligible lease remains the Host's presentation authority.
        if (s.content.content() && !s.content.eligible())
        { s.error(s.clear(), "invalid-content-clear"); s.reason = QStringLiteral("content-lifecycle-invalid"); }
        return;
    }
    const auto extent = lease.extent(), oldExtent = s.content.extent();
    const bool changed = !s.content.eligible() || s.content.content() != lease.content()
        || s.content.targetId() != lease.targetId() || s.content.contentGeneration() != lease.contentGeneration()
        || s.content.deviceGeneration() != lease.deviceGeneration() || extent.width != oldExtent.width || extent.height != oldExtent.height;
    if (!changed) return;
    ComPtr<IDXGISwapChain1> chain;
    HRESULT hr = lease.content()->QueryInterface(IID_PPV_ARGS(&chain));
    DXGI_SWAP_CHAIN_DESC1 description{};
    if (SUCCEEDED(hr)) hr = chain->GetDesc1(&description);
    if (SUCCEEDED(hr) && (!description.Width || !description.Height || description.Width != unsigned(extent.width)
        || description.Height != unsigned(extent.height))) hr = E_UNEXPECTED;
    HWND contentWindow{};
    if (SUCCEEDED(hr))
    {
        s.lastHwndQuery = chain->GetHwnd(&contentWindow);
        if (SUCCEEDED(s.lastHwndQuery) && contentWindow) hr = E_UNEXPECTED;
    }
    ComPtr<ID3D11Device> borrowed;
    if (SUCCEEDED(hr)) hr = chain->GetDevice(IID_PPV_ARGS(&borrowed));
    if (SUCCEEDED(hr)) hr = s.ensureMatte(borrowed.Get(), lease.deviceGeneration());
    if (FAILED(hr))
    {
        // Native content/device validation failed. A still-live old lease alone
        // must not authorize reopening this failed composition owner.
        s.error(hr, "content-or-backing-unavailable"); s.healthy = false;
        s.retire(QStringLiteral("content-or-device-invalid")); return;
    }
    // Device retirement requires fresh desired geometry. Eligible same-device
    // replacement can reuse the accepted aperture without a blank gap.
    if (s.content.content() && s.content.deviceGeneration() != lease.deviceGeneration())
        s.retire(QStringLiteral("device-generation-retired"));
    const bool keepOpen = s.hasAccepted && s.current(reinterpret_cast<WId>(s.webHost), s.page)
        && s.acceptedBinding == s.active && s.acceptedPage == s.page;
    hr = s.preview->SetContent(lease.content());
    if (SUCCEEDED(hr)) hr = s.geometry(s.rootRect, lease, keepOpen);
    if (SUCCEEDED(hr)) hr = s.composition->Commit();
    if (FAILED(hr))
    {
        s.error(hr, "content-replacement-or-commit"); s.rollback(s.rootRect, s.valid(), s.content.content());
        if (!s.content.eligible()) s.retire(QStringLiteral("content-lifecycle-invalid"));
        return;
    }
    if (s.content.content()) ++s.contentReleases;
    s.content = std::move(lease); s.engineDevice = reinterpret_cast<quintptr>(borrowed.Get()); ++s.contentBindings;
    if (keepOpen) { ++s.revision; s.reason.clear(); }
    else s.reason = QStringLiteral("awaiting-geometry");
}

QJsonObject SessionWebViewComposition::currentPresentation(WId host, const QString& page) const
{
    const auto& s = *state_; const bool usable = s.current(host, page) && s.valid();
    const QString reason = usable ? QString{} : !s.current(host, page) ? QStringLiteral("lifecycle-retired")
        : s.content.content() && !s.content.eligible() ? QStringLiteral("content-lifecycle-invalid") : s.reason;
    return {{"acceptedThrough", number(s.revision)}, {"accepted", usable ? s.accepted() : QJsonValue(QJsonValue::Null)},
        {"lifecycleValid", usable}, {"reason", reason}, {"apertureOwner", "host-composition-clip"}};
}
QJsonObject SessionWebViewComposition::projection(WId host, const QString& page, const QString& requestId,
    bool desiredAccepted, const QString& outcome, const QString& reason) const
{
    const auto current = currentPresentation(host, page); const bool usable = current["lifecycleValid"].toBool();
    return {{"status", usable ? "visible" : "suspended"}, {"desiredAccepted", desiredAccepted && usable},
        {"desiredRequestId", requestId}, {"requestOutcome", outcome}, {"reason", reason},
        {"accepted", current["accepted"]}, {"lifecycleValid", usable}, {"apertureOwner", "host-composition-clip"}};
}
QJsonObject SessionWebViewComposition::facts() const
{
    const auto& s = *state_; const auto extent = s.content.extent();
    return {{"root", identity(s.root)}, {"topmostTarget", true}, {"activeBinding", number(s.active)},
        {"attachments", int(s.attachments)}, {"detachments", int(s.detachments)}, {"closed", s.closed}, {"modelC", true},
        {"rootTarget", identity(s.target.Get())}, {"compositionDevice", identity(s.composition.Get())},
        {"commonRootVisual", identity(s.tree.Get())}, {"previewVisual", identity(s.preview.Get())},
        {"previewClipVisual", identity(s.previewClip.Get())}, {"matteVisual", identity(s.matte.Get())},
        {"sessionTrees", 1}, {"rootTargetsCreated", 1}, {"previewVisualsCreated", 1},
        {"webViewVisuals", s.webViewAttached ? 1 : 0}, {"webViewVisual", number(s.webViewVisual)},
        {"hostD3dDevicesCreated", 0},
        {"contentAttached", bool(s.content.content())}, {"ownedContentLeases", s.content.content() ? 1 : 0},
        {"contentIdentity", identity(s.content.content())}, {"contentGeneration", number(s.content.contentGeneration())},
        {"engineDeviceGeneration", number(s.content.deviceGeneration())}, {"engineDeviceIdentity", number(s.engineDevice)},
        {"contentEligible", s.content.eligible()}, {"contentCurrent", s.content.isCurrent()},
        {"bufferWidth", extent.width}, {"bufferHeight", extent.height}, {"contentBindings", int(s.contentBindings)},
        {"leaseReleases", int(s.contentReleases)}, {"geometryCommits", int(s.geometryCommits)},
        {"acceptedThrough", number(s.revision)}, {"accepted", s.accepted()}, {"lifecycleValid", s.valid()},
        {"currentPage", s.page}, {"canonicalRect", rectangle(s.localRect)}, {"rootRect", rectangle(s.rootRect)},
        {"matteReady", s.matteReady}, {"matteSurface", identity(s.matteSurface.Get())},
        {"matteFactory", identity(s.matteFactory.Get())}, {"matteDeviceIdentity", number(s.matteDevice)},
        {"matteDeviceGeneration", number(s.matteGeneration)}, {"matteRootRect", rectangle(s.matteRect)},
        {"matteColor", "#11161d"}, {"matteDimensions", QJsonObject{{"width", 1}, {"height", 1}}},
        {"matteIsMediaProducer", false}, {"windowlessContent", bool(s.content.content())},
        {"swapchainGetHwndResult", qint64(s.lastHwndQuery)}, {"failures", int(s.failures)},
        {"rollbackFailures", int(s.rollbackFailures)}, {"lastError", qint64(s.lastError)},
        {"lastOperation", QString::fromLatin1(s.lastOperation)}, {"compositionHealthy", s.healthy},
        {"apertureOwner", "host-composition-clip"}, {"apiFactsAreScanoutProof", false}};
}

SessionWebViewComposition::Binding::Binding(std::weak_ptr<State> state, std::unique_ptr<Visual> visual, unsigned long long id)
    : state_(std::move(state)), visual_(std::move(visual)), id_(id) {}
SessionWebViewComposition::Binding::~Binding() { detach(); }
IUnknown* SessionWebViewComposition::Binding::visual() const noexcept { return visual_ ? visual_->value.Get() : nullptr; }
long SessionWebViewComposition::Binding::layout(WId host) noexcept
{
    const auto s = state_.lock(); const auto hwnd = reinterpret_cast<HWND>(host);
    if (!s || !s->live() || s->active != id_ || !visual_ || !s->ownsHost(hwnd)
        || hwnd != s->webHost) return E_ABORT;
    RECT client{}; POINT origin{}; SetLastError(ERROR_SUCCESS);
    if (!GetClientRect(hwnd, &client) || (!MapWindowPoints(hwnd, s->root, &origin, 1) && GetLastError() != ERROR_SUCCESS)) return E_FAIL;
    HRESULT hr = visual_->value->SetOffsetX(float(origin.x));
    if (SUCCEEDED(hr)) hr = visual_->value->SetOffsetY(float(origin.y));
    if (SUCCEEDED(hr)) hr = visual_->value->SetClip(D2D_RECT_F{0, 0, float(client.right), float(client.bottom)});
    if (SUCCEEDED(hr)) hr = s->backingGeometry();
    if (SUCCEEDED(hr)) hr = s->composition->Commit();
    s->error(hr, "webview-layout-or-commit"); return hr;
}
long SessionWebViewComposition::Binding::commit() noexcept
{
    const auto s = state_.lock();
    if (!s || !s->live() || s->active != id_) return E_ABORT;
    const HRESULT hr = s->composition->Commit(); s->error(hr, "webview-commit"); return hr;
}
void SessionWebViewComposition::Binding::detach() noexcept
{
    if (!id_) return;
    if (const auto s = state_.lock(); s && !s->closed && s->active == id_)
    {
        s->retire(QStringLiteral("webview-binding-retired"));
        s->page.clear();
        s->active = 0; s->webHost = nullptr; ++s->detachments;
        const HRESULT hr = s->tree->RemoveVisual(visual_->value.Get());
        const HRESULT commit = s->composition->Commit();
        s->error(hr, "webview-detach"); s->error(commit, "webview-detach-commit");
        if (FAILED(hr) || FAILED(commit)) s->healthy = false;
        else { s->webViewAttached = false; s->webViewVisual = 0; }
    }
    id_ = 0; visual_.reset();
}
}
