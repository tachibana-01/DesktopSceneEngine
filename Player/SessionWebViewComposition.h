#pragma once
#include <QJsonObject>
#include <QRect>
#include <QtGui/qwindowdefs.h>
#include <DesktopSceneEngine/HostedCompositionContent.h>
#include <memory>
struct IUnknown;

namespace DesktopSceneEngine::Player
{
// Owns the Session's Model C composition submission and persistent root,
// Preview clip/content and ordinary UI backing; Engine still owns rendering.
class SessionWebViewComposition final
{
    struct State;
public:
    class Binding final
    {
    public:
        ~Binding();
        Binding(const Binding&) = delete;
        Binding& operator=(const Binding&) = delete;
        [[nodiscard]] IUnknown* visual() const noexcept;
        [[nodiscard]] long layout(WId host) noexcept;
        [[nodiscard]] long commit() noexcept;
        void detach() noexcept;
    private:
        friend class SessionWebViewComposition;
        struct Visual;
        Binding(std::weak_ptr<State>, std::unique_ptr<Visual>, unsigned long long);
        std::weak_ptr<State> state_;
        std::unique_ptr<Visual> visual_;
        unsigned long long id_{};
    };
    // The target may be the Session's native client viewport. WebView hosts
    // must belong to that specific viewport subtree.
    explicit SessionWebViewComposition(WId sessionRoot);
    ~SessionWebViewComposition();
    SessionWebViewComposition(const SessionWebViewComposition&) = delete;
    SessionWebViewComposition& operator=(const SessionWebViewComposition&) = delete;
    [[nodiscard]] std::unique_ptr<Binding> attach(WId host);
    void beginPage(WId host, const QString& page);
    // Input is physical Host-local geometry. Accepted geometry is the actual
    // physical Session-root placement after one native coordinate mapping.
    [[nodiscard]] QString placePreview(const QRect& physicalRect, WId host,
        const QString& page, const QString& requestId);
    void retirePreview(const QString& reason) noexcept;
    void syncContent(CompositionContentLease lease);
    [[nodiscard]] long clearPreviewContent() noexcept;
    [[nodiscard]] QJsonObject currentPresentation(WId host, const QString& page) const;
    [[nodiscard]] QJsonObject projection(WId host, const QString& page,
        const QString& requestId, bool desiredAccepted, const QString& outcome,
        const QString& reason) const;
    [[nodiscard]] QJsonObject facts() const;
private:
    std::shared_ptr<State> state_;
};
}
