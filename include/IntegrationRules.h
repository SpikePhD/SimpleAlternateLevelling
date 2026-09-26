#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

// Dependency-free pieces of the integration API (include/SAL_API.h), kept
// apart from Integration.cpp so the registration and query rules are tested
// portably. Logging stays with the caller.
namespace EA::IntegrationRules {
    enum class RegisterResult : std::uint8_t {
        kAccepted,
        kNull,
        kDuplicate,
    };

    // One registrant per slot; the first non-null callback wins. Safe to
    // register from any thread.
    template <class Callback>
    class CallbackSlot {
    public:
        RegisterResult Register(Callback callback) noexcept
        {
            if (!callback) {
                return RegisterResult::kNull;
            }
            Callback expected = nullptr;
            if (!slot_.compare_exchange_strong(expected, callback)) {
                return RegisterResult::kDuplicate;
            }
            return RegisterResult::kAccepted;
        }

        [[nodiscard]] Callback Get() const noexcept { return slot_.load(); }
        [[nodiscard]] bool Has() const noexcept { return Get() != nullptr; }

    private:
        std::atomic<Callback> slot_{ nullptr };
    };

    using XPMultiplierProvider = float (*)(std::uint32_t sourceCategory);

    // The provider's raw value: 1 when none is registered and NaN when it
    // throws (NaN then counts as 1 in RewardRules::SanitizeXPMultiplier).
    [[nodiscard]] inline float QueryXPMultiplier(
        const CallbackSlot<XPMultiplierProvider>& slot,
        std::uint32_t sourceCategory,
        bool& threw) noexcept
    {
        threw = false;
        const auto provider = slot.Get();
        if (!provider) {
            return 1.0f;
        }
        try {
            return provider(sourceCategory);
        } catch (...) {
            threw = true;
            return std::numeric_limits<float>::quiet_NaN();
        }
    }
}
