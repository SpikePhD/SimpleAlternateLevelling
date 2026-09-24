#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace EA::Leveling {
    void ApplyGameSettings(std::string_view reason);
    bool RefreshThreshold(std::string_view reason, std::uint32_t expectedLevel = 0);
    void QueueThresholdRefresh(std::uint32_t expectedLevel, std::string reason);
    void ResetState();

    // Integration refresh request, callable from any thread. Runs on the
    // main thread; during a level-up it is folded into the refresh that
    // follows the LevelUp Menu close, so the engine's XP subtraction still
    // uses the threshold that was current when the level was reached.
    void RequestIntegrationRefresh();

    // Bracket the engine's level-up: LevelIncrease::Event until the final
    // LevelUp Menu close.
    void MarkLevelIncrease();
    void MarkLevelUpFinished();

    // Last threshold written by RefreshThreshold, for display. Safe to read
    // from any thread.
    struct AppliedThreshold {
        float  configured{ 0.0f };  // capped curve value before the multiplier
        float  effective{ 0.0f };   // value written to levelThreshold
        double multiplier{ 1.0 };
    };
    [[nodiscard]] AppliedThreshold LastApplied();
}
