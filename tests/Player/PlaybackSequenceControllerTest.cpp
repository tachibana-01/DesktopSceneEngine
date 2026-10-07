#include "PlaybackSequenceController.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace
{

using DesktopSceneEngine::Player::PlaybackSequenceController;
using DesktopSceneEngine::Player::PlaybackSequenceLayer;
using DesktopSceneEngine::Player::PlaybackSequenceMode;
using DesktopSceneEngine::Player::PlaybackSequenceResult;
using DesktopSceneEngine::Player::PlaybackSequenceResultKind;

using Id = std::string;
using Layer = PlaybackSequenceLayer<Id>;
using Controller = PlaybackSequenceController<Id, Id>;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

template<typename Identity>
bool isIdentity(
    const PlaybackSequenceResult<Identity>& result,
    const Identity& expected
)
{
    return result.kind == PlaybackSequenceResultKind::Identity
        && result.identity.has_value()
        && *result.identity == expected;
}

template<typename Identity>
bool isRoundComplete(const PlaybackSequenceResult<Identity>& result)
{
    return result.kind == PlaybackSequenceResultKind::RoundComplete
        && !result.identity.has_value();
}

template<typename Identity>
bool isNoTarget(const PlaybackSequenceResult<Identity>& result)
{
    return result.kind == PlaybackSequenceResultKind::NoTarget
        && !result.identity.has_value();
}

Layer::IndexSampler scriptedSampler(std::vector<std::size_t> indices)
{
    struct State
    {
        std::vector<std::size_t> indices{};
        std::size_t next = 0;
    };

    const auto state = std::make_shared<State>(State{std::move(indices), 0});
    return [state](std::size_t count, std::mt19937_64&) {
        const std::size_t value = state->indices.empty()
            ? 0
            : state->indices[std::min(state->next++, state->indices.size() - 1)];
        return value % count;
    };
}

Controller::Collections collections(
    std::vector<Id> playlists,
    std::vector<Controller::PlaylistItems> items
)
{
    return Controller::Collections{std::move(playlists), std::move(items)};
}

bool isPosition(
    const Controller::Result& result,
    const Id& playlist,
    const Id& item
)
{
    return result.kind == PlaybackSequenceResultKind::Identity
        && result.identity.has_value()
        && result.identity->playlistId == playlist
        && result.identity->itemId == item;
}

bool verifyListLoopNavigationAndFiniteInvocation()
{
    const std::vector<Id> order{"A", "B", "C", "D"};
    Layer layer{PlaybackSequenceMode::ListLoop, 1};
    layer.rebase("B", order);

    if (!require(isIdentity(layer.next(order), Id{"C"}),
            "ListLoop next should use the current ordered successor")
        || !require(isIdentity(layer.previous(order), Id{"B"}),
            "ListLoop previous should navigate to the current ordered predecessor")
        || !require(isIdentity(layer.previous(std::vector<Id>{"A", "D", "B", "C"}), Id{"D"}),
            "ListLoop previous should use the latest reordered collection")
        || !require(isIdentity(layer.next(std::vector<Id>{"A", "D", "B", "C"}), Id{"B"}),
            "ListLoop next should use the latest reordered collection")
        || !require(isIdentity(layer.next(order), Id{"C"}),
            "ListLoop navigation should remain valid after a reorder")
        || !require(isIdentity(layer.next(std::vector<Id>{"A", "B", "C"}), Id{"A"}),
            "ListLoop next should wrap for direct navigation")
        || !require(isIdentity(layer.previous(std::vector<Id>{"A", "B", "C"}), Id{"C"}),
            "ListLoop previous should wrap for direct navigation"))
    {
        return false;
    }

    layer.rebase("A", order);
    if (!require(isIdentity(layer.advanceInvocation(order), Id{"B"}),
            "A finite ListLoop invocation should advance from its anchor")
        || !require(isIdentity(layer.advanceInvocation(order), Id{"C"}),
            "A finite ListLoop invocation should continue in order")
        || !require(isIdentity(layer.advanceInvocation(order), Id{"D"}),
            "A finite ListLoop invocation should reach the final item")
        || !require(isRoundComplete(layer.advanceInvocation(order)),
            "A finite ListLoop invocation should complete instead of wrapping internally"))
    {
        return false;
    }

    layer.rebase("C", order);
    if (!require(isIdentity(layer.advanceInvocation(order), Id{"D"}),
            "A manual mid-list ListLoop invocation should keep its anchor")
        || !require(isRoundComplete(layer.advanceInvocation(order)),
            "A manual mid-list ListLoop invocation should complete at the list end"))
    {
        return false;
    }

    layer.rebase("missing", order);
    if (!require(isNoTarget(layer.next(order)),
            "A missing ListLoop anchor must require a caller rebase"))
    {
        return false;
    }

    layer.rebase("A", std::vector<Id>{"A"});
    if (!require(isIdentity(layer.next(std::vector<Id>{"A"}), Id{"A"}),
            "A single ListLoop identity should navigate to itself")
        || !require(isIdentity(layer.previous(std::vector<Id>{"A"}), Id{"A"}),
            "A single ListLoop identity should navigate backward to itself")
        || !require(isRoundComplete(layer.advanceInvocation(std::vector<Id>{"A"})),
            "A single finite invocation should complete safely")
        || !require(isNoTarget(layer.next({})),
            "An empty ListLoop collection must return no target safely"))
    {
        return false;
    }

    return true;
}

bool verifyRandomRoundsAndMutationRules()
{
    const std::vector<Id> order{"A", "B", "C", "D"};
    Layer layer{PlaybackSequenceMode::Random, 1, scriptedSampler({0, 0, 0, 0})};
    layer.rebase("B", order);

    for (std::size_t index = 0; index < order.size(); ++index)
    {
        if (!require(isIdentity(layer.advanceInvocation(order), Id{"A"}),
                "Random must sample with replacement and allow all-same rounds"))
        {
            return false;
        }
    }

    if (!require(isRoundComplete(layer.advanceInvocation(order)),
            "A Random invocation must complete only after all N stored entries are consumed"))
    {
        return false;
    }

    layer.rebase("B", order);
    return require(isNoTarget(layer.previousInInvocation(order)),
        "Random previous at its manual anchor must not invent an identity");
}

bool verifyRandomRoundMutationRules()
{
    const std::vector<Id> order{"A", "B", "C", "D"};

    Layer rewindLayer{PlaybackSequenceMode::Random, 2, scriptedSampler({2, 0, 2})};
    rewindLayer.rebase("B", std::vector<Id>{"A", "B", "C"});
    if (!require(isIdentity(rewindLayer.advanceInvocation({"A", "B", "C"}), Id{"C"}),
            "Random should consume its first generated entry")
        || !require(isIdentity(rewindLayer.advanceInvocation({"A", "B", "C"}), Id{"A"}),
            "Random should consume its second generated entry")
        || !require(isIdentity(rewindLayer.advanceInvocation({"A", "B", "C"}), Id{"C"}),
            "Random should consume each independently sampled duplicate-capable entry")
        || !require(isIdentity(rewindLayer.previousInInvocation({"A", "B", "C"}), Id{"A"}),
            "Random previous should move through consumed entries")
        || !require(isIdentity(rewindLayer.previousInInvocation({"A", "B", "C"}), Id{"C"}),
            "Random previous should retain generated-round history")
        || !require(isIdentity(rewindLayer.advanceInvocation({"A", "B", "C"}), Id{"A"}),
            "Random Next after Prev must reuse the stored entry rather than resample"))
    {
        return false;
    }

    Layer mutationLayer{PlaybackSequenceMode::Random, 3, scriptedSampler({1, 1, 2})};
    mutationLayer.rebase("A", std::vector<Id>{"A", "B", "C"});
    if (!require(isIdentity(mutationLayer.advanceInvocation({"A", "C"}), Id{"C"}),
            "Removed future Random entries should be skipped without regenerating the round")
        || !require(isRoundComplete(mutationLayer.advanceInvocation({"A", "C"})),
            "A Random round should complete when all remaining entries were removed")
        || !require(isNoTarget(mutationLayer.advanceInvocation({"A"})),
            "A removed current Random identity must require an explicit rebase"))
    {
        return false;
    }

    Layer addLayer{PlaybackSequenceMode::Random, 4, scriptedSampler({1, 1, 2, 2, 2})};
    addLayer.rebase("A", std::vector<Id>{"A", "B"});
    if (!require(isIdentity(addLayer.advanceInvocation({"A", "B", "C"}), Id{"B"}),
            "A Random round must not add identities introduced after generation")
        || !require(isIdentity(addLayer.advanceInvocation({"A", "B", "C"}), Id{"B"}),
            "Every stored Random entry remains distinct even when equal")
        || !require(isRoundComplete(addLayer.advanceInvocation({"A", "B", "C"})),
            "A current Random round should retain its original finite length"))
    {
        return false;
    }

    Layer reorderLayer{PlaybackSequenceMode::Random, 5, scriptedSampler({1, 2, 0})};
    reorderLayer.rebase("A", std::vector<Id>{"A", "B", "C"});
    if (!require(isIdentity(reorderLayer.advanceInvocation({"C", "A", "B"}), Id{"B"}),
            "Random reordering must not change already-generated stable identities")
        || !require(isIdentity(reorderLayer.advanceInvocation({"C", "A", "B"}), Id{"C"}),
            "Random reordering must preserve the stored generated order"))
    {
        return false;
    }

    return true;
}

bool verifyModesAndClear()
{
    const std::vector<Id> order{"A", "B", "C"};
    Layer layer{PlaybackSequenceMode::ListLoop, 7, scriptedSampler({0, 0, 0})};
    layer.rebase("B", order);

    layer.setMode(PlaybackSequenceMode::RepeatCurrent, order);
    if (!require(isIdentity(layer.advanceInvocation(order), Id{"B"}),
            "RepeatCurrent should repeat the preserved anchor")
        || !require(isIdentity(layer.previousInInvocation(order), Id{"B"}),
            "RepeatCurrent previous should preserve the current identity"))
    {
        return false;
    }

    layer.setMode(PlaybackSequenceMode::Random, order);
    if (!require(isIdentity(layer.advanceInvocation(order), Id{"A"}),
            "RepeatCurrent to Random should preserve the anchor and generate a fresh future round"))
    {
        return false;
    }

    layer.setMode(PlaybackSequenceMode::RepeatCurrent, order);
    if (!require(isIdentity(layer.next(order), Id{"A"}),
            "Random to RepeatCurrent should discard random future state without moving the anchor"))
    {
        return false;
    }

    layer.setMode(PlaybackSequenceMode::ListLoop, std::vector<Id>{"C", "B", "A"});
    if (!require(isIdentity(layer.next({"C", "B", "A"}), Id{"C"}),
            "RepeatCurrent to ListLoop should preserve the anchor and use latest list order"))
    {
        return false;
    }

    Layer rebased{PlaybackSequenceMode::Random, 8, scriptedSampler({0, 0, 0, 1, 1, 1})};
    rebased.rebase("B", order);
    if (!require(isIdentity(rebased.advanceInvocation(order), Id{"A"}),
            "A Random round should begin from its first stored entry"))
    {
        return false;
    }

    rebased.rebase("C", order);
    if (!require(isIdentity(rebased.advanceInvocation(order), Id{"B"}),
            "A manual rebase must discard stale Random future history"))
    {
        return false;
    }

    layer.clear();
    return require(!layer.current().has_value(),
               "Clear should discard the current transient anchor")
        && require(isNoTarget(layer.next(order)),
            "Clear should discard generated sequence state");
}

bool verifyTwoLevelCompositionAndBoundedHistory()
{
    const auto state = collections(
        {"A", "B"},
        {{"A", {"a1", "a2"}}, {"B", {"b1", "b2", "b3"}}});
    Controller controller{11};
    controller.setPlaylistMode(PlaybackSequenceMode::ListLoop, state);
    controller.rebase(state, "A", "a1");

    if (!require(isPosition(controller.completeCurrentItem(state), "A", "a2"),
            "The first Playlist invocation should advance through its items")
        || !require(isPosition(controller.completeCurrentItem(state), "B", "b1"),
            "A completed inner invocation should advance the outer Playlist sequence")
        || !require(isPosition(controller.completeCurrentItem(state), "B", "b2"),
            "The next Playlist should begin a fresh inner invocation")
        || !require(isPosition(controller.completeCurrentItem(state), "B", "b3"),
            "The fresh inner invocation should remain finite")
        || !require(isPosition(controller.previous(state), "B", "b2"),
            "Previous should move inside the current invocation")
        || !require(isPosition(controller.previous(state), "B", "b1"),
            "Previous should continue to the current invocation anchor")
        || !require(isPosition(controller.previous(state), "A", "a2"),
            "Previous at an invocation anchor should use bounded prior invocation history")
        || !require(isPosition(controller.completeCurrentItem(state), "B", "b1"),
            "Forward after a cross-Playlist previous should restore the known invocation without resampling"))
    {
        return false;
    }

    Controller midList{12};
    midList.setPlaylistMode(PlaybackSequenceMode::ListLoop, state);
    const auto midState = collections(
        {"A", "B"},
        {{"A", {"a", "b", "c", "d"}}, {"B", {"b1"}}});
    midList.rebase(midState, "A", "c");
    return require(isPosition(midList.completeCurrentItem(midState), "A", "d"),
               "A manual inner rebase should retain the chosen mid-list anchor")
        && require(isPosition(midList.completeCurrentItem(midState), "B", "b1"),
            "A manual mid-list invocation should complete at the list boundary");
}

bool verifyTwoLevelRepeatRandomAndMutation()
{
    const auto state = collections(
        {"A", "B"},
        {{"A", {"a1", "a2"}}, {"B", {"b1", "b2"}}});

    Controller repeatPlaylist{13};
    repeatPlaylist.setPlaylistMode(PlaybackSequenceMode::RepeatCurrent, state);
    repeatPlaylist.rebase(state, "A", "a1");
    if (!require(isPosition(repeatPlaylist.completeCurrentItem(state), "A", "a2"),
            "RepeatCurrent Playlist should permit its finite inner invocation to finish")
        || !require(isPosition(repeatPlaylist.completeCurrentItem(state), "A", "a1"),
            "RepeatCurrent Playlist should begin a fresh inner invocation for the same Playlist"))
    {
        return false;
    }

    const auto repeatItemState = collections(
        {"A", "B"},
        {{"A", {"a1", "a2"}, PlaybackSequenceMode::RepeatCurrent},
            {"B", {"b1", "b2"}, PlaybackSequenceMode::ListLoop}});
    Controller repeatItem{14};
    repeatItem.setPlaylistMode(PlaybackSequenceMode::ListLoop, repeatItemState);
    repeatItem.rebase(repeatItemState, "A", "a1");
    if (!require(isPosition(repeatItem.completeCurrentItem(repeatItemState), "A", "a1"),
            "RepeatCurrent Item should repeat without completing its Playlist invocation")
        || !require(isPosition(repeatItem.completeCurrentItem(repeatItemState), "A", "a1"),
            "RepeatCurrent Item should remain stable across repeated completions"))
    {
        return false;
    }

    Controller randomController{
        15,
        scriptedSampler({1, 1, 1, 1}),
        scriptedSampler({1, 1, 1, 1})};
    const auto randomState = collections(
        {"A", "B"},
        {{"A", {"a1", "a2"}, PlaybackSequenceMode::Random},
            {"B", {"b1", "b2"}, PlaybackSequenceMode::Random}});
    randomController.setPlaylistMode(PlaybackSequenceMode::Random, randomState);
    randomController.rebase(randomState, "A", "a1");
    if (!require(isPosition(randomController.completeCurrentItem(randomState), "A", "a2"),
            "An inner Random invocation should consume a stored random Item entry")
        || !require(isPosition(randomController.completeCurrentItem(randomState), "A", "a2"),
            "An inner Random round should retain each generated duplicate occurrence")
        || !require(isPosition(randomController.completeCurrentItem(randomState), "B", "b1"),
            "An outer Random invocation should consume its stored Playlist entry")
        || !require(isPosition(randomController.completeCurrentItem(randomState), "B", "b2"),
            "Each selected Playlist should receive its own fresh inner Random round")
        || !require(isPosition(randomController.completeCurrentItem(randomState), "B", "b2"),
            "The fresh inner Random round should retain its independent generated entries")
        || !require(isPosition(randomController.completeCurrentItem(randomState), "B", "b1"),
            "Outer Random duplicates must remain legal across Playlist invocations"))
    {
        return false;
    }

    const auto mutationInitial = collections(
        {"A", "B", "C"},
        {{"A", {"a1", "a2"}}, {"B", {"b1"}}, {"C", {"c1"}}});
    Controller mutationController{16, scriptedSampler({1, 2, 0}), scriptedSampler({0, 0})};
    mutationController.setPlaylistMode(PlaybackSequenceMode::Random, mutationInitial);
    mutationController.rebase(mutationInitial, "A", "a1");
    const auto removedFuturePlaylist = collections(
        {"A", "C"},
        {{"A", {"a1", "a2"}}, {"C", {"c1"}}});
    if (!require(isPosition(mutationController.completeCurrentItem(removedFuturePlaylist), "A", "a2"),
            "A current item should use the latest item collection")
        || !require(isPosition(mutationController.completeCurrentItem(removedFuturePlaylist), "C", "c1"),
            "Removed future outer Random identities should be skipped boundedly"))
    {
        return false;
    }

    const auto currentDeleted = collections(
        {"A", "B"},
        {{"A", {}}, {"B", {"b1"}}});
    Controller deletedCurrent{17};
    deletedCurrent.setPlaylistMode(PlaybackSequenceMode::ListLoop, state);
    deletedCurrent.rebase(state, "A", "a1");
    if (!require(isNoTarget(deletedCurrent.completeCurrentItem(currentDeleted)),
               "A deleted current Item must require a caller rebase rather than guessing")
        && require(isNoTarget(deletedCurrent.previous(currentDeleted)),
            "A deleted current Item must never be dereferenced during Previous"))
    {
        return false;
    }

    const auto addOuterInitial = collections(
        {"A", "B"},
        {{"A", {"a1"}}, {"B", {"b1"}}});
    const auto addOuterCurrent = collections(
        {"A", "B", "C"},
        {{"A", {"a1"}}, {"B", {"b1"}}, {"C", {"c1"}}});
    Controller addOuter{18, scriptedSampler({1, 1, 2, 2, 2})};
    addOuter.setPlaylistMode(PlaybackSequenceMode::Random, addOuterInitial);
    addOuter.rebase(addOuterInitial, "A", "a1");
    if (!require(isPosition(addOuter.completeCurrentItem(addOuterCurrent), "B", "b1"),
               "A Playlist added during a Random round must not enter its first generated outer round")
        && require(isPosition(addOuter.completeCurrentItem(addOuterCurrent), "B", "b1"),
            "The existing outer Random round must retain every stored occurrence before regeneration")
        && require(isRoundComplete(addOuter.completeCurrentItem(addOuterCurrent)),
            "Completing the frozen outer Random round should expose its boundary")
        && require(isPosition(addOuter.completeCurrentItem(addOuterCurrent), "C", "c1"),
            "An added Playlist should become eligible only in the next generated outer Random round"))
    {
        return false;
    }

    const auto emptySelectionState = collections(
        {"A", "B", "C"},
        {{"A", {"a1"}}, {"B", {}}, {"C", {"c1"}}});
    Controller emptySelections{19, scriptedSampler({1, 1, 1, 2, 2, 2})};
    emptySelections.setPlaylistMode(PlaybackSequenceMode::Random, emptySelectionState);
    emptySelections.rebase(emptySelectionState, "A", "a1");
    if (!require(isRoundComplete(emptySelections.completeCurrentItem(emptySelectionState)),
            "An all-empty generated outer Random round must complete without reporting a false stop")
        || !require(isPosition(emptySelections.completeCurrentItem(emptySelectionState), "C", "c1"),
            "The next generated outer Random round should still reach a remaining playable Playlist"))
    {
        return false;
    }

    return true;
}

bool verifyPerPlaylistItemModes()
{
    const auto listThenRandom = collections(
        {"A", "B"},
        {{"A", {"a1", "a2"}, PlaybackSequenceMode::ListLoop},
            {"B", {"b1", "b2"}, PlaybackSequenceMode::Random}});
    Controller controller{20, {}, scriptedSampler({1, 1, 1})};
    controller.setPlaylistMode(PlaybackSequenceMode::ListLoop, listThenRandom);
    controller.rebase(listThenRandom, "A", "a1");

    if (!require(isPosition(controller.completeCurrentItem(listThenRandom), "A", "a2"),
            "Playlist A must use its own ListLoop Item mode")
        || !require(isPosition(controller.completeCurrentItem(listThenRandom), "B", "b1"),
            "A completed Playlist A invocation must enter Playlist B at its first Item")
        || !require(isPosition(controller.completeCurrentItem(listThenRandom), "B", "b2"),
            "Playlist B must use its own Random Item mode after an outer transition"))
    {
        return false;
    }

    controller.rebase(listThenRandom, "A", "a1");
    if (!require(isPosition(controller.completeCurrentItem(listThenRandom), "A", "a2"),
            "A direct rebase must recover Playlist A's ListLoop Item mode"))
    {
        return false;
    }

    controller.rebase(listThenRandom, "B", "b1");
    if (!require(isPosition(controller.completeCurrentItem(listThenRandom), "B", "b2"),
            "A direct rebase must recover Playlist B's Random Item mode"))
    {
        return false;
    }

    const auto randomThenRepeat = collections(
        {"A", "B"},
        {{"A", {"a1", "a2"}, PlaybackSequenceMode::Random},
            {"B", {"b1", "b2"}, PlaybackSequenceMode::RepeatCurrent}});
    Controller secondController{21, {}, scriptedSampler({1, 1, 1})};
    secondController.setPlaylistMode(PlaybackSequenceMode::ListLoop, randomThenRepeat);
    secondController.rebase(randomThenRepeat, "A", "a1");
    if (!require(isPosition(secondController.completeCurrentItem(randomThenRepeat), "A", "a2"),
            "Playlist A Random mode must remain independent from Playlist B")
        || !require(isPosition(secondController.completeCurrentItem(randomThenRepeat), "A", "a2"),
            "A Random round must retain its independently generated duplicate-capable entries"))
    {
        return false;
    }

    secondController.rebase(randomThenRepeat, "B", "b1");
    return require(
        isPosition(secondController.completeCurrentItem(randomThenRepeat), "B", "b1"),
        "Playlist B RepeatCurrent mode must not inherit Playlist A's Random mode");
}

}

int main()
{
    return verifyListLoopNavigationAndFiniteInvocation()
            && verifyRandomRoundsAndMutationRules()
            && verifyRandomRoundMutationRules()
            && verifyModesAndClear()
            && verifyTwoLevelCompositionAndBoundedHistory()
            && verifyTwoLevelRepeatRandomAndMutation()
            && verifyPerPlaylistItemModes()
        ? 0
        : 1;
}
