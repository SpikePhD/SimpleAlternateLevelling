#include "RewardRules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace EA::RewardRules {

    bool QuestLifecycle::Observe(std::uint32_t questID, QuestSignal signal)
    {
        if (questID == 0) {
            return false;
        }

        switch (signal) {
            case QuestSignal::kCompleted:
                return completed_.insert(questID).second;
            case QuestSignal::kStarted:
            case QuestSignal::kReset:
                completed_.erase(questID);
                return false;
        }
        return false;
    }

    void QuestLifecycle::Reset() noexcept
    {
        completed_.clear();
    }

    void NewlyFlaggedTracker::Snapshot(std::span<const std::uint32_t> flagged)
    {
        known_.clear();
        known_.insert(flagged.begin(), flagged.end());
        ready_ = true;
    }

    void NewlyFlaggedTracker::Invalidate() noexcept
    {
        known_.clear();
        ready_ = false;
    }

    std::vector<std::uint32_t> NewlyFlaggedTracker::Observe(
        std::span<const std::uint32_t> flagged)
    {
        if (!ready_) {
            Snapshot(flagged);
            return {};
        }

        std::vector<std::uint32_t> newlyFlagged;
        for (const auto formID : flagged) {
            if (formID != 0 && known_.insert(formID).second) {
                newlyFlagged.push_back(formID);
            }
        }
        return newlyFlagged;
    }

    bool IsObjectiveCompletionTransition(
        std::uint32_t oldState,
        std::uint32_t newState) noexcept
    {
        const auto completed = [](std::uint32_t state) {
            return state == 2u || state == 3u;
        };
        return !completed(oldState) && completed(newState);
    }

    bool ShouldRewardPickpocket(std::int32_t numItems) noexcept
    {
        return numItems > 0;
    }

    bool ShouldRewardKill(
        bool playerCredited,
        bool victimIsPlayer,
        bool victimCommandedByPlayer) noexcept
    {
        return playerCredited && !victimIsPlayer && !victimCommandedByPlayer;
    }

    float CalculateKillReward(
        float baseXP,
        int enemyLevel,
        int playerLevel,
        float levelScaleFactor,
        float globalMultiplier) noexcept
    {
        const double levelDelta = std::max(
            0.0,
            static_cast<double>(enemyLevel) - static_cast<double>(playerLevel));
        const double bonus = levelDelta *
                             static_cast<double>(levelScaleFactor);
        const double total = (static_cast<double>(baseXP) + bonus) *
                             static_cast<double>(globalMultiplier);
        const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
        if (!std::isfinite(total) || total > maxFloat || total < -maxFloat) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return static_cast<float>(total);
    }

    std::string_view ClassifyMarkerType(std::uint16_t markerType) noexcept
    {
        switch (markerType) {
            case 1: return "city";
            case 2: return "town";
            case 3: return "settlement";
            case 4: return "cave";
            case 5: return "camp";
            case 6: return "fort";
            case 7: return "nordic_ruin";
            case 8: return "dwemer_ruin";
            case 9: return "shipwreck";
            case 10: return "grove";
            case 11: return "landmark";
            case 12: return "dragon_lair";
            case 13: return "farm";
            case 14: return "wood_mill";
            case 15: return "mine";
            case 16:
            case 17: return "military_camp";
            case 18: return "doomstone";
            case 19: return "wheat_mill";
            case 20: return "smelter";
            case 21: return "stable";
            case 22: return "imperial_tower";
            case 23: return "clearing";
            case 24: return "pass";
            case 25: return "altar";
            case 26: return "rock";
            case 27: return "lighthouse";
            case 28: return "orc_stronghold";
            case 29: return "giant_camp";
            case 30: return "shack";
            case 31: return "nordic_tower";
            case 32: return "nordic_dwelling";
            case 33: return "docks";
            case 34: return "daedric_shrine";
            case 35:
            case 37:
            case 39:
            case 41:
            case 43:
            case 45:
            case 47:
            case 49:
            case 51: return "castle";
            case 36:
            case 38:
            case 40:
            case 42:
            case 44:
            case 46:
            case 48:
            case 50:
            case 52: return "city";
            case 53: return "daedric_shrine";
            case 54: return "town";
            case 55: return "doomstone";
            case 56: return "landmark";
            case 57:
            case 58: return "docks";
            case 59: return "castle";
            default: return "default";
        }
    }

    std::string_view ClassifyLockLevel(std::int32_t lockLevel) noexcept
    {
        switch (lockLevel) {
            case 0: return "novice";
            case 1: return "apprentice";
            case 2: return "adept";
            case 3: return "expert";
            case 4: return "master";
            default: return "novice";
        }
    }
}
