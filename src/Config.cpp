#include "PCH.h"
#include "Config.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace EA::Config {

    using json = nlohmann::json;

    // Resolve the path to the config JSON from the physical location of our DLL.
    // REX::W32::GetCurrentModule() returns &__ImageBase (our DLL's own base address),
    // ensuring we follow the DLL to wherever it is on disk — critical for MO2 where
    // the DLL lives in the mod folder, not the game's Data directory.
    static std::filesystem::path ResolveConfigPath() {
        wchar_t buf[REX::W32::MAX_PATH] = {};
        REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buf, REX::W32::MAX_PATH);
        return std::filesystem::path(buf).parent_path() / "ExperienceAndAttributes.json";
    }

    // Safely read a float from nested JSON, fall back to defaultVal if any key is missing
    // or the value is not a number.
    static float ReadFloat(const json& j,
                           std::initializer_list<std::string> path,
                           float defaultVal)
    {
        const json* node = &j;
        for (const auto& key : path) {
            if (!node->is_object() || !node->contains(key)) {
                return defaultVal;
            }
            node = &(*node)[key];
        }
        if (!node->is_number()) {
            return defaultVal;
        }
        return node->get<float>();
    }

    // Safely read a string from nested JSON.
    static std::string ReadString(const json& j,
                                   std::initializer_list<std::string> path,
                                   std::string_view defaultVal)
    {
        const json* node = &j;
        for (const auto& key : path) {
            if (!node->is_object() || !node->contains(key)) return std::string(defaultVal);
            node = &(*node)[key];
        }
        return node->is_string() ? node->get<std::string>() : std::string(defaultVal);
    }

    // Safely read a bool from nested JSON.
    static bool ReadBool(const json& j,
                         std::initializer_list<std::string> path,
                         bool defaultVal)
    {
        const json* node = &j;
        for (const auto& key : path) {
            if (!node->is_object() || !node->contains(key)) {
                return defaultVal;
            }
            node = &(*node)[key];
        }
        if (!node->is_boolean()) {
            return defaultVal;
        }
        return node->get<bool>();
    }

    void Load() {
        auto configPath = ResolveConfigPath();

        if (!std::filesystem::exists(configPath)) {
            logger::warn("[EA] Config: File not found at '{}'. Using all defaults.",
                         configPath.string());
            return;
        }

        std::ifstream file(configPath);
        if (!file.is_open()) {
            logger::error("[EA] Config: Could not open '{}'. Using all defaults.",
                          configPath.string());
            return;
        }

        json j;
        try {
            file >> j;
        } catch (const json::parse_error& e) {
            logger::error("[EA] Config: JSON parse error in '{}': {}. Using all defaults.",
                          configPath.string(), e.what());
            return;
        }

        // Debug
        verbose     = ReadBool(j,  {"debug", "verbose"},       verbose);
        maxLogFiles = static_cast<int>(ReadFloat(j, {"debug", "max_log_files"}, static_cast<float>(maxLogFiles)));

        // Quest XP
        xpQuestMain     = ReadFloat(j, {"xp_sources", "quest", "main"},      xpQuestMain);
        xpQuestSide     = ReadFloat(j, {"xp_sources", "quest", "side"},      xpQuestSide);
        xpQuestMisc     = ReadFloat(j, {"xp_sources", "quest", "misc"},      xpQuestMisc);
        xpQuestFaction  = ReadFloat(j, {"xp_sources", "quest", "faction"},   xpQuestFaction);
        xpQuestDaedric  = ReadFloat(j, {"xp_sources", "quest", "daedric"},   xpQuestDaedric);
        xpQuestCivilWar = ReadFloat(j, {"xp_sources", "quest", "civil_war"}, xpQuestCivilWar);
        xpQuestDLC      = ReadFloat(j, {"xp_sources", "quest", "dlc"},       xpQuestDLC);
        xpQuestOther    = ReadFloat(j, {"xp_sources", "quest", "other"},     xpQuestOther);

        // Kill XP
        xpKillDragon          = ReadFloat(j, {"xp_sources", "kill", "base_dragon"},        xpKillDragon);
        xpKillDaedra          = ReadFloat(j, {"xp_sources", "kill", "base_daedra"},        xpKillDaedra);
        xpKillUndead          = ReadFloat(j, {"xp_sources", "kill", "base_undead"},        xpKillUndead);
        xpKillAnimal          = ReadFloat(j, {"xp_sources", "kill", "base_animal"},        xpKillAnimal);
        xpKillCreature        = ReadFloat(j, {"xp_sources", "kill", "base_creature"},      xpKillCreature);
        xpKillHumanoid        = ReadFloat(j, {"xp_sources", "kill", "base_humanoid"},      xpKillHumanoid);
        xpKillDefault         = ReadFloat(j, {"xp_sources", "kill", "base_default"},       xpKillDefault);
        xpKillLevelScaleFactor = ReadFloat(j, {"xp_sources", "kill", "level_scale_factor"}, xpKillLevelScaleFactor);

        // Pickpocket XP
        xpPickpocketBase = ReadFloat(j, {"xp_sources", "pickpocket", "base"}, xpPickpocketBase);

        // Book XP
        xpBookNew   = ReadFloat(j, {"xp_sources", "book", "new_book"},   xpBookNew);
        xpBookSkill = ReadFloat(j, {"xp_sources", "book", "skill_book"}, xpBookSkill);

        // Location XP
        xpLocationDiscovered = ReadFloat(j, {"xp_sources", "location", "discovered"}, xpLocationDiscovered);
        xpLocationCleared    = ReadFloat(j, {"xp_sources", "location", "cleared"},    xpLocationCleared);

        // Lockpick XP
        xpLockNovice     = ReadFloat(j, {"xp_sources", "lockpick", "novice"},     xpLockNovice);
        xpLockApprentice = ReadFloat(j, {"xp_sources", "lockpick", "apprentice"}, xpLockApprentice);
        xpLockAdept      = ReadFloat(j, {"xp_sources", "lockpick", "adept"},      xpLockAdept);
        xpLockExpert     = ReadFloat(j, {"xp_sources", "lockpick", "expert"},     xpLockExpert);
        xpLockMaster     = ReadFloat(j, {"xp_sources", "lockpick", "master"},     xpLockMaster);

        // Leveling curve
        xpBase     = ReadFloat(j, {"leveling", "xp_base"},     xpBase);
        xpIncrease = ReadFloat(j, {"leveling", "xp_increase"}, xpIncrease);
        xpCap      = ReadFloat(j, {"leveling", "xp_cap"},      xpCap);

        // Skill allocation
        skillPointsPerLevel = static_cast<int>(ReadFloat(j,
            {"skill_allocation", "points_per_level"},
            static_cast<float>(skillPointsPerLevel)));

        // Skill menu UI layout
        menuPanelWidth     = static_cast<int>(ReadFloat(j, {"skill_allocation", "panel_width"},     static_cast<float>(menuPanelWidth)));
        menuPanelHeight    = static_cast<int>(ReadFloat(j, {"skill_allocation", "panel_height"},    static_cast<float>(menuPanelHeight)));
        menuPanelYOffset   = static_cast<int>(ReadFloat(j, {"skill_allocation", "panel_y_offset"},  static_cast<float>(menuPanelYOffset)));
        menuSkillRowGap    = static_cast<int>(ReadFloat(j, {"skill_allocation", "row_gap"},         static_cast<float>(menuSkillRowGap)));
        menuSkillColumnGap = static_cast<int>(ReadFloat(j, {"skill_allocation", "column_gap"},      static_cast<float>(menuSkillColumnGap)));
        menuSkillLabelValueGap = static_cast<int>(ReadFloat(j, {"skill_allocation", "label_value_gap"}, static_cast<float>(menuSkillLabelValueGap)));
        menuSkillValueArrowGap = static_cast<int>(ReadFloat(j, {"skill_allocation", "value_arrow_gap"}, static_cast<float>(menuSkillValueArrowGap)));
        menuSkillButtonTopGap = static_cast<int>(ReadFloat(j, {"skill_allocation", "button_top_gap"}, static_cast<float>(menuSkillButtonTopGap)));
        menuSkillButtonRowOffset = static_cast<int>(ReadFloat(j, {"skill_allocation", "button_row_offset"}, static_cast<float>(menuSkillButtonRowOffset)));
        menuSkillButtonGap   = static_cast<int>(ReadFloat(j, {"skill_allocation", "button_gap"},      static_cast<float>(menuSkillButtonGap)));
        menuFontSize       = static_cast<int>(ReadFloat(j, {"skill_allocation", "font_size"},       static_cast<float>(menuFontSize)));
        menuHeaderFontSize = static_cast<int>(ReadFloat(j, {"skill_allocation", "header_font_size"}, static_cast<float>(menuHeaderFontSize)));

        // New game
        resetSkillsOnNewGame = ReadBool(j, {"reset_skills_on_new_game"}, resetSkillsOnNewGame);

        // Notifications
        notificationsEnabled = ReadBool(j, {"notifications", "enabled"}, notificationsEnabled);
        auto loadMsg = [&](const std::string& key, std::string_view def) {
            notificationMessages[key] = ReadString(j, {"notifications", "messages", key}, def);
        };
        loadMsg("kill_dragon",         "A mighty dragon falls before you");
        loadMsg("kill_daedra",         "Daedric power dissipates");
        loadMsg("kill_undead",         "The undead are put to rest");
        loadMsg("kill_animal",         "The hunt concludes");
        loadMsg("kill_creature",       "A creature is slain");
        loadMsg("kill_humanoid",       "Victory in combat");
        loadMsg("kill_default",        "An enemy is defeated");
        loadMsg("quest_main",          "Your destiny unfolds");
        loadMsg("quest_side",          "Another soul aided");
        loadMsg("quest_misc",          "Task complete");
        loadMsg("quest_faction",       "Honour to your faction");
        loadMsg("quest_daedric",       "Daedric favour earned");
        loadMsg("quest_civil_war",     "For Skyrim");
        loadMsg("quest_dlc",           "A new chapter");
        loadMsg("quest_other",         "Quest complete");
        loadMsg("location_discovered", "A new place discovered");
        loadMsg("location_cleared",    "This place is yours now");
        loadMsg("lock_novice",         "A simple lock yields");
        loadMsg("lock_apprentice",     "A tricky lock yields");
        loadMsg("lock_adept",          "A complex lock yields");
        loadMsg("lock_expert",         "An expert lock yields");
        loadMsg("lock_master",         "A master lock yields");
        loadMsg("pickpocket",          "Fingers like shadows");
        loadMsg("book_read",           "Knowledge gained");
        loadMsg("book_skill",          "A skill honed through study");

        // Log what was loaded (always visible, not gated by verbose)
        logger::info("[EA] Config loaded from: {}", configPath.string());
        logger::info("[EA] Config: verbose={}", verbose);
        logger::info("[EA] Config: Quest XP — main={:.1f}, side={:.1f}, misc={:.1f}, faction={:.1f}, daedric={:.1f}, civil_war={:.1f}, dlc={:.1f}, other={:.1f}",
            xpQuestMain, xpQuestSide, xpQuestMisc, xpQuestFaction,
            xpQuestDaedric, xpQuestCivilWar, xpQuestDLC, xpQuestOther);
        logger::info("[EA] Config: Kill XP — dragon={:.1f}, daedra={:.1f}, undead={:.1f}, animal={:.1f}, creature={:.1f}, humanoid={:.1f}, default={:.1f}, scale={:.2f}",
            xpKillDragon, xpKillDaedra, xpKillUndead, xpKillAnimal,
            xpKillCreature, xpKillHumanoid, xpKillDefault, xpKillLevelScaleFactor);
        logger::info("[EA] Config: Pickpocket XP — base={:.1f}", xpPickpocketBase);
        logger::info("[EA] Config: Book XP — new={:.1f}, skill={:.1f}", xpBookNew, xpBookSkill);
        logger::info("[EA] Config: Location XP — discovered={:.1f}, cleared={:.1f}", xpLocationDiscovered, xpLocationCleared);
        logger::info("[EA] Config: Lock XP — novice={:.1f}, apprentice={:.1f}, adept={:.1f}, expert={:.1f}, master={:.1f}",
            xpLockNovice, xpLockApprentice, xpLockAdept, xpLockExpert, xpLockMaster);
        logger::info("[EA] Config: Leveling — xp_base={:.1f}, xp_increase={:.1f}, xp_cap={:.1f}",
            xpBase, xpIncrease, xpCap);
        logger::info("[EA] Config: Skill allocation — points_per_level={}, panel={}x{}, y_offset={}, row_gap={}, column_gap={}, label_value_gap={}, value_arrow_gap={}, button_top_gap={}, button_gap={}, font={}/{}",
            skillPointsPerLevel, menuPanelWidth, menuPanelHeight, menuPanelYOffset,
            menuSkillRowGap, menuSkillColumnGap, menuSkillLabelValueGap, menuSkillValueArrowGap,
            menuSkillButtonTopGap, menuSkillButtonGap, menuFontSize, menuHeaderFontSize);
        logger::info("[EA] Config: max_log_files={}", maxLogFiles);
        logger::info("[EA] Config: notifications_enabled={}", notificationsEnabled);
        logger::info("[EA] Config: reset_skills_on_new_game={}", resetSkillsOnNewGame);

        // Dump the entire raw JSON to the log for a complete session config record
        try {
            std::ifstream dumpFile(configPath);
            if (dumpFile.is_open()) {
                logger::info("[EA] Config: --- BEGIN ExperienceAndAttributes.json ---");
                std::string line;
                while (std::getline(dumpFile, line)) {
                    logger::info("[EA] Config: {}", line);
                }
                logger::info("[EA] Config: --- END ExperienceAndAttributes.json ---");
            }
        } catch (...) {
            logger::warn("[EA] Config: Could not dump JSON contents to log.");
        }
    }
}
