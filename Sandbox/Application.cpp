#include "Application.h"

#include "Infrastructure/Persistence/ProjectFile.h"

#include <DesktopSceneEngine/Domain/Crop.h>
#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Layout.h>
#include <DesktopSceneEngine/Domain/Library.h>
#include <DesktopSceneEngine/Domain/Media.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Scene.h>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

using DesktopSceneEngine::Domain::EntityId;
using DesktopSceneEngine::Domain::Media;
using DesktopSceneEngine::Domain::Project;
using DesktopSceneEngine::Domain::Scene;

std::filesystem::path findTestResources()
{
    const std::filesystem::path current = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates{
        current / "TestResources",
        current.parent_path() / "TestResources",
        current.parent_path().parent_path() / "TestResources",
        current.parent_path().parent_path().parent_path() / "TestResources"
    };

    for (const auto& candidate : candidates)
    {
        if (std::filesystem::is_directory(candidate))
        {
            return candidate;
        }
    }

    return {};
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
    if (!std::filesystem::is_directory(directory))
    {
        return files;
    }

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

std::string formatPlaybackTime(float seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0F)
    {
        return "--:--";
    }

    const auto totalSeconds = static_cast<long long>(
        (std::min)(
            static_cast<double>(seconds),
            static_cast<double>((std::numeric_limits<long long>::max)())));
    const long long hours = totalSeconds / 3600;
    const long long minutes = (totalSeconds / 60) % 60;
    const long long remainingSeconds = totalSeconds % 60;

    std::ostringstream result;
    result << std::setfill('0');
    if (hours > 0)
    {
        result << std::setw(2) << hours << ':';
    }
    result << std::setw(2) << minutes << ':'
           << std::setw(2) << remainingSeconds;
    return result.str();
}

const char* mediaKindName(DesktopSceneEngine::Engine::MediaKind kind) noexcept
{
    switch (kind)
    {
    case DesktopSceneEngine::Engine::MediaKind::Image:
        return "Image";
    case DesktopSceneEngine::Engine::MediaKind::Video:
        return "Video";
    default:
        return "Unknown";
    }
}

}

bool Application::loadProject(const std::filesystem::path& projectPath)
{
    try
    {
        project_ = DesktopSceneEngine::Infrastructure::Persistence::loadProjectFile(
            projectPath
        );
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Unable to load project '"
            << projectPath.string()
            << "': "
            << error.what()
            << std::endl;
        return false;
    }
}

bool Application::createTestProject()
{
    const std::filesystem::path root = findTestResources();
    const auto images = filesWithExtensions(
        root / "Images",
        {".jpg", ".jpeg", ".png"});
    const auto videos = filesWithExtensions(root / "Videos", {".mp4"});

    if (images.size() < 2 || videos.size() < 2)
    {
        std::cerr
            << "TestResources requires at least two images and two MP4 videos."
            << std::endl;
        return false;
    }

    const EntityId imageAId{"image-a"};
    const EntityId videoAId{"video-a"};
    const EntityId imageBId{"image-b"};
    const EntityId videoBId{"video-b"};

    project_ = Project{
        DesktopSceneEngine::Domain::Library{
            {
                Media{imageAId, "Image A", utf8Path(images[0])},
                Media{videoAId, "Video A", utf8Path(videos[0])},
                Media{imageBId, "Image B", utf8Path(images[1])},
                Media{videoBId, "Video B", utf8Path(videos[1])}
            }
        },
        {
            Scene{
                EntityId{"scene-a"}, imageAId, {}, {},
                DesktopSceneEngine::Domain::Playback{true, 1.0F, 1.0F}, 3.0F
            },
            Scene{
                EntityId{"scene-b"}, videoAId, {}, {},
                DesktopSceneEngine::Domain::Playback{true, 1.0F, 1.0F}, 8.0F
            },
            Scene{
                EntityId{"scene-c"}, imageBId, {}, {},
                DesktopSceneEngine::Domain::Playback{true, 1.0F, 1.0F}, 3.0F
            },
            Scene{
                EntityId{"scene-d"}, videoBId, {}, {},
                DesktopSceneEngine::Domain::Playback{true, 1.0F, 1.0F}, 8.0F
            }
        },
        {}
    };

    return true;
}

bool Application::initializeEngine()
{
    engine_.initialize();
    return engine_.isInitialized();
}

bool Application::bindProject()
{
    if (!project_.has_value() || project_->scenes.empty())
    {
        return false;
    }

    return engine_.bindProject(
        *project_,
        project_->scenes.front().id
    );
}

void Application::shutdown()
{
    engine_.detachProject();
    engine_.shutdown();
    project_.reset();
}

void Application::refreshStatus()
{
    const std::size_t sceneIndex = engine_.activeSceneIndex();
    const std::size_t sceneCount = engine_.sceneCount();

    std::ostringstream status;
    status << "Scene: ";
    if (sceneIndex == (std::numeric_limits<std::size_t>::max)()
        || sceneIndex >= sceneCount)
    {
        status << "-- / " << sceneCount;
    }
    else
    {
        status << (sceneIndex + 1) << " / " << sceneCount;
    }

    status << "    " << (engine_.isPaused() ? "Paused" : "Playing")
           << "    Type: " << mediaKindName(engine_.mediaKind())
           << "    Time: " << formatPlaybackTime(engine_.playbackTime())
           << " / " << formatPlaybackTime(engine_.effectiveDuration())
           << "    Speed: " << std::fixed << std::setprecision(1)
           << engine_.playbackSpeed() << "x"
           << "    Volume: " << std::setprecision(0)
           << engine_.volume() * 100.0F << '%';
    if (engine_.isMuted())
    {
        status << " (Muted)";
    }

    engine_.setStatusText(status.str());
}

int Application::runLoop()
{
    std::cout
        << "Controls: Space pause/resume, 1/NumPad1=1x, "
        << "2/NumPad2=2x, 5/NumPad5=0.5x, N=next, "
        << "Up/Down=volume, M=mute, Esc=exit."
        << std::endl;

    bool running = true;
    auto previousTime = std::chrono::steady_clock::now();
    float statusTimer = 0.0F;
    refreshStatus();

    while (running)
    {
        if (!engine_.processEvents())
        {
            break;
        }

        if (engine_.consumeKeyPress(VK_ESCAPE))
        {
            running = false;
        }
        if (engine_.consumeKeyPress(VK_SPACE))
        {
            engine_.togglePause();
        }
        if (engine_.consumeKeyPress('1')
            || engine_.consumeKeyPress(VK_NUMPAD1))
        {
            engine_.setPlaybackSpeed(1.0F);
        }
        if (engine_.consumeKeyPress('2')
            || engine_.consumeKeyPress(VK_NUMPAD2))
        {
            engine_.setPlaybackSpeed(2.0F);
        }
        if (engine_.consumeKeyPress('5')
            || engine_.consumeKeyPress(VK_NUMPAD5))
        {
            engine_.setPlaybackSpeed(0.5F);
        }
        if (engine_.consumeKeyPress('N'))
        {
            engine_.switchToNextScene();
        }
        if (engine_.consumeKeyPress(VK_UP))
        {
            engine_.setVolume(engine_.volume() + 0.1F);
        }
        if (engine_.consumeKeyPress(VK_DOWN))
        {
            engine_.setVolume(engine_.volume() - 0.1F);
        }
        if (engine_.consumeKeyPress('M'))
        {
            engine_.toggleMute();
        }

        float sliderVolume = 0.0F;
        if (engine_.consumeVolumeSlider(sliderVolume))
        {
            engine_.setVolume(sliderVolume);
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaTime = (std::min)(
            std::chrono::duration<float>(now - previousTime).count(),
            0.1F);
        previousTime = now;

        engine_.update(deltaTime);
        engine_.render();
        statusTimer += deltaTime;
        if (statusTimer >= 0.1F)
        {
            statusTimer = 0.0F;
            refreshStatus();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    shutdown();
    return 0;
}

int Application::run(const std::filesystem::path& projectPath)
{
    if (!loadProject(projectPath))
    {
        return 1;
    }

    if (!initializeEngine())
    {
        shutdown();
        return 2;
    }

    if (!bindProject())
    {
        shutdown();
        return 3;
    }

    return runLoop();
}

int Application::run()
{
    if (!createTestProject())
    {
        return 1;
    }

    if (!initializeEngine())
    {
        shutdown();
        return 2;
    }

    if (!bindProject())
    {
        shutdown();
        return 3;
    }

    return runLoop();
}
