#include "Progression.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace {
    int failures = 0;

    void Check(bool condition, std::string_view message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    void AppendU32(std::vector<std::byte>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFFu));
        bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
        bytes.push_back(static_cast<std::byte>((value >> 16) & 0xFFu));
        bytes.push_back(static_cast<std::byte>((value >> 24) & 0xFFu));
    }

    void AppendI32(std::vector<std::byte>& bytes, std::int32_t value)
    {
        AppendU32(bytes, std::bit_cast<std::uint32_t>(value));
    }

    void AppendFloat(std::vector<std::byte>& bytes, float value)
    {
        AppendU32(bytes, std::bit_cast<std::uint32_t>(value));
    }

    void TestThresholds()
    {
        using namespace EA::Progression;
        const LevelCurve curve{ 5.0f, 1.0f, 10.0f };
        Check(CalculateThreshold(0, curve) == 6.0f, "level zero is treated as level one");
        Check(CalculateThreshold(3, curve) == 8.0f, "threshold below cap");
        Check(CalculateThreshold(5, curve) == 10.0f, "threshold at cap");
        Check(CalculateThreshold(50, curve) == 10.0f, "threshold above cap");
        Check(CalculateThreshold(100, { 20.0f, 0.0f, 50.0f }) == 20.0f, "zero increase");
        Check(CalculateThreshold(1, { 200.0f, 25.0f, 100.0f }) == 100.0f, "cap below base");
        Check(CalculateThreshold(255, { 5.0f, 1.0f, 1000.0f }) == 260.0f,
            "maximum Skyrim player level");
        Check(CalculateThreshold(std::numeric_limits<std::uint32_t>::max(),
            { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 500.0f }) == 500.0f,
            "overflow-prone curve returns cap");

        const auto validated = ValidateCurve(
            { std::numeric_limits<float>::quiet_NaN(), -1.0f, 0.0f },
            { 200.0f, 25.0f, 1000.0f });
        Check(validated.curve.base == 200.0f && validated.replacedBase, "invalid base falls back");
        Check(validated.curve.increase == 25.0f && validated.replacedIncrease, "invalid increase falls back");
        Check(validated.curve.cap == 1000.0f && validated.replacedCap, "invalid cap falls back");
    }

    void TestCosaves()
    {
        using namespace EA::Progression;

        const CosaveState state{ 17, true };
        const auto encoded = EncodeCosaveV6(state);
        const std::array expectedV6{
            std::byte{ 0x11 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x01 }
        };
        Check(encoded == expectedV6, "v6 exact little-endian encoding");
        const auto decoded = DecodeCosave(6, encoded);
        Check(decoded.Succeeded() && decoded.state == state, "v6 round trip");
        Check(!decoded.ignoredLegacyXP.has_value(), "v6 has no legacy XP");

        std::vector<std::byte> v1;
        AppendFloat(v1, 24.0f);
        auto legacy = DecodeCosave(1, v1);
        Check(legacy.Succeeded() && legacy.state == CosaveState{}, "v1 defaults plugin state");
        Check(legacy.ignoredLegacyXP == 24.0f, "v1 reports ignored XP");

        std::vector<std::byte> v2 = v1;
        AppendI32(v2, 10);
        AppendI32(v2, 3);
        Check(DecodeCosave(2, v2).Succeeded() && DecodeCosave(2, v2).state == CosaveState{},
            "v2 exact layout ignores obsolete fields");
        Check(DecodeCosave(3, v1).Succeeded() && DecodeCosave(3, v1).state == CosaveState{},
            "v3 exact layout defaults plugin state");

        std::vector<std::byte> v4 = v1;
        AppendI32(v4, 9);
        auto decodedV4 = DecodeCosave(4, v4);
        Check(decodedV4.Succeeded() && decodedV4.state == CosaveState{ 9, false }, "v4 migrates pending points");

        std::vector<std::byte> v5 = v4;
        v5.push_back(std::byte{ 1 });
        auto decodedV5 = DecodeCosave(5, v5);
        Check(decodedV5.Succeeded() && decodedV5.state == CosaveState{ 9, true }, "v5 migrates plugin state");
        Check(decodedV5.ignoredLegacyXP == 24.0f, "stale v5 XP is diagnostic only");

        const std::vector<std::vector<std::byte>> fixtures{ v1, v2, v1, v4, v5,
            std::vector<std::byte>(encoded.begin(), encoded.end()) };
        for (std::uint32_t version = 1; version <= fixtures.size(); ++version) {
            auto shortRecord = fixtures[version - 1];
            shortRecord.pop_back();
            Check(DecodeCosave(version, shortRecord).status == DecodeStatus::kInvalidLength,
                "short historical record rejected");

            auto longRecord = fixtures[version - 1];
            longRecord.push_back(std::byte{ 0 });
            Check(DecodeCosave(version, longRecord).status == DecodeStatus::kInvalidLength,
                "long historical record rejected");
        }
        Check(DecodeCosave(99, encoded).status == DecodeStatus::kUnsupportedVersion, "unknown version rejected");

        auto negative = encoded;
        negative[0] = negative[1] = negative[2] = negative[3] = std::byte{ 0xFF };
        Check(DecodeCosave(6, negative).status == DecodeStatus::kInvalidData, "negative pending points rejected");

        auto badFlag = encoded;
        badFlag[4] = std::byte{ 2 };
        Check(DecodeCosave(6, badFlag).status == DecodeStatus::kInvalidData, "invalid normalization flag rejected");

        auto badV5Flag = v5;
        badV5Flag[8] = std::byte{ 2 };
        Check(DecodeCosave(5, badV5Flag).status == DecodeStatus::kInvalidData,
            "invalid v5 normalization flag rejected");

        std::optional<CosaveState> accepted;
        Check(AdoptFirstValidCosave(accepted, decoded), "first valid record accepted");
        Check(!AdoptFirstValidCosave(accepted, decodedV5) && accepted == state, "duplicate valid record ignored");
    }

    bool Near(double actual, double expected)
    {
        return std::abs(actual - expected) < 1e-9;
    }

    void TestRewardScale()
    {
        using EA::Progression::RewardScale;
        const EA::Progression::LevelCurve vanilla{ 75.0f, 25.0f, 10000000.0f };
        Check(Near(RewardScale(1, vanilla, 0.5f), 1.0), "level 1 is never scaled");
        Check(Near(RewardScale(0, vanilla, 0.5f), 1.0), "level 0 is treated as level 1");
        Check(Near(RewardScale(30, vanilla, 0.0f), 1.0), "exponent 0 disables scaling");
        Check(Near(RewardScale(30, vanilla, 1.0f), 8.25), "exponent 1 tracks the curve exactly");
        Check(Near(RewardScale(30, vanilla, 0.5f), std::sqrt(8.25)), "default exponent halves the growth");
        Check(Near(RewardScale(30, vanilla, 2.0f), 8.25), "exponent above 1 is clamped");
        Check(Near(RewardScale(30, vanilla, -1.0f), 1.0), "negative exponent is ignored");
        Check(Near(RewardScale(30, vanilla, std::numeric_limits<float>::quiet_NaN()), 1.0), "NaN exponent is ignored");
        const EA::Progression::LevelCurve capped{ 75.0f, 25.0f, 200.0f };
        Check(Near(RewardScale(50, capped, 1.0f), 2.0), "scaling stops growing at the cap");
        Check(Near(RewardScale(10, { 100.0f, 0.0f, 1000.0f }, 0.5f), 1.0), "flat curve never scales");
    }
}

int main()
{
    TestThresholds();
    TestRewardScale();
    TestCosaves();
    if (failures == 0) {
        std::cout << "All progression tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
