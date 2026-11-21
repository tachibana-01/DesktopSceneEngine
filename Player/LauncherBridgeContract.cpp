#include "LauncherBridgeContract.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QUuid>

namespace DesktopSceneEngine::Player::LauncherBridge
{
namespace
{
bool validRequestId(const QJsonValue& value)
{
    if (!value.isString()) return false;
    const auto id = value.toString();
    if (id.isEmpty() || id.size() > 128) return false;
    for (const auto character : id)
        if (character.unicode() < 0x21 || character.unicode() > 0x7e) return false;
    return true;
}

std::optional<QUuid> readUuid(const QJsonValue& value)
{
    if (!value.isString()) return std::nullopt;
    const auto text = value.toString();
    const QUuid uuid{text};
    if (uuid.isNull() || (text.compare(uuid.toString(QUuid::WithoutBraces), Qt::CaseInsensitive) != 0
        && text.compare(uuid.toString(QUuid::WithBraces), Qt::CaseInsensitive) != 0)) return std::nullopt;
    return uuid;
}

int memberCountAtDepth(const QByteArray& json, int memberDepth)
{
    // QJsonObject collapses duplicate keys. Count syntactic root members as well, after
    // QJsonDocument validated the complete JSON grammar, to reject duplicate envelopes.
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

QJsonObject envelope(const QString& requestId, const QString& generation, bool ok, Command type)
{
    return {{"kind", "result"}, {"version", Version}, {"requestId", requestId},
        {"type", commandName(type)}, {"generation", generation}, {"ok", ok}};
}
}

const char* commandName(Command command)
{
    switch (command)
    {
    case Command::Bootstrap: return SnapshotCommand;
    case Command::RefreshDisplays: return RefreshCommand;
    case Command::IdentifyDisplay: return IdentifyCommand;
    case Command::CommitLanguage: return LanguageCommand;
    case Command::CreateSession: return CreateCommand;
    case Command::OpenSession: return OpenCommand;
    }
    return "";
}

ParsedRequest parseRequest(const QByteArray& json, const QString& source,
    const QString& currentDocument, const QString& generation)
{
    if (generation.isEmpty() || currentDocument.isEmpty() || source != currentDocument)
        return {{}, {}, "untrusted-or-stale-document"};
    if (json.isEmpty() || json.size() > 16 * 1024)
        return {{}, {}, "invalid-message-size"};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {{}, {}, "invalid-json-object"};
    const auto object = document.object();
    const auto requestId = validRequestId(object.value("requestId")) ? object.value("requestId").toString() : QString{};
    const auto type = object.value("type") == QJsonValue{CreateCommand} ? Command::CreateSession :
        object.value("type") == QJsonValue{OpenCommand} ? Command::OpenSession :
        object.value("type") == QJsonValue{LanguageCommand} ? Command::CommitLanguage :
        object.value("type") == QJsonValue{IdentifyCommand} ? Command::IdentifyDisplay :
        object.value("type") == QJsonValue{RefreshCommand} ? Command::RefreshDisplays : Command::Bootstrap;
    const auto reject = [&](const char* message) { return ParsedRequest{{}, requestId, message, type}; };
    const QSet<QString> expected{"kind", "version", "requestId", "type", "generation", "payload"};
    const auto keys = object.keys();
    if (memberCountAtDepth(json, 1) != expected.size() || QSet<QString>{keys.begin(), keys.end()} != expected)
        return reject("invalid-envelope-fields");
    if (object.value("kind") != QJsonValue{"command"} || object.value("version") != QJsonValue{Version}
        || requestId.isEmpty() || (object.value("type") != QJsonValue{SnapshotCommand}
            && object.value("type") != QJsonValue{RefreshCommand} && object.value("type") != QJsonValue{IdentifyCommand}
            && object.value("type") != QJsonValue{LanguageCommand} && object.value("type") != QJsonValue{CreateCommand}
            && object.value("type") != QJsonValue{OpenCommand}))
        return reject("invalid-command-envelope");
    if (!object.value("generation").isString() || object.value("generation").toString() != generation)
        return reject("stale-generation");
    if (!object.value("payload").isObject())
        return reject("invalid-payload");
    const auto payload = object.value("payload").toObject();
    if (type == Command::CreateSession || type == Command::OpenSession)
    {
        const auto epoch = readUuid(payload.value("coordinatorEpoch"));
        if (payload.size() != 2 || memberCountAtDepth(json, 2) != 2 || !epoch)
            return reject("invalid-session-command-payload");
        if (type == Command::OpenSession)
        {
            const auto incarnation = readUuid(payload.value("incarnation"));
            if (!incarnation) return reject("invalid-session-incarnation");
            return {Request{requestId, generation, type, {}, {}, *epoch, *incarnation}, requestId, {}, type};
        }
        if (!payload.value("displayId").isString()) return reject("invalid-display-id");
        const auto text = payload.value("displayId").toString();
        const auto canonical = text.toUtf8().toStdString();
        const auto id = DisplayId::parseCanonicalRepresentation(canonical);
        if (QString::fromStdString(canonical) != text || !id || id->canonicalRepresentation() != canonical)
            return reject("invalid-display-id");
        // A canonical weak identity reaches the native capability check; the
        // transport must not promote it or confuse rejection with a parse error.
        return {Request{requestId, generation, type, *id, {}, *epoch}, requestId, {}, type};
    }
    if (type == Command::CommitLanguage)
    {
        if (payload.size() != 1 || memberCountAtDepth(json, 2) != 1 || !payload.value("language").isString())
            return reject("invalid-language-payload");
        const auto language = parseLauncherLanguage(payload.value("language").toString());
        if (!language) return reject("invalid-language");
        return {Request{requestId, generation, type, {}, *language}, requestId, {}, type};
    }
    if (type == Command::IdentifyDisplay)
    {
        if (payload.size() != 1 || memberCountAtDepth(json, 2) != 1 || !payload.value("displayId").isString())
            return reject("invalid-identify-payload");
        const auto text = payload.value("displayId").toString();
        const auto canonical = text.toUtf8().toStdString();
        if (QString::fromStdString(canonical) != text) return reject("invalid-display-id");
        const auto id = DisplayId::parseCanonicalRepresentation(canonical);
        if (!id || id->canonicalRepresentation() != canonical)
            return reject("invalid-display-id");
        return {Request{requestId, generation, type, *id}, requestId, {}, type};
    }
    if (!payload.isEmpty()) return reject("invalid-payload");
    return {Request{requestId, generation, type}, requestId, {}, type};
}

QJsonObject result(const Request& request, const QJsonObject& payload)
{
    auto object = envelope(request.requestId, request.generation, true, request.type);
    object.insert("payload", payload);
    return object;
}

QJsonObject failure(const QString& requestId, const QString& generation, const QString& error, Command type)
{
    auto object = envelope(requestId, generation, false, type);
    object.insert("error", error);
    return object;
}

void PageFence::prepare()
{
    if (closed_) return;
    generation_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    document_ = QString::fromLatin1(DocumentPrefix) + generation_;
    if (!presentationLanguage_.isEmpty()) document_ += QStringLiteral("&language=") + presentationLanguage_;
    prepared_ = true;
}

void PageFence::setPresentationLanguage(UiLanguage language)
{
    presentationLanguage_ = launcherLanguageCode(language);
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

bool CreationOperation::callbackArrived()
{
    if (callback_ || settled_) return false;
    callback_ = true;
    return true;
}

void CreationOperation::factoryReturned(bool success)
{
    returned_ = true;
    failed_ = !success;
}

std::optional<CreationDisposition> CreationOperation::consume(bool closed)
{
    if (settled_ || !returned_ || (!callback_ && !failed_)) return {};
    settled_ = true;
    if (failed_) return CreationDisposition::FactoryFailed;
    return closed ? CreationDisposition::RejectClosed : CreationDisposition::Adopt;
}
}
