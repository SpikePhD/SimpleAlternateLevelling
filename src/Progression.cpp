#include "Progression.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace EA::Progression {
    namespace {
        [[nodiscard]] std::uint32_t ReadU32LE(std::span<const std::byte> data, std::size_t offset) noexcept
        {
            return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset])) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 1])) << 8) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 2])) << 16) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 3])) << 24);
        }

        [[nodiscard]] std::int32_t ReadI32LE(std::span<const std::byte> data, std::size_t offset) noexcept
        {
            return std::bit_cast<std::int32_t>(ReadU32LE(data, offset));
        }

        [[nodiscard]] float ReadFloatLE(std::span<const std::byte> data, std::size_t offset) noexcept
        {
            return std::bit_cast<float>(ReadU32LE(data, offset));
        }

        void WriteI32LE(std::span<std::byte> data, std::size_t offset, std::int32_t value) noexcept
        {
            const auto raw = std::bit_cast<std::uint32_t>(value);
            data[offset]     = static_cast<std::byte>(raw & 0xFFu);
            data[offset + 1] = static_cast<std::byte>((raw >> 8) & 0xFFu);
            data[offset + 2] = static_cast<std::byte>((raw >> 16) & 0xFFu);
            data[offset + 3] = static_cast<std::byte>((raw >> 24) & 0xFFu);
        }

        [[nodiscard]] std::optional<std::size_t> ExpectedRecordLength(std::uint32_t version) noexcept
        {
            switch (version) {
                case 1: return 4;
                case 2: return 12;
                case 3: return 4;
                case 4: return 8;
                case 5: return 9;
                case 6: return kCosaveV6Size;
                default: return std::nullopt;
            }
        }
    }

    CurveValidation ValidateCurve(LevelCurve input, LevelCurve defaults) noexcept
    {
        CurveValidation result{ input };

        if (!std::isfinite(input.base) || input.base <= 0.0f) {
            result.curve.base = defaults.base;
            result.replacedBase = true;
        }
        if (!std::isfinite(input.increase) || input.increase < 0.0f) {
            result.curve.increase = defaults.increase;
            result.replacedIncrease = true;
        }
        if (!std::isfinite(input.cap) || input.cap <= 0.0f) {
            result.curve.cap = defaults.cap;
            result.replacedCap = true;
        }

        return result;
    }

    float CalculateThreshold(std::uint32_t level, LevelCurve curve) noexcept
    {
        const auto effectiveLevel = std::max<std::uint32_t>(level, 1u);
        const double uncapped = static_cast<double>(curve.base) +
                                static_cast<double>(effectiveLevel) * static_cast<double>(curve.increase);
        const double cap = static_cast<double>(curve.cap);

        if (!std::isfinite(uncapped) || uncapped >= cap) {
            return curve.cap;
        }
        if (uncapped <= 0.0) {
            return std::min(curve.base, curve.cap);
        }
        return static_cast<float>(uncapped);
    }

    std::array<std::byte, kCosaveV6Size> EncodeCosaveV6(const CosaveState& state) noexcept
    {
        std::array<std::byte, kCosaveV6Size> encoded{};
        WriteI32LE(encoded, 0, state.pendingSkillPoints);
        encoded[4] = state.skillsNormalized ? std::byte{ 1 } : std::byte{ 0 };
        return encoded;
    }

    DecodeResult DecodeCosave(std::uint32_t version, std::span<const std::byte> data) noexcept
    {
        const auto expectedLength = ExpectedRecordLength(version);
        if (!expectedLength) {
            return { DecodeStatus::kUnsupportedVersion };
        }
        if (data.size() != *expectedLength) {
            return { DecodeStatus::kInvalidLength };
        }

        DecodeResult result;
        result.status = DecodeStatus::kSuccess;

        if (version <= 5) {
            result.ignoredLegacyXP = ReadFloatLE(data, 0);
        }

        if (version == 4 || version == 5) {
            result.state.pendingSkillPoints = ReadI32LE(data, 4);
        } else if (version == 6) {
            result.state.pendingSkillPoints = ReadI32LE(data, 0);
        }

        if (result.state.pendingSkillPoints < 0) {
            result.status = DecodeStatus::kInvalidData;
            return result;
        }

        if (version >= 5) {
            const auto normalizedOffset = version == 5 ? 8u : 4u;
            const auto normalized = std::to_integer<std::uint8_t>(data[normalizedOffset]);
            if (normalized > 1u) {
                result.status = DecodeStatus::kInvalidData;
                return result;
            }
            result.state.skillsNormalized = normalized != 0u;
        }

        return result;
    }

    bool AdoptFirstValidCosave(
        std::optional<CosaveState>& accepted,
        const DecodeResult&         decoded) noexcept
    {
        if (!decoded.Succeeded() || accepted.has_value()) {
            return false;
        }
        accepted = decoded.state;
        return true;
    }
}
