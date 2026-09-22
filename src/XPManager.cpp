#include "PCH.h"
#include "XPManager.h"
#include "Config.h"
#include "RewardRules.h"

#include <cmath>

namespace EA::XPManager {

    namespace {
        void ShowXPNotification(const char* a_message)
        {
            using NotificationFn = void (*)(const char*, const char*, bool);
            static REL::Relocation<NotificationFn> notify{ RELOCATION_ID(52050, 52933) };
            notify(a_message, nullptr, true);
        }
    }

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    static int                            s_pendingSkillPoints = 0;
    static std::unordered_set<RE::FormID> s_deadActors;
    static std::unordered_set<RE::FormID> s_readBooks;
    static RewardRules::QuestLifecycle    s_questLifecycle;
    static std::unordered_set<std::uintptr_t> s_discoveredLocationMarkers;
    static std::unordered_set<RE::FormID>     s_clearedLocations;
    static std::uint64_t                       s_rewardGeneration = 1;

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
    // Cosave accessors for plugin-owned state
    // -----------------------------------------------------------------------
    int  GetPendingSkillPoints()      { return s_pendingSkillPoints; }
    void SetPendingSkillPoints(int n) { s_pendingSkillPoints = n; }

    // -----------------------------------------------------------------------
    // Kill guard
    // -----------------------------------------------------------------------
    bool RegisterKill(RE::FormID actorID) {
        if (s_deadActors.contains(actorID)) {
            logger::debug("[EA] Kill guard: FormID {:08X} already dead - skipped.", actorID);
            return false;
        }
        s_deadActors.insert(actorID);
        return true;
    }

    // -----------------------------------------------------------------------
    // Book guard
    // -----------------------------------------------------------------------
    bool RegisterBookRead(RE::FormID bookID) {
        if (s_readBooks.contains(bookID)) {
            logger::debug("[EA] Book guard: FormID {:08X} already awarded XP - skipped.", bookID);
            return false;
        }
        s_readBooks.insert(bookID);
        return true;
    }

    // -----------------------------------------------------------------------
    // Quest guard
    // -----------------------------------------------------------------------
    bool ObserveQuestStatus(RE::FormID questID, RewardRules::QuestSignal signal) {
        return s_questLifecycle.Observe(questID, signal);
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

    bool RegisterLocationClear(RE::FormID locationID) {
        if (locationID == 0) {
            return false;
        }
        if (s_clearedLocations.contains(locationID)) {
            logger::debug("[EA] Location clear guard: FormID {:08X} already awarded - skipped.", locationID);
            return false;
        }
        s_clearedLocations.insert(locationID);
        return true;
    }

    void ResetRewardGuards() {
        s_deadActors.clear();
        s_readBooks.clear();
        s_questLifecycle.Reset();
        s_discoveredLocationMarkers.clear();
        s_clearedLocations.clear();
        ++s_rewardGeneration;
        if (s_rewardGeneration == 0) {
            s_rewardGeneration = 1;
        }
        logger::info("[EA] Reward state: all transient guards reset.");
    }

    std::uint64_t GetRewardGeneration() {
        return s_rewardGeneration;
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
        if (!std::isfinite(amount) || amount <= 0.0f) {
            logger::warn("[EA] AwardXP: rejected invalid amount {} from source '{}'.", amount, context.sourceKey);
            return;
        }

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
        if (!std::isfinite(systemXPBefore) || systemXPBefore < 0.0f) {
            logger::error("[EA] AwardXP: native XP bucket is invalid ({}); award rejected.", systemXPBefore);
            return;
        }

        const float systemXPAfter = systemXPBefore + amount;
        if (!std::isfinite(systemXPAfter)) {
            logger::error("[EA] AwardXP: XP addition would overflow ({} + {}); award rejected.",
                systemXPBefore, amount);
            return;
        }
        skills->data->xp = systemXPAfter;

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
            ShowXPNotification(msg.c_str());
        }
    }
}
