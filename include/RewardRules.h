#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace EA::RewardRules {

    enum class QuestSignal : std::uint8_t {
        kCompleted,
        kStarted,
        kReset,
    };

    class QuestLifecycle {
    public:
        [[nodiscard]] bool Observe(std::uint32_t questID, QuestSignal signal);
        void Reset() noexcept;

    private:
        std::unordered_set<std::uint32_t> completed_;
    };

    // LocationCleared::Event carries no location, so rewards are derived from
    // the set of ever-cleared locations: anything cleared since the snapshot
    // taken at load/new game was just cleared. The ever-cleared flag lives in
    // the save, so each location awards at most once per playthrough.
    class ClearedLocationTracker {
    public:
        void Snapshot(std::span<const std::uint32_t> everCleared);
        void Invalidate() noexcept;
        [[nodiscard]] bool Ready() const noexcept { return ready_; }

        // Returns locations newly ever-cleared since the last snapshot or
        // observation. Without a snapshot it only records state, so a missed
        // snapshot can never award every location already cleared in the save.
        [[nodiscard]] std::vector<std::uint32_t> Observe(
            std::span<const std::uint32_t> everCleared);

    private:
        std::unordered_set<std::uint32_t> known_;
        bool                              ready_{ false };
    };

    [[nodiscard]] bool IsObjectiveCompletionTransition(
        std::uint32_t oldState,
        std::uint32_t newState) noexcept;

    [[nodiscard]] bool ShouldRewardBook(
        bool activationSucceeded,
        bool playerActivated,
        bool alreadyRead) noexcept;

    [[nodiscard]] bool ShouldRewardPickpocket(std::int32_t numItems) noexcept;

    // ActorKill fires once per death, so respawned or resurrected actors are
    // eligible again. The player's own summons, thralls, and reanimated
    // corpses never award XP, which prevents reanimate-and-kill farming.
    [[nodiscard]] bool ShouldRewardKill(
        bool playerCredited,
        bool victimIsPlayer,
        bool victimCommandedByPlayer) noexcept;

    [[nodiscard]] float CalculateKillReward(
        float baseXP,
        int enemyLevel,
        int playerLevel,
        float levelScaleFactor,
        float globalMultiplier) noexcept;

    [[nodiscard]] std::string_view ClassifyMarkerType(std::uint16_t markerType) noexcept;
    [[nodiscard]] std::string_view ClassifyLockLevel(std::int32_t lockLevel) noexcept;
}
