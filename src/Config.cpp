#include "PCH.h"
#include "Config.h"
#include "Progression.h"
#include "LogPolicy.h"
#include "UIRules.h"

#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <limits>

namespace EA::Config {

    using json = nlohmann::json;

    // Resolve the path to the config JSON from the physical location of our DLL.
    // REX::W32::GetCurrentModule() returns &__ImageBase (our DLL's own base address),
    // ensuring we follow the DLL to wherever it is on disk — critical for MO2 where
    // the DLL lives in the mod folder, not the game's Data directory.
    static std::filesystem::path ResolveConfigPath() {
        wchar_t buf[260] = {};
        REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buf, static_cast<std::uint32_t>(std::size(buf)));
        return std::filesystem::path(buf).parent_path() / "SimpleAlternateLevelling.json";
    }

    // Safely read a float from nested JSON, fall back to defaultVal if any key is missing
    // or the value is not a number.
    static float ReadFloat(const json& j,
                           std::initializer_list<std::string> path,
                           float defaultVal,
                           bool* rejectedValue = nullptr)
    {
        if (rejectedValue) {
            *rejectedValue = false;
        }
        const json* node = &j;
        for (const auto& key : path) {
            if (!node->is_object() || !node->contains(key)) {
                return defaultVal;
            }
            node = &(*node)[key];
        }
        if (!node->is_number()) {
            if (rejectedValue) {
                *rejectedValue = true;
            }
            return defaultVal;
        }
        try {
            const double value = node->get<double>();
            const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(value) || value < -maxFloat || value > maxFloat) {
                if (rejectedValue) {
                    *rejectedValue = true;
                }
                return defaultVal;
            }
            return static_cast<float>(value);
        } catch (const json::exception&) {
            if (rejectedValue) {
                *rejectedValue = true;
            }
            return defaultVal;
        }
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

    struct LogRetentionResult {
        int  value{ LogPolicy::kDefaultMaxLogFiles };
        bool invalid{ false };
    };

    struct NumberResult {
        double value{ 0.0 };
        bool   present{ false };
        bool   invalid{ false };
    };

    static NumberResult ReadNumber(
        const json& j, std::initializer_list<std::string> path, double defaultValue)
    {
        const json* node = &j;
        for (const auto& key : path) {
            if (!node->is_object() || !node->contains(key)) {
                return { defaultValue, false, false };
            }
            node = &(*node)[key];
        }
        if (!node->is_number()) {
            return { defaultValue, true, true };
        }
        try {
            const auto value = node->get<double>();
            return { value, true, !std::isfinite(value) };
        } catch (const json::exception&) {
            return { defaultValue, true, true };
        }
    }

    static LogRetentionResult ReadLogRetention(const json& j) {
        if (!j.contains("debug") || !j["debug"].is_object() ||
            !j["debug"].contains("max_log_files")) {
            return {};
        }

        const auto& value = j["debug"]["max_log_files"];
        if (!value.is_number_integer()) {
            return { LogPolicy::kDefaultMaxLogFiles, true };
        }

        try {
            const auto raw = value.get<std::int64_t>();
            const auto validated = LogPolicy::ValidateMaxLogFiles(raw);
            return { validated, raw < 0 || raw > LogPolicy::kMaximumMaxLogFiles };
        } catch (const json::exception&) {
            return { LogPolicy::kDefaultMaxLogFiles, true };
        }
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
        } catch (const json::exception& e) {
            logger::error("[EA] Config: JSON error in '{}': {}. Using all defaults.",
                          configPath.string(), e.what());
            return;
        }

        // Debug
        verbose     = ReadBool(j,  {"debug", "verbose"},       verbose);
        const auto logRetention = ReadLogRetention(j);
        maxLogFiles = logRetention.value;
        if (logRetention.invalid) {
            logger::warn("[EA] Config: debug.max_log_files must be an integer from 0 through {}; using default {}.",
                LogPolicy::kMaximumMaxLogFiles, LogPolicy::kDefaultMaxLogFiles);
        }

        // Quest XP
        xpQuestMain     = ReadFloat(j, {"xp_sources", "quest", "main"},      xpQuestMain);
        xpQuestFaction  = ReadFloat(j, {"xp_sources", "quest", "faction"},   xpQuestFaction);
        xpQuestDLC      = ReadFloat(j, {"xp_sources", "quest", "dlc"},       xpQuestDLC);
        xpQuestCollege  = ReadFloat(j, {"xp_sources", "quest", "college"},   xpQuestCollege);
        xpQuestThieves  = ReadFloat(j, {"xp_sources", "quest", "thieves"},   xpQuestThieves);
        xpQuestBrotherhood = ReadFloat(j, {"xp_sources", "quest", "brotherhood"}, xpQuestBrotherhood);
        xpQuestCompanions = ReadFloat(j, {"xp_sources", "quest", "companions"}, xpQuestCompanions);
        xpQuestSide     = ReadFloat(j, {"xp_sources", "quest", "side"},      xpQuestSide);
        xpQuestMisc     = ReadFloat(j, {"xp_sources", "quest", "misc"},      xpQuestMisc);
        xpQuestDaedric  = ReadFloat(j, {"xp_sources", "quest", "daedric"},   xpQuestDaedric);
        xpQuestCivilWar = ReadFloat(j, {"xp_sources", "quest", "civil_war"}, xpQuestCivilWar);
        xpQuestDawnguard = ReadFloat(j, {"xp_sources", "quest", "dawnguard"}, xpQuestDawnguard);
        xpQuestDragonborn = ReadFloat(j, {"xp_sources", "quest", "dragonborn"}, xpQuestDragonborn);
        xpQuestObjectives = ReadFloat(j, {"xp_sources", "quest", "objectives"}, xpQuestObjectives);
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
        xpKillGlobalMultiplier = ReadFloat(j, {"xp_sources", "kill", "global_multiplier"}, xpKillGlobalMultiplier);

        // Pickpocket XP
        xpPickpocketBase = ReadFloat(j, {"xp_sources", "pickpocket", "base"}, xpPickpocketBase);

        // Book XP
        xpBookNew   = ReadFloat(j, {"xp_sources", "book", "new_book"},   xpBookNew);
        xpBookSkill = ReadFloat(j, {"xp_sources", "book", "skill_book"}, xpBookSkill);
        bookUseValueReward = ReadBool(j, {"xp_sources", "book", "use_value_reward"}, bookUseValueReward);
        bookValueMultiplier = ReadFloat(j, {"xp_sources", "book", "value_multiplier"}, bookValueMultiplier);
        bookReadingMultiplier = ReadFloat(j, {"xp_sources", "book", "reading_multiplier"}, bookReadingMultiplier);

        // Location XP
        xpLocationDiscovered = ReadFloat(j, {"xp_sources", "location", "discovered"}, xpLocationDiscovered);
        xpLocationCleared    = ReadFloat(j, {"xp_sources", "location", "cleared"},    xpLocationCleared);

        locationDiscoveryRewards.clear();
        locationClearingRewards.clear();
        const std::pair<std::string_view, float> discoveryDefaults[] = {
            {"city", xpLocationDiscovered}, {"town", xpLocationDiscovered}, {"settlement", xpLocationDiscovered},
            {"cave", xpLocationDiscovered}, {"camp", xpLocationDiscovered}, {"fort", xpLocationDiscovered},
            {"nordic_ruin", xpLocationDiscovered}, {"dwemer_ruin", xpLocationDiscovered}, {"shipwreck", xpLocationDiscovered},
            {"grove", xpLocationDiscovered}, {"landmark", xpLocationDiscovered}, {"dragon_lair", xpLocationDiscovered},
            {"farm", xpLocationDiscovered}, {"wood_mill", xpLocationDiscovered}, {"mine", xpLocationDiscovered},
            {"military_camp", xpLocationDiscovered}, {"doomstone", xpLocationDiscovered}, {"wheat_mill", xpLocationDiscovered},
            {"smelter", xpLocationDiscovered}, {"stable", xpLocationDiscovered}, {"imperial_tower", xpLocationDiscovered},
            {"clearing", xpLocationDiscovered}, {"pass", xpLocationDiscovered}, {"altar", xpLocationDiscovered},
            {"rock", xpLocationDiscovered}, {"lighthouse", xpLocationDiscovered}, {"orc_stronghold", xpLocationDiscovered},
            {"giant_camp", xpLocationDiscovered}, {"shack", xpLocationDiscovered}, {"nordic_tower", xpLocationDiscovered},
            {"nordic_dwelling", xpLocationDiscovered}, {"docks", xpLocationDiscovered}, {"daedric_shrine", xpLocationDiscovered},
            {"castle", xpLocationDiscovered}, {"default", xpLocationDiscovered}
        };
        const std::pair<std::string_view, float> clearingDefaults[] = {
            {"city", xpLocationCleared}, {"town", xpLocationCleared}, {"settlement", xpLocationCleared},
            {"cave", xpLocationCleared}, {"camp", xpLocationCleared}, {"fort", xpLocationCleared},
            {"nordic_ruin", xpLocationCleared}, {"dwemer_ruin", xpLocationCleared}, {"shipwreck", xpLocationCleared},
            {"grove", xpLocationCleared}, {"landmark", xpLocationCleared}, {"dragon_lair", xpLocationCleared},
            {"farm", xpLocationCleared}, {"wood_mill", xpLocationCleared}, {"mine", xpLocationCleared},
            {"military_camp", xpLocationCleared}, {"doomstone", xpLocationCleared}, {"wheat_mill", xpLocationCleared},
            {"smelter", xpLocationCleared}, {"stable", xpLocationCleared}, {"imperial_tower", xpLocationCleared},
            {"clearing", xpLocationCleared}, {"pass", xpLocationCleared}, {"altar", xpLocationCleared},
            {"rock", xpLocationCleared}, {"lighthouse", xpLocationCleared}, {"orc_stronghold", xpLocationCleared},
            {"giant_camp", xpLocationCleared}, {"shack", xpLocationCleared}, {"nordic_tower", xpLocationCleared},
            {"nordic_dwelling", xpLocationCleared}, {"docks", xpLocationCleared}, {"daedric_shrine", xpLocationCleared},
            {"castle", xpLocationCleared}, {"default", xpLocationCleared}
        };
        for (const auto& [key, def] : discoveryDefaults) {
            locationDiscoveryRewards.emplace(std::string(key),
                ReadFloat(j, {"xp_sources", "location", "discovery", std::string(key)}, def));
        }
        for (const auto& [key, def] : clearingDefaults) {
            locationClearingRewards.emplace(std::string(key),
                ReadFloat(j, {"xp_sources", "location", "clearing", std::string(key)}, def));
        }

        // Lockpick XP
        xpLockNovice     = ReadFloat(j, {"xp_sources", "lockpick", "novice"},     xpLockNovice);
        xpLockApprentice = ReadFloat(j, {"xp_sources", "lockpick", "apprentice"}, xpLockApprentice);
        xpLockAdept      = ReadFloat(j, {"xp_sources", "lockpick", "adept"},      xpLockAdept);
        xpLockExpert     = ReadFloat(j, {"xp_sources", "lockpick", "expert"},     xpLockExpert);
        xpLockMaster     = ReadFloat(j, {"xp_sources", "lockpick", "master"},     xpLockMaster);

        // Leveling curve
        bool rejectedXPBase = false;
        bool rejectedXPIncrease = false;
        bool rejectedXPCap = false;
        xpBase     = ReadFloat(j, {"leveling", "xp_base"},     xpBase, &rejectedXPBase);
        xpIncrease = ReadFloat(j, {"leveling", "xp_increase"}, xpIncrease, &rejectedXPIncrease);
        xpCap      = ReadFloat(j, {"leveling", "xp_cap"},      xpCap, &rejectedXPCap);

        // Preserve the distinction between a missing field (use the current
        // default) and a present but unrepresentable field (reject and warn).
        if (rejectedXPBase) {
            xpBase = std::numeric_limits<float>::quiet_NaN();
        }
        if (rejectedXPIncrease) {
            xpIncrease = std::numeric_limits<float>::quiet_NaN();
        }
        if (rejectedXPCap) {
            xpCap = std::numeric_limits<float>::quiet_NaN();
        }

        const auto validatedCurve = Progression::ValidateCurve(
            { xpBase, xpIncrease, xpCap },
            { kDefaultXPBase, kDefaultXPIncrease, kDefaultXPCap });
        if (validatedCurve.replacedBase) {
            logger::warn("[EA] Config: leveling.xp_base is invalid; using built-in default {:.1f}.", kDefaultXPBase);
        }
        if (validatedCurve.replacedIncrease) {
            logger::warn("[EA] Config: leveling.xp_increase is invalid; using built-in default {:.1f}.", kDefaultXPIncrease);
        }
        if (validatedCurve.replacedCap) {
            logger::warn("[EA] Config: leveling.xp_cap is invalid; using built-in default {:.1f}.", kDefaultXPCap);
        }
        xpBase     = validatedCurve.curve.base;
        xpIncrease = validatedCurve.curve.increase;
        xpCap      = validatedCurve.curve.cap;

        // Skill allocation and UI layout. Each present invalid value is
        // rejected independently so one typo cannot poison the entire menu.
        const auto readInteger = [&](std::string_view key, int defaultValue, int minimum, int maximum) {
            const auto raw = ReadNumber(j, { "skill_allocation", std::string(key) }, defaultValue);
            const auto validated = raw.invalid
                ? UIRules::IntegerValidation{ defaultValue, true }
                : UIRules::ValidateInteger(raw.value, defaultValue, minimum, maximum);
            if (raw.present && validated.replaced) {
                logger::warn("[EA] Config: skill_allocation.{} must be an integer from {} through {}; using default {}.",
                    key, minimum, maximum, defaultValue);
            }
            return validated.value;
        };
        const auto readGap = [&](std::string_view key, int defaultValue, int minimum = 0, int maximum = 120) {
            return readInteger(key, defaultValue, minimum, maximum);
        };

        skillPointsPerLevel = readInteger("points_per_level", kDefaultSkillPointsPerLevel, 0, 1000);
        const auto rawCap = ReadNumber(j, { "skill_allocation", "skill_cap" }, kDefaultSkillCap);
        const auto validatedCap = rawCap.invalid
            ? UIRules::FloatValidation{ kDefaultSkillCap, true }
            : UIRules::ValidateFloat(rawCap.value, kDefaultSkillCap, 1.0f, 1000.0f);
        skillCap = validatedCap.value;
        if (rawCap.present && validatedCap.replaced) {
            logger::warn("[EA] Config: skill_allocation.skill_cap must be finite and from 1 through 1000; using default {:.1f}.",
                kDefaultSkillCap);
        }

        menuPanelWidth = readInteger("panel_width", kDefaultMenuPanelWidth, 480, 1280);
        const auto rawHeight = ReadNumber(j, { "skill_allocation", "panel_height" }, kDefaultMenuPanelHeight);
        const auto validatedHeight = rawHeight.invalid
            ? UIRules::IntegerValidation{ kDefaultMenuPanelHeight, true }
            : UIRules::ValidatePanelHeight(rawHeight.value, kDefaultMenuPanelHeight);
        menuPanelHeight = validatedHeight.value;
        if (rawHeight.present && validatedHeight.replaced) {
            logger::warn("[EA] Config: skill_allocation.panel_height must be 0 or an integer from 300 through 720; using default {}.",
                kDefaultMenuPanelHeight);
        }
        menuPanelYOffset = readInteger("panel_y_offset", kDefaultMenuPanelYOffset, -360, 360);
        menuSkillRowGap = readInteger("row_gap", kDefaultMenuSkillRowGap, 24, 72);
        menuSkillColumnGap = readInteger("column_gap", kDefaultMenuSkillColumnGap, 0, 200);
        menuSkillLabelValueGap = readGap("label_value_gap", kDefaultMenuSkillLabelValueGap);
        menuSkillValueArrowGap = readGap("value_arrow_gap", kDefaultMenuSkillValueArrowGap);
        menuSkillButtonTopGap = readGap("button_top_gap", kDefaultMenuSkillButtonTopGap);
        menuSkillButtonRowOffset = readInteger("button_row_offset", kDefaultMenuSkillButtonRowOffset, -72, 120);
        menuSkillButtonGap = readGap("button_gap", kDefaultMenuSkillButtonGap);
        menuFontSize = readInteger("font_size", kDefaultMenuFontSize, 8, 40);
        menuHeaderFontSize = readInteger("header_font_size", kDefaultMenuHeaderFontSize, 10, 48);

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
        loadMsg("quest_college",       "Arcane knowledge advanced");
        loadMsg("quest_thieves",       "A shadowed contract concludes");
        loadMsg("quest_brotherhood",   "Silence is rewarded");
        loadMsg("quest_companions",    "Strength is earned");
        loadMsg("quest_side",          "Another soul aided");
        loadMsg("quest_misc",          "Task complete");
        loadMsg("quest_faction",       "Honour to your faction");
        loadMsg("quest_daedric",       "Daedric favour earned");
        loadMsg("quest_civil_war",     "For Skyrim");
        loadMsg("quest_dawnguard",     "The night shifts");
        loadMsg("quest_dragonborn",    "A new chapter");
        loadMsg("quest_objectives",    "Objective complete");
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
        logger::info("[EA] Config: Book XP — new={:.1f}, skill={:.1f}, reading_mult={:.2f}, value_mode={}",
            xpBookNew, xpBookSkill, bookReadingMultiplier, bookUseValueReward);
        logger::info("[EA] Config: Location XP — discovered={:.1f}, cleared={:.1f}", xpLocationDiscovered, xpLocationCleared);
        logger::info("[EA] Config: Lock XP — novice={:.1f}, apprentice={:.1f}, adept={:.1f}, expert={:.1f}, master={:.1f}",
            xpLockNovice, xpLockApprentice, xpLockAdept, xpLockExpert, xpLockMaster);
        logger::info("[EA] Config: Leveling — xp_base={:.1f}, xp_increase={:.1f}, xp_cap={:.1f}",
            xpBase, xpIncrease, xpCap);
        logger::info("[EA] Config: Skill allocation — points_per_level={}, panel={}x{}, y_offset={}, row_gap={}, column_gap={}, label_value_gap={}, value_arrow_gap={}, button_top_gap={}, button_gap={}, font={}/{}",
            skillPointsPerLevel, menuPanelWidth, menuPanelHeight, menuPanelYOffset,
            menuSkillRowGap, menuSkillColumnGap, menuSkillLabelValueGap, menuSkillValueArrowGap,
            menuSkillButtonTopGap, menuSkillButtonGap, menuFontSize, menuHeaderFontSize);
        logger::info("[EA] Config: Skill cap - {:.1f}", skillCap);
        logger::info("[EA] Config: max_log_files={}", maxLogFiles);
        logger::info("[EA] Config: notifications_enabled={}", notificationsEnabled);
        logger::info("[EA] Config: reset_skills_on_new_game={}", resetSkillsOnNewGame);

        // Dump the raw JSON only for explicitly verbose diagnostic sessions.
        if (verbose) {
            try {
                std::ifstream dumpFile(configPath);
                if (dumpFile.is_open()) {
                    logger::info("[EA] Config: --- BEGIN SimpleAlternateLevelling.json ---");
                    std::string line;
                    while (std::getline(dumpFile, line)) {
                        logger::info("[EA] Config: {}", line);
                    }
                    logger::info("[EA] Config: --- END SimpleAlternateLevelling.json ---");
                }
            } catch (...) {
                logger::warn("[EA] Config: Could not dump JSON contents to log.");
            }
        }
    }
}
