#include "PCH.h"
#include "EventSinks.h"
#include "XPManager.h"
#include "Config.h"
#include "RE/E/ExtraMapMarker.h"
#include "RE/L/LocationCleared.h"
#include "RE/L/LocationDiscovery.h"
#include "RE/B/BGSLocation.h"

// TESTrackedStatsEvent is fully defined in CommonLibSSE-NG at
// RE/T/TESTrackedStatsEvent.h (included transitively via RE/Skyrim.h in PCH).

namespace EA::EventSinks {

    // Cache the lock difficulty seen in TESLockChangedEvent; consumed by "Locks Picked" stat.
    static RE::LOCK_LEVEL s_cachedLockLevel = RE::LOCK_LEVEL::kVeryEasy;

    namespace {
        static std::string_view ClassifyMarkerType(RE::MARKER_TYPE type) {
            switch (type) {
                case RE::MARKER_TYPE::kCity: return "city";
                case RE::MARKER_TYPE::kTown: return "town";
                case RE::MARKER_TYPE::kSettlement: return "settlement";
                case RE::MARKER_TYPE::kCave: return "cave";
                case RE::MARKER_TYPE::kCamp: return "camp";
                case RE::MARKER_TYPE::kFort: return "fort";
                case RE::MARKER_TYPE::kNordicRuin: return "nordic_ruin";
                case RE::MARKER_TYPE::kDwemerRuin: return "dwemer_ruin";
                case RE::MARKER_TYPE::kShipwreck: return "shipwreck";
                case RE::MARKER_TYPE::kGrove: return "grove";
                case RE::MARKER_TYPE::kLandmark: return "landmark";
                case RE::MARKER_TYPE::kDragonLair: return "dragon_lair";
                case RE::MARKER_TYPE::kFarm: return "farm";
                case RE::MARKER_TYPE::kWoodMill: return "wood_mill";
                case RE::MARKER_TYPE::kMine: return "mine";
                case RE::MARKER_TYPE::kImperialCamp:
                case RE::MARKER_TYPE::kStormcloakCamp:
                case RE::MARKER_TYPE::kGiantCamp:
                    return "military_camp";
                case RE::MARKER_TYPE::kDoomstone: return "doomstone";
                case RE::MARKER_TYPE::kWheatMill: return "wheat_mill";
                case RE::MARKER_TYPE::kSmelter: return "smelter";
                case RE::MARKER_TYPE::kStable: return "stable";
                case RE::MARKER_TYPE::kImperialTower: return "imperial_tower";
                case RE::MARKER_TYPE::kClearing: return "clearing";
                case RE::MARKER_TYPE::kPass: return "pass";
                case RE::MARKER_TYPE::kAltar: return "altar";
                case RE::MARKER_TYPE::kRock: return "rock";
                case RE::MARKER_TYPE::kLighthouse: return "lighthouse";
                case RE::MARKER_TYPE::kOrcStronghold: return "orc_stronghold";
                case RE::MARKER_TYPE::kShack: return "shack";
                case RE::MARKER_TYPE::kNordicTower: return "nordic_tower";
                case RE::MARKER_TYPE::kNordicDwelling: return "nordic_dwelling";
                case RE::MARKER_TYPE::kDocks: return "docks";
                case RE::MARKER_TYPE::kRiftenCastle:
                case RE::MARKER_TYPE::kWindhelmCastle:
                case RE::MARKER_TYPE::kWhiterunCastle:
                case RE::MARKER_TYPE::kSolitudeCastle:
                case RE::MARKER_TYPE::kMarkarthCastle:
                case RE::MARKER_TYPE::kWinterholdCastle:
                case RE::MARKER_TYPE::kMorthalCastle:
                case RE::MARKER_TYPE::kFalkreathCastle:
                case RE::MARKER_TYPE::kDawnstarCastle:
                    return "castle";
                case RE::MARKER_TYPE::kRiftenCapitol:
                case RE::MARKER_TYPE::kWindhelmCapitol:
                case RE::MARKER_TYPE::kWhiterunCapitol:
                case RE::MARKER_TYPE::kSolitudeCapitol:
                case RE::MARKER_TYPE::kMarkarthCapitol:
                case RE::MARKER_TYPE::kWinterholdCapitol:
                case RE::MARKER_TYPE::kMorthalCapitol:
                case RE::MARKER_TYPE::kFalkreathCapitol:
                case RE::MARKER_TYPE::kDawnstarCapitol:
                    return "city";
                case RE::MARKER_TYPE::kDLC02MiraakTemple:
                case RE::MARKER_TYPE::kDLC02RavenRock:
                case RE::MARKER_TYPE::kDLC02StandingStone:
                case RE::MARKER_TYPE::kDLC02TelvanniTower:
                case RE::MARKER_TYPE::kDLC02CastleKarstaag:
                    return "castle";
                default:
                    return "default";
            }
        }

        static std::string_view ClassifyLocation(RE::BGSLocation* location) {
            if (!location) {
                return "default";
            }

            if (auto ref = location->worldLocMarker.get()) {
                if (auto* extraMap = ref->extraList.GetByType<RE::ExtraMapMarker>()) {
                    if (extraMap->mapData) {
                        auto key = ClassifyMarkerType(static_cast<RE::MARKER_TYPE>(extraMap->mapData->type.underlying()));
                        if (key != "default"sv) {
                            return key;
                        }
                    }
                }
            }

            static auto* kwCity = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeCity");
            static auto* kwTown = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeTown");
            static auto* kwSettlement = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeSettlement");
            static auto* kwCave = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeCave");
            static auto* kwCamp = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeCamp");
            static auto* kwFort = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeFort");
            static auto* kwNordicRuin = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeNordicRuins");
            static auto* kwDwemerRuin = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeDwemerRuins");
            static auto* kwShipwreck = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeShipwreck");
            static auto* kwGrove = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeGrove");
            static auto* kwLandmark = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeLandmark");
            static auto* kwDragonLair = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeDragonLair");
            static auto* kwFarm = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeFarm");
            static auto* kwWoodMill = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeWoodMill");
            static auto* kwMine = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeMine");
            static auto* kwMilitary = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeMilitaryCamp");
            static auto* kwDoomstone = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeDoomstone");
            static auto* kwWheatMill = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeWheatMill");
            static auto* kwSmelter = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeSmelter");
            static auto* kwStable = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeStable");
            static auto* kwImperialTower = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeImperialTower");
            static auto* kwClearing = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeClearing");
            static auto* kwPass = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypePass");
            static auto* kwAltar = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeAltar");
            static auto* kwRock = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeRock");
            static auto* kwLighthouse = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeLighthouse");
            static auto* kwOrc = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeOrcStronghold");
            static auto* kwGiant = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeGiantCamp");
            static auto* kwShack = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeShack");
            static auto* kwNordicTower = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeNordicTower");
            static auto* kwNordicDwelling = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeNordicDwelling");
            static auto* kwDocks = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeDocks");
            static auto* kwDaedricShrine = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeDaedricShrine");
            static auto* kwCastle = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeCastle");

            auto matches = [&](RE::BGSKeyword* kw) {
                return kw && location->HasKeyword(kw);
            };

            if (matches(kwCity)) return "city";
            if (matches(kwTown)) return "town";
            if (matches(kwSettlement)) return "settlement";
            if (matches(kwCave)) return "cave";
            if (matches(kwCamp)) return "camp";
            if (matches(kwFort)) return "fort";
            if (matches(kwNordicRuin)) return "nordic_ruin";
            if (matches(kwDwemerRuin)) return "dwemer_ruin";
            if (matches(kwShipwreck)) return "shipwreck";
            if (matches(kwGrove)) return "grove";
            if (matches(kwLandmark)) return "landmark";
            if (matches(kwDragonLair)) return "dragon_lair";
            if (matches(kwFarm)) return "farm";
            if (matches(kwWoodMill)) return "wood_mill";
            if (matches(kwMine)) return "mine";
            if (matches(kwMilitary)) return "military_camp";
            if (matches(kwDoomstone)) return "doomstone";
            if (matches(kwWheatMill)) return "wheat_mill";
            if (matches(kwSmelter)) return "smelter";
            if (matches(kwStable)) return "stable";
            if (matches(kwImperialTower)) return "imperial_tower";
            if (matches(kwClearing)) return "clearing";
            if (matches(kwPass)) return "pass";
            if (matches(kwAltar)) return "altar";
            if (matches(kwRock)) return "rock";
            if (matches(kwLighthouse)) return "lighthouse";
            if (matches(kwOrc)) return "orc_stronghold";
            if (matches(kwGiant)) return "giant_camp";
            if (matches(kwShack)) return "shack";
            if (matches(kwNordicTower)) return "nordic_tower";
            if (matches(kwNordicDwelling)) return "nordic_dwelling";
            if (matches(kwDocks)) return "docks";
            if (matches(kwDaedricShrine)) return "daedric_shrine";
            if (matches(kwCastle)) return "castle";

            if (location->parentLoc) {
                return ClassifyLocation(location->parentLoc);
            }
            return "default";
        }
    }


    // -----------------------------------------------------------------------
    // PRIMARY SINK — TESTrackedStatsEvent
    // -----------------------------------------------------------------------
    struct OnLocationDiscovery : public RE::BSTEventSink<RE::LocationDiscovery::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::LocationDiscovery::Event* event,
            RE::BSTEventSource<RE::LocationDiscovery::Event>*) override
        {
            if (!event || !event->mapMarkerData) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto markerKey = reinterpret_cast<std::uintptr_t>(event->mapMarkerData);
            if (!XPManager::RegisterLocationDiscovery(markerKey)) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto typeKey = ClassifyMarkerType(static_cast<RE::MARKER_TYPE>(event->mapMarkerData->type.underlying()));
            auto reward = Config::GetReward(Config::locationDiscoveryRewards, typeKey, Config::xpLocationDiscovered);

            XPManager::AwardXP(reward,
                XPManager::MakeStatContext("Location Discovered", "location_discovery", 1, typeKey));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    struct OnLocationCleared : public RE::BSTEventSink<RE::LocationCleared::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::LocationCleared::Event*,
            RE::BSTEventSource<RE::LocationCleared::Event>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* location = player->GetCurrentLocation();
            if (!location) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (!XPManager::RegisterLocationClear(location->GetFormID())) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto typeKey = ClassifyLocation(location);
            auto reward = Config::GetReward(Config::locationClearingRewards, typeKey, Config::xpLocationCleared);
            auto* name = location->GetFullName();
            auto subject = (name && name[0]) ? name : "Location Cleared";

            XPManager::AwardXP(reward,
                XPManager::MakeStatContext(subject, "location_cleared", 1, typeKey));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    struct OnTrackedStats : public RE::BSTEventSink<RE::TESTrackedStatsEvent> {

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESTrackedStatsEvent*                  event,
            RE::BSTEventSource<RE::TESTrackedStatsEvent>*) override
        {
            if (!event) return RE::BSEventNotifyControl::kContinue;

            const auto& stat = event->stat;

            if (stat == "Locations Discovered") {
                logger::info("[EA] TrackedStat: Locations Discovered observed (diagnostic only). Counter={}.",
                    event->value);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Dungeons Cleared") {
                logger::info("[EA] TrackedStat: Dungeons Cleared observed (diagnostic only). Counter={}.",
                    event->value);
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
                XPManager::AwardXP(Config::xpQuestObjectives,
                    XPManager::MakeStatContext(stat, "quest_objectives", event->value, "misc_objective"));
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
    // KILL SINK — TESDeathEvent
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
            float totalXP     = (baseXP + bonus) * Config::xpKillGlobalMultiplier;

            XPManager::AwardXP(totalXP,
                XPManager::MakeKillContext(dying->GetName(), dying->GetFormID(), enemyLevel, typeName));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // QUEST SINK — TESQuestStageEvent
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
                    xp = Config::xpQuestCollege;   typeName = "quest_college";   break;
                case RE::QUEST_DATA::Type::kThievesGuild:
                    xp = Config::xpQuestThieves;   typeName = "quest_thieves";    break;
                case RE::QUEST_DATA::Type::kDarkBrotherhood:
                    xp = Config::xpQuestBrotherhood; typeName = "quest_brotherhood"; break;
                case RE::QUEST_DATA::Type::kCompanionsQuest:
                    xp = Config::xpQuestCompanions; typeName = "quest_companions"; break;
                case RE::QUEST_DATA::Type::kSideQuest:
                    xp = Config::xpQuestSide;      typeName = "quest_side";      break;
                case RE::QUEST_DATA::Type::kMiscellaneous:
                    xp = Config::xpQuestMisc;      typeName = "quest_misc";      break;
                case RE::QUEST_DATA::Type::kDaedric:
                    xp = Config::xpQuestDaedric;   typeName = "quest_daedric";   break;
                case RE::QUEST_DATA::Type::kCivilWar:
                    xp = Config::xpQuestCivilWar;  typeName = "quest_civil_war"; break;
                case RE::QUEST_DATA::Type::kDLC01_Vampire:
                    xp = Config::xpQuestDawnguard; typeName = "quest_dawnguard"; break;
                case RE::QUEST_DATA::Type::kDLC02_Dragonborn:
                    xp = Config::xpQuestDragonborn; typeName = "quest_dragonborn"; break;
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
    static OnLocationDiscovery s_locationDiscoverySink;
    static OnLocationCleared   s_locationClearedSink;
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

        }

        auto* discoverySrc = RE::LocationDiscovery::GetEventSource();
        if (discoverySrc) {
            discoverySrc->AddEventSink(&s_locationDiscoverySink);
            logger::info("[EA] EventSinks: [1/6] LocationDiscovery event sink registered.");
        } else {
            logger::error("[EA] EventSinks: LocationDiscovery event source is null.");
        }

        auto* clearedSrc = RE::LocationCleared::GetEventSource();
        if (clearedSrc) {
            clearedSrc->AddEventSink(&s_locationClearedSink);
            logger::info("[EA] EventSinks: [2/6] LocationCleared event sink registered.");
        } else {
            logger::error("[EA] EventSinks: LocationCleared event source is null.");
        }

        src->GetEventSource<RE::TESTrackedStatsEvent>()->AddEventSink(&s_trackedStatsSink);
        logger::info("[EA] EventSinks: [3/6] TESTrackedStatsEvent registered.");

        src->GetEventSource<RE::TESDeathEvent>()->AddEventSink(&s_killSink);
        logger::info("[EA] EventSinks: [4/6] TESDeathEvent (kill) registered.");

        src->GetEventSource<RE::TESQuestStageEvent>()->AddEventSink(&s_questSink);
        logger::info("[EA] EventSinks: [5/6] TESQuestStageEvent registered.");

        src->GetEventSource<RE::TESLockChangedEvent>()->AddEventSink(&s_lockChangedSink);
        logger::info("[EA] EventSinks: [6/6] TESLockChangedEvent (lock level cache) registered.");

        logger::warn("[EA] EventSinks: TESActorValueChangeEvent sink SKIPPED — "
                     "struct not defined in this CommonLibSSE-NG build. "
                     "Attribute selection will not be logged.");

        logger::warn("[EA] EventSinks: TESPerkEntryRunEvent sink SKIPPED — "
                     "struct forward-declared only, no field definitions available. "
                     "Perk selection will not be logged.");

        logger::info("[EA] EventSinks: All sinks registered (6/6 active, 2/2 diagnostic sinks skipped).");
    }
}
