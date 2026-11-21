#include "SessionReadModel.h"
#include "SessionApplication.h"
#include "SessionBridgeContract.h"

#include <DesktopSceneEngine/Domain/AuthoredProject.h>
#include <QJsonArray>
#include <QThread>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace DesktopSceneEngine::Player
{
namespace
{
QString uuid(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }
QString canonical(const DisplayId& id)
{
    const auto value = id.canonicalRepresentation();
    if (!value) throw std::logic_error("Invalid native Display identity");
    return QString::fromStdString(*value);
}
QJsonValue identity(const QUuid& id) { return id.isNull() ? QJsonValue{QJsonValue::Null} : QJsonValue{uuid(id)}; }
QJsonValue itemIdentity(const std::optional<ItemId>& id) { return id ? identity(id->value) : QJsonValue{QJsonValue::Null}; }
QString language(UiLanguage value) { return value == UiLanguage::SimplifiedChinese ? QStringLiteral("zh_CN") : QStringLiteral("en"); }
QString background(CanvasOutsideMode value) { return value == CanvasOutsideMode::Transparent ? QStringLiteral("reveal") : QStringLiteral("cover"); }
QString mode(Domain::PlaybackSequenceMode value)
{
    switch (value)
    {
    case Domain::PlaybackSequenceMode::ListLoop: return QStringLiteral("list-loop");
    case Domain::PlaybackSequenceMode::Random: return QStringLiteral("random");
    case Domain::PlaybackSequenceMode::RepeatCurrent: return QStringLiteral("repeat-current");
    }
    throw std::logic_error("Invalid native sequence mode");
}
QString displayStatus(DisplaySelectionAvailability value)
{
    switch (value)
    {
    case DisplaySelectionAvailability::Available: return QStringLiteral("success");
    case DisplaySelectionAvailability::Unresolved: return QStringLiteral("success");
    case DisplaySelectionAvailability::EnumerationFailed: return QStringLiteral("api-failure");
    }
    throw std::logic_error("Invalid native Display availability");
}
QJsonValue displayDescriptor(const DisplaySelectionState& selection)
{
    const auto bound = selection.boundDisplay();
    if (!bound) return QJsonValue{QJsonValue::Null};
    QJsonValue refresh{QJsonValue::Null};
    if (bound->refreshRate && bound->refreshRate->denominator)
        refresh = static_cast<double>(bound->refreshRate->numerator) / bound->refreshRate->denominator;
    return QJsonObject{{"id", canonical(bound->id)},
        {"friendlyName", QString::fromStdString(bound->friendlyName)}, {"sourceDeviceName", QString::fromStdString(bound->sourceDeviceName)},
        {"displayNumber", bound->displayNumber}, {"pixelWidth", bound->pixelWidth}, {"pixelHeight", bound->pixelHeight},
        {"desktopX", bound->desktopX}, {"desktopY", bound->desktopY}, {"primary", bound->primary}, {"refreshHz", refresh}};
}
QJsonObject presentation(const Domain::AuthoredItemPresentation& value)
{
    const auto& crop = value.crop;
    const auto& layout = value.layout;
    const auto& media = value.mediaPresentation;
    QString policy;
    switch (media.mode)
    {
    case Domain::MediaPresentationMode::Native: policy = QStringLiteral("native"); break;
    case Domain::MediaPresentationMode::AutoFill: policy = QStringLiteral("auto-fill"); break;
    case Domain::MediaPresentationMode::Custom: policy = QStringLiteral("custom"); break;
    }
    if (policy.isEmpty()) throw std::logic_error("Invalid native media presentation");
    return {{"crop", QJsonObject{{"x", crop.x}, {"y", crop.y}, {"width", crop.width}, {"height", crop.height}}},
        {"layout", QJsonObject{{"positionX", layout.positionX}, {"positionY", layout.positionY}, {"scaleX", layout.scaleX}, {"scaleY", layout.scaleY}}},
        {"mediaPresentation", QJsonObject{{"mode", policy}, {"customAspect", QJsonObject{{"numerator", media.customAspect.numerator}, {"denominator", media.customAspect.denominator}}}}}};
}
QJsonObject authoredItem(const Domain::AuthoredItem& value)
{
    return {{"id", QString::fromStdString(value.id.value())}, {"alias", QString::fromStdString(value.displayAlias)},
        {"source", QString::fromStdString(value.source)}, {"kind", value.mediaType == Domain::AuthoredMediaType::Video ? "video" : "image"},
        {"enabled", value.enabled}, {"duration", value.imageDurationSeconds}, {"rate", value.videoPlaybackRate},
        {"mediaGain", value.itemMediaVolume}, {"presentation", presentation(value.presentation)}};
}
QJsonObject project(const Domain::AuthoredProject& value)
{
    QJsonArray playlists;
    for (const auto& playlist : value.playlists)
    {
        QJsonArray items;
        for (const auto& item : playlist.items) items.append(authoredItem(item));
        playlists.append(QJsonObject{{"id", QString::fromStdString(playlist.id.value())},
            {"alias", QString::fromStdString(playlist.displayAlias)}, {"enabled", playlist.enabled},
            {"masterVolume", playlist.masterVolume}, {"itemMode", mode(playlist.itemPlaybackMode)}, {"items", items}});
    }
    return {{"displayOwner", canonical(value.displayOwner.displayId())},
        {"canvasAspect", QJsonArray{value.desktopCanvas.aspectRatio.numerator, value.desktopCanvas.aspectRatio.denominator}},
        {"playlistMode", mode(value.playlistPlaybackMode)}, {"playlists", playlists}};
}
}

QJsonObject SessionReadModel::mediaImportSnapshot() const
{
    QJsonValue pending{QJsonValue::Null};
    if (owner_.pendingMediaImport_)
    {
        const auto& operation = *owner_.pendingMediaImport_;
        QJsonObject value{{"token", uuid(operation.token)},
            {"playlistId", uuid(std::visit([](const auto& target) { return target.playlistId.value; }, operation.target))}};
        pending = value;
    }
    // The existing main-page DTO is the B3 visual-import contract, not a C4
    // BGM client. It may observe shared pending/revision truth, but must not
    // receive a BGM result disguised as imported visual Items. Typed BGM
    // completion remains on the same native owner for the later peer adapter.
    const bool visualResult = owner_.lastMediaImport_ && !owner_.lastMediaImport_->bgmTarget;
    return {{"pending", pending}, {"last", visualResult
        ? QJsonValue{SessionBridge::authoredPayload(*owner_.lastMediaImport_)} : QJsonValue{QJsonValue::Null}}};
}

SessionReadModel::SessionReadModel(const SessionApplication& owner, QUuid incarnation)
    : owner_(owner), incarnation_(std::move(incarnation))
{
    if (incarnation_.isNull()) throw std::invalid_argument("Session snapshot requires its native Join incarnation");
    observed_ = signature();
}

QJsonObject SessionReadModel::desktopSnapshot() const
{
    const auto platform = owner_.platformFacts();
    const auto capability = owner_.desktopDeploymentProjection();
    return {{"desired", owner_.desktopDeploymentDesired_}, {"targetRegistered", platform.desktopTargetRegistered},
        {"hostOwned", platform.desktopHostOwned}, {"hostHealthy", platform.desktopHealthy},
        {"busy", capability.commandPending}, {"canDeploy", capability.canDeploy},
        {"canUndeploy", capability.canUndeploy}};
}

QJsonObject SessionReadModel::snapshot() const
{
    if (QThread::currentThread() != owner_.thread())
        throw std::logic_error("Session snapshot must be captured on the native control thread");
    const auto& workspace = owner_.workspaceModel_;
    const auto& engine = owner_.engine_;
    const auto& document = owner_.documentState_;
    QJsonValue activePlaylist{QJsonValue::Null};
    QJsonValue activeItem{QJsonValue::Null};
    if (owner_.activePlaybackProjection_)
    {
        const auto& active = *owner_.activePlaybackProjection_;
        activePlaylist = identity(active.sourcePlaylistId.value);
        const auto index = engine.activeSceneIndex();
        if (const auto id = active.itemForSceneIndex(index)) activeItem = identity(id->value);
    }
    const auto committed = owner_.persistedLanguage();
    const auto backgroundRead = owner_.persistedCanvasOutsideMode();
    const auto platform = owner_.platformFacts();
    QJsonObject result{{"session", QJsonObject{{"incarnation", uuid(incarnation_)},
                {"boundDisplayId", canonical(owner_.displaySelection_.boundDisplayId())}}},
        {"document", QJsonObject{{"path", document.currentPath ? QJsonValue{QString::fromStdWString(document.currentPath->wstring())} : QJsonValue{QJsonValue::Null}},
            {"displayName", owner_.currentDocumentDisplayName()}, {"authoredRevision", QString::number(workspace.authoredRevision())},
            {"cleanRevision", QString::number(document.cleanRevision)}, {"dirty", document.isDirty(workspace.authoredRevision())}}},
        {"project", project(workspace.authoredProject())},
        {"selection", QJsonObject{{"playlistId", identity(workspace.selectedPlaylistId().value)}, {"itemId", itemIdentity(workspace.selectedItemId())}}},
        {"active", QJsonObject{{"playlistId", activePlaylist}, {"itemId", activeItem}}},
        {"audioEffects", owner_.audioEffectsSnapshotAfterDrain()},
        {"display", QJsonObject{{"status", displayStatus(owner_.displaySelection_.availability())}, {"descriptor", displayDescriptor(owner_.displaySelection_)}}},
        {"desktop", desktopSnapshot()},
        {"preferences", QJsonObject{{"readStatus", "Unavailable"}, // Legacy Highlight loader exposes no read status.
            {"highlight", owner_.applicationPreferences_.highlightCanvasBoundary}, {"background", background(owner_.currentCanvasOutsideMode_)},
            {"persistedBackground", backgroundRead.value ? QJsonValue{background(*backgroundRead.value)} : QJsonValue{QJsonValue::Null}},
            {"backgroundReadStatus", backgroundRead.status == QSettings::NoError ? "Success" : "Failure"}}},
        {"language", QJsonObject{{"readStatus", committed ? "Success" : "Failure"},
            {"committed", committed ? QJsonValue{language(*committed)} : QJsonValue{QJsonValue::Null}},
            {"effective", language(owner_.effectiveLanguage())}}},
        {"host", QJsonObject{{"ready", false}, {"sessionRoleReady", owner_.engineInitialized_ && !owner_.closing()},
            {"closing", owner_.closePending()}, {"ending", owner_.closing()}, {"visible", platform.visible},
            {"minimized", platform.minimized}, {"maximized", platform.maximized}, {"hideEligible", platform.hideEligible}}}};
    if (owner_.documentCommandsPrepared_)
        result.insert("documentOperation", owner_.documentTransactionSnapshot());
    if (owner_.authoredCommandsPrepared_)
        result.insert("mediaImport", mediaImportSnapshot());
    return result;
}

QJsonObject SessionReadModel::signature() const
{
    if (QThread::currentThread() != owner_.thread())
        throw std::logic_error("Session invalidation must be observed on the native control thread");
    const auto clock = owner_.engine_.playbackClockSample();
    const auto& workspace = owner_.workspaceModel_;
    QJsonObject result{{"revision", QString::number(workspace.authoredRevision())},
        {"clean", QString::number(owner_.documentState_.cleanRevision)},
        {"path", owner_.documentState_.currentPath ? QString::fromStdWString(owner_.documentState_.currentPath->wstring()) : QString{}},
        {"selectedPlaylist", identity(workspace.selectedPlaylistId().value)}, {"selectedItem", itemIdentity(workspace.selectedItemId())},
        {"activePlaylist", identity(workspace.activePlaylistId().value)}, {"activeItem", itemIdentity(workspace.activeItemId())},
        {"paused", clock.paused}, {"rate", clock.rate}, {"muted", clock.outputMuted},
        {"media", owner_.engine_.sceneCount() > 0}, {"desktop", desktopSnapshot()},
        {"display", static_cast<int>(owner_.displaySelection_.availability())},
        {"displayDescriptor", displayDescriptor(owner_.displaySelection_)},
        {"highlight", owner_.applicationPreferences_.highlightCanvasBoundary}, {"background", background(owner_.currentCanvasOutsideMode_)},
        {"language", language(owner_.effectiveLanguage())},
        {"closing", owner_.closePending()}, {"ending", owner_.closing()}};
    result.insert("audioEffects", owner_.audioEffectsSnapshotAfterDrain());
    if (owner_.autonomousPreferences_)
    {
        const auto persistedLanguage = owner_.persistedLanguage();
        const auto persistedCanvas = owner_.persistedCanvasOutsideMode();
        result.insert("persistedLanguage", persistedLanguage ? QJsonValue{language(*persistedLanguage)}
            : QJsonValue{QJsonValue::Null});
        result.insert("persistedBackground", persistedCanvas.value ? QJsonValue{background(*persistedCanvas.value)}
            : QJsonValue{QJsonValue::Null});
        result.insert("backgroundReadStatus", static_cast<int>(persistedCanvas.status));
    }
    if (owner_.documentCommandsPrepared_)
        result.insert("documentOperation", owner_.documentTransactionSnapshot());
    if (owner_.authoredCommandsPrepared_)
        result.insert("mediaImport", mediaImportSnapshot());
    return result;
}

bool SessionReadModel::pollChanged()
{
    const auto next = signature();
    if (next == observed_) return false;
    observed_ = next;
    return true;
}

QJsonObject SessionReadModel::bgmEditorHeader(const QUuid& permit, PlaylistId playlist) const
{
    if (QThread::currentThread() != owner_.thread())
        throw std::logic_error("BGM peer projection must use the native control thread");
    const_cast<DesktopSceneEngine::Engine&>(owner_.engine_).pollAudioExecution();
    const auto context = owner_.bgmEditPermits_.constFind(permit);
    if (!owner_.engineInitialized_ || owner_.documentIncarnation_.isNull() || owner_.documentClosePermitted_
        || owner_.closing() || context == owner_.bgmEditPermits_.cend()
        || context->documentIncarnation != owner_.documentIncarnation_ || context->playlistId != playlist
        || !owner_.workspaceModel_.authoredPlaylist(playlist))
        throw std::runtime_error("Native Background Music context is unavailable");
    const auto* value = owner_.workspaceModel_.authoredPlaylist(playlist);
    const auto revision = owner_.workspaceModel_.authoredRevision();
    QJsonValue pending{QJsonValue::Null}, last{QJsonValue::Null};
    if (owner_.pendingMediaImport_ && !std::holds_alternative<SessionApplication::VisualMediaImportTarget>(owner_.pendingMediaImport_->target)
        && std::visit([](const auto& target) { return target.playlistId; }, owner_.pendingMediaImport_->target) == playlist)
        pending = uuid(owner_.pendingMediaImport_->token);
    if (owner_.lastMediaImport_ && owner_.lastMediaImport_->bgmTarget && bgmPlaylistId(*owner_.lastMediaImport_->bgmTarget) == playlist)
        last = uuid(owner_.lastMediaImport_->token);
    return {{"revision", QString::number(revision)}, {"dirty", owner_.documentState_.isDirty(revision)},
        {"language", language(owner_.effectiveLanguage())}, {"playlistAlias", QString::fromStdString(value->displayAlias)},
        {"busy", owner_.pendingDocumentOperation_.has_value() || owner_.pendingMediaImport_.has_value()
            || owner_.authoredOperationExecuting_ || owner_.closePending()
            || owner_.engine_.audioExecutionState().effects[0].pending()},
        {"pending", pending}, {"lastImportToken", last}};
}

QJsonObject SessionReadModel::bgmEditorSnapshot(const QUuid& permit, PlaylistId playlist, std::optional<ItemId> selected) const
{
    const auto header = bgmEditorHeader(permit, playlist);
    const auto& model = owner_.workspaceModel_;
    const auto* value = model.authoredPlaylist(playlist);
    if (selected && owner_.validateBgmTarget(ItemBgmTarget{playlist, *selected}) == AuthoredStatus::WrongParent)
        throw std::runtime_error("Item does not belong to captured Playlist");
    if (!selected || !model.authoredItem(*selected))
        selected = value->items.empty() ? std::nullopt : std::optional<ItemId>{ItemId{QUuid{QString::fromStdString(value->items.front().id.value())}}};
    const auto authored = model.authoredBackgroundMusic(playlist, selected);
    if (!authored) throw std::runtime_error("Native Background Music configuration is unavailable");
    const auto queue = [](const Domain::BackgroundMusicQueue& source) {
        static constexpr const char* modes[]{"current-single-loop", "list-loop", "shuffle"};
        QJsonArray tracks;
        for (const auto& track : source.tracks) tracks.append(QJsonObject{{"id", QString::fromStdString(track.id.value())},
            {"alias", QString::fromStdString(track.displayAlias)}, {"source", QString::fromStdString(track.source)}, {"volume", track.volume}});
        return QJsonObject{{"enabled", source.enabled}, {"mode", modes[static_cast<std::size_t>(source.playbackMode)]},
            {"volume", source.volume}, {"tracks", tracks}};
    };
    QJsonArray items;
    for (const auto& item : value->items) items.append(QJsonObject{{"id", QString::fromStdString(item.id.value())}, {"alias", QString::fromStdString(item.displayAlias)}});
    QJsonValue pending{QJsonValue::Null}, last{QJsonValue::Null};
    if (!header.value("pending").isNull())
    {
        const auto& operation = *owner_.pendingMediaImport_;
        const auto item = std::visit([](const auto& target) -> std::optional<ItemId> {
            using Target = std::decay_t<decltype(target)>;
            if constexpr (std::is_same_v<Target, ItemBgmTarget>) return target.itemId;
            else return std::nullopt;
        }, operation.target);
        pending = QJsonObject{{"token", uuid(operation.token)}, {"scope", item ? "item" : "playlist"}, {"itemId", itemIdentity(item)}};
    }
    if (!header.value("lastImportToken").isNull()) last = SessionBridge::authoredPayload(*owner_.lastMediaImport_);
    return {{"permit", uuid(permit)}, {"documentIncarnation", uuid(owner_.documentIncarnation_)},
        {"playlist", QJsonObject{{"id", uuid(playlist.value)}, {"alias", QString::fromStdString(value->displayAlias)}}},
        {"items", items}, {"selectedItemId", itemIdentity(selected)}, {"playlistQueue", queue(authored->playlistMusic)},
        {"itemQueue", authored->itemMusic ? QJsonValue{queue(*authored->itemMusic)} : QJsonValue{QJsonValue::Null}},
        {"revision", header.value("revision")}, {"dirty", header.value("dirty")}, {"language", header.value("language")},
        {"busy", header.value("busy")}, {"pending", pending}, {"lastImport", last}};
}

void SessionReadModel::acknowledgePlaylistMasterRevision(std::uint64_t before)
{
    acknowledgeScalarAuthoredRevision(before);
}

void SessionReadModel::acknowledgeScalarAuthoredRevision(std::uint64_t before)
{
    if (QThread::currentThread() != owner_.thread())
        throw std::logic_error("Scalar authored projection must be observed on the native control thread");
    if (observed_.value("revision").toString() == QString::number(before))
        observed_.insert("revision", QString::number(owner_.workspaceModel_.authoredRevision()));
}
}
