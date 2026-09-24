#pragma once

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

    void NotifyCharacterCreated();
}
