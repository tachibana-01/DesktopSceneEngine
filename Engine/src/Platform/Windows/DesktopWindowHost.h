#pragma once

#include <memory>
#include <string>

namespace DesktopSceneEngine::Rendering
{

struct WindowTarget;

}

namespace DesktopSceneEngine
{
namespace Platform
{
namespace Windows
{

class DesktopWindowHost
{
public:

    DesktopWindowHost();
    ~DesktopWindowHost();

    DesktopWindowHost(const DesktopWindowHost&) = delete;
    DesktopWindowHost& operator=(const DesktopWindowHost&) = delete;
    DesktopWindowHost(DesktopWindowHost&&) = delete;
    DesktopWindowHost& operator=(DesktopWindowHost&&) = delete;

    void initialize();

    // Borrows an already-created native window. The caller retains ownership.
    void initializeExternal(void* handle);

    void shutdown() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;

    [[nodiscard]] Rendering::WindowTarget getWindowTarget() const noexcept;

    [[nodiscard]] bool processMessages() noexcept;

    [[nodiscard]] bool consumeKeyPress(unsigned int virtualKey) noexcept;

    [[nodiscard]] bool consumeVolumeSlider(float& volume) noexcept;

    void setVolumeControl(float volume) noexcept;

    void setMuteControl(bool muted) noexcept;

    void setStatusText(const std::string& text) noexcept;

private:

    struct Impl;

    std::unique_ptr<Impl> impl_{};
};

}
}
}
