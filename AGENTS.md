# Experience and Attributes — AGENTS.md

## What this mod does

Replaces Skyrim AE's vanilla skill-based leveling with an **XP-from-actions** system.
The player levels up by doing things in the world (killing enemies, completing quests,
discovering locations, reading books, picking locks, pickpocketing) rather than by
grinding individual skills. Skill XP is intercepted and discarded at the engine level.

## Architecture

```
SKSEPluginLoad()
├── InitializeLog()          — timestamped spdlog, mirrored to Logs/ + SKSE/Spike/
├── Config::Load()           — reads ExperienceAndAttributes.json immediately
├── Serialization callbacks  — cosave v3: persists s_currentXP across saves
└── MessagingInterface kDataLoaded
    ├── SkillHook::Install()          — trampolines into PlayerCharacter::AddSkillExperience
    │                                   (discards all organic skill XP) and
    │                                   TESObjectBOOK::Read (book XP via deferred task)
    ├── EventSinks::Register()        — BSTEventSink registrations:
    │   ├── TESTrackedStatsEvent      — locations, dungeons, locks, skill books,
    │   │                               misc quests, pickpocket, level-up threshold clamp
    │   ├── TESDeathEvent             — kill XP, per-type (keyword-based), level-delta bonus
    │   └── TESQuestStageEvent        — quest completion XP by type (main/side/faction/…)
    └── GameSettingCollection        — overrides fXPLevelUpBase + fXPLevelUpMult to match
                                       Config curve; also writes levelThreshold directly
                                       (stored value is baked at char creation, not updated
                                       retroactively by game setting changes)
```

### Key files

| File | Role |
|---|---|
| `src/main.cpp` | Plugin entry, log init, cosave callbacks, kDataLoaded orchestration |
| `src/Config.cpp` / `include/Config.h` | JSON loader; all XP values as `inline` globals |
| `src/XPManager.cpp` / `include/XPManager.h` | `AwardXP()` (native XP bucket feed), kill/quest dedup guards, cosave accessors |
| `src/SkillHook.cpp` / `include/SkillHook.h` | `write_branch<5>` hooks: AddSkillExperience (discard), TESObjectBOOK::Read (book XP) |
| `src/EventSinks.cpp` / `include/EventSinks.h` | All BSTEventSink structs + `Register()` |
| `include/PCH.h` | Precompiled header — RE/Skyrim.h, SKSE, spdlog sinks, std includes |
| `data/SKSE/Plugins/ExperienceAndAttributes.json` | Runtime config (XP values, leveling curve, debug flags) |

### XP flow

```
Action in game
  → Hook / Event sink fires on main thread
  → XPManager::AwardXP(amount, source)
      → skills->data->xp += amount          (native engine XP bucket)
      → engine checks xp >= levelThreshold  (every tick, natively)
      → AdvanceLevel() fires natively        (attribute screen, perk point, overflow carry)
  → "Level Increases" TrackedStat fires
      → clamp levelThreshold to xpCap
```

### Leveling formula

```
threshold(level) = min(xpCap, xpBase + level * xpIncrease)
```

`xpBase` → `fXPLevelUpBase`, `xpIncrease` → `fXPLevelUpMult`.
Written to both the game setting and `skills->data->levelThreshold` on kDataLoaded
and on each cosave load (the stored threshold is not retroactively updated by game
settings alone — it must be written directly).

### Cosave

- Record ID: `'EAXP'`, version 3
- Payload: one `float` — cumulative XP this playthrough
- On load: restores `skills->data->xp` and recalculates `levelThreshold`
- v1/v2 had additional fields (trackedLevel, pendingLevelUps); intentionally not read

### Hook addresses (AE 1.6.1170)

| Function | RELOCATION_ID / VTABLE | AE ID | Hook type |
|---|---|---|---|
| `PlayerCharacter::AddSkillExperience` | `RELOCATION_ID(39413, 40488)` | 40488 | `write_branch<5>` |
| `TESObjectBOOK::Activate` | `VTABLE_TESObjectBOOK[0]` (AE 189577) slot 37 | — | `write_vfunc` |

`AddSkillExperienceHook` uses the default trampoline (64 bytes, ~14 bytes used).
`BookActivateHook` patches the vtable directly — no trampoline bytes consumed.

**Why vtable hook for books**: `po3_PapyrusExtender` (present in NGVO) uses `write_branch<5>` on
`TESObjectBOOK::Read` (ID 17842). Two `write_branch<5>` hooks on the same address corrupt each
other's trampolines and cause an access violation on book activation. Hooking `Activate` (virtual,
different address) is collision-free. `IsRead()` is still false inside `Activate` before the
original is called — `Read()` is called internally by `Activate`.

### Important gotchas

- `PlayerCharacter::skills` is not a direct member — use `player->GetInfoRuntimeData().skills`
- `skills->data->levelThreshold` is baked at character creation; setting game settings does
  NOT retroactively update it — write directly in `OnDataLoaded` and `OnGameLoad`
- `RE::DebugNotification` must not be called from inside `TESObjectBOOK::Activate`'s call stack;
  defer via `SKSE::GetTaskInterface()->AddTask()`
- `"Books Read"` TrackedStat is dead in AE 1.6.1170 — use `TESObjectBOOK::Activate` vtable hook
- `"Skill Books Read"` TrackedStat fires for skill books in AE; `"Books Read"` does NOT
- Misc quests never set `IsCompleted()` — award their XP from `"Misc Objectives Completed"` stat only
- `QUEST_DATA::Type::kCompanions` does not exist — use `kCompanionsQuest`
- `TESActorValueChangeEvent` and `TESPerkEntryRunEvent` have no struct definitions in this
  CommonLibSSE-NG build; those sinks are commented out

## Build

```bash
cmake -B build -S . \
  "-DCMAKE_TOOLCHAIN_FILE=C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-windows \
  -DCMAKE_BUILD_TYPE=Release \
  "-DSKYRIM_PATH=C:/Modlist/NGVO/mods/Experience and Attributes" \
  -DBUILD_TESTS=OFF

cmake --build build --config Release
```

DLL is auto-copied to `C:\Modlist\NGVO\mods\Experience and Attributes\SKSE\Plugins\`
on a successful build (CMake post-build step).

**CRITICAL**: `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"` must be
set before `project()` in CMakeLists.txt to force `/MT`. Without it the DLL uses `/MD`
and fails with error 0x7E at game launch (spdlog.dll / fmt.dll missing).

Use triplet `x64-windows` (not `x64-windows-static` — causes LNK2038 mismatch for DLLs).

## Paths

| Thing | Path |
|---|---|
| Source | `C:\Users\lucac\Documents\MyProjects\Experience and Attributes\` |
| Build output | `build\Release\ExperienceAndAttributes.dll` |
| Deploy target | `C:\Modlist\NGVO\mods\Experience and Attributes\SKSE\Plugins\` |
| SKSE log (game) | `Documents\My Games\Skyrim Special Edition\SKSE\Spike\ExperienceAndAttributes_<ts>.log` |
| Dev log mirror | `<project root>\Logs\ExperienceAndAttributes_<ts>.log` |
| Config (game) | `<Skyrim>\Data\SKSE\Plugins\ExperienceAndAttributes.json` |

## Environment — tools in PATH

The development machine has the following tools on PATH (visible in system environment variables):

- **CMake** — `C:\Program Files\CMake\bin`
- **Papyrus compiler** — `C:\Modlist\papyrus-compiler` and `C:\Modlist\papyrus-compiler\Original Compiler`
- **FFDec** (JPEXS Free Flash Decompiler) — `C:\Program Files (x86)\FFDec` — for inspecting/editing `.swf` UI files

## NGVO modlist tools (MO2 tools, available via `C:\Modlist\NGVO\tools\`)

These are launched through MO2 so they see the virtual data folder:

| Tool | Path | Use |
|---|---|---|
| **SSEEdit 64** | `tools\SSEEdit\SSEEdit64.exe` | Inspect FormIDs, verify records, build conflict reports, run edit scripts |
| **SSEDump 64** | `tools\SSEEdit\SSEDump64.exe` | CLI record dump (scriptable) |
| **Synthesis** | `tools\Synthesis\Synthesis.exe` | Patcher pipeline (Mutagen-based); run synthesizer patches |
| **Cathedral Assets Optimizer** | `tools\Cathedral Assets Optimizer\` | Texture/mesh optimization, BSA packing |
| **NifSkope** | `tools\Nifskope Dev 8 [Pre-Release]\` | Inspect/edit `.nif` mesh files |
| **NIF Optimizer** | `tools\NIF Optimizer\` | Batch-optimize NIF meshes for SSE |
| **LOOT** | `tools\LOOT\` | Plugin load order sorting |
| **DynDOLOD** | `tools\DynDOLOD\` | Dynamic distant LOD generation |
| **xLODGen** | `tools\xLODGen\` | Terrain/object LOD generation |
| **zEdit** | `tools\zEdit\` | Merge plugins, apply zPatch rules |
| **BethINI** | `tools\BethINI\` | INI editor with SSE presets |
| **EasyNPC** | `tools\EasyNPC\` | NPC appearance conflict resolution |
| **Vram Texture Analyzer** | `tools\Vram Texture Analyzer\` | Texture VRAM usage profiling |
| **PGPatcher / PCA** | `tools\PGPatcher\`, `tools\PCA\` | Particle/physics patchers |

## Json for testing
Make sure that the deployed config JSON at "C:\Modlist\NGVO\mods\Experience and Attributes\SKSE\Plugins\ExperienceAndAttributes.json" has the following:
Verbose = true 
  ## This is to enable better logging and diagnosis of eventual issues
 "leveling": {
    "xp_base":     5.0,
    "xp_increase": 1.0,
    ## This is to allow for hyper - fast leveling up, as the mod is still in testing phase, this will shorten the amount of testing time needed to reach level-up.