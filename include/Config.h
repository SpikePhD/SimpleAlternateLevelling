#pragma once

namespace EA::Config {

    // -----------------------------------------------------------------------
    // XP source values — populated from JSON on Load()
    // All values are floats. Defaults match the JSON file.
    // -----------------------------------------------------------------------

    // Debug
    inline bool verbose     = false;
    inline int  maxLogFiles = 10;

    // Quest XP
    inline float xpQuestMain     = 50.0f;
    inline float xpQuestSide     = 50.0f;
    inline float xpQuestMisc     = 25.0f;
    inline float xpQuestFaction  = 50.0f;
    inline float xpQuestDaedric  = 50.0f;
    inline float xpQuestCivilWar = 50.0f;
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

    // Pickpocket XP
    inline float xpPickpocketBase      =  5.0f;

    // Book XP
    inline float xpBookNew       = 2.0f;
    inline float xpBookSkill     = 2.0f;

    // Location XP
    inline float xpLocationDiscovered = 10.0f;
    inline float xpLocationCleared    = 15.0f;

    // Lockpick XP
    inline float xpLockNovice     = 2.0f;
    inline float xpLockApprentice = 3.0f;
    inline float xpLockAdept      = 4.0f;
    inline float xpLockExpert     = 5.0f;
    inline float xpLockMaster     = 6.0f;

    // Leveling curve
    inline float xpBase     = 200.0f;
    inline float xpIncrease =  25.0f;
    inline float xpCap      = 1000.0f;

    // Skill allocation
    inline int skillPointsPerLevel = 10;

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

    // -----------------------------------------------------------------------
    // Loads config from JSON. Safe to call before game data is loaded.
    // Missing keys fall back to the inline defaults above.
    // -----------------------------------------------------------------------
    void Load();
}
