#pragma once

#include <array>
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

    // Detects forms whose saved, one-way game flag was set since a snapshot
    // taken at load/new game: ever-cleared locations and read books. Used
    // where events do not identify the form (LocationCleared::Event is empty,
    // inventory reading bypasses TESObjectBOOK::Activate). The flags live in
    // the save, so each form awards at most once per playthrough.
    class NewlyFlaggedTracker {
    public:
        void Snapshot(std::span<const std::uint32_t> flagged);
        void Invalidate() noexcept;
        [[nodiscard]] bool Ready() const noexcept { return ready_; }

        // Returns forms newly flagged since the last snapshot or observation.
        // Without a snapshot it only records state, so a missed snapshot can
        // never award every form already flagged in the save.
        [[nodiscard]] std::vector<std::uint32_t> Observe(
            std::span<const std::uint32_t> flagged);

    private:
        std::unordered_set<std::uint32_t> known_;
        bool                              ready_{ false };
    };

    // Misc quest scripts often complete every remaining objective (including
    // branches the player never took) in the same frame when the errand ends.
    // Completions are collected for one frame and each quest's batch pays a
    // single objective reward, while one-at-a-time completions pay normally.
    struct ObjectiveBatch {
        std::uint32_t questID{ 0 };
        std::uint32_t firstObjectiveIndex{ 0 };
        std::uint32_t count{ 0 };

        friend bool operator==(const ObjectiveBatch&, const ObjectiveBatch&) = default;
    };

    class ObjectiveBatcher {
    public:
        // Returns true when this is the first completion since the last
        // flush, i.e. the caller should schedule a flush for the next frame.
        bool Add(std::uint32_t questID, std::uint32_t objectiveIndex);
        // One entry per quest, in order of first completion; clears state.
        [[nodiscard]] std::vector<ObjectiveBatch> Flush();
        void Reset() noexcept { batches_.clear(); }
        [[nodiscard]] bool Empty() const noexcept { return batches_.empty(); }

    private:
        std::vector<ObjectiveBatch> batches_;
    };

    [[nodiscard]] bool IsObjectiveCompletionTransition(
        std::uint32_t oldState,
        std::uint32_t newState) noexcept;

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

    // Groups every award source for its level-scaling weight.
    enum class RewardSource : std::uint8_t {
        kQuest,
        kKill,
        kExploration,
        kLock,
        kBook,
        kPickpocket,
        kCount
    };

    // Maps an AwardContext source key (e.g. "quest_main", "kill",
    // "location_cleared", "lock_picked", "book_skill", "pickpocket").
    [[nodiscard]] RewardSource ClassifyRewardSource(std::string_view sourceKey) noexcept;

    // Per-source XP totals for the in-game Stats page. Session-only.
    struct SourceTotals {
        double xp{ 0.0 };
        std::uint32_t count{ 0 };
    };

    class SessionStats {
    public:
        void Add(RewardSource source, double xp) noexcept;
        void Reset() noexcept;
        [[nodiscard]] double TotalXP() const noexcept;
        [[nodiscard]] std::uint32_t TotalCount() const noexcept;
        [[nodiscard]] const SourceTotals& For(RewardSource source) const noexcept;
        // Fraction (0-1) of all session XP that came from this source.
        [[nodiscard]] double Share(RewardSource source) const noexcept;

    private:
        std::array<SourceTotals, static_cast<std::size_t>(RewardSource::kCount)> totals_{};
    };

    [[nodiscard]] std::string_view ClassifyMarkerType(std::uint16_t markerType) noexcept;
    [[nodiscard]] std::string_view ClassifyLockLevel(std::int32_t lockLevel) noexcept;
}
