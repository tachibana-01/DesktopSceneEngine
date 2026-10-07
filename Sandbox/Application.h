#pragma once

#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/Engine.h>

#include <filesystem>
#include <optional>

class Application
{
public:

    int run();

    int run(const std::filesystem::path& projectPath);

private:

    bool loadProject(const std::filesystem::path& projectPath);

    bool createTestProject();

    int runLoop();

    void refreshStatus();

    bool initializeEngine();

    bool bindProject();

    void shutdown();

    std::optional<DesktopSceneEngine::Domain::Project> project_{};
    DesktopSceneEngine::Engine engine_{};
};
