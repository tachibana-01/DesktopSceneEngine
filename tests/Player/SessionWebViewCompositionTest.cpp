#include "SessionWebViewComposition.h"
#include "GraphicsBackend.h"
#include "Renderer.h"
#include <DesktopSceneEngine/Engine.h>
#include <QApplication>
#include <QWidget>
#include <iostream>
#include <Windows.h>
using namespace DesktopSceneEngine::Player;
namespace DesktopSceneEngine::Rendering
{
// Existing diagnostic friendship; the producer remains the real production
// Engine/backend, including real composition swap chains and successful Present.
class CanvasTargetFitTestAccess
{
public:
    static GraphicsBackend& backend(Engine& engine) { return engine.renderer().backend_; }
};
}
int main(int argc,char** argv)
{
    QApplication app(argc,argv);int checks=0,failures=0;
    const auto check=[&](bool pass,const char* label){++checks;if(!pass){++failures;std::cerr<<"FAIL "<<label<<'\n';}};
    QWidget root;root.resize(800,600);QWidget host(&root);host.setAttribute(Qt::WA_NativeWindow);host.setGeometry(17,23,600,400);root.show();
    try {
        auto target=std::make_unique<SessionWebViewComposition>(root.winId());
        auto first=target->attach(host.winId());
        check(first->visual()!=nullptr&&SUCCEEDED(first->layout(host.winId())),"current same-root Host binds real visual");
        check(target->facts()["attachments"]==1,"one attachment");
        bool refused=false;try{auto duplicate=target->attach(host.winId());}catch(const std::exception&){refused=true;}
        check(refused&&target->facts()["attachments"]==1,"reject duplicate live attachment");
        QWidget otherRoot;const auto unrelated=otherRoot.winId();
        check(FAILED(first->layout(unrelated)),"reject unrelated-root geometry");
        first->detach();check(target->facts()["activeBinding"]=="0"&&target->facts()["detachments"]==1,"explicit detach clears root once");
        auto second=target->attach(host.winId());const QString identity=target->facts().value("activeBinding").toString();
        first->detach();first.reset();check(target->facts()["activeBinding"]==identity,"old detached lease cannot detach new visual");
        check(SUCCEEDED(second->commit()),"current binding commits");
        target.reset();check(FAILED(second->commit()),"binding cannot commit after Session attachment owner retirement");
        second.reset();check(IsWindow(reinterpret_cast<HWND>(root.winId()))&&IsWindow(reinterpret_cast<HWND>(host.winId())),"composition teardown preserves Session and Host HWNDs");
    }catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}
    try {
        using namespace DesktopSceneEngine;
        Engine engine;
        const auto id = engine.initialize(HostedCompositionTarget{{320, 180}});
        check(id.isValid(), "Model C uses real windowless Engine target");
        auto target = std::make_unique<SessionWebViewComposition>(root.winId());
        auto binding = target->attach(host.winId());
        const auto initial = target->facts();
        check(initial["sessionTrees"] == 1 && initial["rootTargetsCreated"] == 1
            && initial["previewVisualsCreated"] == 1 && initial["webViewVisuals"] == 1
            && initial["hostD3dDevicesCreated"] == 0, "one Model C Session tree with one Preview and WebView");
        target->beginPage(host.winId(), "page-1");
        check(!target->placePreview(QRect(10, 20, 320, 180), host.winId(), "page-1", "unready").isEmpty()
            && target->currentPresentation(host.winId(), "page-1")["accepted"].isNull(),
            "unpresented Engine content cannot become accepted");
        const auto notReady = target->currentPresentation(host.winId(), "page-1");
        engine.render();
        target->syncContent(engine.acquireHostedCompositionContent(id));
        check(target->facts()["contentAttached"].toBool() && target->facts()["contentEligible"].toBool()
            && target->facts()["matteReady"].toBool() && target->facts()["matteDeviceIdentity"] == target->facts()["engineDeviceIdentity"],
            "Host attaches actual M1 lease and initializes backing on borrowed Engine device");
        check(notReady["reason"] != target->currentPresentation(host.winId(), "page-1")["reason"]
            && target->currentPresentation(host.winId(), "page-1")["reason"] == "awaiting-geometry",
            "content becoming eligible independently projects fresh-geometry need");
        const QRect firstRect(10, 20, 320, 180);
        check(target->placePreview(firstRect, host.winId(), "page-1", "first").isEmpty(), "fresh desired geometry is accepted");
        const auto accepted = target->currentPresentation(host.winId(), "page-1");
        const auto acceptedFact = accepted["accepted"].toObject();
        POINT expectedOrigin{};
        SetLastError(ERROR_SUCCESS);
        check(MapWindowPoints(reinterpret_cast<HWND>(host.winId()), reinterpret_cast<HWND>(root.winId()), &expectedOrigin, 1)
            || GetLastError() == ERROR_SUCCESS, "native physical Host origin is available");
        check(acceptedFact["rect"].toObject()["x"].toInt() == expectedOrigin.x + 10
            && acceptedFact["rect"].toObject()["y"].toInt() == expectedOrigin.y + 20
            && acceptedFact["requestId"] == "first" && acceptedFact["revision"] == "1",
            "accepted fact describes committed root-space physical geometry mapped exactly once");
        const auto commitCount = target->facts().value("geometryCommits");
        check(target->placePreview(firstRect, host.winId(), "page-1", "new-transport-uuid").isEmpty()
            && target->currentPresentation(host.winId(), "page-1") == accepted
            && target->facts()["geometryCommits"] == commitCount,
            "same material presentation with new transport UUID makes no Commit or revision");
        const auto noOpAck = target->projection(host.winId(), "page-1", "new-transport-uuid", true, "accepted", {});
        check(noOpAck["desiredAccepted"].toBool() && noOpAck["desiredRequestId"] == "new-transport-uuid"
            && noOpAck["accepted"].toObject()["requestId"] == "first", "no-op acknowledgement may reference original material request");
        RECT hostClient{};
        check(GetClientRect(reinterpret_cast<HWND>(host.winId()), &hostClient), "native physical Host client is available");
        check(!target->placePreview(QRect(10, 20, hostClient.right + 1, 180), host.winId(), "page-1", "pending").isEmpty()
            && target->currentPresentation(host.winId(), "page-1") == accepted,
            "inadmissible transient geometry retains the previous accepted presentation");
        target->syncContent({});
        check(target->currentPresentation(host.winId(), "page-1") == accepted
            && target->facts()["ownedContentLeases"] == 1, "empty incoming acquisition retains eligible held lease");
        for (const auto rect : {QRect(10, 20, 480, 270), QRect(10, 20, 240, 135), QRect(110, 20, 320, 180)})
            check(target->placePreview(rect, host.winId(), "page-1", QString::number(rect.width()) + QString::number(rect.x())).isEmpty(),
                "grow/shrink/sidebar desired aperture uses existing uniform buffer fit");
        auto& backend = Rendering::CanvasTargetFitTestAccess::backend(engine);
        const auto engineOwnership = backend.presentationTargetDiagnosticsForTesting(id);
        check(backend.presentationTargetCountForTesting() == 1 && !engineOwnership.hasCompositionTarget
            && !engineOwnership.hasCompositionVisual && backend.compositionDiagnosticsForTesting().capabilityCreationCount == 0,
            "one Engine content endpoint has no Session DComp target or placement visual");
        const auto beforeReplacement = target->currentPresentation(host.winId(), "page-1");
        const auto oldIdentity = target->facts().value("contentIdentity");
        auto retained = engine.acquireHostedCompositionContent(id);
        check(backend.requestHostedContentReplacementForTesting(id)
            && backend.beginFrame() == Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable,
            "create real pending same-device replacement");
        target->syncContent(engine.acquireHostedCompositionContent(id));
        check(retained.eligible() && !retained.isCurrent()
            && target->currentPresentation(host.winId(), "page-1") == beforeReplacement
            && target->facts()["contentIdentity"] == oldIdentity,
            "unpresented replacement retains actual valid old content without blank gap");
        backend.renderPrepared({});
        target->syncContent(engine.acquireHostedCompositionContent(id));
        const auto replaced = target->currentPresentation(host.winId(), "page-1");
        check(replaced["lifecycleValid"].toBool() && target->facts()["contentIdentity"] != oldIdentity
            && replaced["accepted"].toObject()["contentGeneration"].toString().toULongLong()
                > beforeReplacement["accepted"].toObject()["contentGeneration"].toString().toULongLong()
            && replaced["acceptedThrough"].toString().toULongLong() == beforeReplacement["acceptedThrough"].toString().toULongLong() + 1,
            "eligible replacement attaches and advances accepted fact at successful Commit");
        const auto identities = target->facts();
        target->beginPage(host.winId(), "page-2");
        check(target->currentPresentation(host.winId(), "page-2")["accepted"].isNull()
            && target->facts()["contentIdentity"] == identities["contentIdentity"]
            && target->facts()["previewVisual"] == identities["previewVisual"]
            && target->facts()["acceptedThrough"] == identities["acceptedThrough"],
            "F5 retires accepted permit but preserves tree Preview lease and revision high-water");
        check(!target->placePreview(firstRect, host.winId(), "page-1", "stale-page").isEmpty()
            && target->placePreview(firstRect, host.winId(), "page-2", "new-page").isEmpty(), "old page fenced and new page accepts fresh geometry");
        target->retirePreview("minimized");
        check(target->currentPresentation(host.winId(), "page-2")["accepted"].isNull()
            && target->facts()["contentIdentity"] == identities["contentIdentity"]
            && target->placePreview(firstRect, host.winId(), "page-2", "restored").isEmpty(),
            "minimize revokes permit without HWND sizing; restore accepts fresh desired geometry");
        binding->detach();
        check(target->facts()["webViewVisuals"] == 0 && target->facts()["commonRootVisual"] == identities["commonRootVisual"]
            && target->facts()["previewVisual"] == identities["previewVisual"]
            && target->facts()["contentIdentity"] == identities["contentIdentity"] && target->facts()["contentEligible"].toBool(),
            "Host recreation removes only WebView child and keeps persistent tree Preview and Engine lease");
        auto replacementBinding = target->attach(host.winId());
        target->beginPage(host.winId(), "page-3");
        check(target->placePreview(firstRect, host.winId(), "page-3", "replacement-host").isEmpty()
            && target->facts()["webViewVisuals"] == 1 && target->facts()["previewVisualsCreated"] == 1,
            "new Host binding reuses single existing Preview tree");
        const auto beforeResize = target->facts().value("acceptedThrough");
        check(engine.setHostedPresentationExtent(id, {480, 270})
            && backend.beginFrame() == Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable,
            "real allocation resize retires old immutable content snapshot");
        target->syncContent(engine.acquireHostedCompositionContent(id));
        check(!target->currentPresentation(host.winId(), "page-3")["lifecycleValid"].toBool()
            && target->facts()["ownedContentLeases"] == 0 && target->facts()["acceptedThrough"] == beforeResize,
            "invalid old snapshot fails closed while unpresented resized content cannot replace its authority");
        backend.renderPrepared({});
        target->syncContent(engine.acquireHostedCompositionContent(id));
        check(target->currentPresentation(host.winId(), "page-3")["reason"] == "awaiting-geometry"
            && target->placePreview(firstRect, host.winId(), "page-3", "post-resize").isEmpty(),
            "new successful Present restores content eligibility and fresh desired geometry restores acceptance");
        engine.shutdown();
        target->syncContent({});
        check(!target->currentPresentation(host.winId(), "page-3")["lifecycleValid"].toBool()
            && target->facts()["ownedContentLeases"] == 0 && !target->facts()["matteReady"].toBool(),
            "true Engine lifecycle invalidation closes clip and releases Host lease/backing");
        check(SUCCEEDED(target->clearPreviewContent()) && target->facts()["failures"] == 0,
            "explicit cleanup checks native submissions without fake success");
        target.reset();
        check(FAILED(replacementBinding->commit()), "retired Session owner fences surviving binding");
    } catch (const std::exception& error) { ++failures; std::cerr << error.what() << '\n'; }
    std::cout<<"Session composition ownership: "<<checks-failures<<'/'<<checks<<" PASS\n";return failures?1:0;
}
