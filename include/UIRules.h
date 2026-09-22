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
    [[nodiscard]] IntegerValidation ValidatePanelHeight(double candidate, int defaultValue);
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
        kInvalidValue
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
}
