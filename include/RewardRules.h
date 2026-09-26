#pragma once

#include "SAL_API.h"

#include <array>
#include <cstddef>
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

    // Public SAL_API.h category for the V4 XP multiplier provider. Mapped
    // explicitly so reordering RewardSource cannot change the public values.
    [[nodiscard]] constexpr std::uint32_t ToPublicXPSource(RewardSource source) noexcept
    {
        switch (source) {
            case RewardSource::kQuest: return SAL::kXPSourceQuest;
            case RewardSource::kKill: return SAL::kXPSourceKill;
            case RewardSource::kExploration: return SAL::kXPSourceExploration;
            case RewardSource::kLock: return SAL::kXPSourceLock;
            case RewardSource::kBook: return SAL::kXPSourceBook;
            case RewardSource::kPickpocket: return SAL::kXPSourcePickpocket;
            case RewardSource::kCount: break;
        }
        return SAL::kXPSourceQuest;
    }

    static_assert(ToPublicXPSource(RewardSource::kQuest) == SAL::kXPSourceQuest);
    static_assert(ToPublicXPSource(RewardSource::kKill) == SAL::kXPSourceKill);
    static_assert(ToPublicXPSource(RewardSource::kExploration) == SAL::kXPSourceExploration);
    static_assert(ToPublicXPSource(RewardSource::kLock) == SAL::kXPSourceLock);
    static_assert(ToPublicXPSource(RewardSource::kBook) == SAL::kXPSourceBook);
    static_assert(ToPublicXPSource(RewardSource::kPickpocket) == SAL::kXPSourcePickpocket);
    static_assert(static_cast<std::size_t>(RewardSource::kCount) == 6,
        "a new RewardSource needs a public kXPSource* constant and a ToPublicXPSource case");

    // The V4 XP multiplier after validation. Non-finite or <= 0 values count
    // as 1; values above kMaxXPMultiplier are clamped (a garbage guard, not a
    // balance cap). Finite values in (0, 1) are allowed and reduce XP.
    inline constexpr double kMaxXPMultiplier = 100.0;

    enum class XPMultiplierStatus : std::uint8_t {
        kApplied,
        kInvalid,
        kClamped,
    };

    struct XPMultiplier {
        double             value{ 1.0 };
        XPMultiplierStatus status{ XPMultiplierStatus::kApplied };
    };

    [[nodiscard]] XPMultiplier SanitizeXPMultiplier(float raw) noexcept;

    // amount = base * level scaling * multiplier, unrounded: the native XP
    // bucket is a float, so fractional bonuses accumulate. The result may be
    // non-finite or <= 0; the caller rejects those, which covers overflow.
    [[nodiscard]] float CombineReward(float base, double scale, double multiplier) noexcept;

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
