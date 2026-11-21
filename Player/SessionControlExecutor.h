#pragma once

#include "ApplicationPreferences.h"
#include <DesktopSceneEngine/DisplayIdentity.h>
#include <QThread>
#include <functional>
#include <memory>
#include <mutex>

namespace DesktopSceneEngine::Player
{
class SessionApplication;
class SessionPlatform;

// One process-owned Session and Engine run on this private native event loop.
// The UI may submit commands or detached facts. It never borrows either owner.
class SessionControlExecutor final
{
public:
    using Command = std::function<void(SessionApplication&)>;
    using SettingsFactory = std::function<std::unique_ptr<QSettings>()>;
    using PlatformFactory = std::function<std::unique_ptr<SessionPlatform>(SessionApplication&)>;
    // Runs on control. An adapter posts the result to its own executor.
    using Started = std::function<void(bool)>;

    SessionControlExecutor();
    ~SessionControlExecutor();
    SessionControlExecutor(const SessionControlExecutor&) = delete;
    SessionControlExecutor& operator=(const SessionControlExecutor&) = delete;

    void start(DisplayId display, ApplicationPreferences initialPreferences,
        SettingsFactory openSettings, Started started, PlatformFactory platform = {});
    [[nodiscard]] bool submit(Command command);
    // A previously admitted operation owns its terminal platform reply.
    // This lane is independent of fresh intent queue capacity. Its callers
    // issue at most one result per outstanding operation or explicit fence.
    [[nodiscard]] bool submitPlatformResult(Command result);
    void stop();

private:
    class Endpoint;
    QThread thread_;
    Endpoint* endpoint_ = nullptr;
    std::mutex admission_;
    bool accepting_ = false;
    bool started_ = false;
    bool stopping_ = false;
    void* audioFactEvent_ = nullptr;
};
}
