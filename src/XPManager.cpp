#include "PCH.h"
#include "XPManager.h"
#include "Config.h"

namespace EA::XPManager {

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    static float                          s_currentXP          = 0.0f;
    static int                            s_pendingSkillPoints = 0;
    static std::unordered_set<RE::FormID> s_deadActors;
    static std::unordered_set<RE::FormID> s_readBooks;
    static std::unordered_set<RE::FormID> s_completedQuests;
    static std::unordered_set<std::uintptr_t> s_discoveredLocationMarkers;
    static std::unordered_set<RE::FormID>     s_clearedLocations;

    // -----------------------------------------------------------------------
    // Context builders
    // -----------------------------------------------------------------------
    AwardContext MakeBookContext(std::string_view title, RE::FormID formID, bool skillBook, bool alreadyRead) {
        AwardContext ctx;
        ctx.kind        = AwardKind::Book;
        ctx.sourceKey   = skillBook ? "book_skill"sv : "book_read"sv;
        ctx.subject     = title;
        ctx.formID      = formID;
        ctx.skillBook   = skillBook;
        ctx.alreadyRead = alreadyRead;
        return ctx;
    }

    AwardContext MakeKillContext(std::string_view actorName, RE::FormID formID, int actorLevel, std::string_view killType) {
        AwardContext ctx;
        ctx.kind      = AwardKind::Kill;
        ctx.sourceKey = "kill"sv;
        ctx.subject   = actorName;
        ctx.formID    = formID;
        ctx.level     = actorLevel;
        ctx.subtype   = killType;
        return ctx;
    }

    AwardContext MakeQuestContext(std::string_view questName, RE::FormID questID, std::string_view questType) {
        AwardContext ctx;
        ctx.kind      = AwardKind::Quest;
        ctx.sourceKey = questType;
        ctx.subject   = questName;
        ctx.formID    = questID;
        ctx.subtype   = questType;
        ctx.state     = "completed"sv;
        return ctx;
    }

    AwardContext MakeStatContext(std::string_view statName, std::string_view sourceKey, int counter, std::string_view subtype) {
        AwardContext ctx;
        ctx.kind      = AwardKind::Stat;
        ctx.sourceKey = sourceKey;
        ctx.subject   = statName;
        ctx.counter   = counter;
        ctx.subtype   = subtype;
        return ctx;
    }

    // -----------------------------------------------------------------------
    // Cosave accessors
    // -----------------------------------------------------------------------
    float GetCurrentXP()         { return s_currentXP; }
    void  SetCurrentXP(float xp) { s_currentXP = xp; }

    int  GetPendingSkillPoints()      { return s_pendingSkillPoints; }
    void SetPendingSkillPoints(int n) { s_pendingSkillPoints = n; }

    // -----------------------------------------------------------------------
    // Kill guard
    // -----------------------------------------------------------------------
    bool RegisterKill(RE::FormID actorID) {
        if (s_deadActors.contains(actorID)) {
            logger::debug("[EA] Kill guard: FormID {:08X} already dead â€” skipped.", actorID);
            return false;
        }
        s_deadActors.insert(actorID);
        return true;
    }

    void ResetKillGuard() {
        s_deadActors.clear();
        logger::debug("[EA] Kill guard: cleared.");
    }

    // -----------------------------------------------------------------------
    // Book guard
    // -----------------------------------------------------------------------
    bool RegisterBookRead(RE::FormID bookID) {
        if (s_readBooks.contains(bookID)) {
            logger::debug("[EA] Book guard: FormID {:08X} already awarded XP Ã¢â‚¬â€ skipped.", bookID);
            return false;
        }
        s_readBooks.insert(bookID);
        return true;
    }

    void ResetBookGuard() {
        s_readBooks.clear();
        logger::debug("[EA] Book guard: cleared.");
    }

    // -----------------------------------------------------------------------
    // Quest guard
    // -----------------------------------------------------------------------
    bool RegisterQuestXP(RE::FormID questID) {
        if (s_completedQuests.contains(questID)) {
            logger::debug("[EA] Quest guard: FormID {:08X} already awarded XP â€” skipped.", questID);
            return false;
        }
        s_completedQuests.insert(questID);
        return true;
    }

    void AwardXPIfQuestNew(RE::FormID questID, float amount, const AwardContext& context) {
        if (!RegisterQuestXP(questID)) return;
        AwardXP(amount, context);
    }

    void ResetQuestGuard() {
        s_completedQuests.clear();
        logger::info("[EA] Quest guard: cleared.");
    }

    bool RegisterLocationDiscovery(std::uintptr_t markerKey) {
        if (markerKey == 0) {
            return false;
        }
        if (s_discoveredLocationMarkers.contains(markerKey)) {
            logger::debug("[EA] Location discovery guard: marker {:08X} already awarded - skipped.",
                static_cast<unsigned long long>(markerKey));
            return false;
        }
        s_discoveredLocationMarkers.insert(markerKey);
        return true;
    }

    void ResetLocationDiscoveryGuard() {
        s_discoveredLocationMarkers.clear();
        logger::debug("[EA] Location discovery guard: cleared.");
    }

    bool RegisterLocationClear(RE::FormID locationID) {
        if (locationID == 0) {
            return false;
        }
        if (s_clearedLocations.contains(locationID)) {
            logger::debug("[EA] Location clear guard: FormID {:08X} already awarded â€” skipped.", locationID);
            return false;
        }
        s_clearedLocations.insert(locationID);
        return true;
    }

    void ResetLocationClearGuard() {
        s_clearedLocations.clear();
        logger::debug("[EA] Location clear guard: cleared.");
    }

    // -----------------------------------------------------------------------
    // Internal formatting helpers
    // -----------------------------------------------------------------------
    static std::string DescribeContext(const AwardContext& context) {
        switch (context.kind) {
            case AwardKind::Book:
                return std::format("book title='{}' formID={:08X} skillBook={} alreadyRead={}",
                    context.subject,
                    context.formID.value_or(0),
                    context.skillBook,
                    context.alreadyRead);

            case AwardKind::Kill:
                return std::format("kill actor='{}' formID={:08X} level={} type={}",
                    context.subject,
                    context.formID.value_or(0),
                    context.level.value_or(0),
                    context.subtype);

            case AwardKind::Quest:
                return std::format("quest name='{}' formID={:08X} type={} state={}",
                    context.subject,
                    context.formID.value_or(0),
                    context.subtype,
                    context.state.empty() ? "unknown"sv : context.state);

            case AwardKind::Stat: {
                auto detail = std::format("stat name='{}' counter={}",
                    context.subject,
                    context.counter.value_or(0));
                if (!context.subtype.empty()) {
                    detail += std::format(" subtype={}", context.subtype);
                }
                return detail;
            }
        }

        return "source=unknown";
    }

    // -----------------------------------------------------------------------
    // AwardXP
    //
    // Feeds XP directly into the engine's native character XP bucket.
    // The engine checks xp >= levelThreshold every tick and calls
    // AdvanceLevel() when crossed - handling the attribute screen, perk point,
    // level increment, threshold update for the next level, and overflow carry
    // entirely natively. No chaining code needed on our side.
    // -----------------------------------------------------------------------
    void AwardXP(float amount, const AwardContext& context) {
        if (amount <= 0.0f) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("[EA] AwardXP: PlayerCharacter is null.");
            return;
        }

        auto* skills = player->GetInfoRuntimeData().skills;
        if (!skills || !skills->data) {
            logger::warn("[EA] AwardXP: PlayerSkills or data is null.");
            return;
        }

        float systemXPBefore = skills->data->xp;
        skills->data->xp += amount;
        float systemXPAfter = skills->data->xp;
        s_currentXP = systemXPAfter;

        if (EA::Config::verbose) {
            logger::info("[EA] XP award: +{:.1f} | source={} | {} | system_xp={:.1f} -> {:.1f} | threshold={:.1f} | level={}",
                amount,
                context.sourceKey,
                DescribeContext(context),
                systemXPBefore,
                systemXPAfter,
                skills->data->levelThreshold,
                static_cast<int>(player->GetLevel()));
        }

        if (EA::Config::notificationsEnabled) {
            std::string notifKey;
            if (context.kind == AwardKind::Kill && !context.subtype.empty()) {
                notifKey = "kill_" + std::string(context.subtype);
            } else if (context.sourceKey == "lock_picked" && !context.subtype.empty()) {
                notifKey = "lock_" + std::string(context.subtype);
            } else {
                notifKey = std::string(context.sourceKey);
            }
            auto it = EA::Config::notificationMessages.find(notifKey);
            std::string msg = (it != EA::Config::notificationMessages.end() && !it->second.empty())
                ? std::format("{} +{:.0f} XP", it->second, amount)
                : std::format("+{:.0f} XP", amount);
            RE::DebugNotification(msg.c_str());
        }
    }
}
