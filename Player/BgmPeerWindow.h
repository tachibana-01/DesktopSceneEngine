#pragma once

#include "ProductWindowHost.h"
#include "WebViewSessionHost.h"
#include <functional>
#include <memory>

namespace DesktopSceneEngine::Player
{
// Opaque modeless presentation only. Session owns its map/permit and all data.
class BgmPeerWindow final : public ProductWindowHost
{
public:
    BgmPeerWindow(QUuid session, WebViewSessionHost* environmentOwner,
        WebViewSessionHost::BgmPresentation presentation, WebViewSessionHost::RuntimePresentation runtime,
        std::function<void()> retired, WebViewSessionHost::ControlSubmit control,
        std::function<bool()> closeBlocked = {});
    ~BgmPeerWindow() override;
    [[nodiscard]] QUuid permit() const { return presentation_.permit; }
    [[nodiscard]] PlaylistId playlistId() const { return presentation_.playlistId; }
    [[nodiscard]] WebViewSessionHost* host() const { return host_.get(); }
    [[nodiscard]] bool showForFirstPresentation();
    void publish();
    void resumeModalClose();
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void createHost(WebViewSessionHost* environmentOwner);
    void retire();
    QUuid session_;
    WebViewSessionHost::BgmPresentation presentation_;
    WebViewSessionHost::RuntimePresentation runtime_;
    WebViewSessionHost::ControlSubmit control_;
    std::function<void()> retired_;
    std::function<bool()> closeBlocked_;
    bool closeAfterModal_ = false;
    std::unique_ptr<WebViewSessionHost> host_;
};
}
