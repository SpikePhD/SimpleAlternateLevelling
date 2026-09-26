#!/usr/bin/env python3
"""Generate the English SAL translation file from shipped setting keys."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = ROOT / "data/SKSE/Plugins/SimpleAlternateLevelling.json"
OUTPUT = ROOT / "data/Interface/Translations/SimpleAlternateLevelling_ENGLISH.txt"


FIXED = {
    # Level-up skill allocation menu (EA_SkillMenu.swf).
    "$SAL_SKILL_POINTS_LABEL": "Distribute Skill Points",
    "$SAL_CONFIRM": "Confirm",
    "$SAL_RESET": "Reset",
    "$SAL_ALLOC_LEVEL": "Level",
    "$SAL_ALLOC_REMAINING": "points remaining",
    "$SAL_ALLOC_CARRIED": "carried over from earlier levels",
    "$SAL_ALLOC_BONUS": "bonus from other mods",
    "$SAL_ALLOC_MAX": "Max",
    "$SAL_GROUP_COMBAT": "Combat",
    "$SAL_GROUP_MAGIC": "Magic",
    "$SAL_GROUP_STEALTH": "Stealth",
    "$SAL_ALLOC_HINT": "Arrows: select    Enter or +: add    Backspace or -: remove    R: reset    C or Esc: confirm",
    # SKSE Menu Framework settings page.
    "$SAL_PAGE_SETTINGS": "Settings",
    "$SAL_PAGE_INTRO": "Changes are saved and take effect immediately.",
    "$SAL_PAGE_PRESET": "Preset",
    "$SAL_PAGE_APPLY_PRESET": "Apply preset",
    "$SAL_PAGE_CONFIRM": "Click again to confirm",
    "$SAL_PAGE_DEFAULT": "Default",
    "$SAL_PAGE_NO_CAP": "No cap",
    "$SAL_PAGE_WEIGHTS": "Growth weight by source",
    "$SAL_PAGE_ON": "On",
    "$SAL_PAGE_OFF": "Off",
    "$SAL_PAGE_NEW_CHARACTERS": "Applies when you start a new character; existing characters are not changed.",
    "$SAL_PAGE_COL_LOCATION": "Location type",
    "$SAL_PAGE_COL_DISCOVER": "Discover",
    "$SAL_PAGE_COL_CLEAR": "Clear",
    "$SAL_PAGE_INVALID": "Out of range; the previous value was kept.",
    # Stats and XP Log pages.
    "$SAL_PAGE_STATS": "Stats",
    "$SAL_PAGE_LOG": "XP Log",
    "$SAL_STATS_THIS_LEVEL": "This level",
    "$SAL_STATS_REMAINING": "XP to next level",
    "$SAL_STATS_MODIFIER": "A companion mod changes XP needed per level: x{multiplier} ({configured} XP without it).",
    "$SAL_STATS_ESTIMATE": "About {kills} same-level humanoid kills or {quests} side quests to the next level.",
    "$SAL_STATS_MULTIPLIERS": "Reward multipliers at your level",
    "$SAL_STATS_SESSION": "This session",
    "$SAL_STATS_TOTAL": "XP gained",
    "$SAL_STATS_AWARDS": "Rewards",
    "$SAL_STATS_DURATION": "Session length (h:mm)",
    "$SAL_STATS_RATE": "XP per hour",
    "$SAL_STATS_COL_SOURCE": "Source",
    "$SAL_STATS_COL_XP": "XP",
    "$SAL_STATS_COL_COUNT": "Count",
    "$SAL_STATS_COL_SHARE": "Share",
    "$SAL_STATS_NO_PLAYER": "Load a game to see stats.",
    "$SAL_STATS_EMPTY": "No XP gained yet this session. A session starts when you load a game.",
    "$SAL_LOG_ALL": "All sources",
    "$SAL_LOG_SEARCH": "Search",
    "$SAL_LOG_SHOW_NOTES": "Show skipped and merged rewards",
    "$SAL_LOG_COL_TIME": "Time",
    "$SAL_LOG_COL_WHAT": "What",
    "$SAL_LOG_COL_DETAIL": "Details",
    "$SAL_LOG_DETAIL": "base {base} x{scale}",
    "$SAL_LOG_ENEMY_LEVEL": "enemy level {level}",
    "$SAL_LOG_BONUS": "x{multiplier} from other mods",
    "$SAL_LOG_DIAGNOSTICS": "Diagnostics (recent plugin log)",
    "$SAL_LOG_EMPTY": "No entries yet. The log starts when you load a game.",
    "$SAL_NOTE_MINION": "Your own minion: no XP",
    "$SAL_NOTE_BATCH": "{count} objectives completed together: rewarded once",
    "$SAL_NOTE_ZERO": "Reward is set to 0: no XP",
    "$SAL_NOTE_NO_LOCATION": "Location clear with no newly cleared location: no XP",
    "$SAL_RESET_SECTION": "Reset section",
    "$SAL_RESET_ALL": "Reset all to defaults",
    "$SAL_SAVE_FAILED": "Could not save settings. Check the SKSE log.",
    "$SAL_SECTION_PROGRESSION": "Progression",
    "$SAL_SECTION_QUESTS": "Quest XP",
    "$SAL_SECTION_KILLS": "Kill XP",
    "$SAL_SECTION_EXPLORATION": "Exploration XP",
    "$SAL_SECTION_LOCKS": "Lock XP",
    "$SAL_SECTION_BOOKS": "Book XP",
    "$SAL_SECTION_PICKPOCKET": "Pickpocket XP",
    "$SAL_SECTION_STARTING": "Starting Skills",
    "$SAL_SECTION_ALLOCATION": "Skill Points",
    "$SAL_SECTION_NOTIFICATIONS": "Notifications",
    "$SAL_SECTION_ADVANCED": "Advanced",
    "$SAL_PRESET_DEFAULT": "SAL Default",
    "$SAL_PRESET_FASTER": "Faster Progression",
    "$SAL_PRESET_SLOWER": "Slower Progression",
    "$SAL_PRESET_VANILLA_CURVE": "Vanilla Curve",
    "$SAL_PRESET_ZERO": "Zero-Skill Start",
    "$SAL_PRESET_CUSTOM": "Custom Skill Start",
    "$SAL_MODE_VANILLA": "Vanilla",
    "$SAL_MODE_ZERO": "All skills at 0",
    "$SAL_MODE_UNIFORM": "One value for all skills",
    "$SAL_MODE_CUSTOM": "Custom value per skill",
}

LOCATION_NAMES = {
    "default": "Other", "city": "City", "town": "Town", "settlement": "Settlement", "cave": "Cave",
    "camp": "Camp", "fort": "Fort", "nordic_ruin": "Nordic ruin", "dwemer_ruin": "Dwemer ruin",
    "shipwreck": "Shipwreck", "grove": "Grove", "landmark": "Landmark", "dragon_lair": "Dragon lair",
    "farm": "Farm", "wood_mill": "Wood mill", "mine": "Mine", "military_camp": "Military camp",
    "doomstone": "Standing stone", "wheat_mill": "Wheat mill", "smelter": "Smelter", "stable": "Stable",
    "imperial_tower": "Imperial tower", "clearing": "Clearing", "pass": "Pass", "altar": "Altar",
    "rock": "Rock", "lighthouse": "Lighthouse", "orc_stronghold": "Orc stronghold",
    "giant_camp": "Giant camp", "shack": "Shack", "nordic_tower": "Nordic tower",
    "nordic_dwelling": "Nordic dwelling", "docks": "Docks", "daedric_shrine": "Daedric shrine",
    "castle": "Castle",
}

SKILL_NAMES = {
    "one_handed": "One-handed", "two_handed": "Two-handed", "block": "Block", "heavy_armor": "Heavy Armor",
    "light_armor": "Light Armor", "archery": "Archery", "alteration": "Alteration",
    "conjuration": "Conjuration", "destruction": "Destruction", "illusion": "Illusion",
    "restoration": "Restoration", "sneak": "Sneak", "smithing": "Smithing", "alchemy": "Alchemy",
    "enchanting": "Enchanting", "pickpocket": "Pickpocket", "lockpicking": "Lockpicking", "speech": "Speech",
}

LABELS = {
    "leveling.xp_base": "Base XP per level",
    "leveling.xp_increase": "Extra XP per level",
    "leveling.xp_cap": "Maximum XP per level",
    "leveling.reward_scaling": "Reward growth with level",
    "leveling.reward_weights.quest": "Quests",
    "leveling.reward_weights.kill": "Kills",
    "leveling.reward_weights.exploration": "Exploration",
    "leveling.reward_weights.lock": "Locks",
    "leveling.reward_weights.book": "Books",
    "leveling.reward_weights.pickpocket": "Pickpocketing",
    "xp_sources.quest.main": "Main questline",
    "xp_sources.quest.college": "College of Winterhold",
    "xp_sources.quest.thieves": "Thieves Guild",
    "xp_sources.quest.brotherhood": "Dark Brotherhood",
    "xp_sources.quest.companions": "Companions",
    "xp_sources.quest.civil_war": "Civil War",
    "xp_sources.quest.daedric": "Daedric quests",
    "xp_sources.quest.dawnguard": "Dawnguard",
    "xp_sources.quest.dragonborn": "Dragonborn",
    "xp_sources.quest.side": "Side quests",
    "xp_sources.quest.misc": "Miscellaneous quests",
    "xp_sources.quest.objectives": "Each miscellaneous objective",
    "xp_sources.quest.other": "Other quests",
    "xp_sources.kill.base_dragon": "Dragon",
    "xp_sources.kill.base_daedra": "Daedra",
    "xp_sources.kill.base_undead": "Undead",
    "xp_sources.kill.base_humanoid": "Humanoid",
    "xp_sources.kill.base_creature": "Creature",
    "xp_sources.kill.base_animal": "Animal",
    "xp_sources.kill.base_default": "Other enemies",
    "xp_sources.kill.level_scale_factor": "Bonus per level above you",
    "xp_sources.kill.global_multiplier": "Kill XP multiplier",
    "xp_sources.lockpick.novice": "Novice lock",
    "xp_sources.lockpick.apprentice": "Apprentice lock",
    "xp_sources.lockpick.adept": "Adept lock",
    "xp_sources.lockpick.expert": "Expert lock",
    "xp_sources.lockpick.master": "Master lock",
    "xp_sources.book.new_book": "Regular book",
    "xp_sources.book.skill_book": "Skill book",
    "xp_sources.book.use_value_reward": "Base book XP on gold value",
    "xp_sources.book.value_multiplier": "XP per gold of value",
    "xp_sources.book.reading_multiplier": "Book XP multiplier",
    "xp_sources.pickpocket.base": "Successful pickpocket",
    "starting_skills.mode": "Starting skills",
    "starting_skills.all_value": "Value for all skills",
    "skill_allocation.points_per_level": "Skill points per level",
    "skill_allocation.skill_cap": "Skill level cap",
    "notifications.enabled": "Show XP notifications",
    "integration.threshold_multiplier_floor": "Lowest XP-per-level multiplier from companion mods",
    "debug.verbose": "Verbose logging",
    "debug.max_log_files": "Log files to keep (0 keeps all)",
}

DESCRIPTIONS = {
    "leveling.xp_base": "XP needed for a level, before the per-level increase. Vanilla uses 75.",
    "leveling.xp_increase": "Added to the XP needed for each level you gain. Vanilla uses 25.",
    "leveling.xp_cap": "The XP needed per level never exceeds this value.",
    "leveling.reward_scaling": "How much XP rewards grow as the XP needed per level grows. 0: rewards never grow, so each level takes more work in line with the curve. 1: every level takes the same work. 0.5: levels still get harder, but gently. Each source's weight below multiplies this.",
    "leveling.reward_weights.quest": "Growth weight for quest and objective rewards. 1 grows at the main rate; lower keeps quests closer to their base value at high levels.",
    "leveling.reward_weights.kill": "Growth weight for kill rewards. Above 1 makes fighting a bigger share of XP at high levels.",
    "leveling.reward_weights.exploration": "Growth weight for discovering and clearing locations.",
    "leveling.reward_weights.lock": "Growth weight for picking locks.",
    "leveling.reward_weights.book": "Growth weight for reading books.",
    "leveling.reward_weights.pickpocket": "Growth weight for pickpocketing.",
    "xp_sources.quest.objectives": "Awarded when an objective of a miscellaneous quest is completed.",
    "xp_sources.kill.level_scale_factor": "Extra XP for each level the enemy is above you. Enemies at or below your level give the base value.",
    "xp_sources.kill.global_multiplier": "Multiplies every kill reward, including the level bonus.",
    "xp_sources.book.use_value_reward": "When on, regular books and spell tomes give XP based on their gold value instead of the fixed amount.",
    "xp_sources.book.value_multiplier": "XP per gold of the book's value when value-based XP is on.",
    "xp_sources.book.reading_multiplier": "Multiplies every book reward.",
    "starting_skills.mode": "How skills are set when a new character leaves character creation.",
    "skill_allocation.points_per_level": "Points to distribute at each level-up. Unspent points carry over.",
    "skill_allocation.skill_cap": "Highest level a skill can reach through allocation.",
    "integration.threshold_multiplier_floor": "Companion mods that use SAL's integration API may lower the XP needed per level. They can never go below this fraction of the normal value. 1 turns reductions off. Has no effect without such a mod.",
    "debug.verbose": "Writes detailed diagnostics to the SKSE log. Leave off for normal play.",
    "debug.max_log_files": "Takes effect the next time the game starts.",
}


def label(path: str) -> str:
    if path in LABELS:
        return LABELS[path]
    parts = path.split(".")
    if parts[:2] == ["xp_sources", "location"]:
        return ("Discover " if parts[2] == "discovery" else "Clear ") + LOCATION_NAMES[parts[-1]].lower()
    if parts[:2] == ["starting_skills", "custom"]:
        return SKILL_NAMES[parts[-1]]
    raise KeyError(f"No English label for setting '{path}'; add it to LABELS.")


def walk(node: dict, prefix: str = ""):
    for key, value in node.items():
        if key.startswith("_") or key == "config_version":
            continue
        path = f"{prefix}.{key}" if prefix else key
        if path == "notifications.messages":
            continue
        if isinstance(value, dict):
            yield from walk(value, path)
        elif isinstance(value, (int, float, bool)) or path == "starting_skills.mode":
            yield path


def setting_key(path: str) -> str:
    return path.replace(".", "_").upper()


def main() -> None:
    defaults = json.loads(DEFAULTS.read_text(encoding="utf-8"))
    lines = [f"{key}\t{value}" for key, value in FIXED.items()]
    lines += [f"$SAL_LOCTYPE_{key.upper()}\t{name}" for key, name in LOCATION_NAMES.items()]
    for path in walk(defaults):
        lines.append(f"$SAL_SETTING_{setting_key(path)}\t{label(path)}")
    for path, text in DESCRIPTIONS.items():
        lines.append(f"$SAL_DESC_{setting_key(path)}\t{text}")
    OUTPUT.write_bytes(b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"))


if __name__ == "__main__":
    main()
