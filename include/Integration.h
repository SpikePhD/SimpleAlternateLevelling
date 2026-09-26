#pragma once

#include "RewardRules.h"

#include <cstdint>

// SAL side of the public integration API (include/SAL_API.h). Registration
// slots are process-wide; callbacks are only invoked on the main thread.
namespace EA::Integration {
    // Broadcasts the interface to all plugins. Call once at kPostPostLoad.
    void Broadcast();

    [[nodiscard]] bool HasThresholdMultiplier();
    // The registered provider's raw value, or 1 when none is registered.
    // Validation and clamping are Progression::ApplyThresholdMultiplier's job.
    [[nodiscard]] float ThresholdMultiplier();

    [[nodiscard]] bool HasLevelUpStep();
    [[nodiscard]] bool WantsLevelUpStep(std::uint32_t level);

    [[nodiscard]] bool HasPreSkillMenuStep();
    [[nodiscard]] bool WantsPreSkillMenuStep(std::uint32_t level);

    [[nodiscard]] bool HasSkillPointBonus();
    // The provider's raw value (0 when none is registered or it throws).
    // Clamping is UIRules::ClampSkillPointBonus's job.
    [[nodiscard]] std::int32_t SkillPointBonus(std::uint32_t level);

    // The V4 XP multiplier for one award, already validated by
    // RewardRules::SanitizeXPMultiplier (1 when none is registered). Queried
    // live on every award. Invalid or clamped values warn once per session.
    [[nodiscard]] double XPMultiplier(RewardRules::RewardSource source);

    void NotifyCharacterCreated();
}
