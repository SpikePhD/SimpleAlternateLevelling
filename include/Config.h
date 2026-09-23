#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace EA { class SettingsModel; }

namespace EA::Config {

    // -----------------------------------------------------------------------
    // XP source values — populated from JSON on Load()
    // All values are floats. Defaults match the JSON file.
    // -----------------------------------------------------------------------

    // Debug
    inline bool verbose     = false;
    inline int  maxLogFiles = 10;

    // Applied once to a new character after RaceMenu closes.
    enum class StartingSkillsMode { Vanilla, Zero, Uniform, Custom };
    inline StartingSkillsMode startingSkillsMode = StartingSkillsMode::Vanilla;
    inline float startingSkillsUniformValue = 0.0f;
    inline std::unordered_map<std::string, float> startingSkillsCustom{};
    inline int settingsHotkey = 0x44; // F10 DirectInput scan code

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
    inline constexpr float kDefaultXPBase     = 75.0f;
    inline constexpr float kDefaultXPIncrease = 25.0f;
    inline constexpr float kDefaultXPCap      = 10000000.0f;

    inline float xpBase     = kDefaultXPBase;
    inline float xpIncrease = kDefaultXPIncrease;
    inline float xpCap      = kDefaultXPCap;

    // Skill allocation
    inline constexpr int   kDefaultSkillPointsPerLevel = 10;
    inline constexpr float kDefaultSkillCap = 200.0f;
    inline int skillPointsPerLevel = kDefaultSkillPointsPerLevel;
    inline float skillCap = kDefaultSkillCap;

    // Skill menu UI layout (passed to SWF at runtime)
    inline constexpr int kDefaultMenuPanelWidth = 820;
    inline constexpr int kDefaultMenuPanelHeight = 0;
    inline constexpr int kDefaultMenuPanelYOffset = -90;
    inline constexpr int kDefaultMenuSkillRowGap = 36;
    inline constexpr int kDefaultMenuSkillColumnGap = 22;
    inline constexpr int kDefaultMenuSkillLabelValueGap = 4;
    inline constexpr int kDefaultMenuSkillValueArrowGap = 2;
    inline constexpr int kDefaultMenuSkillButtonTopGap = 18;
    inline constexpr int kDefaultMenuSkillButtonRowOffset = 12;
    inline constexpr int kDefaultMenuSkillButtonGap = 16;
    inline constexpr int kDefaultMenuFontSize = 13;
    inline constexpr int kDefaultMenuHeaderFontSize = 16;

    inline int menuPanelWidth = kDefaultMenuPanelWidth;
    inline int menuPanelHeight = kDefaultMenuPanelHeight;
    inline int menuPanelYOffset = kDefaultMenuPanelYOffset;
    inline int menuSkillRowGap = kDefaultMenuSkillRowGap;
    inline int menuSkillColumnGap = kDefaultMenuSkillColumnGap;
    inline int menuSkillLabelValueGap = kDefaultMenuSkillLabelValueGap;
    inline int menuSkillValueArrowGap = kDefaultMenuSkillValueArrowGap;
    inline int menuSkillButtonTopGap = kDefaultMenuSkillButtonTopGap;
    inline int menuSkillButtonRowOffset = kDefaultMenuSkillButtonRowOffset;
    inline int menuSkillButtonGap = kDefaultMenuSkillButtonGap;
    inline int menuFontSize = kDefaultMenuFontSize;
    inline int menuHeaderFontSize = kDefaultMenuHeaderFontSize;

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
    void ApplyEffective();
    [[nodiscard]] EA::SettingsModel& Settings();
    [[nodiscard]] bool SaveDraftAndApply(std::string& error);
}
