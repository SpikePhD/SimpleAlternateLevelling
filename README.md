# Simple Alternate Levelling

Simple Alternate Levelling is an SKSE plugin for Skyrim Special Edition and
Anniversary Edition. It replaces skill-use leveling with XP awarded for world
actions such as combat, quests, exploration, books, locks, and pickpocketing.

The current release targets Skyrim runtime **1.7.104.0**. It is built for the
post-1.6.629 structure layout and supports non-VR SE/AE runtimes in that range
when matching SKSE and Address Library data are available. Runtimes earlier
than 1.6.629, Skyrim VR, Epic Store, Microsoft Store, and console builds are not
supported. Install the SKSE and Address Library versions matching your game
runtime before installing this mod.

## Save compatibility

The plugin uses cosave format v6 for mod-owned pending skill points and skill
normalization state. Skyrim's native player XP remains in the main save.

Back up saves before upgrading from cosave v5. After creating a v6 cosave,
downgrading to the old v5 DLL is unsupported.

## Prerequisites

- Windows and Visual Studio 2022 with C++ desktop tools
- CMake 3.21 or newer
- vcpkg, with `VCPKG_ROOT` set to its root directory
- Python 3 for deterministic packaging
- Java 17 and FFDec 25.1.3 only when rebuilding the Scaleform menu
- Git with submodule support

Clone the repository and its pinned CommonLib dependency:

```powershell
git clone --recurse-submodules <repository-url>
cd SimpleAlternateLevelling
```

For an existing clone:

```powershell
git submodule update --init --recursive
```

## Build and test

Build the Release plugin and dependency-free tests using the required
`x64-windows-static-md` triplet:

```powershell
cmake --preset windows-release-tests
cmake --build --preset windows-release-tests
ctest --preset windows-release-tests
```

The plugin is written to
`out/build/windows-release-tests/Release/SimpleAlternateLevelling.dll`.

The pure progression, reward, and logging-policy tests do not require vcpkg,
CommonLib, Skyrim, or Windows:

```sh
cmake --preset portable-tests
cmake --build --preset portable-tests
ctest --preset portable-tests
```

## Local deployment

Keep machine-specific paths in an ignored `CMakeUserPresets.json`. Inherit from
`windows-release-tests` and set `SAL_DEPLOY_DIR` to the root of an MO2 mod. When
that value is present, successful plugin builds copy the DLL, JSON, both SWFs, and
translation file into the correct mod directory structure.

`SKYRIM_PATH` remains a deprecated compatibility alias for `SAL_DEPLOY_DIR`.

## Package

```powershell
cmake --build --preset windows-package
```

This creates a deterministic archive and checksum under the repository's ignored
`Deployed/` folder. Install the ZIP directly through Mod Organizer 2 or another
mod manager. The archive contains exactly these Data-relative paths:

```text
Interface/EA_SkillMenu.swf
Interface/Translations/SimpleAlternateLevelling_ENGLISH.txt
SKSE/Plugins/SimpleAlternateLevelling.dll
SKSE/Plugins/SimpleAlternateLevelling.json
```

## Skill allocation UI

Allocations are previews until Confirm is activated. Reset discards the preview,
while Confirm, `C`, or Escape atomically applies it and continues to Skyrim's
vanilla attribute-selection menu. The final point does not auto-confirm.

Each skill has - and + buttons; points added in this session show as a gold
"+N", and capped skills show "Max". Use the mouse or keyboard: arrow keys move
through the Combat, Magic, and Stealth columns and the Reset/Confirm row, Enter
or `+` adds a point, Backspace or `-` removes one, Tab and Shift+Tab cycle
controls, `R` resets, and `C` or Escape confirms. Controller navigation is not
supported.

UI strings use Skyrim translation files. Additional languages can provide
`Interface/Translations/SimpleAlternateLevelling_<LANGUAGE>.txt` with the same
`$SAL_*` keys as the English file.

## How XP is calculated

Skyrim's own skill XP is switched off: skills rise only through the level-up
allocation screen, and character XP comes only from the actions below. Every
value in this section is a default and can be changed on the settings page
(SKSE Menu Framework) or in `SimpleAlternateLevelling.user.json`.

### XP needed per level

```text
XP needed(level) = min(cap, base + level x increase)
```

Defaults match vanilla: base 75, increase 25, no cap. Level 1 needs 100 XP,
level 10 needs 325, level 30 needs 825, and level 50 needs 1,325. Overflow
carries into the next level.

A companion plugin using the [Integration API](#integration-api) may shorten
levels with a multiplier, clamped to `integration.threshold_multiplier_floor`
(default 0.5) through 1. The multiplier changes only the XP needed; reward
scaling below always uses the unmodified curve. The Stats page shows the
effective XP needed and notes when a modifier is active.

### Every reward: base value x level scaling

Each action has a **base value** (below). When it is awarded, the base value is
multiplied by a **level scaling factor**, so rewards grow as levels get longer:

```text
reward  = base value x scaling
scaling = (XP needed at your level / XP needed at level 1) ^ exponent
exponent = min(1, reward growth x source weight)
```

- **Reward growth** (default 0.5) sets how much rewards keep up with the curve:
  0 = rewards never grow (effort per level rises with the curve), 1 = every
  level takes the same effort.
- **Source weights** tilt the growth per source. Defaults: quests 0.6,
  kills 1.5, exploration, locks, books, and pickpocketing 1.0. Quests carry the
  early levels; fighting becomes the main source of XP later.
- The exponent never exceeds 1, and the factor stops growing once the XP curve
  reaches its cap.

With the defaults the scaling factors are:

| Your level | XP needed | Quests (x^0.30) | Kills (x^0.75) | Other sources (x^0.50) |
|---|---|---|---|---|
| 1 | 100 | x1.00 | x1.00 | x1.00 |
| 10 | 325 | x1.42 | x2.42 | x1.80 |
| 30 | 825 | x1.88 | x4.87 | x2.87 |
| 50 | 1,325 | x2.17 | x6.94 | x3.64 |

### Base value of each source

**Quests** (weight: quests). Awarded once when a quest completes; repeatable
quests can pay again after they restart. The value depends on the quest type:

| Quest type | Base XP |
|---|---|
| Main questline, Daedric, Civil War, Dragonborn | 75 |
| College, Thieves Guild, Dark Brotherhood, Companions, Side quests, Dawnguard | 50 |
| Miscellaneous, any other type | 25 |

**Miscellaneous objectives** (weight: quests). 10 XP each time an objective of
a miscellaneous quest is completed. When a quest completes several objectives
in the same moment (typically every leftover branch as an errand wraps up),
that batch pays a single objective reward.

**Kills** (weight: kills). Awarded when you kill an enemy, or when an actor
you command does (a summon, thrall, or reanimated corpse). Kills by regular
followers do not count. Killing your own summons, thralls, or reanimated
corpses never pays. Respawned enemies pay again.

```text
kill base = (type value + max(0, enemy level - your level) x level bonus) x kill multiplier
```

| Enemy type (first match) | Type value |
|---|---|
| Dragon | 20 |
| Daedra | 15 |
| Undead | 8 |
| Animal | 3 |
| Creature | 5 |
| Humanoid | 5 |
| Anything else | 5 |

Level bonus defaults to 1 XP per level the enemy is above you; the kill
multiplier defaults to 1.0.

**Exploration** (weight: exploration). Discovering a location pays once, based
on its map-marker type; clearing a location pays once per playthrough, based on
its marker type or location keywords. Examples of defaults:

| Location type | Discover | Clear |
|---|---|---|
| Cave, camp, mine | 10 | 40 (mine 30) |
| Fort | 15 | 60 |
| Nordic ruin, Dwemer ruin, dragon lair | 15-30 | 100 |
| City, town, settlement | 15 | 30 |
| Giant camp | 20 | 60 |
| Anything unlisted ("Other") | 10 | 15 |

All 35 location types have their own discover and clear values on the settings
page.

**Locks** (weight: locks). Each successful lockpick pays by the lock's tier,
read when the lockpicking screen opens: novice 2, apprentice 3, adept 4,
expert 5, master 6.

**Books** (weight: books). The first read of each book pays, whether read from
the world, your inventory, or a container; spell tomes count as books.

```text
book base = skill book           -> skill book value (2)
            other books, value on -> max(1, gold value) x value multiplier (0.25)
            other books, value off-> regular book value (2)
          then x book multiplier (1.0)
```

Value-based XP is off by default.

**Pickpocketing** (weight: pickpocketing). 5 XP per successful pickpocket.

### Worked examples (defaults)

| Action | Level 1 | Level 10 | Level 30 | Level 50 |
|---|---|---|---|---|
| Side quest | 50 | 71 | 94 | 109 |
| Main quest | 75 | 107 | 141 | 163 |
| Misc objective | 10 | 14 | 19 | 22 |
| Same-level bandit | 5 | 12 | 24 | 35 |
| Dragon | 20 | 48 | 97 | 139 |
| Cave cleared | 40 | 72 | 115 | 146 |
| Nordic ruin cleared | 100 | 180 | 287 | 364 |
| Novice lock | 2 | 3.6 | 5.7 | 7.3 |

At level 1 a level takes 20 same-level bandits or 2 side quests; at level 30 it
takes about 34 bandits or 9 side quests, and at level 50 about 38 bandits or 12
side quests. The in-game **Stats** page shows the live multipliers, an estimate
for your next level, and where this session's XP came from.

## In-game settings

In-game settings use [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352),
an optional dependency. Open its Mod Control Panel (F1 by default) and choose
**Simple Alternate Levelling / Settings**. Without the framework, edit
`SimpleAlternateLevelling.user.json` instead; SAL works the same either way.

Like MCM, changes are saved and take effect as soon as you finish editing a
value. Sections cover progression, quest, kill, exploration, lock, book, and
pickpocket XP, starting skills, skill points, notifications, and advanced
settings. Values that differ from the default are highlighted, and hovering a
setting shows what it does and its default. Each section has **Reset section**;
**Reset all to defaults** asks for a second click. Presets include SAL Default,
Faster/Slower Progression, Vanilla Curve, Zero-Skill Start, and Custom Skill Start.

Starting skills can stay vanilla, begin at zero, begin at one chosen value, or
use separate values for all 18 skills. This choice is captured at new-character
creation; changing it does not alter an existing character. Skill points per
level affect future awards only. Changing the XP curve updates the live level
threshold without changing accumulated native XP.

`SimpleAlternateLevelling.json` is the shipped default configuration. Menu
changes are written atomically to `SimpleAlternateLevelling.user.json` beside
it, with only values that differ from the defaults. The effective config is the
defaults plus those overrides. Deleting the user file restores defaults on the
next launch. Legacy `reset_skills_on_new_game=true` migrates to zero-skill
start, while `false` migrates to vanilla. Unknown or invalid user values are
ignored individually; a future schema version is ignored safely. Custom
notification strings may still be edited in either JSON file.

### Rebuild the Scaleform menu

The normal plugin build uses the committed SWF and does not require Java or
FFDec. To rebuild after editing `assets/swf_src/scripts/frame_1/DoAction.as`, set
`SAL_JAVA_EXECUTABLE` to a Java 17 executable and `SAL_FFDEC_JAR` to the pinned
FFDec 25.1.3 jar, then run:

```powershell
cmake --build --preset windows-release-tests --target rebuild_skill_menu
cmake --build --preset windows-release-tests --target verify_skill_menu
```

The official `ffdec_25.1.3.zip` SHA-256 is
`0b39cd56d1365f161059fff2b7055ea90446782fd1c452a2ce62ab22d66a1e4e`.
When adding a setting key, regenerate the English translation file with
`python tools/generate_settings_translation.py`, then review its labels.

## Runtime files

- Configuration: `Data/SKSE/Plugins/SimpleAlternateLevelling.json`
- User overrides: `Data/SKSE/Plugins/SimpleAlternateLevelling.user.json`
- Logs: the standard SKSE log directory as
  `SimpleAlternateLevelling_<timestamp>.log`

`debug.max_log_files` accepts integers from `0` through `1000`. The default is
`10`; `0` keeps all session logs.

## Integration API

SAL exposes a small versioned interface so a separately built companion SKSE
plugin can cooperate with it. Without such a plugin SAL behaves exactly as
before. Copy [`include/SAL_API.h`](include/SAL_API.h) into the companion
project; it depends only on `<cstdint>` and uses plain C types and function
pointers, so it is safe across DLLs built with different toolchains.

### Handshake

1. In `SKSEPlugin_Load`, register a messaging listener for the sender
   `SAL::kSenderName` (`"SimpleAlternateLevelling"`).
2. At `kPostPostLoad`, SAL dispatches a message of type
   `SAL::kMessageInterface` to all listeners. `msg->data` points to a static
   `SAL::SALInterfaceV1` that stays valid for the life of the process.
3. Check `msg->dataLen >= sizeof(SAL::SALInterfaceV1)` and `version >= 1`,
   keep the pointer, and register callbacks.

```cpp
messaging->RegisterListener(SAL::kSenderName, [](SKSE::MessagingInterface::Message* msg) {
    if (!msg || msg->type != SAL::kMessageInterface || msg->dataLen < sizeof(SAL::SALInterfaceV1)) {
        return;
    }
    g_sal = static_cast<const SAL::SALInterfaceV1*>(msg->data);
    g_sal->RegisterThresholdMultiplier(&MyMultiplier);
    g_sal->RegisterLevelUpStep(&MyWantsStep);
    g_sal->RegisterCharacterCreated(&MyOnCharacterCreated);
});
```

If SAL is not installed, no message arrives and the companion should run
without it.

### Rules

- Each slot accepts **one registrant**. The first valid registration wins;
  a second one, or a null callback, returns `false` and is logged.
- SAL invokes every callback **on the main thread**. Callbacks must not throw
  or block. SAL catches exceptions defensively and treats them as "no effect".
- `RequestThresholdRefresh` and `ContinueLevelUp` may be called from **any
  thread**; SAL performs the work on the main thread.
- Nothing is persisted. SAL's cosave format is unchanged, and all pending
  hand-off state is discarded on load, revert, and new game.

### Functions

| Member | Contract |
|---|---|
| `version` | `1` for this layout. Later versions only append members. |
| `RegisterThresholdMultiplier(float (*provider)())` | `provider()` returns a multiplier for the XP needed per level. It is called whenever SAL writes the threshold: data load, game load, new game, settings changes, after each level-up, and on request. Non-finite or `<= 0` values are ignored (treated as 1.0); results are clamped to `[threshold_multiplier_floor, 1.0]`. It never changes reward scaling or the native `fXPLevelUpBase`/`fXPLevelUpMult` settings. |
| `RequestThresholdRefresh()` | Recomputes the threshold after the provider's value changes. Requests made during a level-up (from `LevelIncrease` until the vanilla LevelUp Menu closes, including a level-up step) are folded into the refresh SAL already does when that menu closes, so the engine's XP subtraction is never disturbed. |
| `RegisterLevelUpStep(bool (*wantsStep)(uint32_t level))` | Adds a step between SAL's skill menu and the vanilla LevelUp Menu. When SAL is about to open the vanilla menu (after Confirm, and also when its own menu is skipped because there are no points or it failed to open), it calls `wantsStep(level)` with the player's current level. `false` continues immediately, as if no step were registered. `true` makes SAL wait for `ContinueLevelUp`. Only decide and queue your UI here. |
| `ContinueLevelUp()` | Ends the wait and opens the vanilla LevelUp Menu exactly once. Idempotent; ignored when SAL is not waiting. |
| `RegisterCharacterCreated(void (*callback)())` | Called once per new character, after RaceSex Menu/RaceMenu closes and SAL has applied its starting skills (in Vanilla starting-skills mode, right after the menu closes). Never called for loaded saves or for a mid-game `showracemenu`. |

### Level-up step owners must always continue

A step owner **must call `ContinueLevelUp` on every exit path**: confirm,
cancel, Escape, errors, and menus closed by other mods. Until it does, the
vanilla perk/attribute screen does not open.

As a fail-safe, SAL continues by itself when the game has stayed unpaused for
10 seconds while it is waiting, and logs a warning. Keep a pausing menu open
for the whole step so the fail-safe never interrupts a player who is still
choosing. If something else opens the vanilla LevelUp Menu while SAL is waiting,
SAL treats that as the continuation.
