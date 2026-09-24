#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace EA::Progression {

    inline constexpr std::uint32_t kCosaveVersion = 6;
    inline constexpr std::size_t   kCosaveV6Size   = 5;
    inline constexpr std::size_t   kMaxCosaveRecordSize = 64;

    struct LevelCurve {
        float base;
        float increase;
        float cap;
    };

    struct CurveValidation {
        LevelCurve curve;
        bool       replacedBase{ false };
        bool       replacedIncrease{ false };
        bool       replacedCap{ false };
    };

    [[nodiscard]] CurveValidation ValidateCurve(LevelCurve input, LevelCurve defaults) noexcept;
    [[nodiscard]] float CalculateThreshold(std::uint32_t level, LevelCurve curve) noexcept;

    // Multiplier applied to every XP reward so rewards grow with the level
    // curve: (threshold(level) / threshold(1)) ^ exponent. Exponent 0 keeps
    // rewards flat (effort per level grows with the curve); 1 makes every
    // level cost the same effort. Values between keep levels progressively
    // harder but gentler. Invalid inputs yield 1 (no scaling).
    inline constexpr float kDefaultRewardScaling = 0.5f;
    [[nodiscard]] double RewardScale(std::uint32_t level, LevelCurve curve, float exponent) noexcept;

    // Integration multiplier applied to the live threshold only; reward
    // scaling keeps using the unmodified curve. A non-finite or non-positive
    // provider value counts as 1, and the result is clamped to [floor, 1].
    // A floor outside (0, 1] falls back to the default.
    inline constexpr float kDefaultThresholdMultiplierFloor = 0.5f;

    struct ThresholdModifier {
        float  threshold;
        double multiplier;
        bool   rejected{ false };  // provider value was non-finite or <= 0
        bool   clamped{ false };   // provider value was outside [floor, 1]
    };

    [[nodiscard]] float ValidateMultiplierFloor(float floor) noexcept;
    [[nodiscard]] ThresholdModifier ApplyThresholdMultiplier(
        float threshold, float providerValue, float floor) noexcept;

    struct CosaveState {
        std::int32_t pendingSkillPoints{ 0 };
        bool         skillsNormalized{ false };

        friend bool operator==(const CosaveState&, const CosaveState&) = default;
    };

    enum class DecodeStatus {
        kSuccess,
        kUnsupportedVersion,
        kInvalidLength,
        kInvalidData,
    };

    struct DecodeResult {
        DecodeStatus         status{ DecodeStatus::kInvalidData };
        CosaveState          state{};
        std::optional<float> ignoredLegacyXP{};

        [[nodiscard]] bool Succeeded() const noexcept { return status == DecodeStatus::kSuccess; }
    };

    [[nodiscard]] std::array<std::byte, kCosaveV6Size> EncodeCosaveV6(const CosaveState& state) noexcept;
    [[nodiscard]] DecodeResult DecodeCosave(std::uint32_t version, std::span<const std::byte> data) noexcept;

    // Implements the load policy: the first valid EAXP record wins. Later
    // valid records are treated as duplicates and ignored.
    [[nodiscard]] bool AdoptFirstValidCosave(
        std::optional<CosaveState>& accepted,
        const DecodeResult&         decoded) noexcept;
}
