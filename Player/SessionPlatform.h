#pragma once

#include "DesktopDeployment.h"
#include "SessionAuthoredOperation.h"
#include <DesktopSceneEngine/Engine.h>
#include <DesktopSceneEngine/Platform/Display.h>
#include <QJsonObject>
#include <QStringList>
#include <QUuid>
#include <filesystem>
#include <functional>
#include <optional>

namespace DesktopSceneEngine::Player
{
enum class UiLanguage;
enum class SessionUnsavedChoice { Save, Discard, Cancel };
struct SessionPlatformRequest
{
    QUuid operation;
    QUuid dispatch;

    friend bool operator==(const SessionPlatformRequest&, const SessionPlatformRequest&) = default;
};
enum class SessionPlatformRetirement { AwaitResult, AwaitAcknowledgment };
enum class SessionNotice { Information, Warning, Error };
enum class SessionViewChange
{
    DocumentTitle, CanvasAspect, Library, PlaylistList, ItemList, Inspector,
    Status, ActivePlaybackVolume, BoundDisplay, DesktopDeployment, BgmPlayback,
    ReadSnapshot, ScalarReadSnapshot, RuntimePlayback, RuntimeSample, AudioEffects, CancelTimelineDrag,
    InvalidateTimeline, InvalidateTimelinePreload, ReconcileTimelinePreload,
    ReconcileTimelineSource, CancelRuntimeHover, DiscardAudioDrafts,
    DiscardDocumentDrafts, DiscardSessionDrafts
};
enum class SessionDesktopAttempt { Ready, TransientlyUnavailable, Failed };
struct SessionPlatformFacts
{
    bool visible = false;
    bool minimized = false;
    bool maximized = false;
    bool hideEligible = false;
    bool hiddenToTray = false;
    bool presentationEnding = false;
    bool closeInteractionPending = false;
    bool desktopRetained = false;
    bool desktopTargetRegistered = false;
    bool desktopHostOwned = false;
    bool desktopHealthy = false;
    friend bool operator==(const SessionPlatformFacts&, const SessionPlatformFacts&) = default;
};

struct SessionDesktopResult
{
    SessionDesktopAttempt attempt = SessionDesktopAttempt::Failed;
    std::optional<SessionPlatformFacts> facts;
};

struct SessionDesktopFrame
{
    PresentationInput input;
    CanvasOutsideMode outside = CanvasOutsideMode::Opaque;
};

// Window/native facts and presentation effects only. No writable application
// state, admitted operation, permit, or playback projection lives in this port.
class SessionPlatform
{
public:
    virtual ~SessionPlatform() = default;
    [[nodiscard]] virtual SessionPlatformFacts sessionPlatformFacts() const = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> pickSessionDocumentPath(
        bool save, const std::optional<std::filesystem::path>& current) = 0;
    [[nodiscard]] virtual QStringList pickSessionMediaSources(bool backgroundMusic) = 0;
    [[nodiscard]] virtual SessionUnsavedChoice promptSessionUnsavedChanges(const QString& displayName) = 0;
    // Operation continuations run on the Session control executor. A
    // presentation adapter may complete inline or dispatch the native work and
    // return its result asynchronously. The operation token survives adapter
    // replacement. The dispatch token identifies one native interaction.
    using DocumentPathResult = std::function<void(std::optional<std::filesystem::path>)>;
    using MediaSourcesResult = std::function<void(QStringList)>;
    using RequestRetired = std::function<void()>;
    virtual void requestSessionDocumentPath(const SessionPlatformRequest&, bool save,
        const std::optional<std::filesystem::path>& current, DocumentPathResult complete)
    { complete(pickSessionDocumentPath(save, current)); }
    virtual void requestSessionMediaSources(const SessionPlatformRequest&, bool backgroundMusic, MediaSourcesResult complete)
    { complete(pickSessionMediaSources(backgroundMusic)); }
    // Surface a hidden role only when its admitted Close requires interaction.
    virtual void surfaceSessionCloseDecision() {}
    // An inline picker may still be unwinding its nested event loop. Its
    // result signals that it is gone. An asynchronous adapter must override
    // this method and acknowledge on control after the retiring dispatch has
    // gone or been fenced before launch, even if its result was already queued.
    [[nodiscard]] virtual SessionPlatformRetirement retireSessionPlatformRequest(
        const SessionPlatformRequest&, RequestRetired) noexcept
    { return SessionPlatformRetirement::AwaitResult; }
    using LanguageApplied = std::function<void(std::optional<UiLanguage>)>;
    virtual void applySessionLanguage(UiLanguage, LanguageApplied complete) { complete(std::nullopt); }
    virtual void showSessionNotice(SessionNotice kind, const QString& title, const QString& message) = 0;
    virtual void sessionViewChanged(SessionViewChange change, bool immediate = false) = 0;
    virtual void acknowledgeSessionRevision(std::uint64_t before, bool playlistMaster) = 0;
    virtual void refreshSessionBgmWindow(PlaylistId playlist) = 0;
    virtual void closeSessionBgmWindow(PlaylistId playlist) = 0;
    virtual void closeAllSessionBgmWindows() = 0;
    // Import focus is adapter state keyed by the application operation token.
    virtual void completeSessionMediaImport(const QUuid& token) = 0;
    // These create/release borrowed render targets; Engine runtime is separate.
    [[nodiscard]] virtual bool initializeSessionPresentation(Engine& engine) = 0;
    virtual void detachSessionPresentation(Engine& engine) noexcept = 0;
    virtual void prepareSessionEngineShutdown(Engine& engine) noexcept = 0;
    virtual void finishSessionEngineShutdown() noexcept = 0;
    [[nodiscard]] virtual bool sessionRuntimePresentationReady() const = 0;
    virtual void renderSession(Engine& engine, const Domain::AspectRatio& aspect, CanvasOutsideMode outside) = 0;
    virtual void publishDetachedFrame(PresentationInput, const QString&, CanvasOutsideMode,
        std::optional<Engine::MediaPreparationToken>) {}
    virtual void observePresentationFacts(SessionPlatformFacts) {}
    [[nodiscard]] virtual SessionDesktopAttempt deploySessionDesktop(Engine& engine, const DisplayId& display) = 0;
    [[nodiscard]] virtual bool removeSessionDesktop(Engine& engine) noexcept = 0;
    [[nodiscard]] virtual bool reconcileSessionDesktop(Engine& engine, const DisplayId& display) = 0;
    // Detached target effects run on GUI. The accepted completion returns once
    // on control with observed HWND/graphics ownership facts.
    using DesktopResult = std::function<void(SessionDesktopResult)>;
    // A frame requests deployment, including when it has no active media.
    // An absent frame requests removal.
    [[nodiscard]] virtual bool requestDetachedDesktop(DisplayId, std::optional<SessionDesktopFrame>, DesktopResult) { return false; }
    virtual void beginSessionPresentationTeardown() = 0;
    virtual void requestSessionFinalClose() = 0;
    virtual void sessionCloseCompleted(bool accepted) = 0;
    virtual void sessionExternalCloseSaveFailed(const QString&) { sessionCloseCompleted(false); }
};
}
