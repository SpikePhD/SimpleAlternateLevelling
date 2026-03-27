#pragma once

namespace EA::XPManager {

    // Awards XP from a named source, queues level-ups, fires HUD notification.
    void AwardXP(float amount, std::string_view source);

    // Returns the XP threshold needed to level up from a given level.
    float GetXPThreshold(int playerLevel);

    // Fires ONE pending level-up if any are queued.
    // Call on game load and within AwardXP for deferred delivery.
    void FirePendingLevelUp();

    // Cosave accessors.
    float GetCurrentXP();
    void  SetCurrentXP(float xp);

    // Our own level tracker — independent of player->GetLevel().
    // Initialized from the game on first load; incremented on each level-up.
    int  GetTrackedLevel();
    void SetTrackedLevel(int level);

    // Pending level-up counter — persisted in cosave so level-ups queued
    // before a save are still delivered after reload.
    int  GetPendingLevelUps();
    void SetPendingLevelUps(int count);

    // Level-up in-progress flag.
    // True between TriggerLevelUp and the "Level Increases" TrackedStat event.
    // Prevents writing to the XP bucket while the engine is processing a level-up.
    bool GetLevelUpInProgress();
    void SetLevelUpInProgress(bool val);

    // Kill deduplication guard.
    // Returns true if this is a new kill (XP should be awarded).
    // Returns false if this FormID was already processed this session.
    bool RegisterKill(RE::FormID actorID);

    // Clears the kill guard set. Call on game load and new game.
    void ResetKillGuard();

    // Quest completion deduplication guard.
    // Returns true if this quest FormID has not yet been awarded XP this session.
    // Returns false if already processed (skip).
    bool RegisterQuestXP(RE::FormID questID);

    // Awards XP only if the quest FormID has not been seen before this session.
    // Combines RegisterQuestXP + AwardXP in one call.
    void AwardXPIfQuestNew(RE::FormID questID, float amount, std::string_view source);

    // Clears the quest guard set. Call on game load and new game.
    void ResetQuestGuard();
}
