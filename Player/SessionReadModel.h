#pragma once

#include <QJsonObject>
#include <QUuid>
#include <cstdint>
#include "SessionAuthoredOperation.h"

namespace DesktopSceneEngine::Player
{
class SessionApplication;

// Session-lifetime projection of the application authority and observed platform
// facts. Owns no Project/Engine copies and exposes no mutation API.
class SessionReadModel final
{
public:
    SessionReadModel(const SessionApplication& owner, QUuid incarnation);
    [[nodiscard]] QJsonObject snapshot() const;
    [[nodiscard]] const QUuid& sessionIncarnation() const { return incarnation_; }
    [[nodiscard]] QJsonObject bgmEditorHeader(const QUuid& permit, PlaylistId playlist) const;
    [[nodiscard]] QJsonObject bgmEditorSnapshot(const QUuid& permit, PlaylistId playlist, std::optional<ItemId> item) const;
    // Low-frequency invalidation using existing native status observation. The
    // playback clock is deliberately excluded; B1 is not runtime streaming.
    [[nodiscard]] bool pollChanged();
    // A command read-back carries only this revision change. Leave every other
    // signature field (and any already-unobserved mutation) for normal polling.
    void acknowledgePlaylistMasterRevision(std::uint64_t before);
    void acknowledgeScalarAuthoredRevision(std::uint64_t before);

private:
    [[nodiscard]] QJsonObject desktopSnapshot() const;
    [[nodiscard]] QJsonObject mediaImportSnapshot() const;
    [[nodiscard]] QJsonObject signature() const;
    const SessionApplication& owner_;
    const QUuid incarnation_;
    QJsonObject observed_;
};
}
