#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace EA::UIRules {
    inline constexpr std::size_t kSkillCount = 18;

    enum class SessionState {
        kIdle,
        kOpening,
        kActive,
        kCommitting,
        kClosing
    };

    struct IntegerValidation {
        int  value;
        bool replaced;
    };

    struct FloatValidation {
        float value;
        bool  replaced;
    };

    [[nodiscard]] IntegerValidation ValidateInteger(
        double candidate, int defaultValue, int minimum, int maximum);
    [[nodiscard]] FloatValidation ValidateFloat(
        double candidate, float defaultValue, float minimum, float maximum);
    [[nodiscard]] std::optional<int> CheckedPointTotal(int pendingPoints, int grant);
    [[nodiscard]] std::optional<int> ParseIntegralIdentifier(double value);
    [[nodiscard]] std::optional<std::size_t> FindWhitelistedIdentifier(
        int value, std::span<const int> whitelist);

    enum class AllocationResult {
        kAllocated,
        kInvalidState,
        kInvalidSkill,
        kNoPoints,
        kAtCap,
        kInvalidValue,
        kNothingToRemove
    };

    enum class CommitStatus {
        kReady,
        kInvalidState,
        kInvalidValue,
        kSnapshotDrift
    };

    struct CommitPlan {
        CommitStatus                         status{ CommitStatus::kInvalidState };
        std::array<float, kSkillCount>        finalValues{};
        int                                  pendingPoints{ 0 };

        [[nodiscard]] bool Ready() const noexcept { return status == CommitStatus::kReady; }
    };

    class AllocationSession {
    public:
        [[nodiscard]] bool BeginOpening() noexcept;
        [[nodiscard]] bool Begin(
            int totalPoints,
            float cap,
            const std::array<float, kSkillCount>& snapshot);
        [[nodiscard]] AllocationResult Allocate(std::size_t skillIndex);
        // Returns one point allocated in this session. Never lowers a skill
        // below its snapshot value.
        [[nodiscard]] AllocationResult Deallocate(std::size_t skillIndex);
        [[nodiscard]] bool Reset();
        [[nodiscard]] CommitPlan PrepareCommit(
            const std::array<float, kSkillCount>& currentValues);
        void MarkClosing() noexcept;
        void Cancel() noexcept;

        [[nodiscard]] SessionState State() const noexcept { return _state; }
        [[nodiscard]] int TotalPoints() const noexcept { return _totalPoints; }
        [[nodiscard]] int RemainingPoints() const noexcept { return _remainingPoints; }
        [[nodiscard]] float Cap() const noexcept { return _cap; }
        [[nodiscard]] float Preview(std::size_t index) const noexcept { return _preview[index]; }
        [[nodiscard]] std::uint32_t Delta(std::size_t index) const noexcept { return _deltas[index]; }
        [[nodiscard]] bool HasChanges() const noexcept;

    private:
        SessionState                          _state{ SessionState::kIdle };
        int                                   _totalPoints{ 0 };
        int                                   _remainingPoints{ 0 };
        float                                 _cap{ 1.0f };
        std::array<float, kSkillCount>         _snapshot{};
        std::array<float, kSkillCount>         _preview{};
        std::array<std::uint32_t, kSkillCount> _deltas{};
    };

    // Hand-off between SAL's skill menu and the vanilla LevelUp Menu when an
    // integration registers a level-up step. Never persisted.
    enum class HandoffDecision {
        kContinueNow,  // queue the vanilla LevelUp Menu immediately
        kAwaitStep,    // the step owner must call ContinueLevelUp
    };

    class LevelUpHandoff {
    public:
        // Continuation fail-safe: while waiting, the game must stay unpaused
        // this long before SAL continues on the step owner's behalf.
        static constexpr double kDefaultGraceSeconds = 10.0;

        [[nodiscard]] HandoffDecision Begin(bool stepRegistered, bool wantsStep) noexcept;
        // True exactly once per wait; false when not waiting.
        [[nodiscard]] bool Continue() noexcept;
        // Called on the main thread for each periodic sample while waiting.
        // Returns true when the game has stayed unpaused for graceSeconds
        // without a continuation; the caller then continues on the step
        // owner's behalf.
        [[nodiscard]] bool ObserveSample(bool gamePaused, double nowSeconds,
            double graceSeconds = kDefaultGraceSeconds) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool Waiting() const noexcept { return _waiting; }

    private:
        bool                  _waiting{ false };
        std::optional<double> _unpausedSince{};
    };

    // Fires the integration character-created callback once per new game,
    // after a creation menu has closed, no creation menu remains open, and
    // starting skills are settled (normalized, or Vanilla mode).
    class CharacterCreatedSignal {
    public:
        void Arm() noexcept;
        void ObserveCreationMenuClosed() noexcept;
        // True exactly once per Arm() when every condition holds.
        [[nodiscard]] bool TryFire(bool creationMenuOpen, bool skillsSettled) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool Armed() const noexcept { return _armed; }
        [[nodiscard]] bool CreationMenuClosed() const noexcept { return _menuClosed; }

    private:
        bool _armed{ false };
        bool _menuClosed{ false };
    };
}
