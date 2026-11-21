#include "LauncherBridgeContract.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>

using namespace DesktopSceneEngine::Player::LauncherBridge;

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    int checks = 0;
    int failures = 0;
    int a2Checks = 0;
    int a3Checks = 0;
    int a4Checks = 0;
    const auto check = [&](bool accepted, const char* label) {
        ++checks;
        if (!accepted) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
    };
    const auto checkA2 = [&](bool accepted, const char* label) { ++a2Checks; check(accepted, label); };
    const auto checkA3 = [&](bool accepted, const char* label) { ++a3Checks; check(accepted, label); };
    const auto checkA4 = [&](bool accepted, const char* label) { ++a4Checks; check(accepted, label); };
    PageFence page;
    page.prepare();
    const auto originalGeneration = page.generation();
    const auto originalDocument = page.document();
    check(!originalGeneration.isEmpty() && originalDocument.endsWith(originalGeneration), "native page generation");
    check(page.navigationStarting(originalDocument) == NavigationDisposition::Allow, "initial navigation allowed");
    QJsonObject request{{"kind", "command"}, {"version", Version}, {"requestId", "bootstrap:1"},
        {"type", SnapshotCommand}, {"generation", originalGeneration}, {"payload", QJsonObject{}}};
    const auto serialize = [](const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); };
    const auto parse = [&](const QJsonObject& value) {
        return parseRequest(serialize(value), originalDocument, originalDocument, originalGeneration);
    };
    const auto parsed = parse(request);
    check(parsed.request.has_value(), "exact read-only command accepted");
    const auto commandEpoch = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto commandIncarnation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto create = request;
    create.insert("type", "CreateSession");
    create.insert("payload", QJsonObject{{"coordinatorEpoch", commandEpoch},
        {"displayId", "dse-display-id:v1:target-device-path:7:panel-a"}});
    check(parse(create).request.has_value(), "A6 canonical Create command accepted");
    auto open = request;
    open.insert("type", "OpenSession");
    open.insert("payload", QJsonObject{{"coordinatorEpoch", commandEpoch}, {"incarnation", commandIncarnation}});
    check(parse(open).request.has_value(), "A6 exact-incarnation Open command accepted");
    const auto created = parse(create);
    const auto opened = parse(open);
    check(created.request && created.request->coordinatorEpoch == QUuid{commandEpoch}
        && created.request->displayId->canonicalRepresentation() == "dse-display-id:v1:target-device-path:7:panel-a"
        && !created.request->incarnation, "A6 Create keeps epoch and exact canonical Display independently");
    check(opened.request && opened.request->coordinatorEpoch == QUuid{commandEpoch}
        && opened.request->incarnation == QUuid{commandIncarnation} && !opened.request->displayId,
        "A6 Open carries exact incarnation, never Display-based retargeting");
    for (const auto& command : {create, open})
    {
        auto invalid = command;
        invalid.insert("generation", "retired-create-open-page");
        check(!parse(invalid).request, "A6 stale command generation rejected");
        check(!parseRequest(serialize(command), "http://127.0.0.1:5173/", originalDocument, originalGeneration).request,
            "A6 development origin cannot invoke production lifecycle commands");
        for (const auto& invalidEpoch : {QJsonValue{}, QJsonValue{1}, QJsonValue{""}, QJsonValue{"not-an-epoch"},
            QJsonValue{"00000000-0000-0000-0000-000000000000"}, QJsonValue{QString{commandEpoch}.remove('-')}})
        {
            auto payload = command["payload"].toObject();
            payload.insert("coordinatorEpoch", invalidEpoch);
            invalid = command;
            invalid.insert("payload", payload);
            check(!parse(invalid).request, "A6 malformed/null epoch rejected before execution");
        }
        auto payload = command["payload"].toObject();
        payload.insert("fallbackToCreate", true);
        invalid = command;
        invalid.insert("payload", payload);
        check(!parse(invalid).request, "A6 extra mutation/fallback flags rejected");
        auto duplicateEpoch = serialize(command);
        duplicateEpoch.replace("\"coordinatorEpoch\":", "\"coordinatorEpoch\":\"ignored\",\"coordinatorEpoch\":");
        check(!parseRequest(duplicateEpoch, originalDocument, originalDocument, originalGeneration).request,
            "A6 duplicate epoch key rejected");
        const auto type = command["type"] == "CreateSession" ? Command::CreateSession : Command::OpenSession;
        check(failure(command["requestId"].toString(), originalGeneration, "provider-failed", type)["ok"] == false,
            "A6 transport failure remains a separate envelope from native capability result");
    }
    for (const auto& invalidIncarnation : {QJsonValue{}, QJsonValue{""}, QJsonValue{17}, QJsonValue{"DISPLAY1"},
        QJsonValue{"00000000-0000-0000-0000-000000000000"}})
    {
        auto invalid = open;
        invalid.insert("payload", QJsonObject{{"coordinatorEpoch", commandEpoch}, {"incarnation", invalidIncarnation}});
        check(!parse(invalid).request, "A6 Open requires exact non-null UUID incarnation");
    }
    auto wrongOpenTarget = open;
    wrongOpenTarget.insert("payload", QJsonObject{{"coordinatorEpoch", commandEpoch},
        {"displayId", "dse-display-id:v1:target-device-path:7:panel-a"}});
    check(!parse(wrongOpenTarget).request, "A6 Open cannot target a Display");
    auto weakCreate = create;
    weakCreate.insert("payload", QJsonObject{{"coordinatorEpoch", commandEpoch}, {"displayId",
        QString::fromStdString(*DesktopSceneEngine::DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY2").canonicalRepresentation())}});
    const auto parsedWeakCreate = parse(weakCreate);
    check(parsedWeakCreate.request && !parsedWeakCreate.request->displayId->isPersistentOwnerCapable(),
        "A6 weak canonical Create target reaches explicit native capability rejection without promotion");
    auto language = request;
    language.insert("type", "CommitLanguage");
    language.insert("payload", QJsonObject{{"language", "zh_CN"}});
    const auto parsedLanguage = parse(language);
    checkA4(parsedLanguage.request.has_value(), "A4 CommitLanguage canonical Chinese accepted");
    for (const auto& payload : {QJsonObject{}, QJsonObject{{"language", "zh-CN"}},
        QJsonObject{{"language", "EN"}}, QJsonObject{{"language", "fr"}},
        QJsonObject{{"language", 1}}, QJsonObject{{"language", QJsonArray{"en"}}},
        QJsonObject{{"language", "en"}, {"save", true}}})
    {
        auto invalid = language;
        invalid.insert("payload", payload);
        checkA4(!parse(invalid).request, "A4 invalid/noncanonical language rejected before capability");
    }
    auto duplicateLanguage = serialize(language);
    duplicateLanguage.replace("\"language\":", "\"language\":\"en\",\"language\":");
    checkA4(!parseRequest(duplicateLanguage, originalDocument, originalDocument, originalGeneration).request,
        "A4 duplicate language payload rejected");
    auto staleLanguage = language;
    staleLanguage.insert("generation", "retired-language-page");
    checkA4(!parse(staleLanguage).request, "A4 stale language request rejected");
    checkA4(!parseRequest(serialize(language), "http://127.0.0.1:5173/", originalDocument, originalGeneration).request,
        "A4 untrusted language request rejected");
    if (parsedLanguage.request)
    {
        checkA4(parsedLanguage.request->language == DesktopSceneEngine::Player::UiLanguage::SimplifiedChinese,
            "A4 canonical payload parses to native enum");
        for (const auto status : {"UNCHANGED", "READ_FAILURE", "SAVE_FAILURE", "COMMITTED"})
        {
            const QJsonObject payload{{"status", status},
                {"language", QJsonObject{{"readStatus", "Success"}, {"committed", "zh_CN"}, {"effective", "en"}, {"revision", 4}}},
                {"warnings", QJsonArray{"TranslationFallback", "NotificationFailure"}}};
            const auto response = result(*parsedLanguage.request, payload);
            checkA4(response["ok"] == true && response["type"] == "CommitLanguage"
                && response["requestId"] == language["requestId"] && response["generation"] == originalGeneration
                && response["payload"].toObject() == payload,
                "A4 transport preserves capability status, separate truth and post-commit warnings");
        }
        language.insert("payload", QJsonObject{{"language", "en"}});
        checkA4(parse(language).request.has_value(), "A4 CommitLanguage canonical English accepted");
    }
    auto identify = request;
    identify.insert("type", "IdentifyDisplay");
    identify.insert("payload", QJsonObject{{"displayId", "dse-display-id:v1:target-device-path:7:panel-a"}});
    const auto parsedIdentify = parse(identify);
    checkA3(parsedIdentify.request.has_value() && parsedIdentify.request->displayId.has_value(), "A3 typed exact canonical IdentifyDisplay accepted");
    if (parsedIdentify.request)
    {
        checkA3(parsedIdentify.request->displayId->canonicalRepresentation()
            == "dse-display-id:v1:target-device-path:7:panel-a", "A3 parsed target identity remains exact");
        const auto message = result(*parsedIdentify.request, {{"displayId", "dse-display-id:v1:target-device-path:7:panel-a"}, {"status", "Shown"}});
        checkA3(message["type"] == "IdentifyDisplay" && message["requestId"] == identify["requestId"]
            && message["generation"] == originalGeneration && message["ok"].toBool(), "A3 Identify result correlation");
    }
    auto weakIdentify = identify;
    const auto weakCanonical = QString::fromStdString(*DesktopSceneEngine::DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY2").canonicalRepresentation());
    weakIdentify.insert("payload", QJsonObject{{"displayId", weakCanonical}});
    const auto parsedWeak = parse(weakIdentify);
    checkA3(parsedWeak.request && parsedWeak.request->displayId && !parsedWeak.request->displayId->isPersistentOwnerCapable(),
        "A3 valid weak identity is accepted independently of owner capability");
    const auto unicode = QString::fromStdString(*DesktopSceneEngine::DisplayId::fromTargetDevicePath("target:\xe5\xb1\x8f").canonicalRepresentation());
    auto unicodeIdentify = identify;
    unicodeIdentify.insert("payload", QJsonObject{{"displayId", unicode}});
    checkA3(parse(unicodeIdentify).request.has_value(), "A3 UTF-8 byte length identity is parsed by native code");
    for (const auto& payload : {QJsonObject{}, QJsonObject{{"displayId", 1}}, QJsonObject{{"displayId", QJsonArray{"id"}}},
        QJsonObject{{"displayId", ""}}, QJsonObject{{"displayId", "DISPLAY 1"}},
        QJsonObject{{"displayId", "dse-display-id:v1:target-device-path:99:panel-a"}},
        QJsonObject{{"displayId", "dse-display-id:v1:target-device-path:7:panel-a"}, {"duration", 1}},
        QJsonObject{{"displayId", "dse-display-id:v1:target-device-path:7:panel-a"}, {"fallbackToPrimary", true}}})
    {
        auto invalid = identify;
        invalid.insert("payload", payload);
        const auto rejected = parse(invalid);
        checkA3(!rejected.request && rejected.type == Command::IdentifyDisplay,
            "A3 malformed Identify payload rejected before capability dispatch");
    }
    auto duplicatePayload = QJsonDocument(identify).toJson(QJsonDocument::Compact);
    duplicatePayload.replace("\"displayId\":", "\"displayId\":\"ignored\",\"displayId\":");
    checkA3(!parseRequest(duplicatePayload, originalDocument, originalDocument, originalGeneration).request,
        "A3 duplicate target field rejected");
    auto invalidGeneration = identify;
    invalidGeneration.insert("generation", "retired-identify-page");
    checkA3(!parse(invalidGeneration).request, "A3 stale Identify generation rejected");
    checkA3(!parseRequest(QJsonDocument(identify).toJson(), "https://example.com/", originalDocument, originalGeneration).request,
        "A3 untrusted Identify source rejected");
    auto refresh = request;
    refresh.insert("type", "RefreshDisplays");
    const auto parsedRefresh = parse(refresh);
    checkA2(parsedRefresh.request.has_value(), "A2 RefreshDisplays typed command accepted");
    if (parsedRefresh.request)
        checkA2(result(*parsedRefresh.request, {{"status", "Success"}, {"displays", QJsonArray{}}})["type"]
            == "RefreshDisplays", "A2 Refresh result echoes command type");
    for (const auto status : {"Success", "ApiFailure", "IncompletePathMetadata", "DuplicateIdentity"})
    {
        if (!parsedRefresh.request) break;
        const QJsonObject catalog{{"status", status}, {"displays", QJsonArray{}}};
        const auto message = result(*parsedRefresh.request, catalog);
        checkA2(message["ok"].toBool() && message["payload"].toObject() == catalog
            && message["requestId"] == request["requestId"] && message["generation"] == originalGeneration,
            "A2 bridge success preserves the distinct enumeration outcome");
    }
    auto invalidRefresh = refresh;
    invalidRefresh.insert("generation", "retired-page");
    checkA2(!parse(invalidRefresh).request, "A2 stale Refresh rejected");
    invalidRefresh = refresh;
    invalidRefresh.insert("payload", QJsonObject{{"id", "ignored-mutation"}});
    const auto rejectedRefresh = parse(invalidRefresh);
    checkA2(!rejectedRefresh.request && failure(rejectedRefresh.requestId, originalGeneration,
        rejectedRefresh.error, rejectedRefresh.type)["type"] == "RefreshDisplays", "A2 invalid Refresh correlated failure retains type");
    invalidRefresh = refresh;
    invalidRefresh.insert("version", 2);
    checkA2(!parse(invalidRefresh).request, "A2 Refresh version restriction preserved");
    invalidRefresh = refresh;
    invalidRefresh.insert("sideEffect", true);
    checkA2(!parse(invalidRefresh).request, "A2 Refresh does not accept extra mutation fields");
    const auto response = result(*parsed.request, {{"coordinatorEpoch", "epoch"}});
    check(response.value("ok").toBool() && response.value("requestId") == request.value("requestId")
        && response.value("type") == request.value("type") && response.value("generation") == request.value("generation")
        && response.value("payload").toObject().value("coordinatorEpoch") == QJsonValue{"epoch"}, "result correlation and payload");
    const auto denied = failure("bootstrap:1", originalGeneration, "invalid-payload");
    check(denied.value("ok") == QJsonValue{false} && !denied.contains("payload")
        && denied.value("error") == QJsonValue{"invalid-payload"}, "failure envelope");
    for (const auto* field : {"kind", "version", "requestId", "type", "generation", "payload"})
    {
        auto missing = request;
        missing.remove(QLatin1String(field));
        check(!parse(missing).request, "missing envelope field rejected");
    }
    auto wrong = request;
    wrong.insert("extra", true);
    check(!parse(wrong).request, "extra envelope field rejected");
    for (const auto& command : {"Refresh", "Identify", "CreateSession", "StartSession", "LaunchSession",
        "OpenSession", "ShowSession", "ActivateSession", "CloseSession", "RequestClose", "ForceClose",
        "LauncherSessionsChanged", "Ping", "ready"})
    {
        wrong = request;
        wrong.insert("type", command);
        check(!parse(wrong).request, "unsupported or incomplete command rejected");
    }
    wrong = request;
    wrong.insert("version", 2);
    check(!parse(wrong).request, "wrong version rejected");
    wrong = request;
    wrong.insert("version", "1");
    check(!parse(wrong).request, "string version rejected");
    wrong = request;
    wrong.insert("generation", "old-generation");
    check(!parse(wrong).request, "stale generation rejected");
    wrong = request;
    wrong.insert("payload", QJsonObject{{"refresh", true}});
    check(!parse(wrong).request, "nonempty payload rejected");
    wrong = request;
    wrong.insert("payload", QJsonArray{});
    check(!parse(wrong).request, "non-object payload rejected");
    for (const auto& id : {QString{}, QString(129, QLatin1Char('x')), QStringLiteral("contains space"), QString::fromUtf8("非ASCII")})
    {
        wrong = request;
        wrong.insert("requestId", id);
        check(!parse(wrong).request, "invalid requestId rejected");
    }
    wrong = request;
    wrong.insert("requestId", QString(128, QLatin1Char('x')));
    check(parse(wrong).request.has_value(), "maximum requestId accepted");
    auto duplicate = serialize(request);
    duplicate.chop(1);
    duplicate += ",\"kind\":\"command\"}";
    check(!parseRequest(duplicate, originalDocument, originalDocument, originalGeneration).request, "duplicate envelope fields rejected");
    check(!parseRequest("[]", originalDocument, originalDocument, originalGeneration).request, "array rejected");
    check(!parseRequest("{", originalDocument, originalDocument, originalGeneration).request, "malformed JSON rejected");
    check(!parseRequest(QByteArray(16385, ' '), originalDocument, originalDocument, originalGeneration).request, "oversized JSON rejected");
    for (const auto& source : {QStringLiteral("http://127.0.0.1:5173/"), QStringLiteral("https://launcher.dse.local.evil/index.html"),
        QStringLiteral("https://launcher.dse.local/other.html"), originalDocument + "&extra=true"})
        check(!parseRequest(serialize(request), source, originalDocument, originalGeneration).request, "non-exact source rejected");
    check(page.navigationStarting("https://example.com/") == NavigationDisposition::Reject, "external navigation rejected");
    check(page.navigationStarting(originalDocument) == NavigationDisposition::RestartWithFreshGeneration, "reload intercepted");
    check(page.generation() != originalGeneration && page.document() != originalDocument, "reload rotates native generation and URI");
    check(!parseRequest(serialize(request), originalDocument, page.document(), page.generation()).request, "retired page source fenced");
    checkA2(!parseRequest(serialize(refresh), originalDocument, page.document(), page.generation()).request, "A2 old Refresh source and generation fenced after Reload");
    checkA3(!parseRequest(serialize(identify), originalDocument, page.document(), page.generation()).request,
        "A3 old Identify document is fenced after reload");
    checkA4(!parseRequest(serialize(language), originalDocument, page.document(), page.generation()).request,
        "A4 old language document is fenced after reload");
    check(!parseRequest(serialize(create), originalDocument, page.document(), page.generation()).request,
        "A6 retired Create document fenced after reload");
    check(!parseRequest(serialize(open), originalDocument, page.document(), page.generation()).request,
        "A6 retired Open document fenced after reload");
    check(page.navigationStarting(page.document()) == NavigationDisposition::Allow, "fresh reload navigation accepted once");
    auto freshRefresh = refresh;
    freshRefresh.insert("generation", page.generation());
    checkA2(parseRequest(serialize(freshRefresh), page.document(), page.document(), page.generation()).request.has_value(),
        "A2 current reloaded page can request a fresh catalog");
    page.close();
    check(page.navigationStarting(originalDocument) == NavigationDisposition::Reject && page.generation().isEmpty(), "closed page fenced");
    PageFence localizedPage;
    localizedPage.setPresentationLanguage(DesktopSceneEngine::Player::UiLanguage::SimplifiedChinese);
    localizedPage.prepare();
    const auto localizedDocument = localizedPage.document();
    checkA4(localizedDocument.endsWith("&language=zh_CN")
        && localizedPage.navigationStarting(localizedDocument) == NavigationDisposition::Allow,
        "A4 prebootstrap effective hint is part of exact trusted document");
    localizedPage.setPresentationLanguage(DesktopSceneEngine::Player::UiLanguage::English);
    checkA4(localizedPage.document() == localizedDocument
        && localizedPage.navigationStarting(localizedDocument) == NavigationDisposition::RestartWithFreshGeneration
        && localizedPage.document().endsWith("&language=en"),
        "A4 language hint updates on reload without changing live document or weakening source fence");

    CreationOperation inlineEnvironment;
    check(inlineEnvironment.callbackArrived() && !inlineEnvironment.consume(false), "inline environment callback cannot adopt before factory return");
    inlineEnvironment.factoryReturned(true);
    check(inlineEnvironment.consume(true) == CreationDisposition::RejectClosed, "close during environment creation rejects late adoption");
    check(!inlineEnvironment.consume(false) && !inlineEnvironment.callbackArrived(), "settled environment callback is one-shot");
    CreationOperation controller;
    controller.factoryReturned(true);
    check(!controller.consume(true), "close while controller pending waits for callback");
    check(controller.callbackArrived() && controller.consume(true) == CreationDisposition::RejectClosed, "controller callback after closing rejects adoption");
    CreationOperation valid;
    valid.factoryReturned(true);
    valid.callbackArrived();
    check(valid.consume(false) == CreationDisposition::Adopt, "open owner adopts successful callback");
    CreationOperation failed;
    failed.callbackArrived();
    failed.factoryReturned(false);
    check(failed.consume(false) == CreationDisposition::FactoryFailed, "factory failure takes precedence over inline callback");
    std::cout << "Launcher bridge contract: " << checks - failures << '/' << checks << " PASS\n";
    std::cout << "A2 command/result checks: " << a2Checks << " cases, failures=" << failures << '\n';
    std::cout << "A3 bridge checks: " << a3Checks << " cases, failures=" << failures << '\n';
    std::cout << "A4 bridge checks: " << a4Checks << " cases, failures=" << failures << '\n';
    return failures ? 1 : 0;
}
