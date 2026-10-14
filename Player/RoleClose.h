#pragma once

#include <QUuid>

namespace DesktopSceneEngine::Player
{
struct RoleCloseKey
{
    QUuid generation;
    quint64 request = 0;
    friend bool operator==(const RoleCloseKey&, const RoleCloseKey&) = default;
};

enum class RoleCloseResult : quint8
{
    Received = 0, Pending = 1, CloseAccepted = 2, CloseAborted = 3,
    Rejected = 4, Uncertain = 5, RoleGone = 6
};

struct RoleCloseOperation
{
    RoleCloseKey key;
    RoleCloseResult result = RoleCloseResult::Uncertain;
};

inline bool terminalCloseResult(RoleCloseResult result)
{
    return result == RoleCloseResult::CloseAccepted || result == RoleCloseResult::CloseAborted
        || result == RoleCloseResult::Rejected || result == RoleCloseResult::RoleGone;
}
}
