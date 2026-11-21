#include "SessionApplication.h"
#include "TimelineUtils.h"

#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <utility>

namespace DesktopSceneEngine::Player
{
namespace
{
constexpr std::size_t SequenceResolutionAttempts = 2;
}

void SessionApplication::applyAuthoredOperationDeferred(const AuthoredRequest& request, AuthoredCompletion complete)
{
    requireExecutor();
    runAuthoredWithDeferredRebuild([&] { return applyAuthoredOperation(request); }, std::move(complete));
}

void SessionApplication::runAuthoredWithDeferredRebuild(
    const std::function<AuthoredResult()>& execute, AuthoredCompletion complete)
{
    if (!autonomousMediaPreparation_)
    {
        complete(execute());
        return;
    }

    std::optional<AuthoredRebuildPlan> plan;
    AuthoredResult result;
    {
        QScopedValueRollback<decltype(authoredRebuildCapture_)> capturing{authoredRebuildCapture_, &plan};
        result = execute();
    }
    if (!plan)
    {
        complete(std::move(result));
        return;
    }
    beginPreparedAuthoredRebuild(std::move(*plan), std::move(result), std::move(complete));
}

std::optional<Engine::MediaPreparationToken> SessionApplication::requestAuthoredRebuildMedia(
    const AuthoredRebuildPlan& plan)
{
    const auto index = plan.replacement.sceneIndexForItem(plan.item);
    if (!index || *index >= plan.replacement.project.scenes.size()) return {};
    return engine_.prepareSceneMedia(plan.replacement.project,
        plan.replacement.project.scenes[*index].id, plan.reboundAtStart ? 0.0F : plan.position);
}

void SessionApplication::beginPreparedAuthoredRebuild(
    AuthoredRebuildPlan plan, AuthoredResult result, AuthoredCompletion complete)
{
    const bool startsTarget = plan.mode == ActiveProjectionRebuildMode::StartTargetItem;
    const auto playlist = plan.replacement.sourcePlaylistId;
    const auto item = plan.item;
    // An admitted delete must not continue playing the deleted Item while its
    // successor prepares. Keep the separate Playlist BGM scope, just as a
    // synchronous projection rebind does.
    if (startsTarget)
    {
        rollbackPresentation_.reset();
        renewRuntimeBinding();
        engine_.detachProject();
        updateEffectiveOutputVolume();
    }
    result.audioEffect.reset();
    result.runtimeOnApplied.reset();
    const auto media = requestAuthoredRebuildMedia(plan);
    PreparedAuthoredRebuild authored{std::move(plan), std::move(result), std::move(complete)};
    if (!media)
    {
        if (startsTarget) stopActivePlayback();
        completeAuthoredRebuild(std::move(authored), startsTarget
            ? ActiveProjectionRebuildResult::Stopped : ActiveProjectionRebuildResult::Unchanged);
        return;
    }
    pendingRuntimeIntent_.emplace(PreparedRuntimeIntent{*media, runtimeBindingToken_,
        workspaceModel_.authoredRevision(), playlist, item, {}, {}, false, false, false, {}, std::move(authored)});
    publishReadOnlyInvalidation();
}

void SessionApplication::completeAuthoredRebuild(PreparedAuthoredRebuild authored,
    ActiveProjectionRebuildResult rebuild, std::optional<AudioEffectToken> effect)
{
    const bool startsTarget = authored.plan.mode == ActiveProjectionRebuildMode::StartTargetItem;
    auto& result = authored.result;
    describeAuthoredRebuild(result, rebuild, startsTarget);
    if (effect)
    {
        const bool runtimeCommitted = rebuild == ActiveProjectionRebuildResult::Restored
            || rebuild == ActiveProjectionRebuildResult::ReboundAtStart
            || rebuild == ActiveProjectionRebuildResult::StartedTarget;
        result.audioEffect = finishNativeAudioEffect(*effect, runtimeCommitted);
        const auto phase = result.audioEffect->value(QStringLiteral("phase")).toString();
        if (runtimeCommitted && (phase == "pending" || phase == "preparing"))
        {
            result.runtimeOnApplied = result.runtime;
            result.runtime = RuntimeOutcome::Pending;
        }
        else if (phase == "failed") result.runtime = RuntimeOutcome::Failed;
        else if (runtimeCommitted && phase == "cancelled") result.runtime = RuntimeOutcome::Cancelled;
        else if (phase == "deferred-unavailable") result.runtime = RuntimeOutcome::DeferredUnavailable;
    }
    for (auto& source : result.sources)
    {
        if (source.outcome != MediaSourceOutcome::Committed) continue;
        source.runtime = result.runtime;
        for (const auto& warning : result.warnings)
            if (warning.code.startsWith(QStringLiteral("runtime-"))
                || warning.code.startsWith(QStringLiteral("position-")))
                source.warnings.push_back(warning);
    }
    result.authoredRevision = workspaceModel_.authoredRevision();
    result.dirty = documentState_.isDirty(result.authoredRevision);
    publishReadOnlyInvalidation();
    publishRuntimePlayback(true);
    authored.complete(std::move(result));
}

void SessionApplication::applyRuntimeOperationDeferred(const RuntimeRequest& request, RuntimeCompletion complete)
{
    requireExecutor();
    if (autonomousMediaPreparation_ && pendingRuntimeIntent_ && pendingRuntimeIntent_->authored
        && request.action == RuntimeAction::PlayPause)
    {
        complete({QStringLiteral("busy"), tr("An operation is pending."), runtimePlaybackSnapshot(), std::nullopt});
        return;
    }
    if (!autonomousMediaPreparation_ || request.action == RuntimeAction::PlayPause
        || request.action == RuntimeAction::ToggleOutputMute)
    {
        complete(applyRuntimeOperation(request));
        return;
    }

    const auto fail = [this, &complete](QString status, const QString& message) {
        complete({std::move(status), message, runtimePlaybackSnapshot(), std::nullopt});
    };
    if (!runtimeCommandsPrepared_ || !engineInitialized_ || closePending() || closing())
    {
        fail(QStringLiteral("unavailable"), tr("Playback is unavailable."));
        return;
    }
    if (pendingRuntimeIntent_ || pendingDocumentOperation_ || pendingMediaImport_ || authoredOperationExecuting_)
    {
        fail(QStringLiteral("busy"), tr("An operation is pending."));
        return;
    }

    if (request.action == RuntimeAction::PlayPlaylist || request.action == RuntimeAction::PlayItem)
    {
        const PlaylistId playlist{request.playlistId};
        const auto* model = workspaceModel_.playlist(playlist);
        if (!model)
        {
            fail(QStringLiteral("rejected"), tr("The Playlist no longer exists."));
            return;
        }
        const auto item = request.action == RuntimeAction::PlayItem
            ? std::optional<ItemId>{ItemId{request.itemId}} : workspaceModel_.firstPlayableItem(playlist);
        if (request.action == RuntimeAction::PlayItem && std::none_of(model->items.cbegin(), model->items.cend(),
            [&](const auto& member) { return member.id == *item; }))
        {
            fail(QStringLiteral("rejected"), tr("The Item does not belong to this Playlist."));
            return;
        }
        if (!model->enabled)
        {
            fail(QStringLiteral("unplayable"), tr("The Playlist is disabled."));
            return;
        }
        if (model->items.isEmpty())
        {
            fail(QStringLiteral("empty"), tr("The Playlist is empty."));
            return;
        }
        if (!item || !workspaceModel_.isItemPlayable(playlist, *item))
        {
            fail(QStringLiteral("unplayable"), tr("There is no playable Item at this target."));
            return;
        }
        prepareRuntimeScene(playlist, *item, true, false, {}, std::move(complete));
        return;
    }

    if (request.action != RuntimeAction::Seek)
    {
        startPreparedSequenceAdvance(request.action == RuntimeAction::Previous, false, std::move(complete));
        return;
    }

    const auto item = activeTimelineItemId();
    if (request.bindingToken != runtimeBindingToken_ || !item || request.itemId != item->value)
    {
        fail(QStringLiteral("stale"), tr("The active playback binding changed."));
        return;
    }
    if (request.value < 0 || request.value > TimelineSliderMaximum
        || !std::isfinite(request.duration) || request.duration <= 0.0)
    {
        fail(QStringLiteral("rejected"), tr("The timeline target is invalid."));
        return;
    }
    const auto duration = engine_.effectiveDuration();
    if (request.duration != duration)
    {
        fail(QStringLiteral("stale"), tr("The active playback duration changed."));
        return;
    }
    if (isTimelineEndIntent(request.value))
    {
        startPreparedSequenceAdvance(false, false, std::move(complete));
        return;
    }

    const auto media = engine_.prepareSeek(timelineTimeForValue(request.value, duration));
    if (!media)
    {
        fail(QStringLiteral("failed"), tr("The Engine could not seek this media."));
        return;
    }
    pendingRuntimeIntent_.emplace(PreparedRuntimeIntent{
        *media, runtimeBindingToken_, workspaceModel_.authoredRevision(),
        activePlaybackProjection_->sourcePlaylistId, *item, {}, {}, true, false, false, std::move(complete)});
}

void SessionApplication::prepareRuntimeScene(PlaylistId playlist, ItemId item, bool select, bool natural,
    std::optional<PlaybackSequence> sequence, RuntimeCompletion complete)
{
    const auto* authored = workspaceModel_.authoredPlaylist(playlist);
    std::optional<ActivePlaybackProjection> candidate;
    const ActivePlaybackProjection* projection = nullptr;
    if (!select && activePlaybackProjection_ && activePlaybackProjection_->sourcePlaylistId == playlist)
        projection = &*activePlaybackProjection_;
    else if (authored)
    {
        candidate = makeActivePlaybackProjection(*authored);
        if (candidate) projection = &*candidate;
    }
    const auto sceneIndex = projection ? projection->sceneIndexForItem(item) : std::nullopt;
    if (!sceneIndex || *sceneIndex >= projection->project.scenes.size())
    {
        if (natural) (void)engine_.consumeNaturalSceneCompletion();
        if (complete) complete({QStringLiteral("prepare-failed"), tr("Unable to prepare the playback sequence."),
            runtimePlaybackSnapshot(), std::nullopt});
        return;
    }
    const auto media = engine_.prepareSceneMedia(projection->project, projection->project.scenes[*sceneIndex].id);
    if (!media)
    {
        if (natural) (void)engine_.consumeNaturalSceneCompletion();
        if (complete) complete({QStringLiteral("bind-failed"), tr("Media could not be bound for playback."),
            runtimePlaybackSnapshot(), std::nullopt});
        return;
    }
    cancelTimelineDrag();
    pendingRuntimeIntent_.emplace(PreparedRuntimeIntent{*media, runtimeBindingToken_, workspaceModel_.authoredRevision(),
        playlist, item, std::move(candidate), std::move(sequence), false, select, natural, std::move(complete)});
}

void SessionApplication::startPreparedSequenceAdvance(bool previous, bool natural, RuntimeCompletion complete)
{
    const auto fail = [this, &complete](const QString& reason) {
        if (complete) complete({QStringLiteral("unavailable"), reason, runtimePlaybackSnapshot(), std::nullopt});
    };
    if (!activePlaybackProjection_ || pendingRuntimeIntent_)
    {
        if (natural && !activePlaybackProjection_) (void)engine_.consumeNaturalSceneCompletion();
        fail(tr("No active media is loaded."));
        return;
    }
    const auto collections = playbackSequenceCollections();
    synchronizePlaybackSequenceConfiguration(collections);
    if (!playbackSequence_.current()) rebasePlaybackSequenceToActivePosition();
    auto sequence = playbackSequence_;
    auto result = previous ? sequence.previous(collections) : sequence.completeCurrentItem(collections);
    if (!previous)
    {
        const auto attempts = (std::max)(std::size_t{1}, collections.playlistIds.size() * SequenceResolutionAttempts);
        for (std::size_t attempt = 1; attempt < attempts && !result.identity
            && result.kind != PlaybackSequenceResultKind::NoTarget; ++attempt)
            result = sequence.completeCurrentItem(collections);
    }
    if (result.identity)
    {
        const auto target = *result.identity;
        prepareRuntimeScene(target.playlistId, target.itemId, false, natural, std::move(sequence), std::move(complete));
        return;
    }
    if (!previous)
    {
        const auto effect = beginNativeAudioEffect(AudioEffectScope::Structural);
        if (!effect)
        {
            fail(tr("An audio effect is pending."));
            return;
        }
        if (natural) (void)engine_.consumeNaturalSceneCompletion();
        stopActivePlayback();
        (void)finishNativeAudioEffect(*effect);
    }
    fail(previous ? tr("There is no previous playback target.") : tr("There is no next playback target."));
}

void SessionApplication::servicePreparedRuntimeIntent()
{
    if (!pendingRuntimeIntent_) return;
    auto& waiting = *pendingRuntimeIntent_;
    const auto status = engine_.preparedMediaStatus(waiting.media);
    const bool current = waiting.binding == runtimeBindingToken_
        && waiting.authoredRevision == workspaceModel_.authoredRevision() && !closing();
    if (current && status == Engine::MediaPreparationStatus::Pending) return;
    if (current && status == Engine::MediaPreparationStatus::Failed && waiting.authored)
    {
        auto& plan = waiting.authored->plan;
        if (plan.mode == ActiveProjectionRebuildMode::RestoreCurrentItem && plan.position > 0 && !plan.reboundAtStart)
        {
            engine_.cancelMediaPreparation(waiting.media);
            plan.reboundAtStart = true;
            if (const auto fallback = requestAuthoredRebuildMedia(plan))
            {
                waiting.media = *fallback;
                return;
            }
        }
    }

    std::optional<AudioEffectToken> effect;
    if (current && status == Engine::MediaPreparationStatus::Ready)
    {
        effect = beginNativeAudioEffect(AudioEffectScope::Structural, waiting.playlist, waiting.item);
        if (!effect) return;
    }
    auto intent = std::move(*pendingRuntimeIntent_);
    pendingRuntimeIntent_.reset();
    if (!effect)
    {
        engine_.cancelMediaPreparation(intent.media);
        if (intent.natural && current) (void)engine_.consumeNaturalSceneCompletion();
        completePreparedRuntime(std::move(intent), current ? QStringLiteral("bind-failed") : QStringLiteral("stale"),
            current ? tr("Media could not be bound for playback.") : tr("The active playback binding changed."));
        return;
    }

    auto rollback = RollbackPresentation{intent.media, activePlaybackProjection_, playbackSequence_,
        workspaceModel_.activeItemId(), workspaceModel_.selectedPlaylistId(), workspaceModel_.selectedItemId(),
        intent.item, intent.selectItem, intent.seek, {}};
    bool applied = false;
    if (intent.seek)
        applied = engine_.commitPreparedSeek(intent.media);
    else
    {
        const auto* projection = intent.authored ? &intent.authored->plan.replacement
            : intent.replacement ? &*intent.replacement : &*activePlaybackProjection_;
        const auto sceneIndex = projection->sceneIndexForItem(intent.item);
        const bool hadActive = activePlaybackProjection_.has_value();
        const auto priorScope = activeBackgroundMusicScopeSnapshot();
        if (intent.natural) (void)engine_.consumeNaturalSceneCompletion();
        const auto* authoredPlan = intent.authored ? &intent.authored->plan : nullptr;
        const bool restore = authoredPlan
            && authoredPlan->mode == ActiveProjectionRebuildMode::RestoreCurrentItem;
        applied = sceneIndex && engine_.commitPreparedScene(projection->project,
            projection->project.scenes[*sceneIndex].id, intent.media,
            restore && authoredPlan->paused, restore ? std::optional{authoredPlan->rate} : std::nullopt);
        if (applied)
        {
            if (intent.authored) activePlaybackProjection_ = std::move(intent.authored->plan.replacement);
            else if (intent.replacement) activePlaybackProjection_ = std::move(intent.replacement);
            renewRuntimeBinding();
            if (intent.selectItem) (void)workspaceModel_.activateItem(intent.item);
            else (void)workspaceModel_.setActiveItem(intent.item);
            if (intent.sequence) playbackSequence_ = std::move(*intent.sequence);
            else if (!restore) rebasePlaybackSequenceToActivePosition();
            forwardBackgroundMusicScopeAfterStart(hadActive, priorScope);
            invalidateTimelineHoverPreview();
            updateEffectiveOutputVolume();
            synchronizeActivePlaybackVolumeCluster();
            refreshLibrary();
        }
    }
    if (!applied) engine_.cancelMediaPreparation(intent.media);
    else if (engine_.pendingMediaPresentation() == intent.media) rollbackPresentation_ = std::move(rollback);
    refreshStatus();
    completePreparedRuntime(std::move(intent), applied ? QStringLiteral("applied") : QStringLiteral("failed"),
        applied ? QString{} : tr("The Engine could not apply the prepared media."), effect);
}

void SessionApplication::acceptPresentationResult(Engine::MediaPreparationToken ticket, PresentationUploadResult result)
{
    requireExecutor();
    if (!rollbackPresentation_ || rollbackPresentation_->ticket != ticket || closing()) return;
    if (result != PresentationUploadResult::Updated && result != PresentationUploadResult::ResourceFailure) return;
    if (rollbackPresentation_->result != PresentationUploadResult::ResourceFailure)
        rollbackPresentation_->result = result;
    servicePresentationResult();
}

void SessionApplication::servicePresentationResult()
{
    if (!rollbackPresentation_ || !rollbackPresentation_->result) return;
    const auto currentScope = activeBackgroundMusicScopeSnapshot();
    const auto result = engine_.resolveMediaPresentation(
        rollbackPresentation_->ticket, *rollbackPresentation_->result);
    if (result == Engine::MediaPresentationResolution::Pending) return;
    auto rollback = std::exchange(rollbackPresentation_, std::nullopt);
    if (result != Engine::MediaPresentationResolution::RolledBack) return;

    cancelPreparedRuntimeIntent();
    renewRuntimeBinding();
    if (!rollback->seek)
    {
        if (rollback->changedSelection && workspaceModel_.selectedItemId() == rollback->committedItem)
        {
            if (rollback->priorSelectedItem) (void)workspaceModel_.selectItem(*rollback->priorSelectedItem);
            else (void)workspaceModel_.selectPlaylist(rollback->priorSelectedPlaylist);
        }
        if (!rollback->priorProjection || !rollback->priorActiveItem
            || !workspaceModel_.isItemPlayable(rollback->priorProjection->sourcePlaylistId, *rollback->priorActiveItem))
            stopActivePlayback();
        else
        {
            activePlaybackProjection_ = std::move(rollback->priorProjection);
            playbackSequence_ = std::move(rollback->priorSequence);
            (void)workspaceModel_.setActiveItem(*rollback->priorActiveItem);
            updateEffectiveOutputVolume();
            forwardBackgroundMusicScopeAfterStart(currentScope.has_value(), currentScope);
        }
    }
    refreshLibrary();
    refreshStatus();
    publishRuntimePlayback(true);
    notify(SessionNotice::Warning, tr("Unable to play media"), tr("The preview media could not be uploaded."));
}

void SessionApplication::completePreparedRuntime(PreparedRuntimeIntent intent, QString status, QString message,
    std::optional<AudioEffectToken> effect)
{
    if (intent.authored)
    {
        auto authored = std::move(*intent.authored);
        const bool startsTarget = authored.plan.mode == ActiveProjectionRebuildMode::StartTargetItem;
        auto rebuild = startsTarget ? ActiveProjectionRebuildResult::Stopped : ActiveProjectionRebuildResult::Unchanged;
        if (status == "applied")
            rebuild = startsTarget ? ActiveProjectionRebuildResult::StartedTarget
                : authored.plan.reboundAtStart ? ActiveProjectionRebuildResult::ReboundAtStart
                : ActiveProjectionRebuildResult::Restored;
        else if (startsTarget && status != "stale" && status != "cancelled")
            stopActivePlayback();
        completeAuthoredRebuild(std::move(authored), rebuild, effect);
        return;
    }
    std::optional<QJsonObject> receipt;
    if (effect)
    {
        const bool applied = status == QStringLiteral("applied");
        receipt = finishNativeAudioEffect(*effect, applied);
        if (applied)
        {
            const auto phase = receipt->value(QStringLiteral("phase")).toString();
            if (phase == "pending" || phase == "preparing") status = QStringLiteral("pending");
            else if (phase == "failed" || phase == "cancelled" || phase == "deferred-unavailable") status = phase;
        }
    }
    publishRuntimePlayback(true);
    if (intent.complete) intent.complete({std::move(status), std::move(message), runtimePlaybackSnapshot(), std::move(receipt)});
}

void SessionApplication::cancelPreparedRuntimeIntent()
{
    if (!pendingRuntimeIntent_) return;
    auto intent = std::move(*pendingRuntimeIntent_);
    pendingRuntimeIntent_.reset();
    engine_.cancelMediaPreparation(intent.media);
    completePreparedRuntime(std::move(intent), QStringLiteral("cancelled"), tr("The pending playback operation was retired."));
}
}
