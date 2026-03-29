#include "PCH.h"
#include "EventSinks.h"
#include "XPManager.h"
#include "Config.h"
#include "RE/B/BooksRead.h"

// TESTrackedStatsEvent is fully defined in CommonLibSSE-NG at
// RE/T/TESTrackedStatsEvent.h (included transitively via RE/Skyrim.h in PCH).

namespace EA::EventSinks {

    // Cache the lock difficulty seen in TESLockChangedEvent; consumed by "Locks Picked" stat.
    static RE::LOCK_LEVEL s_cachedLockLevel = RE::LOCK_LEVEL::kVeryEasy;

    struct OnBookRead : public RE::BSTEventSink<RE::BooksRead::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::BooksRead::Event* event,
            RE::BSTEventSource<RE::BooksRead::Event>*) override
        {
            if (!event || !event->book) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* book = event->book;
            auto  formID = book->GetFormID();
            auto  title = book->GetFullName();
            bool  alreadyRead = book->IsRead();
            bool  skillBook = event->skillBook;

            if (!XPManager::RegisterBookRead(formID)) {
                logger::debug("[EA] Book guard: duplicate event for '{}' (FormID={:08X}) skillBook={} alreadyRead={} â€” skipped.",
                    title, formID, skillBook, alreadyRead);
                return RE::BSEventNotifyControl::kContinue;
            }

            XPManager::AwardXP(
                skillBook ? Config::xpBookSkill : Config::xpBookNew,
                XPManager::MakeBookContext(title, formID, skillBook, alreadyRead));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // PRIMARY SINK â€” TESTrackedStatsEvent
    // -----------------------------------------------------------------------
    struct OnTrackedStats : public RE::BSTEventSink<RE::TESTrackedStatsEvent> {

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESTrackedStatsEvent*                  event,
            RE::BSTEventSource<RE::TESTrackedStatsEvent>*) override
        {
            if (!event) return RE::BSEventNotifyControl::kContinue;

            const auto& stat = event->stat;

            if (stat == "Locations Discovered") {
                XPManager::AwardXP(Config::xpLocationDiscovered,
                    XPManager::MakeStatContext(stat, "location_discovered", event->value, "discovered"));
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Dungeons Cleared") {
                XPManager::AwardXP(Config::xpLocationCleared,
                    XPManager::MakeStatContext(stat, "location_cleared", event->value, "cleared"));
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Locks Picked") {
                float            xp      = Config::xpLockNovice;
                std::string_view subtype = "novice";
                switch (s_cachedLockLevel) {
                    case RE::LOCK_LEVEL::kVeryEasy: xp = Config::xpLockNovice;     subtype = "novice";     break;
                    case RE::LOCK_LEVEL::kEasy:     xp = Config::xpLockApprentice; subtype = "apprentice"; break;
                    case RE::LOCK_LEVEL::kAverage:  xp = Config::xpLockAdept;      subtype = "adept";      break;
                    case RE::LOCK_LEVEL::kHard:     xp = Config::xpLockExpert;     subtype = "expert";     break;
                    case RE::LOCK_LEVEL::kVeryHard: xp = Config::xpLockMaster;     subtype = "master";     break;
                    default:                        xp = Config::xpLockNovice;     subtype = "novice";     break;
                }
                s_cachedLockLevel = RE::LOCK_LEVEL::kVeryEasy;  // reset after consumption
                XPManager::AwardXP(xp,
                    XPManager::MakeStatContext(stat, "lock_picked", event->value, subtype));
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Skill Books Read") {
                logger::info("[EA] TrackedStat: Skill Books Read observed (diagnostic only). Counter={}.",
                             event->value);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Skill Increases") {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* skills = player ? player->GetInfoRuntimeData().skills : nullptr;
                float engineXP = (skills && skills->data) ? skills->data->xp : -1.0f;
                float threshold = (skills && skills->data) ? skills->data->levelThreshold : -1.0f;

                logger::info("[EA] TrackedStat: Skill Increases observed (diagnostic only). Counter={} | engine_xp={:.1f} threshold={:.1f} level={}.",
                    event->value,
                    engineXP,
                    threshold,
                    player ? static_cast<int>(player->GetLevel()) : -1);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Quests Completed") {
                logger::info("[EA] TrackedStat: Quest completed counter={}. XP via quest stage sink.",
                    event->value);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Misc Objectives Completed") {
                XPManager::AwardXP(Config::xpQuestMisc,
                    XPManager::MakeStatContext(stat, "quest_misc", event->value, "misc_objective"));
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Items Pickpocketed") {
                XPManager::AwardXP(Config::xpPickpocketBase,
                    XPManager::MakeStatContext(stat, "pickpocket", event->value, "base"));
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Level Increases") {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!player) return RE::BSEventNotifyControl::kContinue;

                auto* skills = player->GetInfoRuntimeData().skills;
                if (!skills || !skills->data) return RE::BSEventNotifyControl::kContinue;

                float uncapped = skills->data->levelThreshold;
                float capped   = std::min(uncapped, EA::Config::xpCap);
                skills->data->levelThreshold = capped;

                logger::info("[EA] Level Increases = {} | GetLevel()={} | threshold: {:.1f} -> {:.1f} (cap={:.1f})",
                    event->value,
                    static_cast<int>(player->GetLevel()),
                    uncapped, capped, EA::Config::xpCap);

                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "People Killed"    || stat == "Animals Killed"  ||
                stat == "Creatures Killed" || stat == "Undead Killed"   ||
                stat == "Daedra Killed"    || stat == "Automatons Killed") {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (EA::Config::verbose) {
                logger::trace("[EA] TrackedStat (unhandled): '{}' = {}",
                              event->stat.c_str(), event->value);
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // KILL SINK â€” TESDeathEvent
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

            XPManager::AwardXP(totalXP,
                XPManager::MakeKillContext(dying->GetName(), dying->GetFormID(), enemyLevel, typeName));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // QUEST SINK â€” TESQuestStageEvent
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

            XPManager::AwardXPIfQuestNew(quest->GetFormID(), xp,
                XPManager::MakeQuestContext(quest->GetName(), quest->GetFormID(), typeName));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // LOCK LEVEL CACHE SINK — TESLockChangedEvent
    // Caches the lock difficulty level whenever a lock transitions to unlocked.
    // Consumed by the "Locks Picked" TrackedStat handler to award tier-appropriate XP.
    // -----------------------------------------------------------------------
    struct OnLockChanged : public RE::BSTEventSink<RE::TESLockChangedEvent> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESLockChangedEvent*                  event,
            RE::BSTEventSource<RE::TESLockChangedEvent>*) override
        {
            if (!event || !event->lockedObject) return RE::BSEventNotifyControl::kContinue;
            auto* ref = event->lockedObject.get();
            if (!ref) return RE::BSEventNotifyControl::kContinue;

            auto* extraLock = ref->extraList.GetByType<RE::ExtraLock>();
            if (!extraLock || !extraLock->lock) return RE::BSEventNotifyControl::kContinue;

            // Cache difficulty whenever a lock becomes unlocked.
            // IsLocked() checks REFR_LOCK::Flag::kLocked; GetLockLevel() resolves
            // the LOCK_LEVEL from baseLevel (accounting for leveled lock scaling).
            if (!extraLock->lock->IsLocked()) {
                s_cachedLockLevel = extraLock->lock->GetLockLevel(ref);
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // Static instances
    // -----------------------------------------------------------------------
    static OnBookRead     s_bookReadSink;
    static OnTrackedStats s_trackedStatsSink;
    static OnActorKill    s_killSink;
    static OnQuestStage   s_questSink;
    static OnLockChanged  s_lockChangedSink;

    void Register() {
        auto* src = RE::ScriptEventSourceHolder::GetSingleton();
        if (!src) {
            logger::error("[EA] EventSinks: ScriptEventSourceHolder is null.");
            return;
        }

        logger::info("[EA] EventSinks: Registering sinks...");

        auto* bookSrc = RE::BooksRead::GetEventSource();
        if (bookSrc) {
            bookSrc->AddEventSink(&s_bookReadSink);
            logger::info("[EA] EventSinks: [1/4] BooksRead event sink registered.");
        } else {
            logger::error("[EA] EventSinks: BooksRead event source is null.");
        }

        src->GetEventSource<RE::TESTrackedStatsEvent>()->AddEventSink(&s_trackedStatsSink);
        logger::info("[EA] EventSinks: [2/4] TESTrackedStatsEvent registered.");

        src->GetEventSource<RE::TESDeathEvent>()->AddEventSink(&s_killSink);
        logger::info("[EA] EventSinks: [3/4] TESDeathEvent (kill) registered.");

        src->GetEventSource<RE::TESQuestStageEvent>()->AddEventSink(&s_questSink);
        logger::info("[EA] EventSinks: [4/5] TESQuestStageEvent registered.");

        src->GetEventSource<RE::TESLockChangedEvent>()->AddEventSink(&s_lockChangedSink);
        logger::info("[EA] EventSinks: [5/5] TESLockChangedEvent (lock level cache) registered.");

        logger::warn("[EA] EventSinks: TESActorValueChangeEvent sink SKIPPED â€” "
                     "struct not defined in this CommonLibSSE-NG build. "
                     "Attribute selection will not be logged.");

        logger::warn("[EA] EventSinks: TESPerkEntryRunEvent sink SKIPPED â€” "
                     "struct forward-declared only, no field definitions available. "
                     "Perk selection will not be logged.");

        logger::info("[EA] EventSinks: All sinks registered (5/5 active, 2/2 diagnostic sinks skipped).");
    }
}
