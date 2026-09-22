# Simple Alternate Levelling

Simple Alternate Levelling is an SKSE plugin for Skyrim Special Edition and
Anniversary Edition. It replaces skill-use leveling with XP awarded for world
actions such as combat, quests, exploration, books, locks, and pickpocketing.

The current release targets Skyrim AE runtime **1.7.104.0** and supports Skyrim
SE **1.5.97** and AE **1.6.629+** where SKSE and the matching Address Library are
available. Skyrim VR, Epic Store, Microsoft Store, and console builds are not
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
that value is present, successful plugin builds copy the DLL, JSON, SWF, and
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

Use the mouse or keyboard. Arrow keys navigate the three-column skill grid and
Reset/Confirm row, Tab and Shift+Tab cycle controls, Enter or Space activates the
selection, `R` resets, and `C` confirms. Controller navigation is not supported.

UI strings use Skyrim translation files. Additional languages can provide
`Interface/Translations/SimpleAlternateLevelling_<LANGUAGE>.txt` with the same
three `$SAL_*` keys as the English file.

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

## Runtime files

- Configuration: `Data/SKSE/Plugins/SimpleAlternateLevelling.json`
- Logs: the standard SKSE log directory as
  `SimpleAlternateLevelling_<timestamp>.log`

`debug.max_log_files` accepts integers from `0` through `1000`. The default is
`10`; `0` keeps all session logs.
