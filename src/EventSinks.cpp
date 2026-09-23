#include "PCH.h"
#include "EventSinks.h"
#include "XPManager.h"
#include "Config.h"
#include "Leveling.h"
#include "RewardRules.h"
#include "SkillMenu.h"
#include "RE/A/ActorKill.h"
#include "RE/E/ExtraMapMarker.h"
#include "RE/I/ItemsPickpocketed.h"
#include "RE/L/LevelIncrease.h"
#include "RE/L/LocationCleared.h"
#include "RE/L/LocationDiscovery.h"
#include "RE/L/LockpickingMenu.h"
#include "RE/O/ObjectiveState.h"
#include "RE/Q/QuestStatus.h"
#include "RE/B/BGSLocation.h"

// TESTrackedStatsEvent is fully defined in CommonLibSSE-NG at
// RE/T/TESTrackedStatsEvent.h (included transitively via RE/Skyrim.h in PCH).

namespace EA::EventSinks {
    static bool s_registered = false;

    struct LockAttempt {
        RE::FormID    targetID{ 0 };
        std::int32_t  lockLevel{ 0 };
    };

    static std::optional<LockAttempt> s_lockAttempt;
    static std::optional<std::int32_t> s_lastLockCounter;
    static RewardRules::ClearedLocationTracker s_clearedLocations;

    namespace {
        static std::string_view ClassifyMarkerType(RE::MARKER_TYPE type) {
            return RewardRules::ClassifyMarkerType(
                static_cast<std::uint16_t>(type));
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
    // PRIMARY SINK - TESTrackedStatsEvent
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
            const char* name = event->mapMarkerData->locationName.GetFullName();
            const auto subject = (name && name[0]) ? name : "Location Discovered";

            XPManager::AwardXP(reward,
                XPManager::MakeStatContext(subject, "location_discovery", 1, typeKey));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // LocationCleared::Event is empty, so the cleared location is found by
    // diffing the game's ever-cleared flags against the load-time snapshot.
    static std::vector<std::uint32_t> CollectEverClearedLocations()
    {
        std::vector<std::uint32_t> result;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logger::warn("[EA] Location clear: TESDataHandler is null.");
            return result;
        }
        for (const auto* location : dataHandler->GetFormArray<RE::BGSLocation>()) {
            if (location && location->everCleared) {
                result.push_back(location->GetFormID());
            }
        }
        return result;
    }

    struct OnLocationCleared : public RE::BSTEventSink<RE::LocationCleared::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::LocationCleared::Event*,
            RE::BSTEventSource<RE::LocationCleared::Event>*) override
        {
            LogCurrentLocationFlags("event");

            // The engine dispatches the event before it sets the location's
            // cleared flags, so compare them on the next frame.
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                logger::warn("[EA] Location clear: task interface unavailable; checking immediately.");
                AwardNewlyClearedLocations();
                return RE::BSEventNotifyControl::kContinue;
            }
            const auto generation = XPManager::GetRewardGeneration();
            tasks->AddTask([generation]() {
                if (XPManager::GetRewardGeneration() != generation) {
                    logger::debug("[EA] Location clear: stale deferred check discarded after state reset.");
                    return;
                }
                LogCurrentLocationFlags("deferred");
                AwardNewlyClearedLocations();
            });
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        static void LogCurrentLocationFlags(std::string_view stage)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* location = player ? player->GetCurrentLocation() : nullptr;
            if (!location) {
                logger::info("[EA] Location clear ({}): player has no current location.", stage);
                return;
            }
            auto* name = location->GetFullName();
            logger::info("[EA] Location clear ({}): current='{}' ({:08X}) cleared={} everCleared={}.",
                stage, name ? name : "", location->GetFormID(), location->cleared, location->everCleared);
        }

        static void AwardNewlyClearedLocations()
        {
            if (!s_clearedLocations.Ready()) {
                logger::warn("[EA] Location clear: no load-time snapshot; recording state without reward.");
            }
            const auto newlyCleared = s_clearedLocations.Observe(CollectEverClearedLocations());
            if (newlyCleared.empty()) {
                logger::warn("[EA] Location clear: event had no newly ever-cleared location; skipped.");
                return;
            }

            for (const auto locationID : newlyCleared) {
                auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(locationID);
                if (!location) {
                    continue;
                }
                auto typeKey = ClassifyLocation(location);
                auto reward = Config::GetReward(Config::locationClearingRewards, typeKey, Config::xpLocationCleared);
                auto* name = location->GetFullName();
                auto subject = (name && name[0]) ? name : "Location Cleared";

                XPManager::AwardXP(reward,
                    XPManager::MakeStatContext(subject, "location_cleared", 1, typeKey));
            }
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
                if (event->value < 0) {
                    logger::warn("[EA] Lock reward: invalid tracked-stat counter {} rejected.", event->value);
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (s_lastLockCounter && event->value <= *s_lastLockCounter) {
                    logger::debug("[EA] Lock reward: duplicate/stale counter {} (last={}) skipped.",
                        event->value, *s_lastLockCounter);
                    return RE::BSEventNotifyControl::kContinue;
                }
                s_lastLockCounter = event->value;

                std::int32_t lockLevel = 0;
                RE::FormID targetID = 0;
                if (s_lockAttempt) {
                    lockLevel = s_lockAttempt->lockLevel;
                    targetID = s_lockAttempt->targetID;
                    s_lockAttempt.reset();
                } else {
                    logger::warn("[EA] Lock reward: success counter {} had no captured Lockpicking Menu context; using novice fallback.",
                        event->value);
                }

                const auto subtype = RewardRules::ClassifyLockLevel(lockLevel);
                float xp = Config::xpLockNovice;
                if (subtype == "apprentice") xp = Config::xpLockApprentice;
                else if (subtype == "adept") xp = Config::xpLockAdept;
                else if (subtype == "expert") xp = Config::xpLockExpert;
                else if (subtype == "master") xp = Config::xpLockMaster;

                logger::info("[EA] Lock reward accepted: target={:08X}, tier={}, counter={}.",
                    targetID, subtype, event->value);
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
                logger::info("[EA] TrackedStat: Quest completed counter={} (diagnostic only; XP via QuestStatus::Event).",
                    event->value);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Misc Objectives Completed") {
                logger::info("[EA] TrackedStat: Misc Objectives Completed={} (diagnostic only; XP via ObjectiveState::Event).",
                    event->value);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Items Pickpocketed") {
                logger::info("[EA] TrackedStat: Items Pickpocketed={} (diagnostic only; XP via ItemsPickpocketed::Event).",
                    event->value);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (stat == "Level Increases") {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* skills = player ? player->GetInfoRuntimeData().skills : nullptr;
                const float threshold = (skills && skills->data) ? skills->data->levelThreshold : -1.0f;

                logger::info("[EA] TrackedStat: Level Increases = {} | pre-finalization level={} threshold={:.1f} (diagnostic only).",
                    event->value,
                    player ? static_cast<int>(player->GetLevel()) : -1,
                    threshold);

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
        }
    };

    // -----------------------------------------------------------------------
    // KILL SINK — ActorKill::Event
    // -----------------------------------------------------------------------
    struct OnActorKill : public RE::BSTEventSink<RE::ActorKill::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::ActorKill::Event* event,
            RE::BSTEventSource<RE::ActorKill::Event>*) override
        {
            if (!event || !event->killer || !event->victim)
                return RE::BSEventNotifyControl::kContinue;

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player)
                return RE::BSEventNotifyControl::kContinue;

            auto* killer = event->killer;
            auto* dying = event->victim;
            const auto commander = killer->GetCommandingActor();
            const auto victimCommander = dying->GetCommandingActor();
            const bool playerCredited = killer == player || commander.get() == player;
            const bool victimIsPlayer = dying->IsPlayerRef();
            const bool victimCommandedByPlayer = victimCommander.get() == player;
            if (!RewardRules::ShouldRewardKill(playerCredited, victimIsPlayer, victimCommandedByPlayer)) {
                logger::debug("[EA] Kill reward: '{}' killed by '{}' skipped (playerCredited={} victimIsPlayer={} playerMinion={}).",
                    dying->GetName(), killer->GetName(), playerCredited, victimIsPlayer, victimCommandedByPlayer);
                return RE::BSEventNotifyControl::kContinue;
            }

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

            const int playerLevel = static_cast<int>(player->GetLevel());
            const int enemyLevel = static_cast<int>(dying->GetLevel());
            const float totalXP = RewardRules::CalculateKillReward(
                baseXP,
                enemyLevel,
                playerLevel,
                Config::xpKillLevelScaleFactor,
                Config::xpKillGlobalMultiplier);

            XPManager::AwardXP(totalXP,
                XPManager::MakeKillContext(dying->GetName(), dying->GetFormID(), enemyLevel, typeName));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    struct OnLevelIncrease : public RE::BSTEventSink<RE::LevelIncrease::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::LevelIncrease::Event*                  event,
            RE::BSTEventSource<RE::LevelIncrease::Event>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!event || !player || event->player != player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // Do not write the threshold here. The engine subtracts the
            // current threshold from native XP only when the LevelUp Menu
            // completes; changing it now makes that subtraction use the next
            // level's value (120 XP at threshold 100 became -5, not 20).
            logger::info("[EA] Level increase to {}; threshold refresh deferred until the LevelUp Menu closes.",
                event->newLevel);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // Applies the capped threshold after the engine has finished the level-up
    // (XP overflow carried and its own threshold recalculated). The interim
    // close of an intercepted LevelUp Menu is skipped.
    struct OnLevelUpMenu : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent*                  event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!event || event->opening || event->menuName != RE::LevelUpMenu::MENU_NAME) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (SkillMenu::IsDeferringVanillaLevelUp()) {
                logger::debug("[EA] LevelUp Menu interim close during skill allocation; threshold unchanged.");
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto level = player ? static_cast<std::uint32_t>(player->GetLevel()) : 0u;
            Leveling::QueueThresholdRefresh(level, "level-up-menu-closed");
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // QUEST SINK — QuestStatus::Event lifecycle transitions
    // -----------------------------------------------------------------------
    struct OnQuestStatus : public RE::BSTEventSink<RE::QuestStatus::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::QuestStatus::Event*                  event,
            RE::BSTEventSource<RE::QuestStatus::Event>*) override
        {
            if (!event || !event->quest) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* quest = event->quest;
            const auto questID = quest->GetFormID();
            if (event->status == RE::QuestStatus::kStarted) {
                XPManager::ObserveQuestStatus(questID, RewardRules::QuestSignal::kStarted);
                logger::debug("[EA] Quest lifecycle: {:08X} started; completion guard rearmed.", questID);
                return RE::BSEventNotifyControl::kContinue;
            }
            if (event->status == RE::QuestStatus::kReseted) {
                XPManager::ObserveQuestStatus(questID, RewardRules::QuestSignal::kReset);
                logger::debug("[EA] Quest lifecycle: {:08X} reset; completion guard rearmed.", questID);
                return RE::BSEventNotifyControl::kContinue;
            }
            if (event->status != RE::QuestStatus::kCompleted ||
                !XPManager::ObserveQuestStatus(questID, RewardRules::QuestSignal::kCompleted)) {
                logger::debug("[EA] Quest lifecycle: duplicate completion for {:08X} skipped.", questID);
                return RE::BSEventNotifyControl::kContinue;
            }

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

            XPManager::AwardXP(xp,
                XPManager::MakeQuestContext(quest->GetName(), questID, typeName));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // Exact objective, pickpocket, and lock-attempt event sources.
    // -----------------------------------------------------------------------
    struct OnObjectiveState : public RE::BSTEventSink<RE::ObjectiveState::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::ObjectiveState::Event*                  event,
            RE::BSTEventSource<RE::ObjectiveState::Event>*) override
        {
            if (!event || !event->objective || !event->objective->ownerQuest) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* objective = event->objective;
            if (objective->ownerQuest->GetType() != RE::QUEST_DATA::Type::kMiscellaneous ||
                !RewardRules::IsObjectiveCompletionTransition(
                    static_cast<std::uint8_t>(event->oldState),
                    static_cast<std::uint8_t>(event->newState))) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto text = objective->displayText.c_str();
            const auto subject = (text && text[0]) ? text : "Misc Objective";
            XPManager::AwardXP(Config::xpQuestObjectives,
                XPManager::MakeStatContext(subject, "quest_objectives", objective->index, "misc_objective"));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    struct OnItemsPickpocketed : public RE::BSTEventSink<RE::ItemsPickpocketed::Event> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::ItemsPickpocketed::Event*                  event,
            RE::BSTEventSource<RE::ItemsPickpocketed::Event>*) override
        {
            if (!event || !RewardRules::ShouldRewardPickpocket(event->numItems)) {
                return RE::BSEventNotifyControl::kContinue;
            }

            XPManager::AwardXP(Config::xpPickpocketBase,
                XPManager::MakeStatContext("Items Pickpocketed", "pickpocket", event->numItems, "pickpocket"));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    struct OnLockpickingMenu : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent*                  event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!event || event->menuName != RE::LockpickingMenu::MENU_NAME) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (event->opening) {
                auto target = RE::LockpickingMenu::GetTargetReference();
                if (!target) {
                    s_lockAttempt.reset();
                    logger::warn("[EA] Lock reward: Lockpicking Menu opened without a target.");
                    return RE::BSEventNotifyControl::kContinue;
                }

                s_lockAttempt = LockAttempt{
                    target->GetFormID(),
                    static_cast<std::int32_t>(target->GetLockLevel())
                };
                logger::debug("[EA] Lock reward: captured target={:08X}, raw tier={}.",
                    s_lockAttempt->targetID, s_lockAttempt->lockLevel);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (!s_lockAttempt) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto targetID = s_lockAttempt->targetID;
            const auto generation = XPManager::GetRewardGeneration();
            auto* task = SKSE::GetTaskInterface();
            if (!task) {
                logger::warn("[EA] Lock reward: task interface unavailable; clearing closed-menu context for {:08X}.", targetID);
                s_lockAttempt.reset();
                return RE::BSEventNotifyControl::kContinue;
            }

            task->AddTask([targetID, generation]() {
                if (XPManager::GetRewardGeneration() == generation &&
                    s_lockAttempt && s_lockAttempt->targetID == targetID) {
                    logger::debug("[EA] Lock reward: abandoned attempt for {:08X} cleared after menu close.", targetID);
                    s_lockAttempt.reset();
                }
            });
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
    static OnLevelIncrease s_levelIncreaseSink;
    static OnQuestStatus s_questSink;
    static OnObjectiveState s_objectiveSink;
    static OnItemsPickpocketed s_pickpocketSink;
    static OnLockpickingMenu s_lockpickingMenuSink;
    static OnLevelUpMenu s_levelUpMenuSink;

    void ResetRewardState() {
        XPManager::ResetRewardGuards();
        s_lockAttempt.reset();
        s_lastLockCounter.reset();
        s_clearedLocations.Invalidate();
        logger::debug("[EA] Transient reward state reset.");
    }

    void SnapshotClearedLocations(std::string_view reason) {
        const auto everCleared = CollectEverClearedLocations();
        s_clearedLocations.Snapshot(everCleared);
        logger::info("[EA] Location clear: snapshot of {} ever-cleared locations taken (reason={}).",
            everCleared.size(), reason);
    }

    void Register() {
        if (s_registered) {
            logger::debug("[EA] EventSinks: registration already completed.");
            return;
        }
        auto* src = RE::ScriptEventSourceHolder::GetSingleton();
        if (!src) {
            logger::error("[EA] EventSinks: ScriptEventSourceHolder is null.");
            return;
        }

        logger::info("[EA] EventSinks: Registering sinks...");

        auto* discoverySrc = RE::LocationDiscovery::GetEventSource();
        if (discoverySrc) {
            discoverySrc->AddEventSink(&s_locationDiscoverySink);
            logger::info("[EA] EventSinks: [1/9] LocationDiscovery event sink registered.");
        } else {
            logger::error("[EA] EventSinks: LocationDiscovery event source is null.");
        }

        auto* clearedSrc = RE::LocationCleared::GetEventSource();
        if (clearedSrc) {
            clearedSrc->AddEventSink(&s_locationClearedSink);
            logger::info("[EA] EventSinks: [2/9] LocationCleared event sink registered.");
        } else {
            logger::error("[EA] EventSinks: LocationCleared event source is null.");
        }

        if (auto* trackedSrc = src->GetEventSource<RE::TESTrackedStatsEvent>()) {
            trackedSrc->AddEventSink(&s_trackedStatsSink);
            logger::info("[EA] EventSinks: [3/9] TESTrackedStatsEvent registered.");
        } else {
            logger::error("[EA] EventSinks: TESTrackedStatsEvent source is null.");
        }

        if (auto* killSrc = RE::ActorKill::GetEventSource()) {
            killSrc->AddEventSink(&s_killSink);
            logger::info("[EA] EventSinks: [4/9] ActorKill::Event registered.");
        } else {
            logger::error("[EA] EventSinks: ActorKill event source is null.");
        }

        auto* levelIncreaseSrc = RE::LevelIncrease::GetEventSource();
        if (levelIncreaseSrc) {
            levelIncreaseSrc->AddEventSink(&s_levelIncreaseSink);
            logger::info("[EA] EventSinks: [5/9] LevelIncrease::Event registered.");
        } else {
            logger::error("[EA] EventSinks: LevelIncrease event source is null.");
        }

        if (auto* questSrc = RE::QuestStatus::GetEventSource()) {
            questSrc->AddEventSink(&s_questSink);
            logger::info("[EA] EventSinks: [6/9] QuestStatus::Event registered.");
        } else {
            logger::error("[EA] EventSinks: QuestStatus event source is null.");
        }

        if (auto* objectiveSrc = RE::ObjectiveState::GetEventSource()) {
            objectiveSrc->AddEventSink(&s_objectiveSink);
            logger::info("[EA] EventSinks: [7/9] ObjectiveState::Event registered.");
        } else {
            logger::error("[EA] EventSinks: ObjectiveState event source is null.");
        }

        if (auto* pickpocketSrc = RE::ItemsPickpocketed::GetEventSource()) {
            pickpocketSrc->AddEventSink(&s_pickpocketSink);
            logger::info("[EA] EventSinks: [8/9] ItemsPickpocketed::Event registered.");
        } else {
            logger::error("[EA] EventSinks: ItemsPickpocketed event source is null.");
        }

        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink(&s_lockpickingMenuSink);
            logger::info("[EA] EventSinks: [9/9] Lockpicking Menu event sink registered.");
            ui->AddEventSink(&s_levelUpMenuSink);
            logger::info("[EA] EventSinks: LevelUp Menu close sink registered for threshold refresh.");
        } else {
            logger::error("[EA] EventSinks: UI singleton is null; lock context unavailable.");
        }

        logger::warn("[EA] EventSinks: TESActorValueChangeEvent sink SKIPPED - "
                     "struct not defined in this CommonLibSSE-NG build. "
                     "Attribute selection will not be logged.");

        logger::warn("[EA] EventSinks: TESPerkEntryRunEvent sink SKIPPED - "
                     "struct forward-declared only, no field definitions available. "
                     "Perk selection will not be logged.");

        s_registered = true;
        logger::info("[EA] EventSinks: registration pass completed (9 expected, 2 diagnostic sinks skipped).");
    }
}
