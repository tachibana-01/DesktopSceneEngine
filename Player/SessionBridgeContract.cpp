#include "SessionBridgeContract.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iterator>

namespace DesktopSceneEngine::Player::SessionBridge
{
namespace
{
bool validRequestId(const QJsonValue& value)
{
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 128) return false;
    for (const auto character : text)
        if (character.unicode() < 0x21 || character.unicode() > 0x7e) return false;
    return true;
}

int memberCountAtDepth(const QByteArray& json, int memberDepth)
{
    // QJsonObject collapses duplicate keys. Count syntactic members after JSON
    // grammar validation so a duplicated envelope or payload cannot be accepted.
    int depth = 0;
    int count = 0;
    bool string = false;
    bool escaped = false;
    for (const auto character : json)
    {
        if (string)
        {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') string = false;
        }
        else if (character == '"') string = true;
        else if (character == '{' || character == '[') ++depth;
        else if (character == '}' || character == ']') --depth;
        else if (character == ':' && depth == memberDepth) ++count;
    }
    return count;
}

// Preserve the original payload bytes for duplicate-key checks when another
// nested envelope object (audioEffects) is present. QJsonObject alone would
// collapse duplicates; a reserialized payload would lose that evidence.
QByteArray rawRootObject(const QByteArray& json, const QString& name)
{
    int depth = 0;
    qsizetype start = 0;
    bool quoted = false, escaped = false;
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    for (qsizetype i = 0; i < json.size(); ++i)
    {
        const char c = json[i];
        if (quoted)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"')
            {
                quoted = false;
                qsizetype colon = i + 1;
                while (colon < json.size() && space(json[colon])) ++colon;
                if (depth != 1 || colon >= json.size() || json[colon] != ':') continue;
                const auto key = QJsonDocument::fromJson('[' + json.mid(start, i - start + 1) + ']').array();
                if (key.size() != 1 || key.first().toString() != name) continue;
                qsizetype begin = colon + 1;
                while (begin < json.size() && space(json[begin])) ++begin;
                if (begin >= json.size() || json[begin] != '{') return {};
                int nested = 0;
                bool inString = false, escape = false;
                for (qsizetype end = begin; end < json.size(); ++end)
                {
                    const char value = json[end];
                    if (inString) { if (escape) escape = false; else if (value == '\\') escape = true; else if (value == '"') inString = false; }
                    else if (value == '"') inString = true;
                    else if (value == '{') ++nested;
                    else if (value == '}' && --nested == 0) return json.mid(begin, end - begin + 1);
                }
                return {};
            }
        }
        else if (c == '"') { quoted = true; start = i; }
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') --depth;
    }
    return {};
}

QJsonObject envelope(const QString& requestId, const QString& generation,
    const QString& sessionIncarnation, bool ok, const QString& type)
{
    return {{"kind", "result"}, {"version", Version}, {"requestId", requestId},
        {"type", type}, {"generation", generation},
        {"sessionIncarnation", sessionIncarnation}, {"ok", ok}};
}

bool exactFields(const QJsonObject& object, const QSet<QString>& fields)
{
    const auto keys = object.keys();
    return QSet<QString>{keys.begin(), keys.end()} == fields;
}

std::optional<PreviewGeometry::Intent> previewIntent(const QJsonObject& object, const QByteArray& json)
{
    const bool occluded = object.contains("occlusion");
    auto fields = QSet<QString>{"rect", "viewport", "rasterizationScale", "visible"};
    if (occluded) fields.insert("occlusion");
    if (!exactFields(object, fields)
        || memberCountAtDepth(json, 2) != fields.size() || memberCountAtDepth(json, 3) != (occluded ? 8 : 6)
        || !object.value("rect").isObject() || !object.value("viewport").isObject()
        || !object.value("rasterizationScale").isDouble() || !object.value("visible").isBool()) return {};
    const auto rect = object.value("rect").toObject();
    const auto viewport = object.value("viewport").toObject();
    if (!exactFields(rect, {"x", "y", "width", "height"}) || !exactFields(viewport, {"width", "height"})) return {};
    for (const auto& value : rect) if (!value.isDouble()) return {};
    for (const auto& value : viewport) if (!value.isDouble()) return {};
    PreviewGeometry::Intent intent{{rect.value("x").toDouble(), rect.value("y").toDouble(),
        rect.value("width").toDouble(), rect.value("height").toDouble()},
        {viewport.value("width").toDouble(), viewport.value("height").toDouble()},
        object.value("rasterizationScale").toDouble(), object.value("visible").toBool()};
    if (occluded)
    {
        const auto value = object.value("occlusion");
        if (!value.isObject()) return {};
        const auto overlay = value.toObject();
        if (!exactFields(overlay, {"modalActive", "rects"}) || !overlay.value("modalActive").isBool()
            || !overlay.value("rects").isArray()) return {};
        const auto rectangles = overlay.value("rects").toArray();
        if (rectangles.size() > 32 || memberCountAtDepth(json, 4) != 0
            || memberCountAtDepth(json, 5) != rectangles.size() * 4) return {};
        intent.modalActive = overlay.value("modalActive").toBool();
        for (const auto& rectangle : rectangles)
        {
            if (!rectangle.isObject()) return {};
            const auto r = rectangle.toObject();
            if (!exactFields(r, {"x", "y", "width", "height"})) return {};
            for (const auto& number : r) if (!number.isDouble()) return {};
            intent.floatingRects.append(QRectF{r.value("x").toDouble(), r.value("y").toDouble(),
                r.value("width").toDouble(), r.value("height").toDouble()});
        }
    }
    // Reuse the numeric bounds before a current native viewport is available.
    const auto numeric = PreviewGeometry::resolve(intent, {}, intent.observedScale);
    if (!numeric.error.isEmpty()) return {};
    return intent;
}

std::optional<DocumentAction> documentAction(const QString& type)
{
    if (type == QLatin1String(NewDocumentCommand)) return DocumentAction::New;
    if (type == QLatin1String(OpenDocumentCommand)) return DocumentAction::Open;
    if (type == QLatin1String(SaveDocumentCommand)) return DocumentAction::Save;
    if (type == QLatin1String(SaveDocumentAsCommand)) return DocumentAction::SaveAs;
    if (type == QLatin1String(RequestCloseCommand)) return DocumentAction::Close;
    return {};
}

std::optional<AuthoredAction> authoredAction(const QString& type)
{
    static const std::pair<const char*, AuthoredAction> actions[]{
        {"SelectPlaylist", AuthoredAction::SelectPlaylist}, {"AddPlaylist", AuthoredAction::AddPlaylist},
        {"RenamePlaylist", AuthoredAction::RenamePlaylist}, {"RemovePlaylist", AuthoredAction::RemovePlaylist},
        {"MovePlaylist", AuthoredAction::MovePlaylist}, {"SetPlaylistEnabled", AuthoredAction::SetPlaylistEnabled},
        {"SetProjectPolicy", AuthoredAction::SetProjectPolicy}, {"SetPlaylistPolicy", AuthoredAction::SetPlaylistPolicy},
        {"SetPlaylistMaster", AuthoredAction::SetPlaylistMaster}, {"SelectItem", AuthoredAction::SelectItem},
        {"AddMedia", AuthoredAction::AddMedia}, {"RenameItem", AuthoredAction::RenameItem},
        {"RemoveItem", AuthoredAction::RemoveItem}, {"MoveItem", AuthoredAction::MoveItem},
        {"SetItemEnabled", AuthoredAction::SetItemEnabled}, {"SetImageDuration", AuthoredAction::SetImageDuration},
        {"ApplyVideoRate", AuthoredAction::ApplyVideoRate}, {"SetItemGain", AuthoredAction::SetItemGain},
        {"SetItemPresentation", AuthoredAction::SetItemPresentation}};
    for (const auto& [name, action] : actions) if (type == QLatin1String(name)) return action;
    return {};
}

std::optional<QUuid> canonicalUuid(const QJsonValue& value)
{
    if (!value.isString()) return {};
    const auto text = value.toString();
    const QUuid id{text};
    if (id.isNull() || id.toString(QUuid::WithoutBraces) != text) return {};
    return id;
}

bool finiteNumber(const QJsonValue& value, double low, double high)
{
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= low && value.toDouble() <= high;
}

std::optional<TimelineHoverPresentation::Intent> hoverPresentationIntent(const QJsonObject& object, const QByteArray& json)
{
    const QSet<QString> fields{"bindingToken", "itemId", "serial", "time", "rect", "lane", "imageSize", "viewport", "rasterizationScale", "timeLabel"};
    if (!exactFields(object, fields) || memberCountAtDepth(json, 2) != fields.size()
        || memberCountAtDepth(json, 3) != 12 || memberCountAtDepth(json, 4) != 0
        || !validRequestId(object.value("bindingToken"))
        || !finiteNumber(object.value("serial"), 1, std::numeric_limits<qint32>::max())
        || object.value("serial").toDouble() != std::floor(object.value("serial").toDouble())
        || !finiteNumber(object.value("time"), 0, std::numeric_limits<float>::max())
        || !object.value("timeLabel").isString() || !object.value("rasterizationScale").isDouble()) return {};
    const auto item = canonicalUuid(object.value("itemId"));
    if (!item) return {};
    const auto numericObject = [&](const char* name, const QSet<QString>& keys) -> std::optional<QJsonObject> {
        const auto value = object.value(QLatin1String(name));
        if (!value.isObject()) return {};
        const auto numbers = value.toObject();
        if (!exactFields(numbers, keys)) return {};
        for (const auto& number : numbers) if (!number.isDouble()) return {};
        return numbers;
    };
    const auto rect = numericObject("rect", {"x", "y", "width", "height"});
    const auto lane = numericObject("lane", {"x", "y", "width", "height"});
    const auto image = numericObject("imageSize", {"width", "height"});
    const auto viewport = numericObject("viewport", {"width", "height"});
    if (!rect || !lane || !image || !viewport) return {};
    const auto rectangle = [](const QJsonObject& value) {
        return QRectF{value.value("x").toDouble(), value.value("y").toDouble(),
            value.value("width").toDouble(), value.value("height").toDouble()};
    };
    TimelineHoverPresentation::Intent intent;
    intent.bindingToken = object.value("bindingToken").toString();
    intent.itemId = *item;
    intent.serial = static_cast<quint32>(object.value("serial").toDouble());
    intent.time = object.value("time").toDouble();
    intent.rect = rectangle(*rect);
    intent.lane = rectangle(*lane);
    intent.imageSize = QSizeF{image->value("width").toDouble(), image->value("height").toDouble()};
    intent.viewport = QSizeF{viewport->value("width").toDouble(), viewport->value("height").toDouble()};
    intent.observedScale = object.value("rasterizationScale").toDouble();
    intent.timeLabel = object.value("timeLabel").toString();
    if (!TimelineHoverPresentation::validate(intent).isEmpty()) return {};
    return intent;
}

std::optional<AuthoredRequest> authoredIntent(AuthoredAction action, const QJsonObject& payload, const QByteArray& json)
{
    AuthoredRequest request;
    request.action = action;
    QSet<QString> fields;
    if (action != AuthoredAction::AddPlaylist && action != AuthoredAction::SetProjectPolicy)
    {
        fields.insert("playlistId");
        const auto id = canonicalUuid(payload.value("playlistId"));
        if (!id) return {};
        request.playlistId = PlaylistId{*id};
    }
    const bool item = action == AuthoredAction::SelectItem || (action >= AuthoredAction::RenameItem);
    if (item)
    {
        fields.insert("itemId");
        const auto id = canonicalUuid(payload.value("itemId"));
        if (!id) return {};
        request.itemId = ItemId{*id};
    }
    if (payload.contains("expectedRevision"))
    {
        fields.insert("expectedRevision");
        if (!payload.value("expectedRevision").isString()) return {};
        const auto text = payload.value("expectedRevision").toString();
        bool valid = false;
        const auto revision = text.toULongLong(&valid);
        if (!valid || text != QString::number(revision)) return {};
        request.expectedRevision = revision;
    }
    switch (action)
    {
    case AuthoredAction::RenamePlaylist: case AuthoredAction::RenameItem:
        fields.insert("alias");
        if (!payload.value("alias").isString() || payload.value("alias").toString().size() > 4096) return {};
        request.alias = payload.value("alias").toString();
        [[fallthrough]];
    case AuthoredAction::RemovePlaylist: case AuthoredAction::RemoveItem:
        fields.insert("expectedAlias");
        if (!payload.value("expectedAlias").isString() || payload.value("expectedAlias").toString().size() > 4096) return {};
        request.expectedAlias = payload.value("expectedAlias").toString();
        break;
    case AuthoredAction::MovePlaylist: case AuthoredAction::MoveItem:
        fields.insert("direction");
        if (payload.value("direction") != QJsonValue{-1} && payload.value("direction") != QJsonValue{1}) return {};
        request.direction = payload.value("direction").toInt();
        break;
    case AuthoredAction::SetPlaylistEnabled: case AuthoredAction::SetItemEnabled:
        fields.insert("enabled");
        if (!payload.value("enabled").isBool()) return {};
        request.enabled = payload.value("enabled").toBool();
        break;
    case AuthoredAction::SetProjectPolicy: case AuthoredAction::SetPlaylistPolicy:
    {
        fields.insert("mode");
        const auto mode = payload.value("mode").toString();
        if (mode == "list-loop") request.policy = PlaybackSequenceMode::ListLoop;
        else if (mode == "random") request.policy = PlaybackSequenceMode::Random;
        else if (mode == "repeat-current") request.policy = PlaybackSequenceMode::RepeatCurrent;
        else return {};
        break;
    }
    case AuthoredAction::SetPlaylistMaster: case AuthoredAction::SetItemGain:
    case AuthoredAction::SetImageDuration: case AuthoredAction::ApplyVideoRate:
    {
        fields.insert("value");
        const auto low = action == AuthoredAction::SetImageDuration ? 0.1 : action == AuthoredAction::ApplyVideoRate ? 0.5 : 0.0;
        const auto high = action == AuthoredAction::SetImageDuration ? 86400.0 : action == AuthoredAction::ApplyVideoRate ? 2.0 : 1.0;
        if (!finiteNumber(payload.value("value"), low, high)) return {};
        request.value = payload.value("value").toDouble();
        break;
    }
    case AuthoredAction::SetItemPresentation:
    {
        fields.insert("patch");
        if (!payload.value("patch").isObject()) return {};
        const auto patch = payload.value("patch").toObject();
        const QSet<QString> allowed{"mode", "customAspect", "scale", "positionX", "positionY"};
        if (patch.isEmpty() || memberCountAtDepth(json, 3) != patch.size()) return {};
        for (const auto& key : patch.keys()) if (!allowed.contains(key)) return {};
        if (patch.contains("mode"))
        {
            const auto mode = patch.value("mode").toString();
            if (mode == "native") request.presentation.mode = Domain::MediaPresentationMode::Native;
            else if (mode == "auto-fill") request.presentation.mode = Domain::MediaPresentationMode::AutoFill;
            else if (mode == "custom") request.presentation.mode = Domain::MediaPresentationMode::Custom;
            else return {};
        }
        if (patch.contains("customAspect"))
        {
            if (!patch.value("customAspect").isObject()) return {};
            const auto ratio = patch.value("customAspect").toObject();
            if (!exactFields(ratio, {"numerator", "denominator"}) || memberCountAtDepth(json, 4) != 2) return {};
            for (const auto& value : ratio)
                if (!finiteNumber(value, 1, std::numeric_limits<int>::max()) || value.toDouble() != std::floor(value.toDouble())) return {};
            request.presentation.customAspect = Domain::AspectRatio{ratio.value("numerator").toInt(), ratio.value("denominator").toInt()};
        }
        for (const auto* key : {"scale", "positionX", "positionY"}) if (patch.contains(QLatin1String(key)))
        {
            const bool scale = QLatin1String(key) == QLatin1String("scale");
            if (!finiteNumber(patch.value(QLatin1String(key)), scale ? 0.1 : -1.0, scale ? 3.0 : 1.0)) return {};
        }
        if (patch.contains("scale")) request.presentation.scale = patch.value("scale").toDouble();
        if (patch.contains("positionX")) request.presentation.positionX = patch.value("positionX").toDouble();
        if (patch.contains("positionY")) request.presentation.positionY = patch.value("positionY").toDouble();
        break;
    }
    default: break;
    }
    if (!exactFields(payload, fields) || memberCountAtDepth(json, 2) != fields.size()) return {};
    return request;
}
}

ParsedRequest parseRequest(const QByteArray& rawJson, const QString& source,
    const QString& currentDocument, const QString& generation, const QUuid& sessionIncarnation,
    Surface surface)
{
    if (sessionIncarnation.isNull() || generation.isEmpty() || currentDocument.isEmpty() || source != currentDocument)
        return {{}, {}, "untrusted-or-stale-document"};
    if (rawJson.isEmpty() || rawJson.size() > 16 * 1024)
        return {{}, {}, "invalid-message-size"};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(rawJson, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {{}, {}, "invalid-json-object"};
    const auto object = document.object();
    const auto requestId = validRequestId(object.value("requestId")) ? object.value("requestId").toString() : QString{};
    const auto suppliedType = object.value("type").toString();
    const bool main = surface == Surface::Main;
    const auto action = main ? documentAction(suppliedType) : std::nullopt;
    const auto authored = main ? authoredAction(suppliedType) : std::nullopt;
    const bool continuation = main && suppliedType == QLatin1String(ContinueDocumentCommand);
    const bool preview = main && suppliedType == QLatin1String(PreviewRectCommand);
    const bool highlight = main && suppliedType == QLatin1String(SetCanvasBoundaryHighlightCommand);
    const bool background = main && suppliedType == QLatin1String(SetDesktopBackgroundCommand);
    const bool identify = main && suppliedType == QLatin1String(IdentifyBoundDisplayCommand);
    const bool acknowledgeNotice = main && suppliedType == QLatin1String(AcknowledgeSessionNoticeCommand);
    const bool deploy = main && suppliedType == QLatin1String(DeployDesktopCommand);
    const bool undeploy = main && suppliedType == QLatin1String(UndeployDesktopCommand);
    static const std::pair<const char*, RuntimeAction> runtimeActions[]{
        {"PlayPlaylist", RuntimeAction::PlayPlaylist}, {"PlayItem", RuntimeAction::PlayItem},
        {"PlayPause", RuntimeAction::PlayPause}, {"Previous", RuntimeAction::Previous},
        {"Next", RuntimeAction::Next}, {"Seek", RuntimeAction::Seek}, {"ToggleOutputMute", RuntimeAction::ToggleOutputMute}};
    std::optional<RuntimeAction> runtime;
    if (main) for (const auto& [name, value] : runtimeActions)
        if (suppliedType == QLatin1String(name)) runtime = value;
    const bool runtimeRead = main && suppliedType == QLatin1String("GetRuntimePlaybackSample");
    const bool hover = main && suppliedType == QLatin1String("RequestTimelineHover");
    const bool cancelHover = main && suppliedType == QLatin1String("CancelTimelineHover");
    const bool hoverPresentation = main && suppliedType == QLatin1String(PresentTimelineHoverCommand);
    const bool bgmRead = !main && suppliedType == QLatin1String("GetBgmRuntimeState");
    const bool bgmUse = !main && suppliedType == QLatin1String("UseBgmTrack");
    const bool openBgm = main && suppliedType == QLatin1String("OpenBackgroundMusicEditor");
    const bool bgmSnapshot = !main && suppliedType == QLatin1String("GetBgmEditorSnapshot");
    const bool closeBgm = !main && suppliedType == QLatin1String("CloseBgmEditor");
    const bool audioNegotiation = suppliedType == QLatin1String(NegotiateAudioEffectsCommand);
    const bool audioRead = suppliedType == QLatin1String(AudioEffectsCommand);
    static const std::pair<const char*, BgmAuthoredAction> bgmActions[]{
        {"BgmSetEnabled", BgmAuthoredAction::SetEnabled}, {"BgmSetQueueMode", BgmAuthoredAction::SetQueueMode},
        {"BgmSetQueueVolume", BgmAuthoredAction::SetQueueVolume}, {"BgmSetTrackVolume", BgmAuthoredAction::SetTrackVolume},
        {"BgmMoveTrack", BgmAuthoredAction::MoveTrack}, {"BgmRemoveTrack", BgmAuthoredAction::RemoveTrack},
        {"BgmAddTracks", BgmAuthoredAction::AddTracks}};
    std::optional<BgmAuthoredAction> bgmAction;
    if (!main) for (const auto& [name, value] : bgmActions)
        if (suppliedType == QLatin1String(name)) bgmAction = value;
    const bool recognized = (main && suppliedType == QLatin1String(SnapshotCommand))
        || audioNegotiation || audioRead || action || continuation || preview || authored || runtime || runtimeRead
        || hover || cancelHover || hoverPresentation || bgmRead || bgmUse || openBgm || bgmSnapshot || closeBgm || bgmAction
        || highlight || background || identify || deploy || undeploy || acknowledgeNotice;
    const auto type = recognized ? suppliedType : QString::fromLatin1(SnapshotCommand);
    const auto reject = [&](const char* message) { return ParsedRequest{{}, requestId, message, type}; };
    const bool affected = action || continuation || authored || runtime || bgmAction || bgmUse;
    QSet<QString> expected{"kind", "version", "requestId", "type", "generation", "sessionIncarnation", "payload"};
    if (affected) expected.insert("audioEffects");
    const auto keys = object.keys();
    if (memberCountAtDepth(rawJson, 1) != expected.size() || QSet<QString>{keys.begin(), keys.end()} != expected)
        return reject("invalid-envelope-fields");
    if (object.value("kind") != QJsonValue{"command"} || object.value("version") != QJsonValue{Version}
        || requestId.isEmpty() || !recognized)
        return reject("invalid-command-envelope");
    if (object.value("generation") != QJsonValue{generation}) return reject("stale-generation");
    const auto session = sessionIncarnation.toString(QUuid::WithoutBraces);
    if (object.value("sessionIncarnation") != QJsonValue{session}) return reject("wrong-session-context");
    if (!object.value("payload").isObject()) return reject("invalid-payload");
    const auto payloadBytes = rawRootObject(rawJson, QStringLiteral("payload"));
    if (payloadBytes.isEmpty()) return reject("invalid-payload");
    const auto json = QByteArray{"{\"payload\":"} + payloadBytes + '}';
    QUuid audioDocument;
    if (affected)
    {
        const auto capability = object.value("audioEffects").toObject();
        const auto capabilityBytes = rawRootObject(rawJson, QStringLiteral("audioEffects"));
        const auto captured = canonicalUuid(capability.value("documentIncarnation"));
        if (!exactFields(capability, {"version", "documentIncarnation"}) || capability.value("version") != QJsonValue{1}
            || !captured || capabilityBytes.isEmpty() || memberCountAtDepth(QByteArray{"{\"cap\":"} + capabilityBytes + '}', 2) != 2)
            return reject("invalid-audio-effects-context");
        audioDocument = *captured;
    }
    const auto makeRequest = [&] {
        Request value{requestId, generation, session, type};
        value.audioDocument = audioDocument;
        return value;
    };
    if (acknowledgeNotice)
    {
        const auto payload = object.value("payload").toObject();
        const auto notice = canonicalUuid(payload.value("noticeId"));
        if (!notice || !exactFields(payload, {"noticeId"}) || memberCountAtDepth(json, 2) != 1)
            return reject("invalid-session-notice");
        auto request = makeRequest();
        request.acknowledgeSessionNotice = *notice;
        return {request, requestId, {}, type};
    }
    if (highlight || background || identify || deploy || undeploy)
    {
        const auto payload = object.value("payload").toObject();
        auto request = makeRequest();
        if (highlight)
        {
            const auto value = payload.value("highlighted");
            if (!exactFields(payload, {"highlighted"}) || !value.isBool() || memberCountAtDepth(json, 2) != 1)
                return reject("invalid-view-preference");
            request.canvasBoundaryHighlight = value.toBool();
        }
        else if (background)
        {
            const auto mode = payload.value("mode");
            if (!exactFields(payload, {"mode"}) || memberCountAtDepth(json, 2) != 1
                || (mode != QJsonValue{"cover"} && mode != QJsonValue{"reveal"}))
                return reject("invalid-view-preference");
            request.desktopBackground = mode == QJsonValue{"cover"} ? CanvasOutsideMode::Opaque : CanvasOutsideMode::Transparent;
        }
        else
        {
            if (!payload.isEmpty() || memberCountAtDepth(json, 2) != 0) return reject("invalid-payload");
            if (identify) request.identifyBoundDisplay = true;
            else request.desktopDeployment = deploy ? DesktopDeploymentIntent::Deploy : DesktopDeploymentIntent::Undeploy;
        }
        return {request, requestId, {}, type};
    }
    if (audioNegotiation || audioRead)
    {
        const auto payload = object.value("payload").toObject();
        if (audioNegotiation ? (!exactFields(payload, {"version"}) || payload.value("version") != QJsonValue{1}
                || memberCountAtDepth(json, 2) != 1) : (!payload.isEmpty() || memberCountAtDepth(json, 2) != 0))
            return reject("unsupported-audio-effects-contract");
        auto request = makeRequest();
        request.negotiateAudioEffects = audioNegotiation;
        request.readAudioEffects = audioRead;
        return {request, requestId, {}, type};
    }
    if (openBgm || bgmSnapshot || closeBgm)
    {
        const auto payload = object.value("payload").toObject();
        auto request = makeRequest();
        if (openBgm)
        {
            const auto playlist = canonicalUuid(payload.value("playlistId"));
            if (!playlist || !exactFields(payload, {"playlistId"}) || memberCountAtDepth(json, 2) != 1)
                return reject("invalid-bgm-editor-target");
            request.openBgmEditor = PlaylistId{*playlist};
        }
        else if (bgmSnapshot)
        {
            if (!exactFields(payload, {"itemId"}) || memberCountAtDepth(json, 2) != 1)
                return reject("invalid-bgm-editor-target");
            request.bgmEditorRead = true;
            if (!payload.value("itemId").isNull())
            {
                const auto item = canonicalUuid(payload.value("itemId"));
                if (!item) return reject("invalid-bgm-editor-target");
                request.bgmEditorItem = ItemId{*item};
            }
        }
        else
        {
            if (!payload.isEmpty() || memberCountAtDepth(json, 2) != 0) return reject("invalid-payload");
            request.closeBgmEditor = true;
        }
        return {request, requestId, {}, type};
    }
    if (bgmAction)
    {
        const auto payload = object.value("payload").toObject();
        const auto permit = canonicalUuid(payload.value("permit"));
        const auto playlist = canonicalUuid(payload.value("playlistId"));
        const auto scope = payload.value("scope").toString();
        if (!permit || !playlist || (scope != QLatin1String("playlist") && scope != QLatin1String("item")))
            return reject("invalid-bgm-authored-target");
        QSet<QString> fields{"permit", "scope", "playlistId"};
        BgmAuthoredRequest intent;
        intent.action = *bgmAction;
        intent.permit = *permit;
        intent.target = PlaylistBgmTarget{PlaylistId{*playlist}};
        if (scope == QLatin1String("item"))
        {
            fields.insert("itemId");
            const auto item = canonicalUuid(payload.value("itemId"));
            if (!item) return reject("invalid-bgm-authored-target");
            intent.target = ItemBgmTarget{PlaylistId{*playlist}, ItemId{*item}};
        }
        if (*bgmAction == BgmAuthoredAction::SetTrackVolume || *bgmAction == BgmAuthoredAction::MoveTrack
            || *bgmAction == BgmAuthoredAction::RemoveTrack)
        {
            fields.insert("trackId");
            const auto track = canonicalUuid(payload.value("trackId"));
            if (!track) return reject("invalid-bgm-authored-target");
            intent.trackId = Domain::TrackId{track->toString(QUuid::WithoutBraces).toStdString()};
        }
        if (*bgmAction == BgmAuthoredAction::SetEnabled)
        {
            fields.insert("enabled");
            if (!payload.value("enabled").isBool()) return reject("invalid-bgm-authored-payload");
            intent.enabled = payload.value("enabled").toBool();
        }
        if (*bgmAction == BgmAuthoredAction::SetQueueMode)
        {
            fields.insert("mode");
            const auto mode = payload.value("mode").toString();
            if (mode == QLatin1String("current-single-loop")) intent.mode = Domain::MusicPlaybackMode::CurrentSingleLoop;
            else if (mode == QLatin1String("list-loop")) intent.mode = Domain::MusicPlaybackMode::ListLoop;
            else if (mode == QLatin1String("shuffle")) intent.mode = Domain::MusicPlaybackMode::Shuffle;
            else return reject("invalid-bgm-authored-payload");
        }
        if (*bgmAction == BgmAuthoredAction::SetQueueVolume || *bgmAction == BgmAuthoredAction::SetTrackVolume)
        {
            fields.insert("value");
            if (!finiteNumber(payload.value("value"), 0, 1)) return reject("invalid-bgm-authored-payload");
            intent.value = payload.value("value").toDouble();
        }
        if (*bgmAction == BgmAuthoredAction::MoveTrack)
        {
            fields.insert("direction");
            if (payload.value("direction") != QJsonValue{-1} && payload.value("direction") != QJsonValue{1})
                return reject("invalid-bgm-authored-payload");
            intent.direction = payload.value("direction").toInt();
        }
        if (!exactFields(payload, fields) || memberCountAtDepth(json, 2) != fields.size())
            return reject("invalid-bgm-authored-payload");
        auto request = makeRequest();
        request.bgmAuthored = std::move(intent);
        return {request, requestId, {}, type};
    }
    if (bgmRead || bgmUse)
    {
        const auto payload = object.value("payload").toObject();
        const auto permit = canonicalUuid(payload.value("permit"));
        const auto playlist = canonicalUuid(payload.value("playlistId"));
        const auto scope = payload.value("scope").toString();
        if (!permit || !playlist || (scope != QLatin1String("playlist") && scope != QLatin1String("item")))
            return reject("invalid-bgm-runtime-target");
        QSet<QString> fields{"permit", "scope", "playlistId"};
        BgmRuntimeRequest intent{*permit, PlaylistBgmTarget{PlaylistId{*playlist}}};
        if (scope == QLatin1String("item"))
        {
            fields.insert("itemId");
            const auto item = canonicalUuid(payload.value("itemId"));
            if (!item) return reject("invalid-bgm-runtime-target");
            intent.target = ItemBgmTarget{PlaylistId{*playlist}, ItemId{*item}};
        }
        if (bgmUse)
        {
            fields.insert("trackId");
            const auto track = canonicalUuid(payload.value("trackId"));
            if (!track) return reject("invalid-bgm-runtime-target");
            intent.trackId = Domain::TrackId{track->toString(QUuid::WithoutBraces).toStdString()};
        }
        if (!exactFields(payload, fields) || memberCountAtDepth(json, 2) != fields.size())
            return reject("invalid-bgm-runtime-payload");
        auto request = makeRequest();
        request.bgmRuntime = std::move(intent);
        return {request, requestId, {}, type};
    }
    if (hoverPresentation)
    {
        const auto intent = hoverPresentationIntent(object.value("payload").toObject(), json);
        if (!intent) return reject("invalid-hover-presentation");
        auto request = makeRequest();
        request.hoverPresentation = intent;
        return {request, requestId, {}, type};
    }
    if (runtime || runtimeRead || hover || cancelHover)
    {
        const auto payload = object.value("payload").toObject();
        QSet<QString> fields;
        auto request = makeRequest();
        const auto serial = [&](bool positive) -> std::optional<quint32> {
            const auto value = payload.value("serial");
            if (!finiteNumber(value, positive ? 1 : 0, std::numeric_limits<qint32>::max())
                || value.toDouble() != std::floor(value.toDouble())) return {};
            return static_cast<quint32>(value.toDouble());
        };
        if (runtime)
        {
            RuntimeRequest intent{*runtime};
            if (*runtime == RuntimeAction::PlayPlaylist || *runtime == RuntimeAction::PlayItem)
            {
                fields.insert("playlistId");
                const auto id = canonicalUuid(payload.value("playlistId"));
                if (!id) return reject("invalid-runtime-target");
                intent.playlistId = *id;
                if (*runtime == RuntimeAction::PlayItem)
                {
                    fields.insert("itemId");
                    const auto item = canonicalUuid(payload.value("itemId"));
                    if (!item) return reject("invalid-runtime-target");
                    intent.itemId = *item;
                }
            }
            if (*runtime == RuntimeAction::Seek)
            {
                fields = {"bindingToken", "itemId", "duration", "value"};
                const auto item = canonicalUuid(payload.value("itemId"));
                if (!item || !validRequestId(payload.value("bindingToken"))
                    || !finiteNumber(payload.value("duration"), std::numeric_limits<double>::min(), std::numeric_limits<float>::max())
                    || !finiteNumber(payload.value("value"), 0, 10000)
                    || payload.value("value").toDouble() != std::floor(payload.value("value").toDouble()))
                    return reject("invalid-seek-target");
                intent.itemId = *item;
                intent.bindingToken = payload.value("bindingToken").toString();
                intent.duration = payload.value("duration").toDouble();
                intent.value = payload.value("value").toInt();
            }
            request.runtime = intent;
        }
        if (hover)
        {
            fields = {"bindingToken", "itemId", "serial", "time"};
            const auto item = canonicalUuid(payload.value("itemId"));
            const auto sequence = serial(true);
            if (!item || !sequence || !validRequestId(payload.value("bindingToken"))
                || !finiteNumber(payload.value("time"), 0, std::numeric_limits<float>::max()))
                return reject("invalid-hover-target");
            request.hover = TimelineHoverRequest{payload.value("bindingToken").toString(), *item, *sequence, payload.value("time").toDouble()};
        }
        if (cancelHover)
        {
            fields = {"serial"};
            const auto sequence = serial(false);
            if (!sequence) return reject("invalid-hover-cancel");
            request.cancelHoverSerial = *sequence;
        }
        if (!exactFields(payload, fields) || memberCountAtDepth(json, 2) != fields.size())
            return reject("invalid-runtime-payload");
        return {request, requestId, {}, type};
    }
    if (authored)
    {
        const auto intent = authoredIntent(*authored, object.value("payload").toObject(), json);
        if (!intent) return reject("invalid-authored-payload");
        auto request = makeRequest();
        request.authored = intent;
        return {request, requestId, {}, type};
    }
    if (continuation)
    {
        const auto payload = object.value("payload").toObject();
        if (!exactFields(payload, {"token", "choice"}) || memberCountAtDepth(json, 2) != 2
            || !payload.value("token").isString() || !payload.value("choice").isString())
            return reject("invalid-document-decision");
        const auto tokenText = payload.value("token").toString();
        const QUuid token{tokenText};
        const auto choice = payload.value("choice").toString();
        if (token.isNull() || token.toString(QUuid::WithoutBraces) != tokenText
            || (choice != QStringLiteral("save") && choice != QStringLiteral("discard") && choice != QStringLiteral("cancel")))
            return reject("invalid-document-decision");
        auto request = makeRequest();
        request.documentToken = token;
        request.documentChoice = choice == QStringLiteral("save") ? DocumentChoice::Save
            : choice == QStringLiteral("discard") ? DocumentChoice::Discard : DocumentChoice::Cancel;
        return {request, requestId, {}, type};
    }
    if (action)
    {
        if (!object.value("payload").toObject().isEmpty() || memberCountAtDepth(json, 2) != 0)
            return reject("invalid-payload");
        auto request = makeRequest();
        request.documentAction = action;
        return {request, requestId, {}, type};
    }
    if (type == QLatin1String(PreviewRectCommand))
    {
        const auto intent = previewIntent(object.value("payload").toObject(), json);
        if (!intent) return reject("invalid-preview-geometry");
        return {Request{requestId, generation, session, type, intent}, requestId, {}, type};
    }
    if (!object.value("payload").toObject().isEmpty() || memberCountAtDepth(json, 2) != 0)
        return reject("invalid-payload");
    return {Request{requestId, generation, session}, requestId, {}};
}

QJsonObject result(const Request& request, const QJsonObject& snapshot)
{
    auto response = envelope(request.requestId, request.generation, request.sessionIncarnation, true, request.type);
    response.insert("payload", snapshot);
    return response;
}

QJsonObject failure(const QString& requestId, const QString& generation,
    const QUuid& sessionIncarnation, const QString& error, const QString& type)
{
    auto response = envelope(requestId, generation, sessionIncarnation.toString(QUuid::WithoutBraces), false, type);
    response.insert("error", error);
    return response;
}

QJsonObject invalidated(const QString& generation, const QUuid& sessionIncarnation)
{
    return {{"kind", "event"}, {"version", Version}, {"type", SnapshotInvalidated},
        {"generation", generation}, {"sessionIncarnation", sessionIncarnation.toString(QUuid::WithoutBraces)},
        {"payload", QJsonObject{}}};
}

QJsonObject previewGeometryInvalidated(const QString& generation, const QUuid& sessionIncarnation)
{
    auto event = invalidated(generation, sessionIncarnation);
    event.insert("type", PreviewGeometryInvalidated);
    return event;
}

QJsonObject documentResult(const Request& request, const DocumentCommandResult& command)
{
    QString status;
    switch (command.status)
    {
    case DocumentCommandStatus::Accepted: status = QStringLiteral("accepted"); break;
    case DocumentCommandStatus::Busy: status = QStringLiteral("busy"); break;
    case DocumentCommandStatus::StaleOperation: status = QStringLiteral("stale-operation"); break;
    case DocumentCommandStatus::Unavailable: status = QStringLiteral("unavailable"); break;
    }
    return result(request, {{"status", status}, {"token", command.token.isNull()
        ? QJsonValue{QJsonValue::Null} : QJsonValue{command.token.toString(QUuid::WithoutBraces)}}});
}

QJsonObject authoredPayload(const AuthoredResult& command)
{
    static constexpr const char* statuses[]{"accepted", "applied", "no-change", "not-found", "wrong-parent",
        "invalid-value", "stale-value", "stale-revision", "busy", "picker-cancelled", "unavailable",
        "revoked-permit", "wrong-document", "track-not-found"};
    static constexpr const char* authored[]{"committed", "unchanged", "rejected", "not-applicable"};
    static constexpr const char* runtime[]{"not-required", "synchronized", "unchanged", "stopped", "warning", "failed", "pending", "cancelled", "deferred-unavailable"};
    static constexpr const char* restoration[]{"not-required", "restored", "rebound-at-start", "failed"};
    static constexpr const char* sources[]{"committed", "unsupported", "rejected"};
    const auto name = [](auto value, const auto& names) {
        const auto index = static_cast<std::size_t>(value);
        if (index >= std::size(names)) throw std::logic_error("Invalid native authored outcome");
        return QString::fromLatin1(names[index]);
    };
    const auto identity = [](const QUuid& id) { return id.isNull() ? QJsonValue{QJsonValue::Null}
        : QJsonValue{id.toString(QUuid::WithoutBraces)}; };
    const auto warnings = [](const QVector<AuthoredWarning>& values) {
        QJsonArray result;
        for (const auto& value : values) result.append(QJsonObject{{"code", value.code}, {"message", value.message}});
        return result;
    };
    QJsonArray perSource;
    for (const auto& source : command.sources) perSource.append(QJsonObject{
        {"source", source.source}, {"itemId", source.itemId ? identity(source.itemId->value) : QJsonValue{QJsonValue::Null}},
        {"authored", name(source.outcome, sources)}, {"runtime", name(source.runtime, runtime)}, {"warnings", warnings(source.warnings)}});
    QJsonObject result{{"status", name(command.status, statuses)}, {"playlistId", identity(command.playlistId.value)},
        {"itemId", command.itemId ? identity(command.itemId->value) : QJsonValue{QJsonValue::Null}},
        {"token", identity(command.token)}, {"authored", name(command.authored, authored)},
        {"revision", QString::number(command.authoredRevision)}, {"dirty", command.dirty},
        {"runtime", name(command.runtime, runtime)}, {"positionRestore", name(command.positionRestore, restoration)},
        {"warnings", warnings(command.warnings)}, {"sources", perSource}};
    if (command.playlistMasterVolume) result.insert("playlistMasterVolume", *command.playlistMasterVolume);
    if (command.audioEffect) result.insert("audioEffect", *command.audioEffect);
    if (command.runtimeOnApplied) result.insert("runtimeOnApplied", name(*command.runtimeOnApplied, runtime));
    // C2 completion facts are shared with C4's peer-only command gate.
    // Permit issuance remains native editor admission, never a page command.
    if (command.bgmTarget)
    {
        QJsonObject target{{"scope", bgmItemId(*command.bgmTarget) ? "item" : "playlist"},
            {"playlistId", identity(bgmPlaylistId(*command.bgmTarget).value)}};
        if (const auto item = bgmItemId(*command.bgmTarget)) target.insert("itemId", identity(item->value));
        result.insert("bgmTarget", target);
        if (command.bgmTrackId) result.insert("bgmTrackId", QString::fromStdString(command.bgmTrackId->value()));
        QJsonArray files;
        for (const auto& file : command.bgmSources) files.append(QJsonObject{
            {"source", file.source}, {"trackId", file.trackId ? QJsonValue{QString::fromStdString(file.trackId->value())} : QJsonValue{QJsonValue::Null}},
            {"authored", name(file.outcome, sources)}});
        result.insert("bgmSources", files);
        if (command.bgmImport)
        {
            static constexpr const char* outcomes[]{"completed", "cancelled", "rejected-before-picker", "rejected-at-completion"};
            result.insert("bgmImport", name(*command.bgmImport, outcomes));
        }
        if (command.bgmReadback)
        {
            const auto& native = *command.bgmReadback;
            QJsonObject readback;
            if (native.enabled) readback.insert("enabled", *native.enabled);
            if (native.mode)
            {
                static constexpr const char* modes[]{"current-single-loop", "list-loop", "shuffle"};
                readback.insert("mode", name(*native.mode, modes));
            }
            if (native.queueVolume) readback.insert("queueVolume", *native.queueVolume);
            if (native.trackVolume) readback.insert("trackVolume", *native.trackVolume);
            if (native.order)
            {
                QJsonArray order;
                for (const auto& id : *native.order) order.append(QString::fromStdString(id.value()));
                readback.insert("order", order);
            }
            result.insert("bgmReadback", readback);
        }
    }
    return result;
}

QJsonObject bgmRuntimePayload(const BgmRuntimeResult& command)
{
    static constexpr const char* statuses[]{"rejected", "ready", "no-change", "admitted"};
    static constexpr const char* reasons[]{"none", "unavailable", "revoked-permit", "busy", "target-missing", "wrong-parent",
        "track-missing", "disabled", "inactive", "not-switchable", "control-rejected"};
    static constexpr const char* playback[]{"stopped", "playing", "paused"};
    static constexpr const char* phases[]{"silent-empty-queue", "pending-start", "playing", "no-playable-track", "waiting-for-executor"};
    static constexpr const char* stages[]{"unknown", "source-open", "resource-allocation", "mixer-registration", "playback"};
    static constexpr const char* codes[]{"unknown", "open-failure", "no-audio-stream", "decoder-unavailable", "decode-failure",
        "resample-failure", "resource-failure", "allocation-failure", "registration-failure", "input-failed"};
    const auto name = [](auto value, const auto& names) {
        const auto index = static_cast<std::size_t>(value);
        if (index >= std::size(names)) throw std::logic_error("Invalid native BGM runtime fact");
        return QString::fromLatin1(names[index]);
    };
    const QJsonValue null{QJsonValue::Null};
    QJsonObject result{{"status", name(command.status, statuses)}, {"reason", name(command.reason, reasons)}, {"state", null}};
    if (command.audioEffect) result.insert("audioEffect", *command.audioEffect);
    if (!command.state) return result;
    const auto& state = *command.state;
    const auto& scope = state.scope;
    const auto item = bgmItemId(state.target);
    const auto identity = [&](const auto& id) -> QJsonValue { return id ? QJsonValue{QString::fromStdString(id->value)} : null; };
    QJsonValue failure = null;
    if (scope.lastFailedAttempt)
    {
        const auto& failed = *scope.lastFailedAttempt;
        failure = QJsonObject{{"trackId", QString::fromStdString(failed.trackId.value)},
            {"stage", name(failed.stage, stages)}, {"code", name(failed.code, codes)}};
    }
    result.insert("state", QJsonObject{
        {"scope", item ? "item" : "playlist"},
        {"playlistId", bgmPlaylistId(state.target).value.toString(QUuid::WithoutBraces)},
        {"itemId", item ? QJsonValue{item->value.toString(QUuid::WithoutBraces)} : null},
        {"activePlaylistId", identity(state.activePlaylistId)}, {"activeItemId", identity(state.activeItemId)},
        {"playbackState", name(state.playbackState, playback)}, {"active", scope.active}, {"exists", scope.exists},
        {"phase", scope.phase ? QJsonValue{name(*scope.phase, phases)} : null},
        {"currentTrackId", identity(scope.currentTrackId)}, {"switchEligible", scope.switchEligible},
        {"lastFailedAttempt", failure}});
    return result;
}

PageFence::PageFence(const QUuid& sessionIncarnation, Surface surface)
    : sessionIncarnation_(sessionIncarnation), surface_(surface) {}

void PageFence::prepare()
{
    if (closed_ || sessionIncarnation_.isNull()) return;
    generation_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool main = surface_ == Surface::Main;
    document_ = QString::fromLatin1(main ? DocumentPrefix : BgmDocumentPrefix) + generation_
        + QStringLiteral("&session=") + sessionIncarnation_.toString(QUuid::WithoutBraces);
    if (main) document_ += QStringLiteral("&presentation=model-c-production");
    prepared_ = true;
}

NavigationDisposition PageFence::navigationStarting(const QString& uri)
{
    if (closed_ || document_.isEmpty() || uri != document_) return NavigationDisposition::Reject;
    if (prepared_)
    {
        prepared_ = false;
        return NavigationDisposition::Allow;
    }
    prepare();
    return NavigationDisposition::RestartWithFreshGeneration;
}

void PageFence::close()
{
    closed_ = true;
    prepared_ = false;
    generation_.clear();
    document_.clear();
}
}
