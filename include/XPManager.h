#pragma once

#include <optional>
#include <string_view>

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

    // Cosave accessors.
    float GetCurrentXP();
    void  SetCurrentXP(float xp);

    // Kill deduplication guard.
    // Returns true if this is a new kill (XP should be awarded).
    // Returns false if this FormID was already processed this session.
    bool RegisterKill(RE::FormID actorID);

    // Clears the kill guard set. Call on game load and new game.
    void ResetKillGuard();

    // Book deduplication guard.
    // Returns true if this book has not yet been awarded XP this session.
    // Returns false if already processed (skip).
    bool RegisterBookRead(RE::FormID bookID);

    // Clears the book guard set. Call on game load and new game.
    void ResetBookGuard();

    // Quest completion deduplication guard.
    // Returns true if this quest FormID has not yet been awarded XP this session.
    // Returns false if already processed (skip).
    bool RegisterQuestXP(RE::FormID questID);

    // Awards XP only if the quest FormID has not been seen before this session.
    // Combines RegisterQuestXP + AwardXP in one call.
    void AwardXPIfQuestNew(RE::FormID questID, float amount, const AwardContext& context);

    // Clears the quest guard set. Call on game load and new game.
    void ResetQuestGuard();

    // Pending skill points - unspent points from the last level-up's allocation
    // menu that carry over to the next level-up.
    // Persisted in cosave v4.
    int  GetPendingSkillPoints();
    void SetPendingSkillPoints(int n);
}
