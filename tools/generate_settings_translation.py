#!/usr/bin/env python3
"""Generate the English SAL translation file from shipped setting keys."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = ROOT / "data/SKSE/Plugins/SimpleAlternateLevelling.json"
OUTPUT = ROOT / "data/Interface/Translations/SimpleAlternateLevelling_ENGLISH.txt"


FIXED = {
    "$SAL_SKILL_POINTS_LABEL": "Skill points to distribute:",
    "$SAL_CONFIRM": "Confirm",
    "$SAL_RESET": "Reset",
    "$SAL_SETTINGS_TITLE": "Simple Alternate Levelling - Settings",
    "$SAL_APPLY": "Apply",
    "$SAL_CANCEL": "Cancel",
    "$SAL_RESET_SECTION": "Reset Section",
    "$SAL_RESET_ALL": "Reset All",
    "$SAL_PREVIOUS": "Previous",
    "$SAL_NEXT": "Next",
    "$SAL_ON": "On",
    "$SAL_OFF": "Off",
    "$SAL_SETTINGS_HINT": "Click a number to type a value. Changes save on Apply.",
    "$SAL_SAVE_FAILED": "Could not save settings. Check the SKSE log.",
    "$SAL_SECTION_PROGRESSION": "Progression",
    "$SAL_SECTION_QUESTS": "Quest XP",
    "$SAL_SECTION_KILLS": "Kill XP",
    "$SAL_SECTION_EXPLORATION": "Exploration XP",
    "$SAL_SECTION_LOCKS": "Lock XP",
    "$SAL_SECTION_BOOKS": "Book XP",
    "$SAL_SECTION_PICKPOCKET": "Pickpocket XP",
    "$SAL_SECTION_STARTING": "Starting Character",
    "$SAL_SECTION_ALLOCATION": "Skill Allocation",
    "$SAL_SECTION_NOTIFICATIONS": "Notifications",
    "$SAL_SECTION_INTERFACE": "Interface",
    "$SAL_SECTION_ADVANCED": "Advanced / Debug",
    "$SAL_PRESET_DEFAULT": "SAL Default",
    "$SAL_PRESET_FASTER": "Faster Progression",
    "$SAL_PRESET_SLOWER": "Slower Progression",
    "$SAL_PRESET_VANILLA_CURVE": "Vanilla-ish Curve",
    "$SAL_PRESET_ZERO": "Zero-Skill Start",
    "$SAL_PRESET_CUSTOM": "Custom Skill Start",
    "$SAL_MODE_VANILLA": "Vanilla",
    "$SAL_MODE_ZERO": "All Skills = 0",
    "$SAL_MODE_UNIFORM": "All Skills = X",
    "$SAL_MODE_CUSTOM": "Custom Values",
}


def title(value: str) -> str:
    return value.replace("_", " ").title().replace("Xp", "XP")


def label(path: str) -> str:
    parts = path.split(".")
    if parts[0] == "leveling":
        return {"xp_base": "XP threshold base", "xp_increase": "XP threshold increase per level",
                "xp_cap": "XP threshold cap"}[parts[-1]]
    if parts[:2] == ["xp_sources", "quest"]:
        return title(parts[-1]) + " quest XP"
    if parts[:2] == ["xp_sources", "kill"]:
        if parts[-1] == "level_scale_factor": return "Kill level-difference bonus"
        if parts[-1] == "global_multiplier": return "All kill XP multiplier"
        return title(parts[-1].removeprefix("base_")) + " kill XP"
    if parts[:2] == ["xp_sources", "location"]:
        if len(parts) == 3: return title(parts[-1]) + " location fallback XP"
        return ("Discover " if parts[2] == "discovery" else "Clear ") + title(parts[-1]) + " XP"
    if parts[:2] == ["xp_sources", "lockpick"]:
        return title(parts[-1]) + " lock XP"
    if parts[:2] == ["xp_sources", "book"]:
        return {"new_book": "New book XP", "skill_book": "Skill book XP",
                "use_value_reward": "Use book value for XP", "value_multiplier": "Book value multiplier",
                "reading_multiplier": "Book reading multiplier"}[parts[-1]]
    if parts[:2] == ["xp_sources", "pickpocket"]:
        return "Pickpocket XP per success"
    if parts[0] == "starting_skills":
        if parts[-1] == "mode": return "Starting skills mode (new characters only)"
        if parts[-1] == "all_value": return "Starting value for all skills"
        return "Start: " + title(parts[-1])
    if parts[0] == "skill_allocation":
        return {"points_per_level": "Skill points per level", "skill_cap": "Skill level cap",
                "panel_width": "Allocation panel width", "panel_height": "Allocation panel height (0 = auto)",
                "panel_y_offset": "Allocation panel vertical offset", "row_gap": "Skill row spacing",
                "column_gap": "Skill column spacing", "label_value_gap": "Label to value spacing",
                "value_arrow_gap": "Value to arrow spacing", "button_top_gap": "Button top spacing",
                "button_row_offset": "Button row offset", "button_gap": "Button spacing",
                "font_size": "Skill font size", "header_font_size": "Header font size"}[parts[-1]]
    if path == "interface.settings_hotkey": return "Open settings hotkey (scan code; 0 = off)"
    if path == "notifications.enabled": return "Show XP notifications"
    if path == "debug.verbose": return "Verbose logging"
    if path == "debug.max_log_files": return "Maximum retained log files (next launch)"
    return title(parts[-1])


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


def main() -> None:
    defaults = json.loads(DEFAULTS.read_text(encoding="utf-8"))
    lines = [f"{key}\t{value}" for key, value in FIXED.items()]
    for path in walk(defaults):
        lines.append("$SAL_SETTING_" + path.replace(".", "_").upper() + "\t" + label(path))
    OUTPUT.write_bytes(b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"))


if __name__ == "__main__":
    main()
