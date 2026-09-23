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
