#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "RewardRules.h"

namespace EA::XPManager {

    enum class AwardKind {
        Book,
        Kill,
        Quest,
        Stat,
    };

    struct AwardContext {
        AwardKind               kind{ AwardKind::Stat };
        std::string_view        sourceKey{};
        std::string_view        subject{};
        std::string_view        subtype{};
        std::string_view        state{};
        std::optional<RE::FormID> formID{};
        std::optional<int>        level{};
        std::optional<int>        counter{};
        bool                    skillBook{ false };
        bool                    alreadyRead{ false };
    };

    AwardContext MakeBookContext(std::string_view title, RE::FormID formID, bool skillBook, bool alreadyRead);
    AwardContext MakeKillContext(std::string_view actorName, RE::FormID formID, int actorLevel, std::string_view killType);
    AwardContext MakeQuestContext(std::string_view questName, RE::FormID questID, std::string_view questType);
    AwardContext MakeStatContext(std::string_view statName, std::string_view sourceKey, int counter, std::string_view subtype = {});

    // Awards XP from a structured source context. Feeds directly into the
    // engine's native XP bucket - the engine handles level-up UI, perk points,
    // and overflow carry.
    void AwardXP(float amount, const AwardContext& context);

    // Kill deduplication guard.
    // Returns true if this is a new kill (XP should be awarded).
    // Returns false if this FormID was already processed this session.
    bool RegisterKill(RE::FormID actorID);

    // Book deduplication guard.
    // Returns true if this book has not yet been awarded XP this session.
    // Returns false if already processed (skip).
    bool RegisterBookRead(RE::FormID bookID);

    // Returns true only for the first completion in a quest lifecycle. Start
    // and reset signals re-arm repeatable quests without awarding XP.
    bool ObserveQuestStatus(RE::FormID questID, RewardRules::QuestSignal signal);

    // Location discovery/clearing deduplication guards.
    bool RegisterLocationDiscovery(std::uintptr_t markerKey);
    bool RegisterLocationClear(RE::FormID locationID);

    // Clears all transient reward guards and lifecycle state. Call for every
    // load, revert, and new game so state cannot leak between characters.
    void ResetRewardGuards();

    // Deferred reward tasks capture this value and abort if a load/revert/new
    // game has reset transient state before they execute.
    [[nodiscard]] std::uint64_t GetRewardGeneration();

    // Pending skill points - unspent points from the last level-up's allocation
    // menu that carry over to the next level-up.
    // Persisted in the plugin cosave.
    int  GetPendingSkillPoints();
    void SetPendingSkillPoints(int n);
}
