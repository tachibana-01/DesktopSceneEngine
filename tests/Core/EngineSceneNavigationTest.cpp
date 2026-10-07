#include <DesktopSceneEngine/Domain/Crop.h>
#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Layout.h>
#include <DesktopSceneEngine/Domain/Library.h>
#include <DesktopSceneEngine/Domain/Media.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/Domain/Scene.h>
#include <DesktopSceneEngine/Engine.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using DesktopSceneEngine::Domain::EntityId;
using DesktopSceneEngine::Domain::Library;
using DesktopSceneEngine::Domain::Media;
using DesktopSceneEngine::Domain::Playback;
using DesktopSceneEngine::Domain::Project;
using DesktopSceneEngine::Domain::Scene;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

Project makeProject()
{
    const Playback playback{true, 1.25F, 0.75F};
    return Project{
        {},
        {
            Scene{EntityId{std::string{"scene-0"}}, EntityId{std::string{"media-0"}}, {}, {}, playback, 3.0F},
            Scene{EntityId{std::string{"scene-1"}}, EntityId{std::string{"media-1"}}, {}, {}, playback, 3.0F},
            Scene{EntityId{std::string{"scene-2"}}, EntityId{std::string{"media-2"}}, {}, {}, playback, 3.0F}
        },
        {}};
}

Project makeTimedNavigationProject(std::size_t sceneCount)
{
    const Playback playback{true, 1.0F, 1.0F};
    std::vector<Scene> scenes{};
    scenes.reserve(sceneCount);

    for (std::size_t index = 0; index < sceneCount; ++index)
    {
        const std::string suffix = std::to_string(index);
        scenes.push_back(Scene{
            EntityId{std::string{"timed-scene-"} + suffix},
            EntityId{std::string{"timed-media-"} + suffix},
            {},
            {},
            playback,
            1.0F});
    }

    return Project{{}, std::move(scenes), {}};
}

bool verifyNaturalSceneCompletionHandoff()
{
    const Project twoSceneProject = makeTimedNavigationProject(2U);
    const EntityId sceneA{std::string{"timed-scene-0"}};
    const EntityId sceneB{std::string{"timed-scene-1"}};

    DesktopSceneEngine::Engine externalEngine{};
    externalEngine.initialize();
    externalEngine.setSceneCompletionPolicy(
        DesktopSceneEngine::Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    if (!require(
            externalEngine.bindProject(twoSceneProject, sceneA),
            "External-completion test project should bind"))
    {
        externalEngine.shutdown();
        return false;
    }

    externalEngine.update(5.0F);
    const float stabilizedTime = externalEngine.playbackTime();
    if (!require(
            externalEngine.activeSceneIndex() == 0U
                && externalEngine.isPaused(),
            "External completion should keep Scene A stable"))
    {
        externalEngine.shutdown();
        return false;
    }

    externalEngine.update(5.0F);
    if (!require(
        externalEngine.activeSceneIndex() == 0U
                && std::fabs(externalEngine.playbackTime() - stabilizedTime) < 0.001F
                && externalEngine.consumeNaturalSceneCompletion()
                && !externalEngine.consumeNaturalSceneCompletion(),
            "A pending external completion must stay stable and expose one fact"))
    {
        externalEngine.shutdown();
        return false;
    }

    externalEngine.togglePause();
    externalEngine.setPlaybackSpeed(2.0F);
    externalEngine.update(5.0F);
    if (!require(
            externalEngine.isPaused()
                && std::fabs(externalEngine.playbackTime() - stabilizedTime) < 0.001F
                && !externalEngine.consumeNaturalSceneCompletion(),
            "Pause/resume and speed changes must not synthesize another completion"))
    {
        externalEngine.shutdown();
        return false;
    }

    if (!require(
            externalEngine.switchScene(twoSceneProject, sceneA),
            "Restarting the same Scene should succeed before stale-fact checks"))
    {
        externalEngine.shutdown();
        return false;
    }
    externalEngine.update(5.0F);
    if (!require(
            externalEngine.switchScene(twoSceneProject, sceneB)
                && !externalEngine.consumeNaturalSceneCompletion(),
            "A successful different-Scene switch must clear a pending completion"))
    {
        externalEngine.shutdown();
        return false;
    }

    if (!require(
            externalEngine.switchScene(twoSceneProject, sceneA),
            "Restarting Scene A should succeed for same-identity clearing"))
    {
        externalEngine.shutdown();
        return false;
    }
    externalEngine.update(5.0F);
    if (!require(
            externalEngine.switchScene(twoSceneProject, sceneA)
                && !externalEngine.consumeNaturalSceneCompletion(),
            "A successful same-Scene switch must clear a pending completion"))
    {
        externalEngine.shutdown();
        return false;
    }

    externalEngine.update(5.0F);
    if (!require(
            externalEngine.seek(0.5F)
                && !externalEngine.consumeNaturalSceneCompletion(),
            "A successful seek must clear a pending completion"))
    {
        externalEngine.shutdown();
        return false;
    }

    externalEngine.update(5.0F);
    externalEngine.detachProject();
    if (!require(
            !externalEngine.consumeNaturalSceneCompletion(),
            "Detaching a project must clear a pending completion"))
    {
        externalEngine.shutdown();
        return false;
    }

    if (!require(
            externalEngine.bindProject(twoSceneProject, sceneA),
            "Project rebinding should succeed for pending-fact lifetime coverage"))
    {
        externalEngine.shutdown();
        return false;
    }
    externalEngine.update(5.0F);
    externalEngine.detachProject();
    if (!require(
            externalEngine.bindProject(twoSceneProject, sceneB)
                && !externalEngine.consumeNaturalSceneCompletion(),
            "A successful project bind must not retain a prior completion"))
    {
        externalEngine.shutdown();
        return false;
    }

    externalEngine.update(5.0F);
    externalEngine.setSceneCompletionPolicy(
        DesktopSceneEngine::Engine::SceneCompletionPolicy::InternalSceneNavigation);
    if (!require(
            externalEngine.activeSceneIndex() == 1U
                && !externalEngine.consumeNaturalSceneCompletion(),
            "Changing policy must clear pending completion without navigation"))
    {
        externalEngine.shutdown();
        return false;
    }

    if (!require(
            externalEngine.switchScene(twoSceneProject, sceneA),
            "Internal policy should restart the current project's timed Scene"))
    {
        externalEngine.shutdown();
        return false;
    }
    externalEngine.update(5.0F);
    if (!require(
            externalEngine.activeSceneIndex() == 1U
                && !externalEngine.consumeNaturalSceneCompletion(),
            "Internal policy must preserve automatic navigation without external facts"))
    {
        externalEngine.shutdown();
        return false;
    }

    externalEngine.setSceneCompletionPolicy(
        DesktopSceneEngine::Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    if (!require(
            !externalEngine.consumeNaturalSceneCompletion(),
            "Changing to external policy must not synthesize a completion fact"))
    {
        externalEngine.shutdown();
        return false;
    }
    externalEngine.shutdown();

    DesktopSceneEngine::Engine defaultEngine{};
    defaultEngine.initialize();
    if (!require(
            defaultEngine.bindProject(twoSceneProject, sceneA),
            "Default-policy project should bind"))
    {
        defaultEngine.shutdown();
        return false;
    }
    defaultEngine.update(5.0F);
    const bool defaultPolicyPasses = defaultEngine.activeSceneIndex() == 1U
        && !defaultEngine.consumeNaturalSceneCompletion();
    defaultEngine.shutdown();
    if (!require(
            defaultPolicyPasses,
            "Default policy must preserve automatic Scene A to Scene B navigation"))
    {
        return false;
    }

    DesktopSceneEngine::Engine shutdownEngine{};
    shutdownEngine.initialize();
    shutdownEngine.setSceneCompletionPolicy(
        DesktopSceneEngine::Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    if (!require(
            shutdownEngine.bindProject(twoSceneProject, sceneA),
            "Shutdown-clearing project should bind"))
    {
        shutdownEngine.shutdown();
        return false;
    }
    shutdownEngine.update(5.0F);
    shutdownEngine.shutdown();
    if (!require(
            !shutdownEngine.consumeNaturalSceneCompletion(),
            "Shutdown must clear a pending completion"))
    {
        return false;
    }

    const Project oneSceneProject = makeTimedNavigationProject(1U);
    DesktopSceneEngine::Engine oneSceneExternalEngine{};
    oneSceneExternalEngine.initialize();
    oneSceneExternalEngine.setSceneCompletionPolicy(
        DesktopSceneEngine::Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    if (!require(
            oneSceneExternalEngine.bindProject(oneSceneProject, sceneA),
            "One-Scene external project should bind"))
    {
        oneSceneExternalEngine.shutdown();
        return false;
    }
    oneSceneExternalEngine.update(5.0F);
    const bool oneSceneExternalPasses = oneSceneExternalEngine.activeSceneIndex() == 0U
        && oneSceneExternalEngine.consumeNaturalSceneCompletion()
        && !oneSceneExternalEngine.consumeNaturalSceneCompletion();
    oneSceneExternalEngine.shutdown();
    if (!require(
            oneSceneExternalPasses,
            "One-Scene external policy must report completion without self-looping"))
    {
        return false;
    }

    DesktopSceneEngine::Engine oneSceneInternalEngine{};
    oneSceneInternalEngine.initialize();
    if (!require(
            oneSceneInternalEngine.bindProject(oneSceneProject, sceneA),
            "One-Scene internal project should bind"))
    {
        oneSceneInternalEngine.shutdown();
        return false;
    }
    oneSceneInternalEngine.update(5.0F);
    const bool oneSceneInternalPasses = oneSceneInternalEngine.activeSceneIndex() == 0U
        && !oneSceneInternalEngine.consumeNaturalSceneCompletion();
    oneSceneInternalEngine.shutdown();
    return require(
        oneSceneInternalPasses,
        "One-Scene internal policy must preserve self-loop navigation");
}

std::string utf8Path(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return {
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

std::vector<std::filesystem::path> filesWithExtensions(
    const std::filesystem::path& directory,
    const std::vector<std::string>& extensions
)
{
    std::vector<std::filesystem::path> files{};
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::string extension = entry.path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        if (std::find(extensions.begin(), extensions.end(), extension)
            != extensions.end())
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

Project makeMediaProject(
    const std::filesystem::path& imageA,
    const std::filesystem::path& videoA,
    const std::filesystem::path& imageB,
    const std::filesystem::path& videoB
)
{
    const Playback playback{true, 1.25F, 0.75F};
    const EntityId imageAId{std::string{"image-a"}};
    const EntityId videoAId{std::string{"video-a"}};
    const EntityId imageBId{std::string{"image-b"}};
    const EntityId videoBId{std::string{"video-b"}};
    return Project{
        Library{
            {
                Media{imageAId, "Image A", utf8Path(imageA)},
                Media{videoAId, "Video A", utf8Path(videoA)},
                Media{imageBId, "Image B", utf8Path(imageB)},
                Media{videoBId, "Video B", utf8Path(videoB)}
            }
        },
        {
            Scene{EntityId{std::string{"scene-a"}}, imageAId, {}, {}, playback, 3.0F},
            Scene{EntityId{std::string{"scene-b"}}, videoAId, {}, {}, playback, 3.0F},
            Scene{EntityId{std::string{"scene-c"}}, imageBId, {}, {}, playback, 3.0F},
            Scene{EntityId{std::string{"scene-d"}}, videoBId, {}, {}, playback, 3.0F}
        },
        {}};
}

bool hasValidDimensions(const DesktopSceneEngine::Engine& engine)
{
    const auto dimensions = engine.currentMediaDimensions();
    return dimensions.has_value()
        && dimensions->width > 0
        && dimensions->height > 0;
}

}

int main()
{
    if (!verifyNaturalSceneCompletionHandoff())
    {
        return 1;
    }

    const Project project = makeProject();
    const EntityId firstSceneId{std::string{"scene-0"}};

    DesktopSceneEngine::Engine engine{};
    if (!require(
            !engine.switchToPreviousScene(),
            "Previous scene should safely fail without a bound Project")
        || !require(
            !engine.currentMediaDimensions().has_value(),
            "No Project should report no current media dimensions"))
    {
        return 1;
    }

    engine.initialize();
    if (!require(
            engine.bindProject(project, firstSceneId),
            "Multi-scene Project binding should succeed"))
    {
        return 1;
    }

    if (!require(
            !engine.currentMediaDimensions().has_value(),
            "A scene without a loaded visual resource should report no dimensions")
        || !require(
            engine.switchToPreviousScene() && engine.activeSceneIndex() == 2U,
            "Previous at the first scene should wrap to the last scene")
        || !require(
            engine.switchToNextScene() && engine.activeSceneIndex() == 0U,
            "Next after a wrapped previous should return to the first scene")
        || !require(
            engine.switchToNextScene() && engine.activeSceneIndex() == 1U,
            "Next should advance to the middle scene")
        || !require(
            engine.switchToPreviousScene() && engine.activeSceneIndex() == 0U,
            "Previous should return a middle scene to its predecessor"))
    {
        return 1;
    }

    engine.togglePause();
    if (!require(engine.isPaused(), "The bound runtime should pause"))
    {
        return 1;
    }

    if (!require(
            engine.switchToPreviousScene() && engine.activeSceneIndex() == 2U,
            "Previous should use the same switching transaction while paused")
        || !require(
            !engine.isPaused(),
            "Previous should mirror current Next behavior by configuring the target scene")
        || !require(
            engine.playbackSpeed() == 1.25F,
            "Previous should configure the target scene playback speed"))
    {
        return 1;
    }

    engine.shutdown();

    const std::filesystem::path resourceRoot{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    const auto images = filesWithExtensions(
        resourceRoot / "Images", {".jpg", ".jpeg", ".png"});
    const auto videos = filesWithExtensions(resourceRoot / "Videos", {".mp4"});
    if (!require(images.size() >= 2U && videos.size() >= 2U,
            "The navigation smoke test requires two images and two MP4 videos"))
    {
        return 1;
    }

    const Project mediaProject = makeMediaProject(
        images[0], videos[0], images[1], videos[1]);
    DesktopSceneEngine::Engine mediaEngine{};
    mediaEngine.initialize();
    if (!require(
            mediaEngine.bindProject(
                mediaProject,
                EntityId{std::string{"scene-a"}}),
            "The alternating image/video Project should bind")
        || !require(
            mediaEngine.mediaKind() == DesktopSceneEngine::Engine::MediaKind::Image
                && hasValidDimensions(mediaEngine),
            "The initial image should load and report decoded dimensions"))
    {
        return 1;
    }

    mediaEngine.setVolume(0.65F);
    mediaEngine.toggleMute();
    if (!require(
            mediaEngine.switchToPreviousScene()
                && mediaEngine.activeSceneIndex() == 3U
                && mediaEngine.mediaKind()
                    == DesktopSceneEngine::Engine::MediaKind::Video
                && hasValidDimensions(mediaEngine),
            "Previous should wrap from an image to the last video")
        || !require(
            mediaEngine.switchToNextScene()
                && mediaEngine.activeSceneIndex() == 0U
                && mediaEngine.mediaKind()
                    == DesktopSceneEngine::Engine::MediaKind::Image
                && hasValidDimensions(mediaEngine),
            "Next should return from the last video to the first image")
        || !require(
            mediaEngine.switchToNextScene()
                && mediaEngine.activeSceneIndex() == 1U
                && mediaEngine.mediaKind()
                    == DesktopSceneEngine::Engine::MediaKind::Video
                && hasValidDimensions(mediaEngine),
            "Next should load the following video")
        || !require(
            mediaEngine.switchToNextScene()
                && mediaEngine.activeSceneIndex() == 2U
                && mediaEngine.mediaKind()
                    == DesktopSceneEngine::Engine::MediaKind::Image
                && hasValidDimensions(mediaEngine),
            "Next should replace the video with the following image")
        || !require(
            mediaEngine.switchToPreviousScene()
                && mediaEngine.activeSceneIndex() == 1U
                && mediaEngine.mediaKind()
                    == DesktopSceneEngine::Engine::MediaKind::Video
                && hasValidDimensions(mediaEngine),
            "Previous should replace the image with the preceding video")
        || !require(
            std::fabs(mediaEngine.volume() - 0.65F) < 0.001F
                && mediaEngine.isMuted()
                && std::fabs(mediaEngine.playbackSpeed() - 1.25F) < 0.001F,
            "Scene navigation should preserve output settings and scene playback speed"))
    {
        return 1;
    }

    mediaEngine.shutdown();
    return 0;
}
