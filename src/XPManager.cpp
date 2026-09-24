#include "PCH.h"
#include "XPManager.h"
#include "Config.h"
#include "Progression.h"
#include "RewardRules.h"
#include "XPJournal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace EA::XPManager {

    namespace {
        void ShowXPNotification(const char* a_message)
        {
            // cancelIfAlreadyQueued=false: with true the HUD silently drops a
            // message whose text is already queued, e.g. repeated kills.
            using NotificationFn = void (*)(const char*, const char*, bool);
            static REL::Relocation<NotificationFn> notify{ RELOCATION_ID(52050, 52933) };
            notify(a_message, nullptr, false);
        }

        // Awards with the same notification key within this window of the
        // first one are merged into a single HUD message with their total.
        constexpr auto kNotificationMergeWindow = std::chrono::seconds(1);

        // A notification sent while a pausing menu (container, inventory, map,
        // ...) hides the HUD can be lost, e.g. when looting a body right after
        // the kill. Such notifications wait and retry at this interval.
        constexpr auto kNotificationPausedRetry = std::chrono::milliseconds(500);

        struct PendingNotification {
            std::uint64_t id{ 0 };
            std::string   key;
            std::string   label;
            float         total{ 0.0f };
            int           count{ 0 };
        };

        // Two HUD notifications sent in the same frame were observed to lose
        // one (a location clear beside the boss kill that caused it), so the
        // mod's own messages are spaced at least this far apart.
        constexpr auto kNotificationSpacing = std::chrono::milliseconds(1000);

        std::vector<PendingNotification>      s_pendingNotifications;
        std::uint64_t                         s_nextNotificationID{ 1 };
        std::chrono::steady_clock::time_point s_nextNotificationSlot{};

        void ScheduleFlush(std::uint64_t id, std::chrono::milliseconds delay);

        // Whole numbers normally; one decimal below 1 so small value-based
        // book rewards do not read as "+0 XP".
        std::string FormatXP(float amount)
        {
            return amount < 1.0f ? std::format("{:.1f}", amount) : std::format("{:.0f}", amount);
        }

        void FlushNotification(std::uint64_t id)
        {
            const auto it = std::ranges::find(s_pendingNotifications, id, &PendingNotification::id);
            if (it == s_pendingNotifications.end()) {
                return;  // Discarded by a load, revert, or new game.
            }
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
                ScheduleFlush(id, kNotificationPausedRetry);
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now < s_nextNotificationSlot) {
                ScheduleFlush(id, std::chrono::duration_cast<std::chrono::milliseconds>(s_nextNotificationSlot - now));
                return;
            }
            s_nextNotificationSlot = now + kNotificationSpacing;

            const auto message = it->label.empty()
                ? std::format("+{} XP", FormatXP(it->total))
                : std::format("{} +{} XP", it->label, FormatXP(it->total));
            logger::debug("[EA] Notification: '{}' ({} award(s) merged).", message, it->count);
            ShowXPNotification(message.c_str());
            s_pendingNotifications.erase(it);
        }

        // Sleeps off the main thread, then flushes on it; the pending list is
        // only ever touched on the main thread.
        void ScheduleFlush(std::uint64_t id, std::chrono::milliseconds delay)
        {
            if (!SKSE::GetTaskInterface()) {
                FlushNotification(id);
                return;
            }
            std::thread([id, delay]() {
                std::this_thread::sleep_for(delay);
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([id]() { FlushNotification(id); });
                }
            }).detach();
        }

        void QueueNotification(std::string key, std::string label, float amount)
        {
            for (auto& pending : s_pendingNotifications) {
                if (pending.key == key) {
                    pending.total += amount;
                    ++pending.count;
                    return;
                }
            }

            const auto id = s_nextNotificationID++;
            s_pendingNotifications.push_back({ id, std::move(key), std::move(label), amount, 1 });
            ScheduleFlush(id, std::chrono::duration_cast<std::chrono::milliseconds>(kNotificationMergeWindow));
        }
    }

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    static int                            s_pendingSkillPoints = 0;
    static RewardRules::QuestLifecycle    s_questLifecycle;
    static std::unordered_set<std::uintptr_t> s_discoveredLocationMarkers;
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

    void ResetRewardGuards() {
        s_questLifecycle.Reset();
        s_discoveredLocationMarkers.clear();
        s_pendingNotifications.clear();
        XPJournal::ResetSession();
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
    void AwardXP(float baseAmount, const AwardContext& context) {
        if (!std::isfinite(baseAmount) || baseAmount <= 0.0f) {
            logger::warn("[EA] AwardXP: rejected invalid amount {} from source '{}'.", baseAmount, context.sourceKey);
            XPJournal::RecordNote(RewardRules::ClassifyRewardSource(context.sourceKey),
                std::string(context.subject), "$SAL_NOTE_ZERO");
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

        // Every source grows with the level curve (see Progression::RewardScale);
        // its weight sets how fast. RewardScale caps the exponent at 1.
        const auto source = RewardRules::ClassifyRewardSource(context.sourceKey);
        const auto weight = EA::Config::rewardWeights[static_cast<std::size_t>(source)];
        const auto level = static_cast<std::uint32_t>(player->GetLevel());
        const auto scale = Progression::RewardScale(level,
            { EA::Config::xpBase, EA::Config::xpIncrease, EA::Config::xpCap }, EA::Config::rewardScaling * weight);
        const float amount = static_cast<float>(static_cast<double>(baseAmount) * scale);
        if (!std::isfinite(amount) || amount <= 0.0f) {
            logger::warn("[EA] AwardXP: scaled amount {} from source '{}' is invalid; award rejected.",
                amount, context.sourceKey);
            return;
        }

        float systemXPBefore = skills->data->xp;
        if (!std::isfinite(systemXPBefore)) {
            logger::error("[EA] AwardXP: native XP bucket is invalid ({}); award from source '{}' rejected.",
                systemXPBefore, context.sourceKey);
            return;
        }
        if (systemXPBefore < 0.0f) {
            // Saves made with the pre-fix level-up timing can hold a negative
            // bucket. Rejecting forever would stop all progression, so repair it.
            logger::warn("[EA] AwardXP: native XP bucket was negative ({:.1f}); reset to 0 before award from source '{}'.",
                systemXPBefore, context.sourceKey);
            systemXPBefore = 0.0f;
        }

        const float systemXPAfter = systemXPBefore + amount;
        if (!std::isfinite(systemXPAfter)) {
            logger::error("[EA] AwardXP: XP addition would overflow ({} + {}); award rejected.",
                systemXPBefore, amount);
            return;
        }
        skills->data->xp = systemXPAfter;

        if (EA::Config::verbose) {
            logger::info("[EA] XP award: +{:.1f} (base {:.1f} x{:.2f}) | source={} | {} | system_xp={:.1f} -> {:.1f} | threshold={:.1f} | level={}",
                amount,
                baseAmount,
                scale,
                context.sourceKey,
                DescribeContext(context),
                systemXPBefore,
                systemXPAfter,
                skills->data->levelThreshold,
                static_cast<int>(player->GetLevel()));
        }

        XPJournal::Entry entry;
        entry.source = source;
        entry.baseXP = baseAmount;
        entry.scale = scale;
        entry.xp = amount;
        entry.playerLevel = static_cast<int>(level);
        if (context.kind == AwardKind::Kill) {
            entry.subject = std::string(context.subject);
            entry.enemyLevel = context.level.value_or(0);
        } else if (source == RewardRules::RewardSource::kLock && !context.subtype.empty()) {
            std::string tier(context.subtype);
            for (auto& c : tier) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            entry.subjectKey = "$SAL_SETTING_XP_SOURCES_LOCKPICK_" + tier;
        } else if (source == RewardRules::RewardSource::kPickpocket) {
            entry.subjectKey = "$SAL_SETTING_XP_SOURCES_PICKPOCKET_BASE";
        } else {
            entry.subject = std::string(context.subject);
        }
        XPJournal::RecordAward(std::move(entry));

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
            std::string label = it != EA::Config::notificationMessages.end() ? it->second : std::string{};
            QueueNotification(std::move(notifKey), std::move(label), amount);
        }
    }
}
