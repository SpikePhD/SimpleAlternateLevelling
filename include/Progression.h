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
