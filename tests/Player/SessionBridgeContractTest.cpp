#include "SessionBridgeContract.h"
#include "LauncherBridgeContract.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <iostream>
#include <limits>
#include <vector>
#include <utility>

using namespace DesktopSceneEngine::Player::SessionBridge;

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    int checks = 0;
    int failures = 0;
    const auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
    };
    const auto incarnation = QUuid::createUuid();
    PageFence page{incarnation};
    page.prepare();
    const auto generation = page.generation();
    const auto uri = page.document();
    check(!generation.isEmpty() && uri == QString::fromLatin1(DocumentPrefix) + generation
        + "&session=" + incarnation.toString(QUuid::WithoutBraces) + "&presentation=model-c-production",
        "exact packaged modern Session document with native contexts");
    check(page.navigationStarting(uri) == NavigationDisposition::Allow, "initial trusted navigation accepted once");
    const QJsonObject request{{"kind", "command"}, {"version", Version}, {"requestId", "snapshot:1"},
        {"type", SnapshotCommand}, {"generation", generation},
        {"sessionIncarnation", incarnation.toString(QUuid::WithoutBraces)}, {"payload", QJsonObject{}}};
    const auto serialize = [&](QJsonObject value) {
        const auto type = value.value("type").toString();
        const QSet<QString> affected{"NewDocument", "OpenDocument", "SaveDocument", "SaveDocumentAs", "RequestClose", "ContinueDocumentDecision",
            "SelectPlaylist", "AddPlaylist", "RenamePlaylist", "RemovePlaylist", "MovePlaylist", "SetPlaylistEnabled", "SetProjectPolicy", "SetPlaylistPolicy", "SetPlaylistMaster",
            "SelectItem", "AddMedia", "RenameItem", "RemoveItem", "MoveItem", "SetItemEnabled", "SetImageDuration", "ApplyVideoRate", "SetItemGain", "SetItemPresentation",
            "PlayPlaylist", "PlayItem", "PlayPause", "Previous", "Next", "Seek", "ToggleOutputMute", "UseBgmTrack"};
        if (affected.contains(type) || type.startsWith("Bgm")) value.insert("audioEffects", QJsonObject{{"version", 1}, {"documentIncarnation", incarnation.toString(QUuid::WithoutBraces)}});
        return QJsonDocument(value).toJson(QJsonDocument::Compact);
    };
    const auto parse = [&](const QJsonObject& value) {
        return parseRequest(serialize(value), uri, uri, generation, incarnation);
    };
    const auto accepted = parse(request);
    check(accepted.request.has_value(), "exact snapshot command accepted");
    PageFence viewPeer{incarnation, Surface::BackgroundMusic};
    viewPeer.prepare();
    const std::vector<std::pair<const char*, QJsonObject>> viewCommands{
        {"SetCanvasBoundaryHighlight", {{"highlighted", true}}},
        {"SetCanvasBoundaryHighlight", {{"highlighted", false}}},
        {"SetDesktopBackground", {{"mode", "cover"}}},
        {"SetDesktopBackground", {{"mode", "reveal"}}},
        {"IdentifyBoundDisplay", {}}, {"DeployDesktop", {}}, {"UndeployDesktop", {}}};
    for (const auto& [name, payload] : viewCommands)
    {
        auto intent = request;
        intent.insert("type", name);
        intent.insert("payload", payload);
        const auto parsed = parse(intent).request;
        check(parsed.has_value(), "Main accepts exact View or desktop deployment command");
        if (QLatin1String(name) == QLatin1String(SetCanvasBoundaryHighlightCommand))
            check(parsed && parsed->canvasBoundaryHighlight == payload.value("highlighted").toBool(),
                "Highlight carries an explicit boolean, including false");
        else if (QLatin1String(name) == QLatin1String(SetDesktopBackgroundCommand))
            check(parsed && parsed->desktopBackground == (payload.value("mode") == QJsonValue{"cover"}
                ? DesktopSceneEngine::CanvasOutsideMode::Opaque : DesktopSceneEngine::CanvasOutsideMode::Transparent),
                "Background carries the native canvas outside mode");
        else if (QLatin1String(name) == QLatin1String(IdentifyBoundDisplayCommand))
            check(parsed && parsed->identifyBoundDisplay, "Identify uses the Session's native bound display");
        else
            check(parsed && parsed->desktopDeployment == (QLatin1String(name) == QLatin1String(DeployDesktopCommand)
                ? DesktopSceneEngine::Player::DesktopDeploymentIntent::Deploy : DesktopSceneEngine::Player::DesktopDeploymentIntent::Undeploy),
                "Deployment is an explicit native intent");
        auto foreignTarget = payload;
        foreignTarget.insert("displayId", "caller-supplied-display");
        intent.insert("payload", foreignTarget);
        check(!parse(intent).request, "View or deployment may not select a different display");
        intent.insert("payload", payload);
        intent.insert("generation", "retired-generation");
        check(!parse(intent).request, "View or deployment rejected from a retired Main page");
        intent.insert("generation", viewPeer.generation());
        check(!parseRequest(serialize(intent), viewPeer.document(), viewPeer.document(), viewPeer.generation(),
            incarnation, Surface::BackgroundMusic).request, "peer cannot invoke Main View or deployment");
    }
    const auto noticeId = QUuid::createUuid();
    auto noticeAck = request;
    noticeAck.insert("type", AcknowledgeSessionNoticeCommand);
    noticeAck.insert("payload", QJsonObject{{"noticeId", noticeId.toString(QUuid::WithoutBraces)}});
    const auto notice = parse(noticeAck).request;
    check(notice && notice->acknowledgeSessionNotice == noticeId,
        "Main accepts one exact notice acknowledgement without document authority");
    noticeAck.insert("generation", viewPeer.generation());
    check(!parse(noticeAck).request && !parseRequest(serialize(noticeAck), viewPeer.document(), viewPeer.document(),
        viewPeer.generation(), incarnation, Surface::BackgroundMusic).request,
        "notice acknowledgement rejects a retired generation and the BGM peer");
    noticeAck.insert("generation", generation);
    noticeAck.insert("payload", QJsonObject{{"noticeId", noticeId.toString(QUuid::WithoutBraces)}, {"alsoClose", true}});
    check(!parse(noticeAck).request, "notice acknowledgement cannot carry a close decision");
    auto malformedView = request;
    malformedView.insert("type", "SetCanvasBoundaryHighlight");
    malformedView.insert("payload", QJsonObject{{"highlighted", "true"}});
    check(!parse(malformedView).request, "Highlight does not coerce string or numeric values");
    malformedView.insert("type", "SetDesktopBackground");
    malformedView.insert("payload", QJsonObject{{"mode", "transparent"}});
    check(!parse(malformedView).request, "Background rejects renderer aliases and unknown modes");
    malformedView.insert("payload", QJsonObject{{"mode", "cover"}});
    auto duplicateView = serialize(malformedView);
    duplicateView.replace("\"mode\":\"cover\"", "\"mode\":\"cover\",\"mode\":\"reveal\"");
    check(!parseRequest(duplicateView, uri, uri, generation, incarnation).request,
        "Background rejects duplicate payload members");
    for (const auto* field : {"kind", "version", "requestId", "type", "generation", "sessionIncarnation", "payload"})
    {
        auto invalid = request;
        invalid.remove(QLatin1String(field));
        check(!parse(invalid).request, "missing envelope field rejected");
    }
    auto invalid = request;
    invalid.insert("extra", true);
    check(!parse(invalid).request, "extra field rejected");
    for (const auto* command : {"New", "Open", "Save", "Close", "CreateSession", "OpenSession", "Play", "Pause",
        "Seek", "SetOutputMute", "Deploy", "IdentifyDisplay", "CommitLanguage", "GetLauncherBootstrapSnapshot",
        "MainWindowSnapshotInvalidated", "GetMainWindowSnapshot "})
    {
        invalid = request;
        invalid.insert("type", command);
        check(!parse(invalid).request, "domain mutations and unknown commands unavailable at transport");
    }
    for (const auto& version : {QJsonValue{2}, QJsonValue{"1"}, QJsonValue{}, QJsonValue{true}})
    {
        invalid = request;
        invalid.insert("version", version);
        check(!parse(invalid).request, "non-version-one envelope rejected");
    }
    for (const auto& payload : {QJsonValue{QJsonObject{{"playlistId", QUuid::createUuid().toString()}}},
        QJsonValue{QJsonObject{{"refresh", true}}}, QJsonValue{QJsonArray{}}, QJsonValue{}, QJsonValue{"{}"}})
    {
        invalid = request;
        invalid.insert("payload", payload);
        check(!parse(invalid).request, "nonempty or nonobject payload rejected");
    }
    for (const auto& value : {QString{}, QString(129, QLatin1Char('x')), QStringLiteral("has space"), QString::fromUtf8("非ASCII")})
    {
        invalid = request;
        invalid.insert("requestId", value);
        check(!parse(invalid).request && parse(invalid).requestId.isEmpty(), "invalid request correlation is not echoed");
    }
    invalid = request;
    invalid.insert("requestId", QString(128, QLatin1Char('x')));
    check(parse(invalid).request.has_value(), "bounded printable requestId accepted");
    for (const auto& context : {QJsonValue{QUuid::createUuid().toString(QUuid::WithoutBraces)},
        QJsonValue{incarnation.toString(QUuid::WithBraces)}, QJsonValue{""}, QJsonValue{}, QJsonValue{1}})
    {
        invalid = request;
        invalid.insert("sessionIncarnation", context);
        check(!parse(invalid).request, "different or malformed Session context rejected");
    }
    invalid = request;
    invalid.insert("generation", "retired-generation");
    check(!parse(invalid).request, "old page generation rejected even with current source");
    for (const auto& source : {QStringLiteral("http://127.0.0.1:5173/?view=session"),
        QStringLiteral("https://session.dse.local.evil/index.html"),
        QStringLiteral("https://session.dse.local/other.html"), uri + "&extra=1", uri + "#fragment"})
    {
        check(!parseRequest(serialize(request), source, uri, generation, incarnation).request, "nonexact source denied");
        check(page.navigationStarting(source) == NavigationDisposition::Reject, "nonexact navigation denied");
    }
    for (const auto* duplicateKey : {"kind", "version", "requestId", "type", "generation", "sessionIncarnation", "payload"})
    {
        auto duplicate = serialize(request);
        duplicate.chop(1);
        duplicate += ",\"" + QByteArray{duplicateKey} + "\":"
            + QJsonDocument(QJsonArray{request.value(QLatin1String(duplicateKey))}).toJson(QJsonDocument::Compact).mid(1).chopped(1) + "}";
        check(!parseRequest(duplicate, uri, uri, generation, incarnation).request, "duplicate envelope member rejected");
    }
    for (const auto& json : {QByteArray{}, QByteArray{"[]"}, QByteArray{"{"}, QByteArray(16385, ' ')})
        check(!parseRequest(json, uri, uri, generation, incarnation).request, "invalid size/JSON shape rejected");
    check(!parseRequest(serialize(request), uri, uri, generation, {}).request, "missing native Session context fails closed");
    check(!parseRequest(serialize(request), uri, {}, generation, incarnation).request, "missing trusted document fails closed");
    if (accepted.request)
    {
        const QJsonObject snapshot{{"document", QJsonObject{{"authoredRevision", "9"}, {"cleanRevision", "7"}}},
            {"selectedPlaylistId", "selected"}, {"activePlaylistId", "active"}};
        const auto response = result(*accepted.request, snapshot);
        check(response["ok"] == true && response["requestId"] == request["requestId"]
            && response["type"] == request["type"] && response["generation"] == request["generation"]
            && response["sessionIncarnation"] == request["sessionIncarnation"] && response["payload"] == snapshot,
            "result retains correlation and native projection without local reinterpretation");
    }
    const auto failed = failure("snapshot:1", generation, incarnation, "snapshot-unavailable");
    check(failed["ok"] == false && failed["error"] == "snapshot-unavailable" && !failed.contains("payload")
        && failed["sessionIncarnation"] == request["sessionIncarnation"], "snapshot failure is explicit and context bound");
    const auto event = invalidated(generation, incarnation);
    check(event["kind"] == "event" && event["type"] == SnapshotInvalidated
        && event["generation"] == generation && event["sessionIncarnation"] == request["sessionIncarnation"]
        && event["payload"].toObject().isEmpty(), "minimal invalidation contains no fabricated snapshot or command result");
    for (int cycle = 0; cycle < 8; ++cycle)
    {
        const auto oldDocument = page.document();
        const auto oldGeneration = page.generation();
        auto oldRequest = request;
        oldRequest.insert("generation", oldGeneration);
        check(page.navigationStarting(oldDocument) == NavigationDisposition::RestartWithFreshGeneration
            && page.generation() != oldGeneration && page.sessionIncarnation() == incarnation,
            "reload changes page generation but preserves native Session identity");
        check(!parseRequest(serialize(oldRequest), oldDocument, page.document(), page.generation(), incarnation).request,
            "old page cannot request native snapshot after reload");
        auto freshRequest = request;
        freshRequest.insert("generation", page.generation());
        check(page.navigationStarting(page.document()) == NavigationDisposition::Allow
            && parseRequest(serialize(freshRequest), page.document(), page.document(), page.generation(), incarnation).request.has_value(),
            "replacement trusted page can request fresh snapshot without replaying writes");
    }
    page.close();
    page.prepare();
    check(page.closed() && page.generation().isEmpty() && page.document().isEmpty()
        && page.navigationStarting(uri) == NavigationDisposition::Reject, "closed page never revives");
    PageFence missingContext{QUuid{}};
    missingContext.prepare();
    check(missingContext.document().isEmpty()
        && missingContext.navigationStarting(uri) == NavigationDisposition::Reject, "null Session cannot load trusted document");

    // Only Main admits Preview geometry.
    PageFence previewPage{incarnation};
    previewPage.prepare();
    auto geometryRequest = request;
    geometryRequest.insert("generation", previewPage.generation());
    geometryRequest.insert("type", PreviewRectCommand);
    geometryRequest.insert("payload", QJsonObject{
        {"rect", QJsonObject{{"x", .4}, {"y", .4}, {"width", 10.4}, {"height", 10.4}}},
        {"viewport", QJsonObject{{"width", 100}, {"height", 80}}},
        {"rasterizationScale", 1.5}, {"visible", true}});
    const auto parseGeometry = [&](const QJsonObject& value) {
        return parseRequest(serialize(value), previewPage.document(), previewPage.document(),
            previewPage.generation(), incarnation);
    };
    const auto geometry = parseGeometry(geometryRequest);
    check(geometry.request && geometry.request->preview && geometry.request->type == PreviewRectCommand,
        "Main admits typed presentation geometry");
    const auto withOcclusion = [&](bool modal, const QJsonArray& rects) {
        auto value = geometryRequest;
        auto payload = value["payload"].toObject();
        payload.insert("occlusion", QJsonObject{{"modalActive", modal}, {"rects", rects}});
        value.insert("payload", payload);
        return value;
    };
    const auto modalGeometry = parseGeometry(withOcclusion(true, {}));
    check(modalGeometry.request.has_value(), "current modal fact is accepted by the existing Preview geometry capability");
    const auto floatingGeometry = parseGeometry(withOcclusion(false,
        {QJsonObject{{"x", 5}, {"y", 5}, {"width", 2}, {"height", 2}}}));
    check(floatingGeometry.request.has_value(), "current floating rects are accepted atomically with Preview geometry");
    const auto clearedGeometry = parseGeometry(withOcclusion(false, {}));
    check(clearedGeometry.request.has_value(), "overlay close submits fresh current geometry rather than an unconditional show command");
    using DesktopSceneEngine::Player::PreviewGeometry::resolve;
    if (modalGeometry.request && floatingGeometry.request && clearedGeometry.request)
    {
        const auto modal = *modalGeometry.request->preview;
        auto floating = *floatingGeometry.request->preview;
        auto cleared = *clearedGeometry.request->preview;
        const auto clearBounds = resolve(cleared, {150, 120}, 1.5).physicalRect;
        check(!clearBounds.isEmpty() && resolve(modal, {150, 120}, 1.5).physicalRect == clearBounds,
            "a composed modal leaves Preview placement intact");
        check(resolve(floating, {150, 120}, 1.5).physicalRect == clearBounds,
            "a composed menu over Preview leaves the same physical placement");
        floating.floatingRects = {{30, 30, 10, 10}};
        check(resolve(floating, {150, 120}, 1.5).physicalRect == clearBounds,
            "nonintersecting floating geometry also leaves Preview placement intact");
        check(resolve(cleared, {}, 1.5).physicalRect.isEmpty(), "zero native viewport has no admissible new geometry");
        check(resolve(cleared, {150, 120}, 1).physicalRect.isEmpty(), "stale controller scale has no admissible new geometry");
        cleared.visible = false;
        check(resolve(cleared, {150, 120}, 1.5).physicalRect.isEmpty(), "hidden intent is not admissible");
        cleared.visible = true; cleared.rect.setWidth(0);
        check(resolve(cleared, {150, 120}, 1.5).physicalRect.isEmpty(), "zero Preview rectangle has no admissible geometry");
        floating.floatingRects = {{0, 0, -1, 2}};
        check(!resolve(floating, {150, 120}, 1.5).error.isEmpty(), "invalid floating geometry fails closed before placement");
    }
    for (const auto& bad : {QJsonValue{}, QJsonValue{true}, QJsonValue{QJsonArray{}},
            QJsonValue{QJsonObject{{"modalActive", true}}},
            QJsonValue{QJsonObject{{"modalActive", "true"}, {"rects", QJsonArray{}}}},
            QJsonValue{QJsonObject{{"modalActive", false}, {"rects", QJsonArray{}}, {"show", true}}}})
    {
        auto value = withOcclusion(false, {}); auto payload = value["payload"].toObject();
        payload.insert("occlusion", bad); value.insert("payload", payload);
        check(!parseGeometry(value).request, "malformed occlusion cannot request Preview restoration");
    }
    for (const auto& bad : {QJsonValue{-1}, QJsonValue{1000001}, QJsonValue{"3"}, QJsonValue{true}})
        check(!parseGeometry(withOcclusion(false, {QJsonObject{{"x", 0}, {"y", 0}, {"width", bad}, {"height", 2}}})).request,
            "invalid occluder extents are rejected with the existing numeric bounds");
    QJsonArray tooManyOverlays;
    for (int i = 0; i < 33; ++i) tooManyOverlays.append(QJsonObject{{"x", i}, {"y", 0}, {"width", 1}, {"height", 1}});
    check(!parseGeometry(withOcclusion(false, tooManyOverlays)).request, "occlusion payload has a finite32-surface bound");
    auto duplicateOcclusion = serialize(withOcclusion(true, {}));
    duplicateOcclusion.replace("\"modalActive\":true", "\"modalActive\":true,\"modalActive\":false");
    check(!parseRequest(duplicateOcclusion, previewPage.document(), previewPage.document(), previewPage.generation(), incarnation).request,
        "duplicate modal facts cannot collapse into a restore request");
    auto duplicateOverlayRect = serialize(withOcclusion(false, {QJsonObject{{"x", 5}, {"y", 5}, {"width", 2}, {"height", 2}}}));
    duplicateOverlayRect.replace("\"x\":5", "\"x\":5,\"x\":5");
    check(!parseRequest(duplicateOverlayRect, previewPage.document(), previewPage.document(), previewPage.generation(), incarnation).request,
        "duplicate floating rectangle members are rejected before intersection");
    check(!parseRequest(serialize(geometryRequest), previewPage.document(), previewPage.document(),
        previewPage.generation(), incarnation, Surface::BackgroundMusic).request, "BGM peer cannot acquire Preview capability from a message");
    auto readInPreview = request;
    readInPreview.insert("generation", previewPage.generation());
    check(parseGeometry(readInPreview).request.has_value(), "B5a retains native snapshot read capability");
    for (const auto* field : {"rect", "viewport", "rasterizationScale", "visible"})
    {
        auto bad = geometryRequest;
        auto payload = bad["payload"].toObject();
        payload.remove(QLatin1String(field));
        bad.insert("payload", payload);
        check(!parseGeometry(bad).request, "missing geometry payload member rejected");
    }
    for (const auto* group : {"rect", "viewport"})
    {
        auto bad = geometryRequest;
        auto payload = bad["payload"].toObject();
        auto nested = payload[QLatin1String(group)].toObject();
        nested.insert("extra", 1);
        payload.insert(QLatin1String(group), nested);
        bad.insert("payload", payload);
        check(!parseGeometry(bad).request, "extra nested geometry member rejected");
        nested.remove("extra");
        nested.insert("width", "100");
        payload.insert(QLatin1String(group), nested);
        bad.insert("payload", payload);
        check(!parseGeometry(bad).request, "numeric strings in geometry rejected");
    }
    for (const auto& value : {QJsonValue{0}, QJsonValue{-1}, QJsonValue{9}, QJsonValue{"1.5"}, QJsonValue{true}})
    {
        auto bad = geometryRequest;
        auto payload = bad["payload"].toObject();
        payload.insert("rasterizationScale", value);
        bad.insert("payload", payload);
        check(!parseGeometry(bad).request, "invalid rasterization-scale observation rejected");
    }
    auto duplicateGeometry = serialize(geometryRequest);
    duplicateGeometry.replace("\"x\":0.4", "\"x\":0.4,\"x\":0.4");
    check(!parseRequest(duplicateGeometry, previewPage.document(), previewPage.document(),
        previewPage.generation(), incarnation).request, "duplicate nested rect member rejected");
    if (geometry.request && geometry.request->preview)
    {
        using DesktopSceneEngine::Player::PreviewGeometry::resolve;
        auto intent = *geometry.request->preview;
        auto placement = resolve(intent, {150, 120}, 1.5);
        check(placement.error.isEmpty() && !placement.physicalRect.isEmpty() && placement.physicalRect == QRect{1, 1, 15, 15},
            "physical edges round before extents and scale is applied once");
        check(resolve(intent, {150, 120}, 1).error == "preview-scale-mismatch", "observed DPR never overrides controller scale");
        check(resolve(intent, {200, 120}, 1.5).error == "preview-viewport-mismatch", "stale CSS viewport rejected after resize");
        intent.visible = false;
        check(resolve(intent, {150, 120}, 1.5).physicalRect.isEmpty(), "explicit hidden intent has no placement");
        intent.visible = true;
        intent.rect.setWidth(0);
        check(resolve(intent, {150, 120}, 1.5).physicalRect.isEmpty(), "zero width has no admissible placement");
        intent.rect = {-10, 20, 30, 100};
        intent.observedScale = 1;
        placement = resolve(intent, {100, 80}, 1);
        check(!placement.physicalRect.isEmpty() && placement.physicalRect == QRect{0, 20, 20, 60}, "native viewport clips physical bounds");
        intent.rect = {100, 80, 30, 20};
        check(resolve(intent, {100, 80}, 1).physicalRect.isEmpty(), "fully clipped rectangle has no admissible placement");
        check(resolve(intent, {}, 1).physicalRect.isEmpty(), "zero native viewport cannot adopt retained page geometry");
        intent.rect.setX(std::numeric_limits<double>::infinity());
        check(resolve(intent, {100, 80}, 1).error == "invalid-preview-geometry", "nonfinite geometry fails before integer conversion");
        const auto response = result(*geometry.request, {{"status", "visible"}});
        check(response["type"] == PreviewRectCommand && response["payload"].toObject()["status"] == "visible",
            "geometry result preserves typed request correlation");
    }
    const auto geometryEvent = previewGeometryInvalidated(previewPage.generation(), incarnation);
    check(geometryEvent["type"] == PreviewGeometryInvalidated && geometryEvent["payload"].toObject().isEmpty()
        && geometryEvent["sessionIncarnation"] == incarnation.toString(QUuid::WithoutBraces),
        "native geometry invalidation requests fresh presentation without state replay");
    const auto staleDocument = previewPage.document();
    check(previewPage.navigationStarting(staleDocument) == NavigationDisposition::Allow
        && previewPage.navigationStarting(staleDocument) == NavigationDisposition::RestartWithFreshGeneration,
        "Preview page reload rotates the generation through the Main fence");
    check(!parseRequest(serialize(geometryRequest), staleDocument, previewPage.document(), previewPage.generation(), incarnation).request,
        "old-page geometry rejected after generation rotation");
    check(!parseRequest(serialize(withOcclusion(false, {})), staleDocument, previewPage.document(), previewPage.generation(), incarnation).request,
        "old-page overlay cleanup cannot restore Preview after generation rotation");
    geometryRequest.insert("generation", previewPage.generation());
    geometryRequest.insert("sessionIncarnation", QUuid::createUuid().toString(QUuid::WithoutBraces));
    check(parseGeometry(geometryRequest).error == "wrong-session-context", "geometry cannot place another native Session target");

    // No document state or pending operation is stored in the page fence.
    PageFence documentPage{incarnation};
    documentPage.prepare();
    const auto parseDocument = [&](const QJsonObject& value) {
        return parseRequest(serialize(value), documentPage.document(), documentPage.document(),
            documentPage.generation(), incarnation);
    };
    auto documentRequest = request;
    documentRequest.insert("generation", documentPage.generation());
    for (const auto* type : {NewDocumentCommand, OpenDocumentCommand, SaveDocumentCommand, SaveDocumentAsCommand, RequestCloseCommand})
    {
        documentRequest.insert("type", type);
        const auto command = parseDocument(documentRequest);
        check(command.request && command.request->documentAction && command.request->type == type,
            "typed empty-payload document intent admitted");
        check(!parseRequest(serialize(documentRequest), documentPage.document(), documentPage.document(),
            documentPage.generation(), incarnation, Surface::BackgroundMusic).request, "BGM peer cannot acquire document writes");
        auto extra = documentRequest;
        extra.insert("payload", QJsonObject{{"path", "C:/untrusted-project.json"}});
        check(!parseDocument(extra).request, "page cannot choose a filesystem path or own a candidate");
    }
    const auto operation = QUuid::createUuid();
    documentRequest.insert("type", ContinueDocumentCommand);
    for (const auto* choice : {"save", "discard", "cancel"})
    {
        documentRequest.insert("payload", QJsonObject{{"token", operation.toString(QUuid::WithoutBraces)}, {"choice", choice}});
        const auto continuation = parseDocument(documentRequest);
        check(continuation.request && continuation.request->documentChoice && continuation.request->documentToken == operation,
            "exact native operation identity and typed decision parsed");
    }
    for (const auto& token : {QString{}, operation.toString(), QStringLiteral("00000000-0000-0000-0000-000000000000"), operation.toString(QUuid::WithoutBraces).toUpper()})
    {
        auto bad = documentRequest;
        bad.insert("payload", QJsonObject{{"token", token}, {"choice", "save"}});
        check(!parseDocument(bad).request, "noncanonical/empty operation token rejected");
    }
    auto duplicateDecision = serialize(documentRequest);
    duplicateDecision.replace("\"choice\":\"cancel\"", "\"choice\":\"cancel\",\"choice\":\"cancel\"");
    check(!parseRequest(duplicateDecision, documentPage.document(), documentPage.document(), documentPage.generation(),
        incarnation).request, "duplicate continuation payload keys rejected");
    for (const auto* type : {"AddMedia", "SetPlaylistMaster", "Play", "Seek", "SetOutputMute", "Deploy", "CommitLanguage"})
    {
        auto bad = documentRequest;
        bad.insert("type", type);
        bad.insert("payload", QJsonObject{});
        check(!parseDocument(bad).request, "unrelated commands require recognized names and valid payloads");
    }
    const auto continuation = parseDocument(documentRequest);
    if (continuation.request)
    {
        const auto ack = documentResult(*continuation.request,
            {DesktopSceneEngine::Player::DocumentCommandStatus::Accepted, operation});
        check(ack["ok"] == true && ack["type"] == ContinueDocumentCommand
            && ack["payload"].toObject() == QJsonObject{{"status", "accepted"}, {"token", operation.toString(QUuid::WithoutBraces)}},
            "document acknowledgement reports admission, never Saved or completed");
    }
    const auto oldDocument = documentPage.document();
    check(documentPage.navigationStarting(oldDocument) == NavigationDisposition::Allow
        && documentPage.navigationStarting(oldDocument) == NavigationDisposition::RestartWithFreshGeneration,
        "pending document intent survives presentation generation rotation");
    check(!parseRequest(serialize(documentRequest), oldDocument, documentPage.document(), documentPage.generation(),
        incarnation).request, "old page cannot continue native pending decision after reload");

    // Authoring commands use native stable IDs.
    PageFence authoredPage{incarnation};
    authoredPage.prepare();
    auto authoredRequest = request;
    authoredRequest.insert("generation", authoredPage.generation());
    authoredRequest.insert("type", "SelectPlaylist");
    authoredRequest.insert("payload", QJsonObject{{"playlistId", operation.toString(QUuid::WithoutBraces)}});
    const auto selected = parseRequest(serialize(authoredRequest), authoredPage.document(), authoredPage.document(),
        authoredPage.generation(), incarnation);
    check(selected.request && selected.request->authored
        && selected.request->authored->playlistId.value == operation, "authoring parses exact stable-ID selection intent");
    authoredRequest.insert("type", "SetPlaylistMaster");
    authoredRequest.insert("payload", QJsonObject{{"playlistId", operation.toString(QUuid::WithoutBraces)}, {"value", 0.7}});
    const auto master = parseRequest(serialize(authoredRequest), authoredPage.document(), authoredPage.document(),
        authoredPage.generation(), incarnation);
    check(master.request && master.request->authored && master.request->authored->value == 0.7,
        "authored Master is distinct from runtime Output Mute");
    const auto parseAuthored = [&](const char* type, const QJsonObject& payload) {
        auto value = authoredRequest; value.insert("type", type); value.insert("payload", payload);
        return parseRequest(serialize(value), authoredPage.document(), authoredPage.document(),
            authoredPage.generation(), incarnation);
    };
    const auto playlistId = operation.toString(QUuid::WithoutBraces), itemId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject playlistTarget{{"playlistId", playlistId}}, itemTarget{{"playlistId", playlistId}, {"itemId", itemId}};
    const auto with = [](QJsonObject target, const char* field, const QJsonValue& value) { target.insert(QLatin1String(field), value); return target; };
    const QVector<std::pair<const char*, QJsonObject>> commands{
        {"AddPlaylist", {}}, {"SelectPlaylist", playlistTarget},
        {"RenamePlaylist", with(with(playlistTarget, "alias", "duplicate"), "expectedAlias", "prior")},
        {"RemovePlaylist", with(playlistTarget, "expectedAlias", "duplicate")},
        {"MovePlaylist", with(playlistTarget, "direction", -1)}, {"SetPlaylistEnabled", with(playlistTarget, "enabled", false)},
        {"SetProjectPolicy", {{"mode", "random"}}}, {"SetPlaylistPolicy", with(playlistTarget, "mode", "repeat-current")},
        {"SetPlaylistMaster", with(playlistTarget, "value", 0.0)}, {"SelectItem", itemTarget}, {"AddMedia", playlistTarget},
        {"RenameItem", with(with(itemTarget, "alias", "duplicate"), "expectedAlias", "prior")},
        {"RemoveItem", with(itemTarget, "expectedAlias", "duplicate")}, {"MoveItem", with(itemTarget, "direction", 1)},
        {"SetItemEnabled", with(itemTarget, "enabled", true)}, {"SetImageDuration", with(itemTarget, "value", 0.1)},
        {"ApplyVideoRate", with(itemTarget, "value", 1.25)}, {"SetItemGain", with(itemTarget, "value", 0.5)},
        {"SetItemPresentation", with(itemTarget, "patch", QJsonObject{{"positionX", 0.25}})}};
    for (const auto& [type, payload] : commands)
    {
        const auto parsed = parseAuthored(type, payload);
        check(parsed.request && parsed.request->authored && parsed.request->type == QLatin1String(type), "finite authored command parses exact typed payload");
        auto value = authoredRequest; value.insert("type", type); value.insert("payload", payload);
        check(!parseRequest(serialize(value), authoredPage.document(), authoredPage.document(), authoredPage.generation(),
            incarnation, Surface::BackgroundMusic).request, "BGM peer cannot acquire Main authored writes");
        check(!parseAuthored(type, with(payload, "rowIndex", 0)).request, "no index-based or unrecognized authored target fields");
    }
    for (const auto& value : {QJsonValue{playlistId.toUpper()}, QJsonValue{operation.toString()}, QJsonValue{"alias"}, QJsonValue{0}, QJsonValue{}})
        check(!parseAuthored("SelectPlaylist", {{"playlistId", value}}).request, "authored identity must be non-null canonical native UUID");
    check(!parseAuthored("RenameItem", with(itemTarget, "alias", "x")).request, "rename draft requires captured target alias precondition");
    check(!parseAuthored("RemoveItem", with(itemTarget, "expectedAlias", true)).request, "delete precondition cannot be coerced");
    check(!parseAuthored("MoveItem", with(itemTarget, "direction", 0)).request
        && !parseAuthored("MoveItem", with(itemTarget, "direction", 1.5)).request, "adjacent move accepts only minus/plus one");
    check(!parseAuthored("SetPlaylistMaster", with(playlistTarget, "value", 1.01)).request
        && !parseAuthored("SetItemGain", with(itemTarget, "value", -0.01)).request, "public gain/master range is exact");
    check(!parseAuthored("SetItemPresentation", with(itemTarget, "patch", QJsonObject{{"crop", QJsonObject{}}})).request
        && !parseAuthored("SetItemPresentation", with(itemTarget, "patch", QJsonObject{})).request,
        "presentation intent is a nonempty exposed-field patch, never full entity replacement");
    const auto patch = parseAuthored("SetItemPresentation", with(itemTarget, "patch", QJsonObject{
        {"mode", "custom"}, {"customAspect", QJsonObject{{"numerator", 16}, {"denominator", 9}}}}));
    check(patch.request && patch.request->authored && patch.request->authored->presentation.customAspect
        && !patch.request->authored->presentation.positionX && !patch.request->authored->presentation.positionY
        && !patch.request->authored->presentation.scale, "aspect patch leaves untouched axes/scale absent");
    auto duplicatedPatch = authoredRequest;
    duplicatedPatch.insert("type", "SetItemPresentation");
    duplicatedPatch.insert("payload", with(itemTarget, "patch", QJsonObject{{"positionX", 0.25}}));
    auto duplicatedBytes = serialize(duplicatedPatch); duplicatedBytes.replace("\"positionX\":0.25", "\"positionX\":0.25,\"positionX\":0.5");
    check(!parseRequest(duplicatedBytes, authoredPage.document(), authoredPage.document(), authoredPage.generation(),
        incarnation).request, "duplicate nested presentation patch key rejected before mutation");
    for (const auto* type : {"PlayItem", "PlayPlaylist", "Play", "Pause", "Seek", "SetOutputMute", "Deploy", "Hide", "ConfigureBGM"})
        check(!parseAuthored(type, {}).request, "unsupported or incomplete command envelopes are rejected");

    DesktopSceneEngine::Player::AuthoredResult partial;
    partial.status = DesktopSceneEngine::Player::AuthoredStatus::Applied;
    partial.authored = DesktopSceneEngine::Player::AuthoredOutcome::Committed;
    partial.runtime = DesktopSceneEngine::Player::RuntimeOutcome::Warning;
    partial.positionRestore = DesktopSceneEngine::Player::PositionRestoreOutcome::Failed;
    partial.authoredRevision = std::numeric_limits<std::uint64_t>::max(); partial.dirty = true;
    partial.playlistId = DesktopSceneEngine::Player::PlaylistId{operation};
    partial.warnings.append(DesktopSceneEngine::Player::AuthoredWarning{"runtime-sync", "committed edit retained"});
    partial.sources.append({"duplicate.png", DesktopSceneEngine::Player::ItemId{QUuid{itemId}}, DesktopSceneEngine::Player::MediaSourceOutcome::Committed,
        DesktopSceneEngine::Player::RuntimeOutcome::Warning, {DesktopSceneEngine::Player::AuthoredWarning{"runtime-sync", "retained"}}});
    partial.sources.append({"unsupported.txt", {}, DesktopSceneEngine::Player::MediaSourceOutcome::Unsupported,
        DesktopSceneEngine::Player::RuntimeOutcome::NotApplicable, {}});
    const auto serializedPartial = authoredPayload(partial);
    check(serializedPartial["authored"] == "committed" && serializedPartial["runtime"] == "warning"
        && serializedPartial["positionRestore"] == "failed" && serializedPartial["dirty"] == true
        && serializedPartial["revision"] == "18446744073709551615", "partial runtime failure never rewrites committed authored truth or uint64 revision");
    check(serializedPartial["sources"].toArray().size() == 2
        && serializedPartial["sources"].toArray()[0].toObject()["authored"] == "committed"
        && serializedPartial["sources"].toArray()[1].toObject()["authored"] == "unsupported",
        "ordered source outcomes preserve mixed batch rather than one success boolean");

    // Runtime commands belong to the Main production surface.
    PageFence runtimePage{incarnation};
    runtimePage.prepare();
    auto runtimeEnvelope = request;
    runtimeEnvelope.insert("generation", runtimePage.generation());
    for (const auto* type : {"GetRuntimePlaybackSample", "PlayPause", "Previous", "Next", "ToggleOutputMute"})
    {
        runtimeEnvelope.insert("type", type);
        check(parseRequest(serialize(runtimeEnvelope), runtimePage.document(), runtimePage.document(),
            runtimePage.generation(), incarnation).request.has_value(),
            "Main admits typed runtime commands");
    }

    const auto parseRuntime = [&](const char* type, QJsonObject payload, Surface surface = Surface::Main) {
        auto value = runtimeEnvelope;
        value.insert("type", type); value.insert("payload", payload);
        return parseRequest(serialize(value), runtimePage.document(), runtimePage.document(),
            runtimePage.generation(), incarnation, surface);
    };
    const auto negotiated = parseRuntime(NegotiateAudioEffectsCommand, {{"version", 1}});
    check(negotiated.request && negotiated.request->negotiateAudioEffects, "CP1 exact audioEffects v1 negotiation parses before mutation");
    check(parseRuntime(AudioEffectsCommand, {}).request.has_value(), "CP1 bounded native effect read is a separate command");
    for (const auto& bad : {QJsonValue{0}, QJsonValue{2}, QJsonValue{"1"}, QJsonValue{true}, QJsonValue{}})
        check(!parseRuntime(NegotiateAudioEffectsCommand, {{"version", bad}}).request, "CP1 capability version rejects coercion/unknown versions");
    check(!parseRuntime(NegotiateAudioEffectsCommand, {{"version", 1}, {"apply", true}}).request
        && !parseRuntime(AudioEffectsCommand, {{"operation", "guess"}}).request,
        "CP1 negotiation/read cannot carry hidden mutation or unbounded receipt query");
    partial.runtime = DesktopSceneEngine::Player::RuntimeOutcome::Pending;
    partial.runtimeOnApplied = DesktopSceneEngine::Player::RuntimeOutcome::Stopped;
    partial.audioEffect = QJsonObject{{"sequence", "18446744073709551615"}, {"phase", "pending"}};
    const auto pendingAudio = authoredPayload(partial);
    check(pendingAudio["authored"] == "committed" && pendingAudio["runtime"] == "pending"
        && pendingAudio["runtimeOnApplied"] == "stopped" && pendingAudio["dirty"] == true
        && pendingAudio["audioEffect"].toObject()["sequence"] == "18446744073709551615",
        "CP1 pending audio preserves committed authored state and exact completion identity");
    const auto binding = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject seekTarget{{"bindingToken", binding}, {"itemId", itemId}, {"duration", 1.25}, {"value", 9999}};
    const QJsonObject hoverTarget{{"bindingToken", binding}, {"itemId", itemId}, {"serial", 1}, {"time", 0.25}};
    const QJsonObject hoverPresentation{{"bindingToken", binding}, {"itemId", itemId}, {"serial", 1}, {"time", 0.25},
        {"rect", QJsonObject{{"x", 410}, {"y", 468.5}, {"width", 216}, {"height", 152.5}}},
        {"lane", QJsonObject{{"x", 235}, {"y", 612}, {"width", 643}, {"height", 35}}},
        {"imageSize", QJsonObject{{"width", 200}, {"height", 112.5}}},
        {"viewport", QJsonObject{{"width", 1183}, {"height", 711}}},
        {"rasterizationScale", 1.5}, {"timeLabel", "00:00"}};
    check(parseRuntime("PresentTimelineHover", hoverPresentation).request.has_value(),
        "native hover presentation command admits canonical card independently of remaining DOM height");
    const auto presented = parseRuntime("PresentTimelineHover", hoverPresentation);
    check(presented.request && presented.request->hoverPresentation
        && presented.request->hoverPresentation->rect == QRectF{410, 468.5, 216, 152.5}
        && presented.request->hoverPresentation->imageSize == QSizeF{200, 112.5}
        && presented.request->hoverPresentation->timeLabel == QStringLiteral("00:00")
        && presented.request->hoverPresentation->itemId == QUuid{itemId}
        && presented.request->hoverPresentation->bindingToken == binding,
        "presentation preserves canonical card/image distinction, CSS geometry and stable native target");
    check(!parseRuntime("PresentTimelineHover", hoverPresentation, Surface::BackgroundMusic).request,
        "BGM peer cannot present the Main hover card");
    for (const auto* field : {"bindingToken", "itemId", "serial", "time", "rect", "lane", "imageSize", "viewport", "rasterizationScale", "timeLabel"})
    {
        auto missing = hoverPresentation; missing.remove(QLatin1String(field));
        check(!parseRuntime("PresentTimelineHover", missing).request, "presentation requires every exact payload field");
    }
    for (const auto* field : {"visible", "rawPixels", "assetUrl", "extra"})
        check(!parseRuntime("PresentTimelineHover", with(hoverPresentation, field, true)).request,
            "presentation accepts no old DOM assets, hidden authority or extra payload keys");
    for (const auto& value : {QJsonValue{0}, QJsonValue{-1}, QJsonValue{1.5}, QJsonValue{"1"}, QJsonValue{2147483648.0}})
        check(!parseRuntime("PresentTimelineHover", with(hoverPresentation, "serial", value)).request,
            "presentation serial is positive bounded native integer");
    for (const auto& value : {QJsonValue{"0:00"}, QJsonValue{"00:60"}, QJsonValue{"60:00"}, QJsonValue{"00:00.25"}, QJsonValue{QString(33, '0')}, QJsonValue{0.25}})
        check(!parseRuntime("PresentTimelineHover", with(hoverPresentation, "timeLabel", value)).request,
            "timestamp requires bounded accepted time format, never raw float");
    check(parseRuntime("PresentTimelineHover", with(hoverPresentation, "timeLabel", "01:01:01")).request.has_value(),
        "hour time formatting remains accepted");
    check(!parseRuntime("PresentTimelineHover", with(hoverPresentation, "imageSize", QJsonObject{{"width", 216}, {"height", 121.5}})).request,
        "whole-card width cannot be confused with canonical image width");
    check(!parseRuntime("PresentTimelineHover", with(hoverPresentation, "imageSize", QJsonObject{{"width", 104.875}, {"height", 59}})).request,
        "remaining DOM height cannot compress canonical image dimensions");
    check(!parseRuntime("PresentTimelineHover", with(hoverPresentation, "rect", QJsonObject{{"x", 410}, {"y", 468.5}, {"width", 216}, {"height", 121.5}})).request,
        "card height includes footer and padding independently of image aspect");
    check(!parseRuntime("PresentTimelineHover", with(hoverPresentation, "lane", QJsonObject{{"x", 235}, {"y", 612}, {"width", 200}, {"height", 35}})).request,
        "physically narrow central lane rejects rather than scaling or choosing side slots");
    auto duplicatePresentation = runtimeEnvelope;
    duplicatePresentation.insert("type", "PresentTimelineHover"); duplicatePresentation.insert("payload", hoverPresentation);
    auto duplicatePresentationBytes = serialize(duplicatePresentation);
    duplicatePresentationBytes.replace("\"width\":200", "\"width\":200,\"width\":199");
    check(!parseRequest(duplicatePresentationBytes, runtimePage.document(), runtimePage.document(), runtimePage.generation(),
        incarnation).request, "duplicate nested presentation dimensions cannot bypass canonical validation");
    auto oldPresentation = duplicatePresentation;
    oldPresentation.insert("generation", QUuid::createUuid().toString(QUuid::WithoutBraces));
    check(!parseRequest(serialize(oldPresentation), runtimePage.document(), runtimePage.document(), runtimePage.generation(),
        incarnation).request, "old-page presentation request cannot control current popup");

    using namespace DesktopSceneEngine::Player::TimelineHoverPresentation;
    if (!presented.request || !presented.request->hoverPresentation) return 1; // Fail, never dereference an absent parsed intent.
    auto hoverGeometry = *presented.request->hoverPresentation;
    const QSize rawViewport{1775, 1067};
    const QPoint screenOrigin{-1920, 120};
    const auto physical = resolve(hoverGeometry, rawViewport, 1.5, screenOrigin);
    check(!physical.suspended && physical.error.isEmpty() && physical.screenRect == QRect{-1305, 823, 324, 229},
        "150 percent geometry applies controller scale once then adds physical WebView screen origin");
    check(physical.imageRect == QRect{12, 12, 300, 169} && physical.timeRect == QRect{12, 181, 300, 36}
        && physical.fontPixels == 15 && physical.borderPixels == 2,
        "canonical image, time footer, font and border become relative physical paint geometry");
    int previousScreenX = std::numeric_limits<int>::min();
    for (const auto fraction : {.2, .3, .4, .5, .6, .7, .8})
    {
        const double pointer = 254.28125 + (533.59375 - 1) * fraction;
        hoverGeometry.rect.moveLeft(pointer - 108);
        const auto current = resolve(hoverGeometry, rawViewport, 1.5, screenOrigin);
        check(!current.suspended && current.error.isEmpty() && current.screenRect.x() > previousScreenX
            && current.screenRect.width() == 324 && current.screenRect.height() == 229,
            "20 through80 percent preserve one canonical size and continuous increasing screen X");
        check(current.screenRect.x() == screenOrigin.x() + static_cast<int>(std::lround(hoverGeometry.rect.x() * 1.5)),
            "native X never applies a second DPI factor or treats CSS viewport X as local popup X");
        previousScreenX = current.screenRect.x();
    }
    for (const auto edge : {hoverGeometry.lane.left() + 8, hoverGeometry.lane.right() - 8 - 216})
    {
        hoverGeometry.rect.moveLeft(edge);
        const auto current = resolve(hoverGeometry, rawViewport, 1.5, screenOrigin);
        check(!current.suspended && current.screenRect.width() == 324 && current.screenRect.height() == 229,
            "left/right edge placement preserves canonical dimensions");
    }
    hoverGeometry.rect.moveLeft(410);
    for (const auto scale : {1.0, 2.0})
    {
        hoverGeometry.observedScale = scale;
        const QSize viewport{static_cast<int>(std::lround(1183 * scale)), static_cast<int>(std::lround(711 * scale))};
        const auto current = resolve(hoverGeometry, viewport, scale, screenOrigin);
        check(!current.suspended && current.screenRect.width() == static_cast<int>(216 * scale)
            && current.screenRect.x() == screenOrigin.x() + static_cast<int>(410 * scale),
            "100 and200 percent preserve logical canonical size with one DPI conversion");
    }
    hoverGeometry.observedScale = 1.5;
    auto square = hoverGeometry;
    square.imageSize = QSizeF{140, 140}; square.rect = QRectF{410, 441, 156, 180};
    check(resolve(square, rawViewport, 1.5, screenOrigin).screenRect.size() == QSize{234, 270},
        "square Canvas keeps canonical200x140 envelope without incidental Preview fit");
    auto portrait = hoverGeometry;
    portrait.imageSize = QSizeF{78.75, 140}; portrait.rect = QRectF{410, 441, 112, 180};
    const auto portraitPlacement = resolve(portrait, rawViewport, 1.5, screenOrigin);
    check(!portraitPlacement.suspended && portraitPlacement.screenRect.size() == QSize{168, 270}
        && portraitPlacement.imageRect.x() == 25 && portraitPlacement.timeRect.width() == 144,
        "narrow Canvas image is centered inside accepted minimum112px card and full-width footer");
    check(resolve(hoverGeometry, rawViewport, 1, screenOrigin).error == QStringLiteral("hover-scale-mismatch"),
        "observed CSS scale must match current native controller");
    check(resolve(hoverGeometry, QSize{1778, 1067}, 1.5, screenOrigin).error == QStringLiteral("hover-viewport-mismatch"),
        "stale viewport geometry cannot position current screen popup");
    check(resolve(hoverGeometry, {}, 1.5, screenOrigin).suspended
        && resolve(hoverGeometry, {}, 1.5, screenOrigin).error.isEmpty(), "zero native viewport suspends without inventing dimensions");
    hoverGeometry.rect.moveLeft(hoverGeometry.lane.left());
    check(resolve(hoverGeometry, rawViewport, 1.5, screenOrigin).error == QStringLiteral("hover-card-does-not-fit"),
        "native validates central edge inset without shrinking canonical card");
    hoverGeometry.rect.moveLeft(410);
    check(resolve(hoverGeometry, rawViewport, 1.5, QPoint{std::numeric_limits<int>::max(), 0}).error
        == QStringLiteral("hover-screen-coordinate-overflow"), "physical screen origin cannot overflow SetWindowPos coordinates");
    hoverGeometry.rect.setWidth(std::numeric_limits<double>::quiet_NaN());
    check(!resolve(hoverGeometry, rawViewport, 1.5, screenOrigin).error.isEmpty(), "nonfinite geometry fails before native positioning");
    for (const auto& [type, payload] : std::vector<std::pair<const char*, QJsonObject>>{
        {"PlayPlaylist", playlistTarget}, {"PlayItem", itemTarget}, {"Seek", seekTarget},
        {"RequestTimelineHover", hoverTarget}, {"CancelTimelineHover", {{"serial", 2}}}})
    {
        check(parseRuntime(type, payload).request.has_value(), "Main admits a bounded stable-ID runtime intent");
        check(!parseRuntime(type, payload, Surface::BackgroundMusic).request, "BGM peer cannot issue a Main runtime command");
        check(!parseRuntime(type, with(payload, "expectedRevision", "1")).request, "runtime commands do not accept irrelevant authored revision fences");
        check(!parseRuntime(type, with(payload, "rowIndex", 0)).request, "runtime commands reject index targeting/extra payload fields");
    }
    for (const auto& value : {QJsonValue{-1}, QJsonValue{10001}, QJsonValue{9999.5}, QJsonValue{"10000"}, QJsonValue{true}, QJsonValue{}})
        check(!parseRuntime("Seek", with(seekTarget, "value", value)).request, "seek integer normalized range cannot be coerced or exceed bounds");
    check(parseRuntime("Seek", with(seekTarget, "value", 10000)).request.has_value(), "exact endpoint preserved as deterministic native Next intent");
    for (const auto& value : {QJsonValue{0}, QJsonValue{-1}, QJsonValue{"1.25"}, QJsonValue{true}, QJsonValue{}})
        check(!parseRuntime("Seek", with(seekTarget, "duration", value)).request, "seek duration must be positive finite native target fact");
    for (const auto* type : {"Seek", "RequestTimelineHover"})
    {
        const auto target = QString::fromLatin1(type) == "Seek" ? seekTarget : hoverTarget;
        check(!parseRuntime(type, with(target, "bindingToken", "")).request
            && !parseRuntime(type, with(target, "bindingToken", 1)).request, "runtime target requires bounded explicit binding identity");
        check(!parseRuntime(type, with(target, "itemId", "alias")).request, "authored Item identity remains canonical stable UUID");
    }
    for (const auto& value : {QJsonValue{0}, QJsonValue{-1}, QJsonValue{1.5}, QJsonValue{"1"}, QJsonValue{4294967296.0}})
        check(!parseRuntime("RequestTimelineHover", with(hoverTarget, "serial", value)).request, "hover serial is bounded positive integer");
    check(!parseRuntime("RequestTimelineHover", with(hoverTarget, "time", -0.1)).request, "hover time cannot be negative");
    auto duplicateRuntime = runtimeEnvelope;
    duplicateRuntime.insert("type", "Seek"); duplicateRuntime.insert("payload", seekTarget);
    auto duplicateRuntimeBytes = serialize(duplicateRuntime);
    duplicateRuntimeBytes.replace("\"value\":9999", "\"value\":9999,\"value\":10000");
    check(!parseRequest(duplicateRuntimeBytes, runtimePage.document(), runtimePage.document(), runtimePage.generation(),
        incarnation).request, "duplicate seek value cannot smuggle endpoint Next");
    // BGM commands are admitted only on the distinct, captured peer surface.
    PageFence peerPage{incarnation, Surface::BackgroundMusic};
    peerPage.prepare();
    check(peerPage.document().startsWith(QString::fromLatin1(BgmDocumentPrefix))
        && !peerPage.document().contains("preview=") && !peerPage.document().contains("documents=")
        && !peerPage.document().contains("authored=") && !peerPage.document().contains("runtime=")
        && !peerPage.document().contains("presentation="), "opaque BGM peer has its own fenced packaged route without main capabilities");
    auto peerEnvelope = request;
    peerEnvelope.insert("generation", peerPage.generation());
    const auto parsePeer = [&](const char* command, const QJsonObject& payload, Surface surface = Surface::BackgroundMusic) {
        auto value = peerEnvelope; value.insert("type", command); value.insert("payload", payload);
        return parseRequest(serialize(value), peerPage.document(), peerPage.document(), peerPage.generation(),
            incarnation, surface);
    };

    // Runtime requests carry a native-issued peer permit and scoped IDs.
    const auto bgmPermit = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto bgmTrack = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject bgmPlaylist{{"permit", bgmPermit}, {"scope", "playlist"}, {"playlistId", playlistId}};
    const auto bgmItem = with(with(bgmPlaylist, "scope", "item"), "itemId", itemId);
    for (const auto& payload : {bgmPlaylist, bgmItem})
    {
        const auto hydration = parsePeer("GetBgmRuntimeState", payload);
        check(hydration.request && hydration.request->bgmRuntime && !hydration.request->bgmRuntime->trackId
            && hydration.request->bgmRuntime->permit == QUuid{bgmPermit}
            && DesktopSceneEngine::Player::bgmPlaylistId(hydration.request->bgmRuntime->target).value == QUuid{playlistId},
            "C3 hydration retains exact native context without inventing a current track");
        const auto use = parsePeer("UseBgmTrack", with(payload, "trackId", bgmTrack));
        check(use.request && use.request->bgmRuntime && use.request->bgmRuntime->trackId
            && use.request->bgmRuntime->trackId->value() == bgmTrack.toStdString()
            && DesktopSceneEngine::Player::bgmItemId(use.request->bgmRuntime->target).has_value() == payload.contains("itemId"),
            "C3 Use preserves scope and Domain TrackId, never row/source matching");
        for (const auto* command : {"GetBgmRuntimeState", "UseBgmTrack"})
        {
            const auto intent = QString::fromLatin1(command) == "UseBgmTrack" ? with(payload, "trackId", bgmTrack) : payload;
            check(!parsePeer(command, intent, Surface::Main).request, "Main cannot borrow a BGM peer permit");
            for (const auto& field : intent.keys())
            {
                auto missing = intent; missing.remove(field);
                check(!parsePeer(command, missing).request, "BGM runtime rejects every absent required field");
            }
            for (const auto* field : {"expectedRevision", "rowIndex", "currentTrackId", "extra"})
                check(!parsePeer(command, with(intent, field, 0)).request, "BGM runtime rejects authored/optimistic/index extras");
            auto envelope = peerEnvelope;
            envelope.insert("type", command); envelope.insert("payload", intent);
            for (const auto& field : intent.keys())
            {
                auto duplicate = serialize(envelope);
                const auto member = '"' + field.toUtf8() + "\":"
                    + QJsonDocument(QJsonArray{intent.value(field)}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
                duplicate.replace(member, member + ',' + member);
                check(!parseRequest(duplicate, peerPage.document(), peerPage.document(), peerPage.generation(),
                    incarnation, Surface::BackgroundMusic).request, "duplicate BGM payload field cannot smuggle another owner/permit");
            }
            envelope.insert("generation", "retired-page");
            check(!parseRequest(serialize(envelope), peerPage.document(), peerPage.document(), peerPage.generation(),
                incarnation, Surface::BackgroundMusic).request, "retired page cannot hydrate or Use native BGM context");
            envelope.insert("generation", peerPage.generation());
            envelope.insert("sessionIncarnation", QUuid::createUuid().toString(QUuid::WithoutBraces));
            check(!parseRequest(serialize(envelope), peerPage.document(), peerPage.document(), peerPage.generation(),
                incarnation, Surface::BackgroundMusic).request, "another Session cannot borrow BGM permit context");
        }
    }
    for (const auto& field : {QStringLiteral("permit"), QStringLiteral("playlistId"), QStringLiteral("itemId")})
        for (const auto& bad : {QJsonValue{}, QJsonValue{true}, QJsonValue{42}, QJsonValue{""},
            QJsonValue{QUuid::createUuid().toString(QUuid::WithBraces)}, QJsonValue{"00000000-0000-0000-0000-000000000000"}})
            check(!parsePeer("GetBgmRuntimeState", with(bgmItem, field.toLatin1().constData(), bad)).request,
                "BGM contexts require nonnull canonical UUID fields without coercion");
    for (const auto& bad : {QJsonValue{}, QJsonValue{true}, QJsonValue{""}, QJsonValue{"alias"},
        QJsonValue{QUuid::createUuid().toString(QUuid::WithBraces)}})
        check(!parsePeer("UseBgmTrack", with(bgmPlaylist, "trackId", bad)).request, "BGM Use TrackId must be canonical");
    for (const auto& bad : {QJsonValue{}, QJsonValue{true}, QJsonValue{0}, QJsonValue{"global"}, QJsonValue{"Item"}})
        check(!parsePeer("GetBgmRuntimeState", with(bgmPlaylist, "scope", bad)).request, "BGM scope is exact playlist/item vocabulary");
    check(!parsePeer("GetBgmRuntimeState", with(bgmPlaylist, "itemId", itemId)).request
        && !parsePeer("GetBgmRuntimeState", with(bgmPlaylist, "trackId", bgmTrack)).request,
        "playlist hydration cannot carry Item or requested-track ownership");
    for (const auto* command : {"CreateBgmPermit", "SetBgmQueueVolume", "ConfigureBGM"})
        check(!parsePeer(command, bgmPlaylist).request, "C3 exposes no arbitrary permit issuer or authored BGM command");

    namespace BgmObservation = DesktopSceneEngine::BackgroundMusicObservation;
    namespace BgmControl = DesktopSceneEngine::BackgroundMusicControl;
    using DesktopSceneEngine::Player::BgmRuntimeResult;
    using DesktopSceneEngine::Player::BgmRuntimeState;
    using DesktopSceneEngine::Player::BgmRuntimeCommandStatus;
    using DesktopSceneEngine::Player::BgmRuntimeRejectReason;
    BgmRuntimeResult bgmResult;
    check(bgmRuntimePayload(bgmResult) == QJsonObject{{"status", "rejected"}, {"reason", "unavailable"}, {"state", QJsonValue::Null}},
        "unavailable runtime has no fabricated scope/current");
    BgmRuntimeState bgmState;
    bgmState.target = DesktopSceneEngine::Player::ItemBgmTarget{
        DesktopSceneEngine::Player::PlaylistId{QUuid{playlistId}}, DesktopSceneEngine::Player::ItemId{QUuid{itemId}}};
    bgmState.playbackState = BgmObservation::PlaybackState::Paused;
    bgmState.activePlaylistId = BgmControl::PlaylistId{playlistId.toStdString()};
    bgmState.activeItemId = BgmControl::ItemId{itemId.toStdString()};
    bgmState.scope = BgmObservation::ScopeSnapshot{BgmObservation::Scope::Item};
    bgmState.scope.active = bgmState.scope.exists = bgmState.scope.switchEligible = true;
    bgmState.scope.phase = BgmObservation::Phase::Playing;
    const auto observedSuccessor = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bgmState.scope.currentTrackId = BgmControl::TrackId{observedSuccessor.toStdString()};
    bgmState.scope.lastFailedAttempt = BgmObservation::FailedAttempt{BgmControl::TrackId{bgmTrack.toStdString()},
        BgmObservation::FailureStage::SourceOpen, BgmObservation::FailureCode::OpenFailure};
    bgmResult.status = BgmRuntimeCommandStatus::Admitted; bgmResult.reason = BgmRuntimeRejectReason::None; bgmResult.state = bgmState;
    const auto bgmProjection = bgmRuntimePayload(bgmResult);
    const auto bgmFacts = bgmProjection["state"].toObject();
    check(bgmProjection["status"] == "admitted" && bgmProjection["reason"] == "none"
        && bgmFacts["scope"] == "item" && bgmFacts["playlistId"] == playlistId && bgmFacts["itemId"] == itemId
        && bgmFacts["activePlaylistId"] == playlistId && bgmFacts["activeItemId"] == itemId && bgmFacts["playbackState"] == "paused",
        "BGM result separates admission from exact scoped native owner and playback facts");
    check(bgmFacts["currentTrackId"] == observedSuccessor && bgmFacts["currentTrackId"] != bgmTrack
        && bgmFacts["lastFailedAttempt"].toObject() == QJsonObject{{"trackId", bgmTrack}, {"stage", "source-open"}, {"code", "open-failure"}},
        "failed requested A never replaces observed Current B and bounded failure A survives serialization");
    check(!bgmFacts.contains("revision") && !bgmFacts.contains("position") && !bgmFacts.contains("outputAvailable")
        && !bgmFacts.contains("dirty"), "runtime DTO adds no authored truth, clock or fabricated output availability");
    bgmResult.state->scope = BgmObservation::ScopeSnapshot{BgmObservation::Scope::Item};
    const auto inactiveBgm = bgmRuntimePayload(bgmResult)["state"].toObject();
    check(inactiveBgm["currentTrackId"].isNull() && inactiveBgm["phase"].isNull()
        && inactiveBgm["lastFailedAttempt"].isNull() && inactiveBgm["active"] == false && inactiveBgm["switchEligible"] == false,
        "inactive scope transports no other owner's Current/failure or inferred phase");
    bgmResult.state->scope.phase = BgmObservation::Phase::NoPlayableTrack;
    const auto nptBgm = bgmRuntimePayload(bgmResult)["state"].toObject();
    check(nptBgm["phase"] == "no-playable-track" && nptBgm["currentTrackId"].isNull()
        && nptBgm["switchEligible"] == false, "NoPlayableTrack remains current-none/not-switchable in projection");

    auto openEnvelope = runtimeEnvelope;
    openEnvelope.insert("type", "OpenBackgroundMusicEditor");
    openEnvelope.insert("payload", QJsonObject{{"playlistId", playlistId}});
    const auto openEditor = parseRequest(serialize(openEnvelope), runtimePage.document(), runtimePage.document(),
        runtimePage.generation(), incarnation);
    check(openEditor.request && openEditor.request->openBgmEditor
        && openEditor.request->openBgmEditor->value == QUuid{playlistId}, "production Configure captures exact Playlist identity");
    check(!parseRequest(serialize(openEnvelope), runtimePage.document(), runtimePage.document(),
        runtimePage.generation(), incarnation, Surface::BackgroundMusic).request, "BGM peer cannot admit another peer");
    const auto peerHydration = parsePeer("GetBgmEditorSnapshot", {{"itemId", QJsonValue::Null}});
    check(peerHydration.request && peerHydration.request->bgmEditorRead && !peerHydration.request->bgmEditorItem,
        "peer initial hydration asks native for initial editable child without fabricating selection");
    const auto chosenChild = parsePeer("GetBgmEditorSnapshot", {{"itemId", itemId}});
    check(chosenChild.request && chosenChild.request->bgmEditorItem && chosenChild.request->bgmEditorItem->value == QUuid{itemId},
        "peer read carries only editor-local editable Item intent");
    check(!parsePeer("GetBgmEditorSnapshot", {}).request
        && !parsePeer("GetBgmEditorSnapshot", {{"itemId", itemId}, {"playlistId", playlistId}}).request
        && !parsePeer("GetBgmEditorSnapshot", {{"itemId", 1}}).request,
        "peer bootstrap cannot choose another Playlist, omit context or coerce an Item identity");
    check(parsePeer("CloseBgmEditor", {}).request && parsePeer("CloseBgmEditor", {}).request->closeBgmEditor
        && !parsePeer("CloseBgmEditor", {{"session", true}}).request, "peer Close is its own empty-payload command");
    check(parsePeer("GetBgmRuntimeState", bgmPlaylist).request
        && parsePeer("UseBgmTrack", with(bgmItem, "trackId", bgmTrack)).request,
        "peer can use only C3 scoped BGM runtime commands without main runtime capability");
    const std::vector<std::pair<const char*, QJsonObject>> peerCommands{
        {"BgmSetEnabled", with(bgmPlaylist, "enabled", false)},
        {"BgmSetQueueMode", with(bgmPlaylist, "mode", "current-single-loop")},
        {"BgmSetQueueVolume", with(bgmPlaylist, "value", 0.25)},
        {"BgmSetTrackVolume", with(with(bgmItem, "trackId", bgmTrack), "value", 0.5)},
        {"BgmMoveTrack", with(with(bgmPlaylist, "trackId", bgmTrack), "direction", -1)},
        {"BgmRemoveTrack", with(bgmItem, "trackId", bgmTrack)},
        {"BgmAddTracks", bgmItem}};
    for (const auto& [command, payload] : peerCommands)
    {
        const auto parsed = parsePeer(command, payload);
        check(parsed.request && parsed.request->bgmAuthored
            && parsed.request->bgmAuthored->permit == QUuid{bgmPermit}
            && DesktopSceneEngine::Player::bgmPlaylistId(parsed.request->bgmAuthored->target).value == QUuid{playlistId},
            "all finite C2 peer authored commands parse exact native context");
        check(!parsePeer(command, payload, Surface::Main).request, "BGM authored commands unavailable on ordinary Session/DEV page");
        check(!parsePeer(command, with(payload, "sources", QJsonArray{"fake.wav"})).request,
            "peer cannot supply browser file paths or arbitrary fields");
        for (const auto& field : payload.keys())
        {
            auto missing = payload; missing.remove(field);
            check(!parsePeer(command, missing).request, "every required finite BGM authored field is mandatory");
            auto envelope = peerEnvelope; envelope.insert("type", command); envelope.insert("payload", payload);
            auto duplicate = serialize(envelope);
            const auto member = '"' + field.toUtf8() + "\":"
                + QJsonDocument(QJsonArray{payload.value(field)}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
            duplicate.replace(member, member + ',' + member);
            check(!parseRequest(duplicate, peerPage.document(), peerPage.document(), peerPage.generation(), incarnation, Surface::BackgroundMusic).request, "duplicate peer payload field is rejected before native callback");
        }
    }
    for (const auto& bad : {QJsonValue{-0.1}, QJsonValue{1.1}, QJsonValue{"0.5"}, QJsonValue{}, QJsonValue{true}})
        check(!parsePeer("BgmSetQueueVolume", with(bgmPlaylist, "value", bad)).request,
            "queue volume only admits finite native normalized range without coercion");
    for (const auto& bad : {QJsonValue{0}, QJsonValue{2}, QJsonValue{0.5}, QJsonValue{"1"}})
        check(!parsePeer("BgmMoveTrack", with(with(bgmPlaylist, "trackId", bgmTrack), "direction", bad)).request,
            "move only admits exact direction plus/minus one");
    for (const auto& name : {"current-single-loop", "list-loop", "shuffle"})
        check(parsePeer("BgmSetQueueMode", with(bgmPlaylist, "mode", name)).request.has_value(), "native BGM mode vocabulary accepted");
    check(!parsePeer("BgmSetQueueMode", with(bgmPlaylist, "mode", "repeat-current")).request,
        "visual sequence policy is not a BGM queue mode");
    for (const auto* command : {SnapshotCommand, "NewDocument", "SaveDocument", "RequestClose", "PlayPause",
        "PlayItem", "SetPlaylistMaster", "OpenBackgroundMusicEditor", "CreateBgmPermit", "RequestPermit"})
    {
        auto value = peerEnvelope; value.insert("type", command); value.insert("payload", QJsonObject{});
        check(!parseRequest(serialize(value), peerPage.document(), peerPage.document(), peerPage.generation(), incarnation, Surface::BackgroundMusic).request, "BGM peer allowlist cannot inherit main capabilities or issue permits");
    }
    auto stalePeer = peerEnvelope;
    stalePeer.insert("type", "BgmSetQueueVolume"); stalePeer.insert("payload", with(bgmPlaylist, "value", .4));
    stalePeer.insert("generation", "stale-peer-page");
    check(!parseRequest(serialize(stalePeer), peerPage.document(), peerPage.document(), peerPage.generation(), incarnation, Surface::BackgroundMusic).request, "retired peer page cannot queue an authored mutation");

    const auto retiredRuntimeDocument = runtimePage.document();
    (void)runtimePage.navigationStarting(retiredRuntimeDocument); (void)runtimePage.navigationStarting(retiredRuntimeDocument);
    check(!parseRequest(serialize(runtimeEnvelope), retiredRuntimeDocument, runtimePage.document(), runtimePage.generation(),
        incarnation).request, "old-page runtime intent rejected after page generation rotation");

    // Existing creation gate is consumed directly by the new Host. Characterize
    // its callback-before-factory-return contract without introducing a second gate.
    using namespace DesktopSceneEngine::Player::LauncherBridge;
    CreationOperation environment;
    check(environment.callbackArrived() && !environment.consume(false), "inline environment callback waits for factory return");
    environment.factoryReturned(true);
    check(environment.consume(true) == CreationDisposition::RejectClosed, "environment completion after close cannot adopt");
    CreationOperation controller;
    controller.factoryReturned(true);
    check(!controller.consume(true) && controller.callbackArrived()
        && controller.consume(true) == CreationDisposition::RejectClosed, "pending controller callback after close cannot adopt");
    check(!controller.callbackArrived() && !controller.consume(false), "creation settlement one-shot");
    std::cout << "Session bridge contract: " << checks - failures << '/' << checks << " PASS\n";
    return failures ? 1 : 0;
}
