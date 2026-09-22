#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_set>

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

    [[nodiscard]] bool IsObjectiveCompletionTransition(
        std::uint32_t oldState,
        std::uint32_t newState) noexcept;

    [[nodiscard]] bool ShouldRewardBook(
        bool activationSucceeded,
        bool playerActivated,
        bool alreadyRead) noexcept;

    [[nodiscard]] bool ShouldRewardPickpocket(std::int32_t numItems) noexcept;

    [[nodiscard]] float CalculateKillReward(
        float baseXP,
        int enemyLevel,
        int playerLevel,
        float levelScaleFactor,
        float globalMultiplier) noexcept;

    [[nodiscard]] std::string_view ClassifyMarkerType(std::uint16_t markerType) noexcept;
    [[nodiscard]] std::string_view ClassifyLockLevel(std::int32_t lockLevel) noexcept;
}
