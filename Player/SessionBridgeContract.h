#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <atomic>
#include <memory>
#include <optional>
#include <DesktopSceneEngine/PresentationTarget.h>
#include "DesktopDeployment.h"
#include "PreviewGeometry.h"
#include "SessionDocumentTransaction.h"
#include "SessionAuthoredOperation.h"
#include "SessionRuntimeOperation.h"
#include "TimelineHoverPresentationGeometry.h"

namespace DesktopSceneEngine::Player::SessionBridge
{
inline constexpr int Version = 1;
// Transport bound, not a limit on the authoritative document/model. Oversized
// projections produce an explicit failure rather than an unbounded Web message.
inline constexpr qsizetype MaximumResponseBytes = 4 * 1024 * 1024;
inline constexpr auto SnapshotCommand = "GetMainWindowSnapshot";
inline constexpr auto SnapshotInvalidated = "MainWindowSnapshotInvalidated";
inline constexpr auto PreviewRectCommand = "SetPreviewRect";
inline constexpr auto PreviewGeometryInvalidated = "PreviewGeometryInvalidated";
inline constexpr auto PreviewPresentationState = "PreviewPresentationState";
inline constexpr auto PresentTimelineHoverCommand = "PresentTimelineHover";
inline constexpr auto NewDocumentCommand = "NewDocument";
inline constexpr auto OpenDocumentCommand = "OpenDocument";
inline constexpr auto SaveDocumentCommand = "SaveDocument";
inline constexpr auto SaveDocumentAsCommand = "SaveDocumentAs";
inline constexpr auto RequestCloseCommand = "RequestClose";
inline constexpr auto ContinueDocumentCommand = "ContinueDocumentDecision";
inline constexpr auto NegotiateAudioEffectsCommand = "NegotiateAudioEffects";
inline constexpr auto AudioEffectsCommand = "GetAudioEffects";
inline constexpr auto SetCanvasBoundaryHighlightCommand = "SetCanvasBoundaryHighlight";
inline constexpr auto SetDesktopBackgroundCommand = "SetDesktopBackground";
inline constexpr auto IdentifyBoundDisplayCommand = "IdentifyBoundDisplay";
inline constexpr auto AcknowledgeSessionNoticeCommand = "AcknowledgeSessionNotice";
inline constexpr auto DeployDesktopCommand = "DeployDesktop";
inline constexpr auto UndeployDesktopCommand = "UndeployDesktop";
inline constexpr auto DocumentPrefix = "https://session.dse.local/index.html?mode=production&view=session&generation=";
inline constexpr auto BgmDocumentPrefix = "https://session.dse.local/index.html?mode=production&view=background-music&generation=";

struct Request
{
    QString requestId;
    QString generation;
    QString sessionIncarnation;
    QString type = QString::fromLatin1(SnapshotCommand);
    std::optional<PreviewGeometry::Intent> preview;
    std::optional<DocumentAction> documentAction;
    QUuid documentToken;
    std::optional<DocumentChoice> documentChoice;
    std::optional<AuthoredRequest> authored;
    std::optional<RuntimeRequest> runtime;
    std::optional<bool> canvasBoundaryHighlight;
    std::optional<CanvasOutsideMode> desktopBackground;
    bool identifyBoundDisplay = false;
    std::optional<QUuid> acknowledgeSessionNotice;
    std::optional<DesktopDeploymentIntent> desktopDeployment;
    std::optional<TimelineHoverRequest> hover;
    std::optional<quint32> cancelHoverSerial;
    std::optional<TimelineHoverPresentation::Intent> hoverPresentation;
    std::optional<BgmRuntimeRequest> bgmRuntime;
    std::optional<PlaylistId> openBgmEditor;
    std::optional<BgmAuthoredRequest> bgmAuthored;
    bool bgmEditorRead = false;
    std::optional<ItemId> bgmEditorItem;
    bool closeBgmEditor = false;
    bool negotiateAudioEffects = false;
    bool readAudioEffects = false;
    QUuid audioDocument;
    // Native transport lifetime. The parser never takes it from page data.
    std::shared_ptr<const std::atomic_bool> pageAdmission;
};

struct ParsedRequest
{
    std::optional<Request> request;
    QString requestId;
    QString error;
    QString type = QString::fromLatin1(SnapshotCommand);
};

enum class Surface { Main, BackgroundMusic };

// The native Host selects the surface. An origin match or envelope identifier
// cannot grant access to the commands of another surface.
[[nodiscard]] ParsedRequest parseRequest(const QByteArray& json, const QString& source,
    const QString& currentDocument, const QString& generation, const QUuid& sessionIncarnation,
    Surface surface = Surface::Main);
[[nodiscard]] QJsonObject result(const Request& request, const QJsonObject& snapshot);
[[nodiscard]] QJsonObject failure(const QString& requestId, const QString& generation,
    const QUuid& sessionIncarnation, const QString& error, const QString& type = QString::fromLatin1(SnapshotCommand));
[[nodiscard]] QJsonObject invalidated(const QString& generation, const QUuid& sessionIncarnation);
[[nodiscard]] QJsonObject previewGeometryInvalidated(const QString& generation, const QUuid& sessionIncarnation);
[[nodiscard]] QJsonObject documentResult(const Request& request, const DocumentCommandResult& command);
[[nodiscard]] QJsonObject authoredPayload(const AuthoredResult& command);
[[nodiscard]] QJsonObject bgmRuntimePayload(const BgmRuntimeResult& command);

enum class NavigationDisposition { Allow, RestartWithFreshGeneration, Reject };

// The Session role/incarnation is immutable; only the page generation rotates.
class PageFence
{
public:
    explicit PageFence(const QUuid& sessionIncarnation, Surface surface = Surface::Main);
    void prepare();
    [[nodiscard]] NavigationDisposition navigationStarting(const QString& uri);
    [[nodiscard]] const QString& generation() const { return generation_; }
    [[nodiscard]] const QString& document() const { return document_; }
    [[nodiscard]] const QUuid& sessionIncarnation() const { return sessionIncarnation_; }
    void close();
    [[nodiscard]] bool closed() const { return closed_; }
private:
    QUuid sessionIncarnation_;
    QString generation_;
    QString document_;
    bool prepared_ = false;
    bool closed_ = false;
    Surface surface_;
};
}
