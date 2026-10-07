#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace DesktopSceneEngine::Player
{
// An invalidation namespace, not a settings transport or recipient inventory.
inline std::uint32_t globalSettingsChangedMessageId()
{
#ifdef _WIN32
    return RegisterWindowMessageW(L"DesktopSceneEngine.Player.GlobalSettingsChanged.v1");
#else
    return 0U;
#endif
}

inline bool broadcastGlobalSettingsChanged()
{
#ifdef _WIN32
    const auto message = globalSettingsChangedMessageId();
    return message != 0U && PostMessageW(HWND_BROADCAST, message, 0, 0) != FALSE;
#else
    return false;
#endif
}
}
