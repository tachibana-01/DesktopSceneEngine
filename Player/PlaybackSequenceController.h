#pragma once

#include "PlaybackSequenceMode.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace DesktopSceneEngine::Player
{

enum class PlaybackSequenceResultKind
{
    Identity,
    RoundComplete,
    NoTarget
};

template<typename Identity>
struct PlaybackSequenceResult
{
    PlaybackSequenceResultKind kind = PlaybackSequenceResultKind::NoTarget;
    std::optional<Identity> identity{};

    [[nodiscard]] static PlaybackSequenceResult selected(Identity value)
    {
        return {PlaybackSequenceResultKind::Identity, std::move(value)};
    }

    [[nodiscard]] static PlaybackSequenceResult roundComplete()
    {
        return {PlaybackSequenceResultKind::RoundComplete, std::nullopt};
    }

    [[nodiscard]] static PlaybackSequenceResult noTarget()
    {
        return {PlaybackSequenceResultKind::NoTarget, std::nullopt};
    }
};

// A Qt-free, stable-identity sequence primitive. The authoritative collection
// is always supplied by the caller, so this class only retains transient
// anchors and random-round state.
template<typename Identity>
class PlaybackSequenceLayer
{
public:
    using Result = PlaybackSequenceResult<Identity>;
    using IndexSampler = std::function<std::size_t(std::size_t, std::mt19937_64&)>;

    explicit PlaybackSequenceLayer(
        PlaybackSequenceMode mode = PlaybackSequenceMode::ListLoop,
        std::uint64_t randomSeed = 0,
        IndexSampler sampler = {}
    )
        : mode_(mode)
        , random_(randomSeed)
        , sampler_(std::move(sampler))
    {
    }

    [[nodiscard]] PlaybackSequenceMode mode() const noexcept
    {
        return mode_;
    }

    [[nodiscard]] const std::optional<Identity>& current() const noexcept
    {
        return current_;
    }

    void setMode(PlaybackSequenceMode mode, const std::vector<Identity>& orderedIds)
    {
        mode_ = mode;
        invocationAnchor_ = current_;
        clearRandomRound();

        if (mode_ == PlaybackSequenceMode::Random && current_.has_value())
        {
            generateRandomRound(orderedIds);
        }
    }

    void rebase(Identity identity, const std::vector<Identity>& orderedIds)
    {
        current_ = std::move(identity);
        invocationAnchor_ = current_;
        clearRandomRound();

        if (mode_ == PlaybackSequenceMode::Random)
        {
            generateRandomRound(orderedIds);
        }
    }

    void clear()
    {
        current_.reset();
        invocationAnchor_.reset();
        clearRandomRound();
    }

    // Navigation is intentionally distinct from a finite invocation. ListLoop
    // wraps here so Player can preserve direct manual Next/Previous behavior.
    [[nodiscard]] Result next(const std::vector<Identity>& orderedIds)
    {
        if (!hasValidCurrent(orderedIds))
        {
            return Result::noTarget();
        }

        switch (mode_)
        {
        case PlaybackSequenceMode::ListLoop:
            return moveListForward(orderedIds, true);
        case PlaybackSequenceMode::Random:
            return moveRandomForward(orderedIds);
        case PlaybackSequenceMode::RepeatCurrent:
            return Result::selected(*current_);
        }

        return Result::noTarget();
    }

    [[nodiscard]] Result previous(const std::vector<Identity>& orderedIds)
    {
        if (!hasValidCurrent(orderedIds))
        {
            return Result::noTarget();
        }

        switch (mode_)
        {
        case PlaybackSequenceMode::ListLoop:
            return moveListBackward(orderedIds, true);
        case PlaybackSequenceMode::Random:
            return moveRandomBackward(orderedIds);
        case PlaybackSequenceMode::RepeatCurrent:
            return Result::selected(*current_);
        }

        return Result::noTarget();
    }

    // A finite invocation never wraps ListLoop internally. Its boundary is a
    // fact for the outer Playlist sequence, not an implicit Item restart.
    [[nodiscard]] Result advanceInvocation(const std::vector<Identity>& orderedIds)
    {
        if (!hasValidCurrent(orderedIds))
        {
            return Result::noTarget();
        }

        switch (mode_)
        {
        case PlaybackSequenceMode::ListLoop:
            return moveListForward(orderedIds, false);
        case PlaybackSequenceMode::Random:
            return moveRandomForward(orderedIds);
        case PlaybackSequenceMode::RepeatCurrent:
            return Result::selected(*current_);
        }

        return Result::noTarget();
    }

    // Previous within a finite invocation stops at its manual/fresh anchor.
    // The two-level controller can then consult its bounded outer history.
    [[nodiscard]] Result previousInInvocation(const std::vector<Identity>& orderedIds)
    {
        if (!hasValidCurrent(orderedIds))
        {
            return Result::noTarget();
        }

        switch (mode_)
        {
        case PlaybackSequenceMode::ListLoop:
            if (invocationAnchor_.has_value() && *current_ == *invocationAnchor_)
            {
                return Result::noTarget();
            }
            return moveListBackward(orderedIds, false);
        case PlaybackSequenceMode::Random:
            return moveRandomBackward(orderedIds);
        case PlaybackSequenceMode::RepeatCurrent:
            return Result::selected(*current_);
        }

        return Result::noTarget();
    }

private:
    [[nodiscard]] static bool contains(
        const std::vector<Identity>& orderedIds,
        const Identity& identity
    )
    {
        return std::find(orderedIds.begin(), orderedIds.end(), identity) != orderedIds.end();
    }

    [[nodiscard]] bool hasValidCurrent(const std::vector<Identity>& orderedIds) const
    {
        return current_.has_value() && contains(orderedIds, *current_);
    }

    [[nodiscard]] Result moveListForward(
        const std::vector<Identity>& orderedIds,
        bool allowWrap
    )
    {
        const auto current = std::find(orderedIds.begin(), orderedIds.end(), *current_);
        if (current == orderedIds.end())
        {
            return Result::noTarget();
        }

        const auto next = std::next(current);
        if (next == orderedIds.end())
        {
            if (!allowWrap)
            {
                return Result::roundComplete();
            }

            current_ = orderedIds.front();
            return Result::selected(*current_);
        }

        current_ = *next;
        return Result::selected(*current_);
    }

    [[nodiscard]] Result moveListBackward(
        const std::vector<Identity>& orderedIds,
        bool allowWrap
    )
    {
        const auto current = std::find(orderedIds.begin(), orderedIds.end(), *current_);
        if (current == orderedIds.end())
        {
            return Result::noTarget();
        }

        if (current == orderedIds.begin())
        {
            if (!allowWrap)
            {
                return Result::noTarget();
            }

            current_ = orderedIds.back();
            return Result::selected(*current_);
        }

        current_ = *std::prev(current);
        return Result::selected(*current_);
    }

    void clearRandomRound()
    {
        randomRound_.clear();
        nextRandomIndex_ = 0;
        currentRandomIndex_.reset();
    }

    void generateRandomRound(const std::vector<Identity>& orderedIds)
    {
        randomRound_.reserve(orderedIds.size());
        for (std::size_t index = 0; index < orderedIds.size(); ++index)
        {
            randomRound_.push_back(orderedIds[sampleIndex(orderedIds.size())]);
        }
    }

    [[nodiscard]] std::size_t sampleIndex(std::size_t count)
    {
        if (sampler_)
        {
            return sampler_(count, random_) % count;
        }

        std::uniform_int_distribution<std::size_t> distribution{0, count - 1};
        return distribution(random_);
    }

    [[nodiscard]] Result moveRandomForward(const std::vector<Identity>& orderedIds)
    {
        while (nextRandomIndex_ < randomRound_.size())
        {
            const std::size_t index = nextRandomIndex_++;
            const auto& candidate = randomRound_[index];
            if (!contains(orderedIds, candidate))
            {
                continue;
            }

            current_ = candidate;
            currentRandomIndex_ = index;
            return Result::selected(*current_);
        }

        return Result::roundComplete();
    }

    [[nodiscard]] Result moveRandomBackward(const std::vector<Identity>& orderedIds)
    {
        if (!currentRandomIndex_.has_value())
        {
            return Result::noTarget();
        }

        std::size_t index = *currentRandomIndex_;
        while (index > 0)
        {
            --index;
            if (!contains(orderedIds, randomRound_[index]))
            {
                continue;
            }

            current_ = randomRound_[index];
            currentRandomIndex_ = index;
            nextRandomIndex_ = index + 1;
            return Result::selected(*current_);
        }

        if (!invocationAnchor_.has_value() || !contains(orderedIds, *invocationAnchor_))
        {
            return Result::noTarget();
        }

        current_ = *invocationAnchor_;
        currentRandomIndex_.reset();
        nextRandomIndex_ = 0;
        return Result::selected(*current_);
    }

    PlaybackSequenceMode mode_ = PlaybackSequenceMode::ListLoop;
    std::optional<Identity> current_{};
    std::optional<Identity> invocationAnchor_{};
    std::mt19937_64 random_{};
    IndexSampler sampler_{};
    std::vector<Identity> randomRound_{};
    std::size_t nextRandomIndex_ = 0;
    std::optional<std::size_t> currentRandomIndex_{};
};

// Coordinates only transient two-level identity decisions. Collections remain
// caller-owned, and the controller keeps one prior invocation plus one forward
// restoration slot so cross-Playlist Previous never needs unbounded history.
template<typename PlaylistIdentity, typename ItemIdentity>
class PlaybackSequenceController
{
public:
    using PlaylistLayer = PlaybackSequenceLayer<PlaylistIdentity>;
    using ItemLayer = PlaybackSequenceLayer<ItemIdentity>;
    using PlaylistIndexSampler = typename PlaylistLayer::IndexSampler;
    using ItemIndexSampler = typename ItemLayer::IndexSampler;

    struct Position
    {
        PlaylistIdentity playlistId{};
        ItemIdentity itemId{};

        friend bool operator==(const Position&, const Position&) = default;
    };

    struct PlaylistItems
    {
        PlaylistIdentity playlistId{};
        std::vector<ItemIdentity> itemIds{};
        PlaybackSequenceMode itemMode = PlaybackSequenceMode::ListLoop;
    };

    struct Collections
    {
        std::vector<PlaylistIdentity> playlistIds{};
        std::vector<PlaylistItems> itemsByPlaylist{};
    };

    using Result = PlaybackSequenceResult<Position>;

    explicit PlaybackSequenceController(
        std::uint64_t randomSeed = 0,
        PlaylistIndexSampler playlistSampler = {},
        ItemIndexSampler itemSampler = {}
    )
        : playlistLayer_(PlaybackSequenceMode::ListLoop, randomSeed, std::move(playlistSampler))
        , innerSeedGenerator_(randomSeed ^ 0x9e3779b97f4a7c15ULL)
        , itemSampler_(std::move(itemSampler))
    {
    }

    [[nodiscard]] PlaybackSequenceMode playlistMode() const noexcept
    {
        return playlistMode_;
    }

    [[nodiscard]] std::optional<Position> current() const
    {
        if (!currentInvocation_.has_value() || !currentInvocation_->itemLayer.current().has_value())
        {
            return std::nullopt;
        }

        return Position{
            currentInvocation_->playlistId,
            *currentInvocation_->itemLayer.current()};
    }

    void setPlaylistMode(PlaybackSequenceMode mode, const Collections& collections)
    {
        playlistMode_ = mode;
        playlistLayer_.setMode(mode, collections.playlistIds);
        clearInvocationHistory();
    }

    // The Workspace snapshot owns per-Playlist Item modes. This only refreshes
    // the active invocation when its configured mode changes; it never moves
    // the current identity or restarts media.
    void synchronizeItemMode(const Collections& collections)
    {
        if (currentInvocation_.has_value())
        {
            if (const auto* entry = playlistItemsFor(collections, currentInvocation_->playlistId))
            {
                if (currentInvocation_->itemLayer.mode() != entry->itemMode)
                {
                    currentInvocation_->itemLayer.setMode(entry->itemMode, entry->itemIds);
                    clearInvocationHistory();
                }
            }
        }
    }

    void rebase(
        const Collections& collections,
        PlaylistIdentity playlistId,
        ItemIdentity itemId
    )
    {
        playlistLayer_.rebase(playlistId, collections.playlistIds);
        currentInvocation_ = Invocation{
            playlistId,
            makeItemLayer(collections, playlistId)};

        if (const auto* items = itemsFor(collections, currentInvocation_->playlistId))
        {
            currentInvocation_->itemLayer.rebase(std::move(itemId), *items);
        }
        else
        {
            currentInvocation_->itemLayer.rebase(std::move(itemId), {});
        }

        clearInvocationHistory();
    }

    void clear()
    {
        playlistLayer_.clear();
        currentInvocation_.reset();
        clearInvocationHistory();
    }

    // Called when the active Item completed naturally. ListLoop Item rounds are
    // finite here; outer policy chooses the next Playlist only at that boundary.
    [[nodiscard]] Result completeCurrentItem(const Collections& collections)
    {
        if (!hasValidCurrentInvocation(collections))
        {
            return Result::noTarget();
        }

        const auto* items = itemsFor(collections, currentInvocation_->playlistId);
        const auto itemResult = currentInvocation_->itemLayer.advanceInvocation(*items);
        if (itemResult.kind == PlaybackSequenceResultKind::Identity)
        {
            return currentResult();
        }

        if (itemResult.kind == PlaybackSequenceResultKind::NoTarget)
        {
            return Result::noTarget();
        }

        return advanceOuterInvocation(collections);
    }

    // Previous stays inside the finite current Item invocation first. At its
    // anchor it can restore only the immediately previous known invocation.
    [[nodiscard]] Result previous(const Collections& collections)
    {
        if (!hasValidCurrentInvocation(collections))
        {
            return Result::noTarget();
        }

        const auto* items = itemsFor(collections, currentInvocation_->playlistId);
        const auto itemResult = currentInvocation_->itemLayer.previousInInvocation(*items);
        if (itemResult.kind == PlaybackSequenceResultKind::Identity)
        {
            return currentResult();
        }

        if (!previousInvocation_.has_value()
            || !hasValidInvocation(*previousInvocation_, collections))
        {
            return Result::noTarget();
        }

        const auto playlistSnapshot = playlistLayer_;
        const auto playlistResult = playlistLayer_.previous(collections.playlistIds);
        if (playlistResult.kind != PlaybackSequenceResultKind::Identity
            || *playlistResult.identity != previousInvocation_->playlistId)
        {
            playlistLayer_ = playlistSnapshot;
            return Result::noTarget();
        }

        forwardInvocation_ = *currentInvocation_;
        currentInvocation_ = *previousInvocation_;
        previousInvocation_.reset();
        return currentResult();
    }

private:
    struct Invocation
    {
        PlaylistIdentity playlistId{};
        ItemLayer itemLayer{};
    };

    [[nodiscard]] static bool containsPlaylist(
        const std::vector<PlaylistIdentity>& playlistIds,
        const PlaylistIdentity& playlistId
    )
    {
        return std::find(playlistIds.begin(), playlistIds.end(), playlistId) != playlistIds.end();
    }

    [[nodiscard]] static bool containsItem(
        const std::vector<ItemIdentity>& itemIds,
        const ItemIdentity& itemId
    )
    {
        return std::find(itemIds.begin(), itemIds.end(), itemId) != itemIds.end();
    }

    [[nodiscard]] static const PlaylistItems* playlistItemsFor(
        const Collections& collections,
        const PlaylistIdentity& playlistId
    )
    {
        const auto found = std::find_if(
            collections.itemsByPlaylist.begin(),
            collections.itemsByPlaylist.end(),
            [&playlistId](const PlaylistItems& entry) {
                return entry.playlistId == playlistId;
            });

        return found == collections.itemsByPlaylist.end()
            ? nullptr
            : &*found;
    }

    [[nodiscard]] static const std::vector<ItemIdentity>* itemsFor(
        const Collections& collections,
        const PlaylistIdentity& playlistId
    )
    {
        const auto* entry = playlistItemsFor(collections, playlistId);
        return entry == nullptr ? nullptr : &entry->itemIds;
    }

    [[nodiscard]] ItemLayer makeItemLayer(
        const Collections& collections,
        const PlaylistIdentity& playlistId
    )
    {
        const auto* entry = playlistItemsFor(collections, playlistId);
        const PlaybackSequenceMode mode = entry == nullptr
            ? PlaybackSequenceMode::ListLoop
            : entry->itemMode;
        return ItemLayer{mode, innerSeedGenerator_(), itemSampler_};
    }

    [[nodiscard]] bool hasValidInvocation(
        const Invocation& invocation,
        const Collections& collections
    ) const
    {
        if (!containsPlaylist(collections.playlistIds, invocation.playlistId))
        {
            return false;
        }

        const auto* items = itemsFor(collections, invocation.playlistId);
        return items != nullptr
            && invocation.itemLayer.current().has_value()
            && containsItem(*items, *invocation.itemLayer.current());
    }

    [[nodiscard]] bool hasValidCurrentInvocation(const Collections& collections) const
    {
        return currentInvocation_.has_value()
            && playlistLayer_.current().has_value()
            && *playlistLayer_.current() == currentInvocation_->playlistId
            && hasValidInvocation(*currentInvocation_, collections);
    }

    [[nodiscard]] bool hasPlayablePlaylist(const Collections& collections) const
    {
        return std::any_of(
            collections.playlistIds.begin(),
            collections.playlistIds.end(),
            [&collections](const PlaylistIdentity& playlistId) {
                const auto* items = itemsFor(collections, playlistId);
                return items != nullptr && !items->empty();
            });
    }

    void clearInvocationHistory()
    {
        previousInvocation_.reset();
        forwardInvocation_.reset();
    }

    [[nodiscard]] Result currentResult() const
    {
        const auto position = current();
        return position.has_value()
            ? Result::selected(*position)
            : Result::noTarget();
    }

    [[nodiscard]] Result advanceOuterInvocation(const Collections& collections)
    {
        if (!hasPlayablePlaylist(collections))
        {
            return Result::noTarget();
        }

        const auto completedInvocation = *currentInvocation_;
        const auto playlistSnapshot = playlistLayer_;
        const std::size_t maximumAttempts = collections.playlistIds.size();

        for (std::size_t attempt = 0; attempt < maximumAttempts; ++attempt)
        {
            auto playlistResult = playlistLayer_.next(collections.playlistIds);
            if (playlistResult.kind == PlaybackSequenceResultKind::RoundComplete)
            {
                if (playlistMode_ != PlaybackSequenceMode::Random)
                {
                    playlistLayer_ = playlistSnapshot;
                    return Result::noTarget();
                }

                playlistLayer_.rebase(
                    completedInvocation.playlistId,
                    collections.playlistIds);
                return Result::roundComplete();
            }

            if (playlistResult.kind != PlaybackSequenceResultKind::Identity)
            {
                playlistLayer_ = playlistSnapshot;
                return Result::noTarget();
            }

            const auto* items = itemsFor(collections, *playlistResult.identity);
            if (items == nullptr || items->empty())
            {
                continue;
            }

            previousInvocation_ = completedInvocation;
            if (forwardInvocation_.has_value()
                && forwardInvocation_->playlistId == *playlistResult.identity
                && hasValidInvocation(*forwardInvocation_, collections))
            {
                currentInvocation_ = *forwardInvocation_;
                forwardInvocation_.reset();
            }
            else
            {
                currentInvocation_ = Invocation{
                    *playlistResult.identity,
                    makeItemLayer(collections, *playlistResult.identity)};
                currentInvocation_->itemLayer.rebase(items->front(), *items);
                forwardInvocation_.reset();
            }

            return currentResult();
        }

        if (playlistMode_ == PlaybackSequenceMode::Random)
        {
            playlistLayer_.rebase(
                completedInvocation.playlistId,
                collections.playlistIds);
            return Result::roundComplete();
        }

        playlistLayer_ = playlistSnapshot;
        return Result::noTarget();
    }

    PlaybackSequenceMode playlistMode_ = PlaybackSequenceMode::ListLoop;
    PlaylistLayer playlistLayer_{};
    std::mt19937_64 innerSeedGenerator_{};
    ItemIndexSampler itemSampler_{};
    std::optional<Invocation> currentInvocation_{};
    std::optional<Invocation> previousInvocation_{};
    std::optional<Invocation> forwardInvocation_{};
};

}
