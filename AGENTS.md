# Simple Alternate Levelling - AGENTS.md

`AGENTS.md` is the single source of truth for this project. `CLAUDE.md` is intentionally removed to avoid drift.

## What this mod does

Replaces Skyrim AE's vanilla skill-based leveling with an XP-from-actions system.
The player levels up by doing things in the world (killing enemies, completing quests,
discovering locations, reading books, picking locks, pickpocketing) rather than by
grinding individual skills. Skill XP is intercepted and discarded at the engine level.

## Architecture

```text
SKSEPluginLoad()
├── InitializeLog()          - timestamped spdlog in the standard SKSE log directory
├── Config::Load()           - reads SimpleAlternateLevelling.json immediately
├── Serialization callbacks  - cosave v6: persists pendingSkillPoints + skillsNormalized;
│                              native PlayerSkills::xp remains owned by Skyrim
└── MessagingInterface kDataLoaded
    ├── SkillHook::Install()          - trampolines into PlayerCharacter::AddSkillExperience
    │                                   (discards all organic skill XP) and
    │                                   TESObjectBOOK::Activate (triggers the book read-flag check)
    ├── EventSinks::Register()        - BSTEventSink registrations:
    │   ├── TESTrackedStatsEvent      - lock-success counter plus diagnostics only
    │   ├── ActorKill::Event          - player/player-commanded kill XP by type and level delta
    │   ├── QuestStatus::Event        - exact quest completion lifecycle (start/reset rearms)
    │   ├── ObjectiveState::Event     - exact misc-objective completion transitions
    │   ├── ItemsPickpocketed::Event  - one base reward per successful event
    │   ├── MenuOpenCloseEvent        - captures lock target/tier before lockpicking
    │   ├── LevelIncrease::Event      - diagnostic only; never writes the threshold
    │   └── MenuOpenCloseEvent        - LevelUp Menu close writes the capped threshold
    ├── CharCreateWatcher             - MenuOpenCloseEvent sink; fires NormalizeSkills after
    │                                   "RaceSex Menu" or "RaceMenu" closes on a new game
    └── GameSettingCollection         - overrides fXPLevelUpBase + fXPLevelUpMult to match
                                        Config curve; also writes levelThreshold directly
                                        (stored value is baked at char creation, not updated
                                        retroactively by game setting changes)
```

### Key files

| File | Role |
|---|---|
| `src/main.cpp` | Plugin entry, log init, cosave callbacks, kDataLoaded orchestration, CharCreateWatcher |
| `src/Config.cpp` / `include/Config.h` | JSON loader; all XP values as `inline` globals |
| `src/XPManager.cpp` / `include/XPManager.h` | `AwardXP()` (native XP bucket feed, merged HUD notifications), quest/discovery guards, mod-owned pending points |
| `src/Progression.cpp` / `include/Progression.h` | Pure curve validation/threshold calculation and versioned cosave codec |
| `src/RewardRules.cpp` / `include/RewardRules.h` | Dependency-free reward eligibility, lifecycle, arithmetic, and marker/lock mappings |
| `src/Leveling.cpp` / `include/Leveling.h` | Game-setting synchronization and finalized-level threshold refresh |
| `src/UIRules.cpp` / `include/UIRules.h` | Dependency-free UI validation and transactional allocation session rules |
| `src/SkillHook.cpp` / `include/SkillHook.h` | `write_branch<5>` hooks: AddSkillExperience (discard), TESObjectBOOK::Activate (world-read trigger) |
| `src/SkillMenu.cpp` / `include/SkillMenu.h` | Validated Scaleform boundary, menu lifecycle, preview/commit transaction, vanilla continuation |
| `src/SettingsModel.cpp` / `include/SettingsModel.h` | Defaults-driven setting registry, bounds, layering, migration, presets, draft transaction, overrides |
| `src/SettingsPage.cpp` / `include/SettingsPage.h` | Optional settings page in SKSE Menu Framework's Mod Control Panel (ImGui) |
| `src/InfoPages.cpp` / `include/InfoPages.h` | Stats and XP Log pages in SKSE Menu Framework (session-only) |
| `src/XPJournal.cpp` / `include/XPJournal.h` | Mutex-guarded session record of awards and skipped/merged rewards; diagnostics ring buffer |
| `src/UIText.cpp` / `include/UIText.h` | Cached, thread-safe `$SAL_*` translation lookup and `{placeholder}` formatting for pages |
| `extern/SKSEMenuFramework/SKSEMenuFramework.h` | Vendored MIT header from QTR-Modding/SKSE-Menu-Framework-3-Example @ `aa8effa`; runtime-resolved, no link dependency |
| `src/EventSinks.cpp` / `include/EventSinks.h` | All BSTEventSink structs + `Register()` |
| `include/PCH.h` | Precompiled header: RE/Skyrim.h, SKSE, spdlog sinks, std includes |
| `data/SKSE/Plugins/SimpleAlternateLevelling.json` | Runtime config (XP values, leveling curve, debug flags) |
| `data/SKSE/Plugins/SimpleAlternateLevelling.user.json` | Optional menu-written overrides (untracked runtime file) |

### Skill allocation flow

```text
Vanilla LevelUp Menu opens
  -> MenuOpenCloseEvent defers and hides it
  -> SkillMenu snapshots the 18 native skill values
  -> mouse/keyboard allocations update preview deltas only
  -> Reset clears deltas without touching native actor values
  -> Confirm/C/Escape revalidates the snapshot and commits once
  -> unspent points are stored and the vanilla LevelUp Menu opens once
```

The menu state machine is `Idle -> Opening -> Active -> Committing -> Closing`.
Every Scaleform callback must originate from the active movie, have the exact argument
shape, and identify one of the 18 whitelisted skills. Load, revert, and new-game paths
invalidate all deferred menu, normalization, reward, and threshold work.

### Settings page (SKSE Menu Framework)

In-game settings live in SKSE Menu Framework's Mod Control Panel (F1 by default) under
"Simple Alternate Levelling / Settings". The framework is optional: `SettingsPage::Register()`
runs on `kDataLoaded` and does nothing when `SKSEMenuFramework.dll` is absent, in which case
players edit `SimpleAlternateLevelling.user.json`. There is no SAL-specific hotkey or SWF.

The shipped JSON defines the numeric/Boolean registry and defaults;
`SimpleAlternateLevelling.user.json` contains only validated overrides and schema version 2.
Legacy `reset_skills_on_new_game` migrates to `starting_skills.mode` (`zero`/`vanilla`).
Like MCM, each committed edit (Enter, focus loss, +/- click, checkbox, combo, preset, or
section/all reset) is saved atomically and applied immediately; partial typing is never saved.
The page lists settings in an explicit logical order, marks values that differ from the
shipped default, and shows descriptions and defaults in tooltips.

ImGui rendering may run off the main thread. Every settings-model access takes the page's
mutex, and saving/applying (which rebuilds Config globals read by event sinks) is queued to
the main thread with `SKSE::GetTaskInterface()->AddTask`. Keep it that way.

The Stats and XP Log pages read `XPJournal`, which `XPManager::AwardXP` and the event sinks
fill on the main thread (awards, plus notes for own-minion kills, merged objective batches,
zero rewards, and empty location clears). The journal and session stats reset with the other
transient reward state on load, revert, and new game; nothing is saved. Diagnostics come from
an in-memory spdlog ring buffer (last 300 lines, info and above, never trace).

Custom notification text stays JSON-only. Starting-skill mode and values are snapshotted on
`kNewGame`, so later changes cannot alter existing characters. Skill points per level apply
only to future level-ups. Keep all visible page text in the translation file; labels and
descriptions come from `tools/generate_settings_translation.py`.

### XP flow

```text
Action in game
  -> Hook / Event sink fires on main thread
  -> XPManager::AwardXP(amount, source)
      -> skills->data->xp += amount          (native engine XP bucket)
      -> engine checks xp >= levelThreshold  (every tick, natively)
      -> AdvanceLevel() fires natively       (attribute screen, perk point, overflow carry)
  -> LevelIncrease::Event fires             (log only: XP has NOT been deducted yet)
  -> LevelUp Menu completes: engine does xp -= levelThreshold, recalculates threshold
  -> LevelUp Menu close (not the interim close of an intercepted menu)
      -> deferred task writes the centrally calculated, capped levelThreshold
```

Never write `levelThreshold` between `LevelIncrease::Event` and the LevelUp Menu closing.
The engine subtracts whatever threshold is current at completion; writing the next
level's value early turned 120 XP at threshold 100 into -5 instead of 20.

### Leveling formula

```text
threshold(level) = min(xpCap, xpBase + max(level, 1) * xpIncrease)
```

`xpBase` -> `fXPLevelUpBase`, `xpIncrease` -> `fXPLevelUpMult`.

README.md "How XP is calculated" is the player-facing reference for every source's base
value, the scaling, and worked examples. Keep it in sync with any reward change.

Every reward is scaled in `XPManager::AwardXP`, the single entry point for all sources:

```text
reward = base * (threshold(level) / threshold(1)) ^ min(1, reward_scaling * weight[source])
```

`Progression::RewardScale` computes the factor from the same capped curve, so it stops
growing at the cap. `RewardRules::ClassifyRewardSource` maps the award's source key to one
of six weights (quest, kill, exploration, lock, book, pickpocket). Shipped defaults:
`reward_scaling=0.5`, quest weight 0.6, kill weight 1.5, others 1.0, so quests dominate
early and fighting becomes the main XP source at high levels. Kill base values are
`(type base + level-difference bonus) * kill multiplier` before this scaling. The Faster/
Slower presets change only the XP curve, never the scaling or weights.
The threshold calculation uses double-precision intermediate arithmetic. The curve is
written to the game settings, and the result is written directly to
`skills->data->levelThreshold` on data load, cosave load, new game, and after each
LevelUp Menu closes. The stored threshold is not retroactively updated by game setting changes.

### Cosave

- Record ID: `EAXP`, version 6
- Payload: exactly five bytes: little-endian `int32 pendingSkillPoints` + `uint8 skillsNormalized`
- Native `PlayerSkills::xp` is never serialized by the plugin and is never assigned during
  cosave load or revert; Skyrim's main save remains the sole XP authority
- v1-v5 records are accepted at their exact historical lengths; legacy XP is ignored while
  pending points and normalization are migrated where those versions contain them
- The first valid `EAXP` record wins; missing, corrupt, duplicate, or unknown records cannot
  overwrite native XP and fall back to safe plugin-owned defaults
- v6 is forward-only: back up saves before upgrading; loading a v6 cosave with the old v5 DLL
  is unsupported

### Hook addresses (SE/AE)

| Function | RELOCATION_ID / VTABLE | AE ID | Hook type |
|---|---|---|---|
| `PlayerCharacter::AddSkillExperience` | `RELOCATION_ID(39413, 40488)` | 39413 / 40488 | `write_branch<5>` |
| `TESObjectBOOK::Activate` | `VTABLE_TESObjectBOOK[0]` slot `0x37` | runtime Address Library | `write_vfunc` |

`AddSkillExperienceHook` uses the default trampoline (64 bytes, about 14 bytes used).
`BookActivateHook` patches the vtable directly, so it does not consume trampoline bytes.

Why vtable hook for books: `po3_PapyrusExtender` uses `write_branch<5>` on
`TESObjectBOOK::Read` (ID 17842). Two `write_branch<5>` hooks on the same address corrupt
each other's trampolines and cause an access violation on book activation. Hooking `Activate`
is collision-free. `IsRead()` is still false inside `Activate` before the original is called.

### Important gotchas

- `PlayerCharacter::skills` is not a direct member. Use `player->GetInfoRuntimeData().skills`.
- `skills->data->levelThreshold` is baked at character creation; setting game settings does
  not retroactively update it. Write directly in `OnDataLoaded` and `OnGameLoad`.
- `RE::DebugNotification` must not be called from inside `TESObjectBOOK::Activate`'s call stack;
  defer via `SKSE::GetTaskInterface()->AddTask()`.
- `"Books Read"` TrackedStat is unreliable in AE. Book XP comes from diffing
  `TESObjectBOOK::IsRead()` against a snapshot taken on `kPostLoadGame`/`kNewGame`. The check
  runs a frame after Book/Inventory/Container Menu closes and after the `Activate` hook (world
  spell tomes open no menu). Reading from the inventory or a container never calls `Activate`.
  Spell tomes award book XP like other books.
- XP notifications pass `cancelIfAlreadyQueued=false`; with `true` the HUD drops a message whose
  text is already queued. Awards with the same notification key merge for 1 second, messages
  are spaced 1 second apart, and a message due while a pausing menu is open (for example looting
  a body right after the kill) waits until the game resumes; otherwise the HUD loses it.
- `"Skill Books Read"` TrackedStat fires for skill books in AE; `"Books Read"` does not.
- Misc quests never set `IsCompleted()`. Award objective XP from exact
  `ObjectiveState::Event` transitions; the tracked stat is diagnostic only. Misc-quest scripts
  often complete every remaining objective (untaken branches, optional steps) in one frame
  when the errand ends, so completions are batched per quest for one frame and each batch
  pays a single objective reward (`RewardRules::ObjectiveBatcher`).
- Capture lock difficulty when `Lockpicking Menu` opens. Once the reference unlocks, its
  tier is no longer a reliable source for the `"Locks Picked"` success event.
- `QuestStatus::Event` is the quest reward authority. Completion awards once, while started
  and reset signals rearm repeatable quests.
- `ActorKill::Event` fires once per death. Do not add a per-FormID kill guard: placed
  references keep their FormID across cell respawns and `FF` IDs are recycled, so a
  session-long guard silently drops XP. Kills of the player's own commanded actors
  (summons, thralls, reanimated corpses) never award XP.
- `LocationCleared::Event` is empty and does not identify the location. Rewards come from
  diffing `BGSLocation::everCleared` against a snapshot taken on `kPostLoadGame` and
  `kNewGame`, so each location awards once per playthrough, wherever the player is.
- Call `SKSE::Init(a_skse, { .log = false })`. The default `InitInfo` creates CommonLib's
  own logger and replaces the timestamped session logger from `InitializeLog()`.
- `QUEST_DATA::Type::kCompanions` does not exist. Use `kCompanionsQuest`.
- `TESActorValueChangeEvent` and `TESPerkEntryRunEvent` have no struct definitions in this
  CommonLibSSE-NG build; those sinks are commented out.

## Build

```powershell
cmake --preset windows-release-tests
cmake --build --preset windows-release-tests
ctest --preset windows-release-tests
```

Set `VCPKG_ROOT` before configuring. Clone with `--recurse-submodules` or run
`git submodule update --init --recursive` before the first plugin build.

Critical build/runtime note:

- Use `x64-windows-static-md`. This builds `spdlog` and `fmt` as static libraries so the
  plugin does not import `spdlog.dll` or `fmt.dll` at load time.
- Keep `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"` set before
  `project()` so the plugin matches the triplet's `/MD` runtime.
- Do not use `x64-windows` for this project.
- The plugin targets AE 1.7.104 and uses the post-1.6.629 structure layout. It supports
  non-VR SE/AE runtimes at 1.6.629 or later when matching SKSE and Address Library data
  are available. Earlier runtimes and VR are unsupported; unsupported storefronts are rejected.

For dependency-free tests on any supported development OS:

```sh
cmake --preset portable-tests
cmake --build --preset portable-tests
ctest --preset portable-tests
```

`cmake --build --preset windows-package` writes a deterministic, mod-manager-ready ZIP
and SHA-256 file into the ignored repository `Deployed/` folder. For optional local MO2
deployment during plugin builds, use an ignored `CMakeUserPresets.json` to set
`SAL_DEPLOY_DIR`; never add local absolute paths to tracked CMake files or documentation.
The deprecated `SKYRIM_PATH` cache variable is accepted only as a compatibility alias.

The committed `data/Interface/EA_SkillMenu.swf` is rebuilt explicitly from
`assets/swf_src/scripts/frame_1/DoAction.as` with Java 17 and FFDec 25.1.3. Set
`SAL_JAVA_EXECUTABLE` and `SAL_FFDEC_JAR`, then use the `rebuild_skill_menu` and
`verify_skill_menu` targets. Normal plugin builds do not require FFDec.

Build artifacts are under `out/build/<preset>/`. Runtime logs are written directly to the
standard SKSE log directory. `debug.max_log_files=0` disables deletion; valid limits are
integers through 1000.

## Json for testing

The shipped `data/SKSE/Plugins/SimpleAlternateLevelling.json` must keep release
defaults (`verbose=false`, vanilla-like curve `xp_base=75`, `xp_increase=25`, uncapped).
Never commit test values there; it is packaged verbatim.

For local testing, put the fast-test values in
`SKSE/Plugins/SimpleAlternateLevelling.user.json` inside `SAL_DEPLOY_DIR`. The post-build
deploy step overwrites the shipped JSON but never touches the user file:

```json
{
  "config_version": 2,
  "debug": {
    "verbose": true
  },
  "leveling": {
    "xp_base": 5.0,
    "xp_increase": 1.0
  }
}
```
