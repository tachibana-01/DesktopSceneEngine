#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <optional>
#include <DesktopSceneEngine/DisplayIdentity.h>
#include "LauncherLanguageProjection.h"

namespace DesktopSceneEngine::Player::LauncherBridge
{
inline constexpr int Version = 1;
inline constexpr auto SnapshotCommand = "GetLauncherBootstrapSnapshot";
inline constexpr auto RefreshCommand = "RefreshDisplays";
inline constexpr auto IdentifyCommand = "IdentifyDisplay";
inline constexpr auto LanguageCommand = "CommitLanguage";
inline constexpr auto CreateCommand = "CreateSession";
inline constexpr auto OpenCommand = "OpenSession";
inline constexpr auto DocumentPrefix = "https://launcher.dse.local/index.html?mode=production&generation=";

enum class Command { Bootstrap, RefreshDisplays, IdentifyDisplay, CommitLanguage, CreateSession, OpenSession };
[[nodiscard]] const char* commandName(Command command);

struct Request
{
    QString requestId;
    QString generation;
    Command type = Command::Bootstrap;
    std::optional<DisplayId> displayId;
    std::optional<UiLanguage> language;
    std::optional<QUuid> coordinatorEpoch;
    std::optional<QUuid> incarnation;
};

struct ParsedRequest
{
    std::optional<Request> request;
    QString requestId;
    QString error;
    Command type = Command::Bootstrap;
};

// The source URI is checked before parsing. No origin-only or development URL fallback.
[[nodiscard]] ParsedRequest parseRequest(const QByteArray& json, const QString& source,
    const QString& currentDocument, const QString& generation);
[[nodiscard]] QJsonObject result(const Request& request, const QJsonObject& payload);
[[nodiscard]] QJsonObject failure(const QString& requestId, const QString& generation, const QString& error,
    Command type = Command::Bootstrap);

enum class NavigationDisposition { Allow, RestartWithFreshGeneration, Reject };

// Each accepted document, including Reload, receives a new native generation in its URL.
class PageFence
{
public:
    void prepare();
    // Loading/error presentation hint only; affects the next document, never persistence.
    void setPresentationLanguage(UiLanguage language);
    [[nodiscard]] NavigationDisposition navigationStarting(const QString& uri);
    [[nodiscard]] const QString& generation() const { return generation_; }
    [[nodiscard]] const QString& document() const { return document_; }
    void close();
    [[nodiscard]] bool closed() const { return closed_; }
private:
    QString generation_;
    QString document_;
    bool prepared_ = false;
    bool closed_ = false;
    QString presentationLanguage_;
};

enum class CreationDisposition { Adopt, RejectClosed, FactoryFailed };
// A callback can run before the factory returns. Adoption waits for both facts.
class CreationOperation
{
public:
    bool callbackArrived();
    void factoryReturned(bool success);
    [[nodiscard]] std::optional<CreationDisposition> consume(bool closed);
private:
    bool returned_ = false;
    bool callback_ = false;
    bool failed_ = false;
    bool settled_ = false;
};
}
