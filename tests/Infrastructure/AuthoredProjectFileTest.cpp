#include "Infrastructure/Json/AuthoredProjectJson.h"
#include "Infrastructure/Persistence/ProjectFile.h"

#include <DesktopSceneEngine/Domain/AuthoredProject.h>

#include <Windows.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{

using DesktopSceneEngine::Domain::AuthoredItem;
using DesktopSceneEngine::Domain::AuthoredMediaType;
using DesktopSceneEngine::Domain::AuthoredPlaylist;
using DesktopSceneEngine::Domain::AuthoredProject;
using DesktopSceneEngine::Domain::BackgroundMusicTrack;
using DesktopSceneEngine::Domain::ItemId;
using DesktopSceneEngine::Domain::MediaPresentationMode;
using DesktopSceneEngine::Domain::MusicPlaybackMode;
using DesktopSceneEngine::Domain::PlaybackSequenceMode;
using DesktopSceneEngine::Domain::PlaylistId;
using DesktopSceneEngine::Domain::TrackId;

constexpr std::string_view PlaylistIdText =
    "11111111-1111-4111-8111-111111111111";
constexpr std::string_view ImageItemIdText =
    "22222222-2222-4222-8222-222222222222";
constexpr std::string_view VideoItemIdText =
    "33333333-3333-4333-8333-333333333333";
constexpr std::string_view TrackIdText =
    "44444444-4444-4444-8444-444444444444";
constexpr std::string_view ItemTrackIdText =
    "55555555-5555-4555-8555-555555555555";

DesktopSceneEngine::Domain::DisplayOwner testDisplayOwner()
{
    return *DesktopSceneEngine::Domain::DisplayOwner::fromDisplayId(
        DesktopSceneEngine::DisplayId::fromTargetDevicePath(
            "\\\\?\\DISPLAY#AUTHORED_FILE_TEST#DISPLAY_1"));
}

DesktopSceneEngine::Domain::DisplayOwner otherDisplayOwner()
{
    return *DesktopSceneEngine::Domain::DisplayOwner::fromDisplayId(
        DesktopSceneEngine::DisplayId::fromTargetDevicePath(
            "\\\\?\\DISPLAY#AUTHORED_FILE_TEST#DISPLAY_2"));
}

class TemporaryDirectory
{
public:

    TemporaryDirectory()
    {
        const auto ticks = std::chrono::high_resolution_clock::now()
            .time_since_epoch()
            .count();
        const std::filesystem::path parent = std::filesystem::temp_directory_path();

        for (unsigned int attempt = 0U; attempt < 32U; ++attempt)
        {
            path_ = parent
                / ("DesktopSceneEngine_AuthoredProjectFileTest_"
                    + std::to_string(GetCurrentProcessId())
                    + "_"
                    + std::to_string(ticks)
                    + "_"
                    + std::to_string(attempt));

            std::error_code error{};
            if (std::filesystem::create_directory(path_, error))
            {
                return;
            }

            if (error)
            {
                throw std::runtime_error{
                    "Unable to create AuthoredProjectFileTest directory: "
                    + error.message()};
            }
        }

        throw std::runtime_error{
            "Unable to create a unique AuthoredProjectFileTest directory."};
    }

    ~TemporaryDirectory()
    {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
        if (error)
        {
            std::cerr << "Cleanup warning for '" << path_.string()
                << "': " << error.message() << std::endl;
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] std::filesystem::path file(const std::string& name) const
    {
        return path_ / name;
    }

private:

    std::filesystem::path path_{};
};

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output.is_open())
    {
        throw std::runtime_error{"Unable to open test file for writing."};
    }

    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (output.fail())
    {
        throw std::runtime_error{"Unable to write test file."};
    }
}

template <typename Function>
bool expectRuntimeErrorContaining(
    Function&& function,
    std::initializer_list<std::string_view> fragments,
    const char* message)
{
    try
    {
        function();
    }
    catch (const std::runtime_error& error)
    {
        const std::string text = error.what();
        for (const std::string_view fragment : fragments)
        {
            if (text.find(fragment) == std::string::npos)
            {
                std::cerr << "Test failed: " << message
                    << " (missing error fragment '" << fragment
                    << "' in '" << text << "')" << std::endl;
                return false;
            }
        }
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

template <typename Function>
bool expectNoThrow(Function&& function, const char* message)
{
    try
    {
        function();
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Test failed: " << message
            << " (actual error: " << error.what() << ')' << std::endl;
        return false;
    }
}

std::size_t temporarySiblingCount(const std::filesystem::path& target)
{
    const std::wstring prefix = target.filename().wstring() + L".tmp.";
    std::size_t count = 0U;
    for (const std::filesystem::directory_entry& entry
        : std::filesystem::directory_iterator{target.parent_path()})
    {
        const std::wstring name = entry.path().filename().wstring();
        if (name.starts_with(prefix))
        {
            ++count;
        }
    }
    return count;
}

AuthoredProject makeRichProject(
    DesktopSceneEngine::Domain::DisplayOwner owner = testDisplayOwner())
{
    BackgroundMusicTrack track{};
    track.id = TrackId{std::string{TrackIdText}};
    track.source = "C:/music/background.m4a";
    track.displayAlias = "Background";
    track.volume = 0.45;

    BackgroundMusicTrack itemTrack = track;
    itemTrack.id = TrackId{std::string{ItemTrackIdText}};
    itemTrack.displayAlias = "Item Background";

    AuthoredItem image{};
    image.id = ItemId{std::string{ImageItemIdText}};
    image.displayAlias = "Morning";
    image.mediaType = AuthoredMediaType::Image;
    image.source = "C:/media/morning.png";
    image.imageDurationSeconds = 17.5;
    image.presentation.crop = {0.1F, 0.2F, 0.7F, 0.6F};
    image.presentation.layout = {0.25F, -0.35F, 1.2F, 0.8F};
    image.presentation.mediaPresentation.mode = MediaPresentationMode::Native;
    image.presentation.mediaPresentation.customAspect = {4, 3};
    image.itemMediaVolume = 0.8;
    image.musicQueue.enabled = false;
    image.musicQueue.playbackMode = MusicPlaybackMode::Shuffle;
    image.musicQueue.volume = 0.4;
    image.musicQueue.tracks = {itemTrack};

    AuthoredItem video{};
    video.id = ItemId{std::string{VideoItemIdText}};
    video.displayAlias = "Night";
    video.mediaType = AuthoredMediaType::Video;
    video.source = "C:/media/night.mp4";
    video.videoPlaybackRate = 1.7;
    video.presentation.mediaPresentation.mode = MediaPresentationMode::AutoFill;
    video.itemMediaVolume = 0.35;

    AuthoredPlaylist playlist{};
    playlist.id = PlaylistId{std::string{PlaylistIdText}};
    playlist.displayAlias = "Primary";
    playlist.items = {image, video};
    playlist.masterVolume = 0.7;
    playlist.globalMusicQueue.enabled = false;
    playlist.globalMusicQueue.playbackMode = MusicPlaybackMode::CurrentSingleLoop;
    playlist.globalMusicQueue.volume = 0.65;
    playlist.globalMusicQueue.tracks = {track};
    playlist.itemPlaybackMode = PlaybackSequenceMode::Random;

    AuthoredProject project{std::move(owner)};
    project.desktopCanvas.aspectRatio = {21, 9};
    project.playlistPlaybackMode = PlaybackSequenceMode::ListLoop;
    project.playlists = {playlist};
    return project;
}

bool sameAuthoredProject(
    const AuthoredProject& left,
    const AuthoredProject& right)
{
    return DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(left)
        == DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(right);
}

bool verifyNewTargetAndV3RoundTrip(const TemporaryDirectory& directory)
{
    const std::filesystem::path path = directory.file("new-target.json");
    const AuthoredProject original = makeRichProject();

    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        original, path);

    const nlohmann::json document = nlohmann::json::parse(readText(path));
    const AuthoredProject restored =
        DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(path);

    return require(std::filesystem::exists(path), "new target must exist")
        && require(document.at("type") == "DesktopSceneEngineProject",
            "authored file must identify its type")
        && require(document.at("version") == "3.0",
            "authored file must use v3")
        && require(document.at("displayOwner")
                == *original.displayOwner.displayId().canonicalRepresentation(),
            "authored file must preserve its DisplayOwner")
        && require(sameAuthoredProject(original, restored),
            "new target must preserve complete authored semantics")
        && require(restored.playlists[0].id.value() == PlaylistIdText,
            "stable PlaylistId must survive file round-trip")
        && require(temporarySiblingCount(path) == 0U,
            "new-target save must leave no temporary sibling");
}

bool verifyOverwrite(const TemporaryDirectory& directory)
{
    const std::filesystem::path path = directory.file("overwrite.json");
    const std::string oldText = "old target bytes";
    writeText(path, oldText);

    const AuthoredProject original = makeRichProject();
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        original, path);

    const AuthoredProject restored =
        DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(path);
    return require(readText(path) != oldText, "overwrite must replace old bytes")
        && require(sameAuthoredProject(original, restored),
            "overwrite must produce valid authored project")
        && require(temporarySiblingCount(path) == 0U,
            "overwrite must leave no temporary sibling");
}

bool verifyVersionDispatch(const TemporaryDirectory& directory)
{
    bool result = true;
    for (const std::string_view version : {"1.0", "2.0"})
    {
        const std::filesystem::path path = directory.file(
            version == "1.0" ? "pre-release-v1.json" : "pre-release-v2.json");
        writeText(path, nlohmann::json{
            {"type", "DesktopSceneEngineProject"},
            {"version", version}}.dump());
        result = expectRuntimeErrorContaining(
            [&path]()
            {
                (void)DesktopSceneEngine::Infrastructure::Persistence::
                    loadAuthoredProjectFile(path);
            },
            {path.filename().string(), version, "unsupported pre-release"},
            "v1/v2 authored Open must reject as unsupported pre-release") && result;
    }

    const std::filesystem::path v3Path = directory.file("authored-v3-bom.json");
    const AuthoredProject expected = makeRichProject();
    const std::string v3Text =
        DesktopSceneEngine::Infrastructure::Json::toAuthoredProjectJson(expected).dump(4);
    writeText(v3Path, std::string{"\xEF\xBB\xBF"} + v3Text);
    const AuthoredProject restored =
        DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(v3Path);
    result = require(sameAuthoredProject(expected, restored),
        "v3 dispatch must preserve authored semantics and accept UTF-8 BOM") && result;
    result = require(restored.playlists[0].items[0].id.value() == ImageItemIdText,
        "v3 dispatch must preserve stable ItemId") && result;
    return result;
}

bool verifyDuplicateKeys(const TemporaryDirectory& directory)
{
    const std::filesystem::path rootPath = directory.file("duplicate-root.json");
    writeText(rootPath,
        R"({"type":"DesktopSceneEngineProject","version":"3.0","version":"1.0"})");
    bool result = expectRuntimeErrorContaining(
        [&rootPath]()
        {
            (void)DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(
                rootPath);
        },
        {"duplicate", "version", "duplicate-root.json"},
        "duplicate root key must reject with key and path context");

    const std::filesystem::path nestedPath = directory.file("duplicate-nested.json");
    writeText(
        nestedPath,
        R"({"type":"DesktopSceneEngineProject","version":"3.0","backgroundMusic":{"enabled":true,"enabled":false}})");
    result = expectRuntimeErrorContaining(
        [&nestedPath]()
        {
            (void)DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(
                nestedPath);
        },
        {"duplicate", "enabled", "duplicate-nested.json"},
        "duplicate nested key must reject with key and path context") && result;
    return result;
}

bool verifyVersionFailures(const TemporaryDirectory& directory)
{
    const std::pair<std::string, std::string> cases[] = {
        {"missing-version.json", R"({"type":"DesktopSceneEngineProject"})"},
        {"wrong-version-type.json", R"({"type":"DesktopSceneEngineProject","version":3.0})"},
        {"unsupported-3-1.json", R"({"type":"DesktopSceneEngineProject","version":"3.1"})"},
        {"unsupported-4-0.json", R"({"type":"DesktopSceneEngineProject","version":"4.0"})"}};

    bool result = true;
    for (const auto& [name, text] : cases)
    {
        const std::filesystem::path path = directory.file(name);
        writeText(path, text);
        const std::string expected = name.starts_with("unsupported-3") ? "3.1"
            : name.starts_with("unsupported-4") ? "4.0"
            : "version";
        result = expectRuntimeErrorContaining(
            [&path]()
            {
                (void)DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(
                    path);
            },
            {name, expected},
            "invalid file version must reject with path context") && result;
    }
    return result;
}

bool verifySaveAsDestinationPolicy(const TemporaryDirectory& directory)
{
    const auto sourceOwner = testDisplayOwner();
    bool result = true;

    const std::filesystem::path absent = directory.file("absent.json");
    result = expectNoThrow(
        [&]()
        {
            DesktopSceneEngine::Infrastructure::Persistence::
                validateAuthoredProjectSaveAsDestination(sourceOwner, absent);
        },
        "absent Save As destination must be allowed") && result;

    const std::filesystem::path sameOwner = directory.file("same-owner.json");
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        makeRichProject(sourceOwner), sameOwner);
    result = expectNoThrow(
        [&]()
        {
            DesktopSceneEngine::Infrastructure::Persistence::
                validateAuthoredProjectSaveAsDestination(sourceOwner, sameOwner);
        },
        "same-owner v3 Save As destination must be allowed") && result;

    const std::filesystem::path otherOwner = directory.file("other-owner.json");
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        makeRichProject(otherDisplayOwner()), otherOwner);
    result = expectRuntimeErrorContaining(
        [&]()
        {
            DesktopSceneEngine::Infrastructure::Persistence::
                validateAuthoredProjectSaveAsDestination(sourceOwner, otherOwner);
        },
        {"other-owner.json", "different display session"},
        "cross-owner v3 Save As destination must be rejected") && result;

    for (const std::string_view version : {"1.0", "2.0"})
    {
        const std::filesystem::path path = directory.file(
            version == "1.0" ? "replace-v1.json" : "replace-v2.json");
        writeText(path, nlohmann::json{
            {"type", "DesktopSceneEngineProject"},
            {"version", version},
            {"legacyBytes", true}}.dump());
        result = expectNoThrow(
            [&]()
            {
                DesktopSceneEngine::Infrastructure::Persistence::
                    validateAuthoredProjectSaveAsDestination(sourceOwner, path);
            },
            "v1/v2 Save As destination must allow ordinary replacement") && result;
        DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
            makeRichProject(sourceOwner), path);
        result = require(
            nlohmann::json::parse(readText(path)).at("version") == "3.0",
            "v1/v2 ordinary replacement must write current v3 bytes") && result;
    }

    const std::pair<std::string, std::string> allowedUnrelated[] = {
        {"invalid-json.txt", "{ invalid json"},
        {"array.json", "[]"},
        {"missing-type.json", R"({"version":"3.0"})"},
        {"non-string-type.json", R"({"type":42,"version":"3.0"})"},
        {"unrelated.json", R"({"type":"OtherDocument","version":"3.0"})"}};
    for (const auto& [name, text] : allowedUnrelated)
    {
        const std::filesystem::path path = directory.file(name);
        writeText(path, text);
        result = expectNoThrow(
            [&]()
            {
                DesktopSceneEngine::Infrastructure::Persistence::
                    validateAuthoredProjectSaveAsDestination(sourceOwner, path);
            },
            "unrelated/non-project Save As destination must be allowed") && result;
    }

    const std::pair<std::string, std::string> rejectedDse[] = {
        {"missing-dse-version.json", R"({"type":"DesktopSceneEngineProject"})"},
        {"non-string-dse-version.json",
            R"({"type":"DesktopSceneEngineProject","version":3.0})"},
        {"future-dse.json",
            R"({"type":"DesktopSceneEngineProject","version":"4.0"})"},
        {"malformed-v3.json",
            R"({"type":"DesktopSceneEngineProject","version":"3.0","displayOwner":"bad"})"}};
    for (const auto& [name, text] : rejectedDse)
    {
        const std::filesystem::path path = directory.file(name);
        writeText(path, text);
        result = expectRuntimeErrorContaining(
            [&]()
            {
                DesktopSceneEngine::Infrastructure::Persistence::
                    validateAuthoredProjectSaveAsDestination(sourceOwner, path);
            },
            {name},
            "recognizable malformed/future DSE destination must be rejected") && result;
    }

    const std::filesystem::path unreadable = directory.file("directory-destination");
    std::filesystem::create_directory(unreadable);
    result = expectRuntimeErrorContaining(
        [&]()
        {
            DesktopSceneEngine::Infrastructure::Persistence::
                validateAuthoredProjectSaveAsDestination(sourceOwner, unreadable);
        },
        {"directory-destination", "read"},
        "unreadable Save As destination must be rejected") && result;

    return result;
}

bool verifyMalformedEmptyAndMissing(const TemporaryDirectory& directory)
{
    const std::filesystem::path malformed = directory.file("malformed.json");
    writeText(malformed, "{ invalid json");
    bool result = expectRuntimeErrorContaining(
        [&malformed]()
        {
            (void)DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(
                malformed);
        },
        {"malformed.json", "parse"},
        "malformed JSON must preserve parser and path context");

    const std::filesystem::path empty = directory.file("empty.json");
    writeText(empty, "");
    result = expectRuntimeErrorContaining(
        [&empty]()
        {
            (void)DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(
                empty);
        },
        {"empty.json", "empty"},
        "empty file must reject with path context") && result;

    const std::filesystem::path missing = directory.file("missing.json");
    result = expectRuntimeErrorContaining(
        [&missing]()
        {
            (void)DesktopSceneEngine::Infrastructure::Persistence::loadAuthoredProjectFile(
                missing);
        },
        {"missing.json", "open"},
        "missing file must reject with path context") && result;
    return result;
}

bool verifyInvalidProjectPreservesTarget(const TemporaryDirectory& directory)
{
    const std::filesystem::path path = directory.file("invalid-project.json");
    const std::string oldText = "preserve invalid target";
    writeText(path, oldText);

    AuthoredProject invalid = makeRichProject();
    invalid.playlists[0].id = PlaylistId{std::string{"invalid"}};
    const bool threw = expectRuntimeErrorContaining(
        [&invalid, &path]()
        {
            DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
                invalid, path);
        },
        {"invalid-project.json"},
        "invalid authored project must fail before filesystem mutation");

    return threw
        && require(readText(path) == oldText,
            "invalid authored project must preserve exact target bytes")
        && require(temporarySiblingCount(path) == 0U,
            "invalid authored project must create no temporary sibling");
}

bool verifyLockedTargetPreservation(const TemporaryDirectory& directory)
{
    const std::filesystem::path path = directory.file("locked-target.json");
    const std::string oldText = "preserve locked target";
    writeText(path, oldText);

    const HANDLE locked = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (!require(locked != INVALID_HANDLE_VALUE, "test must lock target"))
    {
        return false;
    }

    const AuthoredProject project = makeRichProject();
    const bool threw = expectRuntimeErrorContaining(
        [&project, &path]()
        {
            DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
                project, path);
        },
        {"ReplaceFileW", "locked-target.json"},
        "locked target must reject replacement");
    const BOOL closed = CloseHandle(locked);

    return require(closed != FALSE, "test lock handle must close")
        && threw
        && require(readText(path) == oldText,
            "locked replacement failure must preserve exact target bytes")
        && require(temporarySiblingCount(path) == 0U,
            "locked replacement failure must leave no temporary sibling");
}

}

int main()
{
    try
    {
        const TemporaryDirectory directory{};
        const bool result = verifyNewTargetAndV3RoundTrip(directory)
            && verifyOverwrite(directory)
            && verifyVersionDispatch(directory)
            && verifyDuplicateKeys(directory)
            && verifyVersionFailures(directory)
            && verifySaveAsDestinationPolicy(directory)
            && verifyMalformedEmptyAndMissing(directory)
            && verifyInvalidProjectPreservesTarget(directory)
            && verifyLockedTargetPreservation(directory);
        return result ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Unexpected exception: " << error.what() << std::endl;
        return 1;
    }
}
