#include "PCH.h"
#include "EventSinks.h"
#include "XPManager.h"
#include "Config.h"

// TESTrackedStatsEvent is fully defined in CommonLibSSE-NG at
// RE/T/TESTrackedStatsEvent.h (included transitively via RE/Skyrim.h in PCH).
// Struct layout: { BSFixedString stat; int32_t value; uint32_t pad0C; }
// No forward declaration needed.

namespace EA::EventSinks {

    // -----------------------------------------------------------------------
    // PRIMARY SINK — TESTrackedStatsEvent
    //
    // Skyrim fires this event every time any value on the Statistics journal
    // page increments. One sink replaces all the proxy events used in Task 4.
    //
    // Stat strings verified from SKSE source and community research:
    //   "Locations Discovered"   — new map marker uncovered
    //   "Dungeons Cleared"       — location boss killed / cleared flag set
    //   "Locks Picked"           — lockpick success only (not key opens)
    //   "Books Read"             — first read of each unique book
    //   "Skill Books Read"       — fires ADDITIONALLY for skill books; we skip
    //                              to avoid double-awarding (Books Read covers it)
    //   "Quests Completed"       — all quest types (no type info available here)
    //   "Misc Objectives Completed" — misc tasks
    //   Kill stats               — logged for cross-reference; XP via TESDeathEvent
    // -----------------------------------------------------------------------
    struct OnTrackedStats : public RE::BSTEventSink<RE::TESTrackedStatsEvent> {

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESTrackedStatsEvent*                  event,
            RE::BSTEventSource<RE::TESTrackedStatsEvent>*) override
        {
            if (!event) return RE::BSEventNotifyControl::kContinue;

            // Log every tracked stat unconditionally for diagnostics
            logger::info("[EA] TrackedStat: '{}' = {}",
                event->stat.c_str(), event->value);

            const auto& stat = event->stat;

            if (stat == "Locations Discovered") {
                XPManager::AwardXP(Config::xpLocationDiscovered, "location_discovered");
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Dungeons Cleared") {
                XPManager::AwardXP(Config::xpLocationCleared, "location_cleared");
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Locks Picked") {
                // No lock level in TESTrackedStatsEvent — flat novice XP for now.
                // Difficulty scaling deferred to Task 5 (secondary hook on lockpick fn).
                XPManager::AwardXP(Config::xpLockNovice, "lock_picked");
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Books Read") {
                // Skyrim's own stat counter handles first-read deduplication natively.
                XPManager::AwardXP(Config::xpBookNew, "book_read");
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Skill Books Read") {
                // "Skill Books Read" is the ONLY stat that fires for skill books in AE.
                // "Books Read" does NOT fire for skill books — award XP here directly.
                logger::info("[EA] TrackedStat: Skill book read. Awarding {:.1f} XP.",
                             Config::xpBookSkill);
                XPManager::AwardXP(Config::xpBookSkill, "book_skill");
                return RE::BSEventNotifyControl::kContinue;
            }

            // "Quests Completed" — carries no quest type or FormID.
            // Per-type XP (with FormID dedup) is handled by OnQuestStage below.
            // We do NOT award XP here to avoid doubling with the stage sink,
            // since both sinks share the same FormID guard key space.
            if (stat == "Quests Completed") {
                logger::info("[EA] TrackedStat: Quest completed counter={}. XP via quest stage sink.",
                    event->value);
                return RE::BSEventNotifyControl::kContinue;
            }

            // "Misc Objectives Completed" — misc quests never set IsCompleted(),
            // so the quest stage sink's guard rejects them. This is the ONLY place
            // misc quest XP is awarded. Each counter increment = one distinct action,
            // so no FormID guard is needed here.
            if (stat == "Misc Objectives Completed") {
                logger::info("[EA] TrackedStat: Misc objective completed (counter={}). Awarding {:.1f} XP.",
                    event->value, Config::xpQuestMisc);
                XPManager::AwardXP(Config::xpQuestMisc, "quest_misc");
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Items Pickpocketed") {
                logger::info("[EA] TrackedStat: Item pickpocketed. Awarding {:.1f} XP.", Config::xpPickpocketBase);
                XPManager::AwardXP(Config::xpPickpocketBase, "pickpocket");
                return RE::BSEventNotifyControl::kContinue;
            }

            // "Level Increases" fires after the engine fully completes a level-up
            // (attribute screen confirmed, perk point awarded, level incremented).
            // We use this as the signal to fire the next pending level-up, if any,
            // chaining multiple level-ups one per engine cycle with full vanilla UI.
            if (stat == "Level Increases") {
                logger::info("[EA] TrackedStat: Level Increases = {}. "
                             "Engine finished level-up. Clearing in-progress flag. "
                             "Pending: {}",
                             event->value,
                             EA::XPManager::GetPendingLevelUps());

                // Clear the flag FIRST so FirePendingLevelUp is unblocked.
                EA::XPManager::SetLevelUpInProgress(false);

                // Then fire the next pending level-up if any.
                EA::XPManager::FirePendingLevelUp();
                return RE::BSEventNotifyControl::kContinue;
            }

            // Kill stats: logged for cross-reference; XP via TESDeathEvent with
            // per-actor FormID deduplication guard.
            if (stat == "People Killed"    || stat == "Animals Killed"  ||
                stat == "Creatures Killed" || stat == "Undead Killed"   ||
                stat == "Daedra Killed"    || stat == "Automatons Killed") {
                logger::info("[EA] TrackedStat: Kill stat '{}' = {} — XP via death sink.",
                    event->stat.c_str(), event->value);
                return RE::BSEventNotifyControl::kContinue;
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // KILL SINK — TESDeathEvent
    // Kept because TESTrackedStatsEvent kill stats lack per-actor FormID,
    // making deduplication impossible from stats alone.
    //
    // XP formula: totalXP = baseXP(type) + max(0, enemyLevel - playerLevel) * scaleFactor
    // Type detection uses ActorType keywords in priority order:
    //   Dragon > Daedra > Undead > Animal > Creature > NPC(humanoid) > default
    // Keywords are cached on first kill (all data is loaded by then).
    // -----------------------------------------------------------------------
    struct OnActorKill : public RE::BSTEventSink<RE::TESDeathEvent> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESDeathEvent*                  event,
            RE::BSTEventSource<RE::TESDeathEvent>*) override
        {
            if (!event || !event->actorDying || !event->actorKiller)
                return RE::BSEventNotifyControl::kContinue;

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || event->actorKiller.get() != player)
                return RE::BSEventNotifyControl::kContinue;

            auto* dying = event->actorDying.get();
            if (!dying || dying->IsPlayerRef())
                return RE::BSEventNotifyControl::kContinue;

            if (!XPManager::RegisterKill(dying->GetFormID()))
                return RE::BSEventNotifyControl::kContinue;

            // Cache keyword pointers on first invocation (data is loaded by now)
            static auto* kwDragon   = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeDragon");
            static auto* kwDaedra   = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeDaedra");
            static auto* kwUndead   = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeUndead");
            static auto* kwAnimal   = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeAnimal");
            static auto* kwCreature = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeCreature");
            static auto* kwNPC      = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");

            float       baseXP   = Config::xpKillDefault;
            const char* typeName = "default";

            if      (kwDragon   && dying->HasKeyword(kwDragon))   { baseXP = Config::xpKillDragon;   typeName = "dragon";   }
            else if (kwDaedra   && dying->HasKeyword(kwDaedra))   { baseXP = Config::xpKillDaedra;   typeName = "daedra";   }
            else if (kwUndead   && dying->HasKeyword(kwUndead))   { baseXP = Config::xpKillUndead;   typeName = "undead";   }
            else if (kwAnimal   && dying->HasKeyword(kwAnimal))   { baseXP = Config::xpKillAnimal;   typeName = "animal";   }
            else if (kwCreature && dying->HasKeyword(kwCreature)) { baseXP = Config::xpKillCreature; typeName = "creature"; }
            else if (kwNPC      && dying->HasKeyword(kwNPC))      { baseXP = Config::xpKillHumanoid; typeName = "humanoid"; }

            int   playerLevel = static_cast<int>(player->GetLevel());
            int   enemyLevel  = static_cast<int>(static_cast<RE::Actor*>(dying)->GetLevel());
            float bonus       = static_cast<float>(std::max(0, enemyLevel - playerLevel))
                                    * Config::xpKillLevelScaleFactor;
            float totalXP     = baseXP + bonus;

            logger::info("[EA] Kill: '{}' (FormID={:08X}) type={} lv={} player_lv={} | base={:.1f} bonus={:.1f} total={:.1f} XP.",
                dying->GetName(), dying->GetFormID(), typeName,
                enemyLevel, playerLevel, baseXP, bonus, totalXP);

            XPManager::AwardXP(totalXP, "kill");
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // QUEST SINK — TESQuestStageEvent
    // Kept because TESTrackedStatsEvent "Quests Completed" carries no quest
    // type, so per-type XP differentiation requires this sink.
    // -----------------------------------------------------------------------
    struct OnQuestStage : public RE::BSTEventSink<RE::TESQuestStageEvent> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESQuestStageEvent*                  event,
            RE::BSTEventSource<RE::TESQuestStageEvent>*) override
        {
            if (!event) return RE::BSEventNotifyControl::kContinue;

            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(event->formID);
            if (!quest || !quest->IsCompleted())
                return RE::BSEventNotifyControl::kContinue;

            float            xp       = Config::xpQuestOther;
            std::string_view typeName = "quest_other";
            auto             type     = quest->GetType();

            switch (type) {
                case RE::QUEST_DATA::Type::kMainQuest:
                    xp = Config::xpQuestMain;      typeName = "quest_main";      break;
                case RE::QUEST_DATA::Type::kMagesGuild:
                case RE::QUEST_DATA::Type::kThievesGuild:
                case RE::QUEST_DATA::Type::kDarkBrotherhood:
                case RE::QUEST_DATA::Type::kCompanionsQuest:
                    xp = Config::xpQuestFaction;   typeName = "quest_faction";   break;
                case RE::QUEST_DATA::Type::kSideQuest:
                    xp = Config::xpQuestSide;      typeName = "quest_side";      break;
                case RE::QUEST_DATA::Type::kMiscellaneous:
                    xp = Config::xpQuestMisc;      typeName = "quest_misc";      break;
                case RE::QUEST_DATA::Type::kDaedric:
                    xp = Config::xpQuestDaedric;   typeName = "quest_daedric";   break;
                case RE::QUEST_DATA::Type::kCivilWar:
                    xp = Config::xpQuestCivilWar;  typeName = "quest_civil_war"; break;
                case RE::QUEST_DATA::Type::kDLC01_Vampire:
                case RE::QUEST_DATA::Type::kDLC02_Dragonborn:
                    xp = Config::xpQuestDLC;       typeName = "quest_dlc";       break;
                default:
                    xp = Config::xpQuestOther;     typeName = "quest_other";     break;
            }

            logger::info("[EA] Quest completed: '{}' type={} ({}) formID={:08X} | Awarding {:.1f} XP.",
                quest->GetName(), static_cast<int>(type), typeName, quest->GetFormID(), xp);

            XPManager::AwardXPIfQuestNew(quest->GetFormID(), xp, typeName);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // ATTRIBUTE TRACKING — TESActorValueChangeEvent
    //
    // NOTE: TESActorValueChangeEvent has NO struct definition in this build of
    // CommonLibSSE-NG (checked include/RE/T/ — file does not exist). The event
    // is absent from ScriptEventSourceHolder's BSTEventSource base list.
    // Sink is commented out; attribute selection is NOT tracked this build.
    // -----------------------------------------------------------------------
    // struct OnActorValueChange : public RE::BSTEventSink<RE::TESActorValueChangeEvent> { ... };

    // -----------------------------------------------------------------------
    // PERK TRACKING — TESPerkEntryRunEvent
    //
    // NOTE: TESPerkEntryRunEvent IS forward-declared in ScriptEventSourceHolder.h
    // and SkyrimVM.h, but has NO struct definition in this CommonLibSSE-NG build
    // (no RE/T/TESPerkEntryRunEvent.h exists). Field access (perkId, target) is
    // therefore impossible. Sink is commented out; perk selection NOT tracked.
    // -----------------------------------------------------------------------
    // struct OnPerkEntry : public RE::BSTEventSink<RE::TESPerkEntryRunEvent> { ... };

    // -----------------------------------------------------------------------
    // Static instances
    // -----------------------------------------------------------------------
    static OnTrackedStats s_trackedStatsSink;
    static OnActorKill    s_killSink;
    static OnQuestStage   s_questSink;

    void Register() {
        auto* src = RE::ScriptEventSourceHolder::GetSingleton();
        if (!src) {
            logger::error("[EA] EventSinks: ScriptEventSourceHolder is null.");
            return;
        }

        logger::info("[EA] EventSinks: Registering sinks...");

        src->GetEventSource<RE::TESTrackedStatsEvent>()->AddEventSink(&s_trackedStatsSink);
        logger::info("[EA] EventSinks: [1/3] TESTrackedStatsEvent registered.");

        src->GetEventSource<RE::TESDeathEvent>()->AddEventSink(&s_killSink);
        logger::info("[EA] EventSinks: [2/3] TESDeathEvent (kill) registered.");

        src->GetEventSource<RE::TESQuestStageEvent>()->AddEventSink(&s_questSink);
        logger::info("[EA] EventSinks: [3/3] TESQuestStageEvent registered.");

        // TESActorValueChangeEvent: no struct definition in this CommonLibSSE-NG build.
        logger::warn("[EA] EventSinks: TESActorValueChangeEvent sink SKIPPED — "
                     "struct not defined in this CommonLibSSE-NG build. "
                     "Attribute selection will not be logged.");

        // TESPerkEntryRunEvent: forward-declared only, no field access possible.
        logger::warn("[EA] EventSinks: TESPerkEntryRunEvent sink SKIPPED — "
                     "struct forward-declared only, no field definitions available. "
                     "Perk selection will not be logged.");

        logger::info("[EA] EventSinks: All sinks registered (3/3 active, 2/2 diagnostic sinks skipped).");
    }
}
