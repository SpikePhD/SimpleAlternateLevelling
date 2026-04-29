#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace EA::Config {

    // -----------------------------------------------------------------------
    // XP source values — populated from JSON on Load()
    // All values are floats. Defaults match the JSON file.
    // -----------------------------------------------------------------------

    // Debug
    inline bool verbose     = false;
    inline int  maxLogFiles = 10;

    // New game
    inline bool resetSkillsOnNewGame = false;

    // Notifications
    inline bool notificationsEnabled = true;
    inline std::unordered_map<std::string, std::string> notificationMessages{};

    // Quest XP
    inline float xpQuestMain     = 75.0f;
    inline float xpQuestCollege  = 50.0f;
    inline float xpQuestThieves  = 50.0f;
    inline float xpQuestBrotherhood = 50.0f;
    inline float xpQuestCompanions = 50.0f;
    inline float xpQuestSide     = 50.0f;
    inline float xpQuestMisc     = 25.0f;
    inline float xpQuestFaction  = 50.0f;
    inline float xpQuestDaedric  = 75.0f;
    inline float xpQuestCivilWar = 75.0f;
    inline float xpQuestDawnguard = 50.0f;
    inline float xpQuestDragonborn = 75.0f;
    inline float xpQuestObjectives = 10.0f;
    inline float xpQuestDLC      = 50.0f;
    inline float xpQuestOther    = 25.0f;

    // Kill XP — type-based base + level-delta bonus
    inline float xpKillDragon          = 20.0f;
    inline float xpKillDaedra          = 15.0f;
    inline float xpKillUndead          =  8.0f;
    inline float xpKillAnimal          =  3.0f;
    inline float xpKillCreature        =  5.0f;
    inline float xpKillHumanoid        =  5.0f;
    inline float xpKillDefault         =  5.0f;
    inline float xpKillLevelScaleFactor =  1.0f;
    inline float xpKillGlobalMultiplier = 1.0f;

    // Pickpocket XP
    inline float xpPickpocketBase      =  5.0f;

    // Book XP
    inline float xpBookNew       = 2.0f;
    inline float xpBookSkill     = 2.0f;
    inline bool  bookUseValueReward = false;
    inline float bookValueMultiplier = 1.0f;
    inline float bookReadingMultiplier = 1.0f;

    // Location XP
    inline float xpLocationDiscovered = 10.0f;
    inline float xpLocationCleared    = 15.0f;
    inline std::unordered_map<std::string, float> locationDiscoveryRewards{};
    inline std::unordered_map<std::string, float> locationClearingRewards{};

    // Lockpick XP
    inline float xpLockNovice     = 2.0f;
    inline float xpLockApprentice = 3.0f;
    inline float xpLockAdept      = 4.0f;
    inline float xpLockExpert     = 5.0f;
    inline float xpLockMaster     = 6.0f;

    // Leveling curve
    inline float xpBase     = 5.0f;
    inline float xpIncrease = 1.0f;
    inline float xpCap      = 500.0f;

    // Skill allocation
    inline int skillPointsPerLevel = 10;
    inline float skillCap = 200.0f;

    // Skill menu UI layout (passed to SWF at runtime)
    inline int menuPanelWidth     = 820;
    inline int menuPanelHeight    = 0;
    inline int menuPanelYOffset   = -90;
    inline int menuSkillRowGap    = 36;
    inline int menuSkillColumnGap = 22;
    inline int menuSkillLabelValueGap = 4;
    inline int menuSkillValueArrowGap = 2;
    inline int menuSkillButtonTopGap = 18;
    inline int menuSkillButtonRowOffset = 12;
    inline int menuSkillButtonGap = 16;
    inline int menuFontSize       = 13;
    inline int menuHeaderFontSize = 16;

    inline float GetReward(
        const std::unordered_map<std::string, float>& rewards,
        std::string_view                              key,
        float                                         fallback)
    {
        const auto it = rewards.find(std::string(key));
        return it != rewards.end() ? it->second : fallback;
    }

    // -----------------------------------------------------------------------
    // Loads config from JSON. Safe to call before game data is loaded.
    // Missing keys fall back to the inline defaults above.
    // -----------------------------------------------------------------------
    void Load();
}
