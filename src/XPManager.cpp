#include "PCH.h"
#include "XPManager.h"
#include "Config.h"

namespace EA::XPManager {

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    static float                          s_currentXP          = 0.0f;
    static int                            s_trackedLevel       = 1;
    static int                            s_pendingLevelUps    = 0;
    static bool                           s_levelUpInProgress  = false;
    static std::unordered_set<RE::FormID> s_deadActors;
    static std::unordered_set<RE::FormID> s_completedQuests;

    // -----------------------------------------------------------------------
    // Cosave accessors
    // -----------------------------------------------------------------------
    float GetCurrentXP()            { return s_currentXP; }
    void  SetCurrentXP(float xp)    { s_currentXP = xp; }
    int   GetTrackedLevel()         { return s_trackedLevel; }
    void  SetTrackedLevel(int lvl)  { s_trackedLevel = lvl; }
    int   GetPendingLevelUps()      { return s_pendingLevelUps; }
    void  SetPendingLevelUps(int n) { s_pendingLevelUps = n; }
    bool  GetLevelUpInProgress()        { return s_levelUpInProgress; }
    void  SetLevelUpInProgress(bool val) {
        s_levelUpInProgress = val;
        logger::info("[EA] LevelUpInProgress set to {}.", val);
    }

    // -----------------------------------------------------------------------
    // Kill guard
    // -----------------------------------------------------------------------
    bool RegisterKill(RE::FormID actorID) {
        if (s_deadActors.contains(actorID)) {
            logger::debug("[EA] Kill guard: FormID {:08X} already dead — skipped.", actorID);
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
    // Quest guard
    // -----------------------------------------------------------------------
    bool RegisterQuestXP(RE::FormID questID) {
        if (s_completedQuests.contains(questID)) {
            logger::debug("[EA] Quest guard: FormID {:08X} already awarded XP — skipped.", questID);
            return false;
        }
        s_completedQuests.insert(questID);
        return true;
    }

    void AwardXPIfQuestNew(RE::FormID questID, float amount, std::string_view source) {
        if (!RegisterQuestXP(questID)) return;
        AwardXP(amount, source);
    }

    void ResetQuestGuard() {
        s_completedQuests.clear();
        logger::info("[EA] Quest guard: cleared.");
    }

    // -----------------------------------------------------------------------
    // GetXPThreshold
    // threshold(level) = min(xp_cap, xp_base + (current_level * xp_increase))
    // 'level' is the player's level at the START of that tier (before leveling up).
    // -----------------------------------------------------------------------
    float GetXPThreshold(int playerLevel) {
        float raw = Config::xpBase +
                    (static_cast<float>(playerLevel) * Config::xpIncrease);
        return std::min(Config::xpCap, raw);
    }

    // -----------------------------------------------------------------------
    // TriggerLevelUp
    //
    // Sets the player's internal XP bucket to exactly the level threshold.
    // The vanilla engine checks xp >= levelThreshold every tick; when true it
    // fires the FULL native level-up sequence: attribute selection screen,
    // perk point award, and level increment.
    //
    // This is the approach used by the Experience mod (community reference
    // implementation). AdvanceLevel(false) was rejected because the engine
    // recalculates player level from skill XP totals on the next frame —
    // since our SkillHook blocks all skill XP those totals are zero, so the
    // engine immediately resets the player back to level 1.
    // -----------------------------------------------------------------------
    static void TriggerLevelUp(RE::PlayerCharacter* player) {
        auto* skills = player->GetInfoRuntimeData().skills;
        if (!skills || !skills->data) {
            logger::error("[EA] TriggerLevelUp: PlayerSkills or data is null — level-up FAILED.");
            return;
        }

        // Read state BEFORE the write
        int   levelBefore     = static_cast<int>(player->GetLevel());
        float xpBefore        = skills->data->xp;
        float thresholdBefore = skills->data->levelThreshold;

        // Trigger the level-up
        skills->data->xp = skills->data->levelThreshold;

        // Read state AFTER the write
        int   levelAfter = static_cast<int>(player->GetLevel());
        float xpAfter    = skills->data->xp;

        logger::info("[EA] TriggerLevelUp: "
                     "player->GetLevel() before={} after={} | "
                     "skills->data->xp before={:.1f} after={:.1f} | "
                     "levelThreshold={:.1f}",
                     levelBefore, levelAfter,
                     xpBefore, xpAfter,
                     thresholdBefore);

        if (levelAfter > levelBefore) {
            logger::info("[EA] TriggerLevelUp: SUCCESS — engine immediately advanced level.");
        } else {
            logger::info("[EA] TriggerLevelUp: DEFERRED — level not yet updated "
                         "(engine will process on next tick).");
        }
    }

    // -----------------------------------------------------------------------
    // FirePendingLevelUp
    //
    // Fires exactly ONE pending level-up if any are queued.
    // Increments s_trackedLevel so subsequent threshold calculations are correct.
    // Called from AwardXP (both at the start and end) and from OnGameLoad so
    // level-ups queued before a save are not lost on reload.
    // -----------------------------------------------------------------------
    void FirePendingLevelUp() {
        if (s_pendingLevelUps <= 0) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        // Guard: never fire while the engine is still processing a level-up.
        // The "Level Increases" TrackedStat event will call us again when ready.
        if (s_levelUpInProgress) {
            logger::info("[EA] FirePendingLevelUp: level-up in progress — "
                         "deferring. Pending: {}", s_pendingLevelUps);
            return;
        }

        s_levelUpInProgress = true;
        s_pendingLevelUps--;
        s_trackedLevel++;

        logger::info("[EA] FirePendingLevelUp: Triggering level-up to {}. "
                     "Remaining pending: {} | InProgress: true",
                     s_trackedLevel, s_pendingLevelUps);

        TriggerLevelUp(player);

        RE::DebugNotification(
            std::format("Level Up! You are now level {}.", s_trackedLevel).c_str());
    }

    // -----------------------------------------------------------------------
    // AwardXP
    //
    // Adds 'amount' XP. Uses s_trackedLevel (not player->GetLevel()) for all
    // threshold math so calculations stay correct even when the engine lags.
    //
    // Level-up delivery is deferred: we count how many thresholds are crossed,
    // increment s_pendingLevelUps for each, then fire one immediately.
    // Remaining pending level-ups fire one-per-call on future XP events,
    // giving the vanilla engine one full tick per level-up to process the
    // attribute screen and perk point.
    // -----------------------------------------------------------------------
    void AwardXP(float amount, std::string_view source) {
        if (amount <= 0.0f) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("[EA] XPManager::AwardXP called but PlayerCharacter is null.");
            return;
        }

        s_currentXP += amount;
        float threshold = GetXPThreshold(s_trackedLevel);

        logger::info("[EA] XP +{:.1f} from '{}' | Total: {:.1f} / {:.1f} | "
                     "TrackedLevel {} ({:.1f}% to next)",
            amount, source, s_currentXP, threshold, s_trackedLevel,
            (s_currentXP / threshold) * 100.0f);

        RE::DebugNotification(
            std::format("+{:.0f} XP ({})", amount, source).c_str());

        // Count how many level thresholds this XP award crosses.
        // Use a local level counter so s_trackedLevel stays consistent
        // with FirePendingLevelUp calls (which are the only place it increments).
        int localLevel = s_trackedLevel;
        while (s_currentXP >= threshold) {
            s_currentXP -= threshold;
            localLevel++;
            threshold = GetXPThreshold(localLevel);
            s_pendingLevelUps++;

            logger::info("[EA] Level threshold crossed. Queuing level-up to {}. "
                         "Carry-over XP: {:.1f} | Next threshold: {:.1f} | "
                         "Total pending: {}",
                         localLevel, s_currentXP, threshold, s_pendingLevelUps);
        }

        // Fire ONE level-up now to start the chain.
        // Subsequent ones fire when the engine confirms each level via the
        // "Level Increases" TrackedStat event caught in EventSinks.cpp.
        if (s_pendingLevelUps > 0) {
            FirePendingLevelUp();
        }

        logger::info("[EA] XP progress: {:.1f} / {:.1f} ({:.1f}% to next) | "
                     "TrackedLevel: {} | Pending: {}",
            s_currentXP, GetXPThreshold(s_trackedLevel),
            (s_currentXP / GetXPThreshold(s_trackedLevel)) * 100.0f,
            s_trackedLevel, s_pendingLevelUps);

        // Ground-truth snapshot: compare engine state vs our tracked state.
        // If GetLevel() consistently diverges from s_trackedLevel, the engine
        // is not accepting our TriggerLevelUp writes.
        logger::info("[EA] PlayerState: GetLevel()={} s_trackedLevel={} "
                     "s_pendingLevelUps={} s_currentXP={:.1f}",
                     static_cast<int>(player->GetLevel()),
                     s_trackedLevel,
                     s_pendingLevelUps,
                     s_currentXP);
    }
}
