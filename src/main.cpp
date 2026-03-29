#include "PCH.h"
#include "Config.h"
#include "SkillHook.h"
#include "SkillMenu.h"
#include "EventSinks.h"
#include "XPManager.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

namespace {

    // Returns the absolute path to the DLL's own directory (.../Data/SKSE/Plugins/).
    std::filesystem::path GetPluginsDir() {
        wchar_t buf[REX::W32::MAX_PATH] = {};
        REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buf, REX::W32::MAX_PATH);
        return std::filesystem::path(buf).parent_path();
    }

    // Reads ONLY max_log_files from JSON before the logger exists.
    // On any failure returns the default (10) silently.
    int ReadMaxLogFiles() {
        try {
            auto configPath = GetPluginsDir() / "SimpleAlternateLevelling.json";
            std::ifstream file(configPath);
            if (!file.is_open()) return 10;
            nlohmann::json j;
            file >> j;
            if (j.contains("debug") && j["debug"].is_object() &&
                j["debug"].contains("max_log_files") &&
                j["debug"]["max_log_files"].is_number_integer()) {
                return j["debug"]["max_log_files"].get<int>();
            }
        } catch (...) {}
        return 10;
    }

    void InitializeLog() {
        auto logDir = logger::log_directory();
        if (!logDir) {
            SKSE::stl::report_and_fail("Failed to find SKSE log directory."sv);
        }

        // Create .../SKSE/Spike/ subdirectory
        auto spikeDir = *logDir / "Spike";
        std::filesystem::create_directories(spikeDir);

        // Build timestamped filename: SimpleAlternateLevelling_2026-03-27_10-26-21.log
        auto now  = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream ts;
        ts << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
        auto logFilename = std::format("SimpleAlternateLevelling_{}.log", ts.str());
        auto logPath     = spikeDir / logFilename;

        // Mirror sink: project Logs folder for direct access during development
        auto projectLogDir = std::filesystem::path(
            "C:/Users/lucac/Documents/MyProjects/Experience and Attributes/Logs");
        std::filesystem::create_directories(projectLogDir);
        auto projectLogPath = projectLogDir / logFilename;

        // Combine both sinks into a dist_sink
        auto sink1    = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(),        true);
        auto sink2    = std::make_shared<spdlog::sinks::basic_file_sink_mt>(projectLogPath.string(), true);
        auto distSink = std::make_shared<spdlog::sinks::dist_sink_mt>();
        distSink->add_sink(sink1);
        distSink->add_sink(sink2);

        auto log = std::make_shared<spdlog::logger>("EA", distSink);
        log->set_level(spdlog::level::trace);
        log->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(std::move(log));

        // Log rotation: delete oldest EA session logs if over the limit.
        // Applied to both the SKSE Spike folder and the project Logs folder.
        int maxFiles = ReadMaxLogFiles();
        auto rotateDir = [&](const std::filesystem::path& dir) {
            if (maxFiles <= 0) return;
            std::vector<std::filesystem::path> logFiles;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                auto name = entry.path().filename().string();
                if (name.starts_with("SimpleAlternateLevelling_") && name.ends_with(".log")) {
                    logFiles.push_back(entry.path());
                }
            }
            // Lexicographic sort = chronological (YYYY-MM-DD_HH-MM-SS format)
            std::sort(logFiles.begin(), logFiles.end());
            while (static_cast<int>(logFiles.size()) > maxFiles) {
                std::error_code ec;
                std::filesystem::remove(logFiles.front(), ec);
                logFiles.erase(logFiles.begin());
            }
        };
        rotateDir(spikeDir);
        rotateDir(projectLogDir);
    }

    // -----------------------------------------------------------------------
    // New-game skill reset
    // -----------------------------------------------------------------------

    // Set true on kNewGame, cleared when CharCreateWatcher fires.
    // Guards against kPostLoadGame (which fires before RaceMenu) or mid-game
    // showracemenu console calls.
    static bool s_awaitingCharCreate = false;

    // Persisted in cosave (v5). True after NormalizeSkills() has run for this
    // character. Prevents the normalization from re-running on every load.
    static bool s_skillsNormalized = false;
    static bool s_normalizeTaskQueued = false;

    static bool IsCreationMenuOpen() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        return ui->IsMenuOpen("RaceSex Menu") || ui->IsMenuOpen("RaceMenu");
    }

    static std::vector<RE::ActorValue> GetSkillActorValues() {
        std::vector<RE::ActorValue> skills;
        auto* avList = RE::ActorValueList::GetSingleton();
        if (!avList) {
            logger::warn("[EA] NormalizeSkills: ActorValueList is null.");
            return skills;
        }

        const auto total = static_cast<int>(RE::ActorValue::kTotal);
        skills.reserve(18);
        for (int i = 0; i < total; ++i) {
            auto  av   = static_cast<RE::ActorValue>(i);
            auto* info = avList->GetActorValue(av);
            if (!info || !info->skill) {
                continue;
            }
            skills.push_back(av);
        }

        return skills;
    }

    static void NormalizeSkills() {
        if (!EA::Config::resetSkillsOnNewGame) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("[EA] NormalizeSkills: PlayerCharacter is null.");
            return;
        }

        auto* avo = static_cast<RE::Actor*>(player)->AsActorValueOwner();
        auto  skills = GetSkillActorValues();
        if (skills.empty()) {
            logger::warn("[EA] NormalizeSkills: no skill actor values were discovered.");
            return;
        }

        auto* avList = RE::ActorValueList::GetSingleton();
        logger::info("[EA] NormalizeSkills: reading skill values before reset:");
        for (auto av : skills) {
            auto* info = avList ? avList->GetActorValue(av) : nullptr;
            const char* name = (info && info->fullName.data() && info->fullName.data()[0])
                ? info->fullName.data()
                : "???";

            float current = avo->GetBaseActorValue(av);
            logger::info("[EA]   skill '{}' ({}) = {:.1f}", name, static_cast<int>(av), current);
            if (current != 0.0f) {
                avo->SetBaseActorValue(av, current - current);
            }

            float residual = avo->GetActorValue(av);
            if (residual != 0.0f) {
                avo->ModActorValue(av, -residual);
            }
        }
        s_skillsNormalized = true;
        logger::info("[EA] NormalizeSkills: all discovered skills set to 0 (one-time reset done).");
    }

    static void QueueNormalizeSkillsWhenReady() {
        if (!EA::Config::resetSkillsOnNewGame || !s_awaitingCharCreate || s_skillsNormalized || s_normalizeTaskQueued) {
            return;
        }

        s_normalizeTaskQueued = true;
        SKSE::GetTaskInterface()->AddTask([]() {
            s_normalizeTaskQueued = false;

            if (!EA::Config::resetSkillsOnNewGame || !s_awaitingCharCreate || s_skillsNormalized) {
                return;
            }

            if (IsCreationMenuOpen()) {
                QueueNormalizeSkillsWhenReady();
                return;
            }

            s_awaitingCharCreate = false;
            logger::info("[EA] RaceSex/RaceMenu closed on new game â€” normalizing skills now.");
            NormalizeSkills();
        });
    }

    // Watches for RaceMenu closing on a new game and queues NormalizeSkills.
    // s_awaitingCharCreate is set on kNewGame and is the primary guard against
    // mid-game showracemenu calls. s_skillsNormalized is the secondary guard
    // against re-entry on subsequent loads of the same character.
    struct CharCreateWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent*              event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!EA::Config::resetSkillsOnNewGame) return RE::BSEventNotifyControl::kContinue;
            if (!event) return RE::BSEventNotifyControl::kContinue;

            // Support both vanilla ("RaceSex Menu") and modded ("RaceMenu") installs.
            if (event->opening ||
                (event->menuName != "RaceSex Menu" && event->menuName != "RaceMenu"))
                return RE::BSEventNotifyControl::kContinue;

            if (s_awaitingCharCreate && !s_skillsNormalized) {
                logger::info("[EA] Menu '{}' closed during character creation - checking whether skills can be normalized.",
                    event->menuName.c_str());
                QueueNormalizeSkillsWhenReady();
            }
#if 0
            if (s_awaitingCharCreate && !s_skillsNormalized) {
                s_awaitingCharCreate = false;
                logger::info("[EA] RaceMenu closed on new game — queuing NormalizeSkills.");
                // Chain two AddTask calls to defer normalization by 2 game frames,
                // ensuring all post-RaceMenu race/attribute application is complete.
                SKSE::GetTaskInterface()->AddTask([]() {
                    SKSE::GetTaskInterface()->AddTask(NormalizeSkills);
                });
            }
#endif
            return RE::BSEventNotifyControl::kContinue;
        }
    };
    static CharCreateWatcher s_charCreateWatcher;

    // -----------------------------------------------------------------------
    // Cosave callbacks
    // -----------------------------------------------------------------------

    constexpr std::uint32_t kEASaveID  = 'EAXP';
    constexpr std::uint32_t kEAVersion = 5;  // v5: xp + pendingSkillPoints + skillsNormalized

    void OnGameSave(SKSE::SerializationInterface* intfc) {
        if (!intfc->OpenRecord(kEASaveID, kEAVersion)) {
            logger::error("[EA] Cosave: Failed to open write record.");
            return;
        }
        float         xp                = EA::XPManager::GetCurrentXP();
        int           pendingSkillPoints = EA::XPManager::GetPendingSkillPoints();
        std::uint8_t  normalized         = s_skillsNormalized ? 1u : 0u;
        intfc->WriteRecordData(&xp,                sizeof(xp));
        intfc->WriteRecordData(&pendingSkillPoints, sizeof(pendingSkillPoints));
        intfc->WriteRecordData(&normalized,         sizeof(normalized));
        logger::info("[EA] Cosave: Saved XP={:.1f}, pendingSkillPoints={}, skillsNormalized={}.",
            xp, pendingSkillPoints, s_skillsNormalized);
    }

    void OnGameLoad(SKSE::SerializationInterface* intfc) {
        // Reset guards on every load — FormIDs from the previous session
        // are invalid in the new save's worldspace.
        EA::XPManager::ResetKillGuard();
        EA::XPManager::ResetBookGuard();
        EA::XPManager::ResetQuestGuard();

        std::uint32_t type, version, length;
        while (intfc->GetNextRecordInfo(type, version, length)) {
            if (type == kEASaveID) {
                float xp               = 0.0f;
                int   pendingSkillPts  = 0;

                intfc->ReadRecordData(&xp, sizeof(xp));

                if (version >= 4) {
                    intfc->ReadRecordData(&pendingSkillPts, sizeof(pendingSkillPts));
                }
                // v1/v2/v3 cosaves: pendingSkillPoints defaults to 0 (no carry-over).

                std::uint8_t normalized = 0u;
                if (version >= 5) {
                    intfc->ReadRecordData(&normalized, sizeof(normalized));
                }
                // v1–v4 cosaves: skillsNormalized defaults to false — first load will
                // normalize skills if reset_skills_on_new_game is enabled.

                EA::XPManager::SetCurrentXP(xp);
                EA::XPManager::SetPendingSkillPoints(pendingSkillPts);
                s_skillsNormalized = (normalized != 0u);

                // Restore the XP into the engine's bucket and recalculate the
                // threshold for the current level using our formula.
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player) {
                    auto* skills = player->GetInfoRuntimeData().skills;
                    if (skills && skills->data) {
                        skills->data->xp = xp;
                        int   level        = static_cast<int>(player->GetLevel());
                        float newThreshold = std::min(EA::Config::xpCap,
                            EA::Config::xpBase + static_cast<float>(level) * EA::Config::xpIncrease);
                        skills->data->levelThreshold = newThreshold;
                        logger::info("[EA] Cosave: levelThreshold set to {:.1f} for level {}.",
                                     newThreshold, level);
                    }
                }

                logger::info("[EA] Cosave: Loaded XP={:.1f}, pendingSkillPoints={}, skillsNormalized={}.",
                    xp, pendingSkillPts, s_skillsNormalized);
            } else {
                logger::warn("[EA] Cosave: Unknown record {:#010x} — skipped.", type);
            }
        }
    }

    void OnGameRevert(SKSE::SerializationInterface*) {
        EA::XPManager::SetCurrentXP(0.0f);
        EA::XPManager::SetPendingSkillPoints(0);
        EA::XPManager::ResetKillGuard();
        EA::XPManager::ResetBookGuard();
        EA::XPManager::ResetQuestGuard();
        s_skillsNormalized   = false;
        s_awaitingCharCreate = false;
        logger::info("[EA] Cosave: Reverted — all state reset.");
    }

    // -----------------------------------------------------------------------
    // kDataLoaded callback — all hooks and sinks registered here
    // -----------------------------------------------------------------------
    void OnDataLoaded() {
        EA::SkillHook::Install();
        EA::EventSinks::Register();
        EA::SkillMenu::Register();
        logger::info("[EA] SkillMenu registered.");

        // Set vanilla leveling game settings to match our config curve.
        // The engine uses: threshold = fXPLevelUpBase + (level * fXPLevelUpMult)
        // This is identical to our old formula — now the engine computes it natively.
        auto* settings = RE::GameSettingCollection::GetSingleton();
        if (settings) {
            auto* base = settings->GetSetting("fXPLevelUpBase");
            auto* mult = settings->GetSetting("fXPLevelUpMult");
            if (base) {
                base->data.f = EA::Config::xpBase;
                logger::info("[EA] OnDataLoaded: fXPLevelUpBase set to {:.1f}.", EA::Config::xpBase);
            } else {
                logger::warn("[EA] OnDataLoaded: fXPLevelUpBase not found in GameSettingCollection.");
            }
            if (mult) {
                mult->data.f = EA::Config::xpIncrease;
                logger::info("[EA] OnDataLoaded: fXPLevelUpMult set to {:.1f}.", EA::Config::xpIncrease);
            } else {
                logger::warn("[EA] OnDataLoaded: fXPLevelUpMult not found in GameSettingCollection.");
            }
            // Block character XP from skill rank-ups (skill books, trainers).
            // When a skill ranks up the engine calls UseSkill() which awards
            // newLevel * fXPPerSkillRank to skills->data->xp. Setting this to
            // 0 makes every rank-up contribute 0 character XP so only our
            // explicit AwardXP calls feed the level bucket.
            auto* perRank = settings->GetSetting("fXPPerSkillRank");
            if (perRank) {
                perRank->data.f = 0.0f;
                logger::info("[EA] OnDataLoaded: fXPPerSkillRank set to 0.0 (blocks skill-rank character XP).");
            } else {
                logger::warn("[EA] OnDataLoaded: fXPPerSkillRank not found — skill-book/trainer XP may leak.");
            }
        } else {
            logger::warn("[EA] OnDataLoaded: GameSettingCollection is null — leveling curve NOT applied.");
        }

        // Recalculate and write levelThreshold for the current level.
        //
        // skills->data->levelThreshold is baked at character creation using the
        // vanilla game settings active at that time. Setting fXPLevelUpBase/Mult
        // above only affects the engine's NEXT threshold calculation (after a
        // level-up) — it does not retroactively update the stored threshold.
        // We must write it directly so the engine uses our formula from the start.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto* skills = player->GetInfoRuntimeData().skills;
            if (skills && skills->data) {
                int   level        = static_cast<int>(player->GetLevel());
                float newThreshold = std::min(EA::Config::xpCap,
                    EA::Config::xpBase + static_cast<float>(level) * EA::Config::xpIncrease);
                skills->data->levelThreshold = newThreshold;
                logger::info("[EA] OnDataLoaded: levelThreshold set to {:.1f} for level {}.",
                             newThreshold, level);
            }
        }

        // Register CharCreateWatcher unconditionally; ProcessEvent checks config flag at runtime.
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink(&s_charCreateWatcher);
            logger::info("[EA] OnDataLoaded: CharCreateWatcher registered.");
            if (s_awaitingCharCreate && !s_skillsNormalized) {
                logger::info("[EA] OnDataLoaded: kNewGame already armed - checking whether creation menus are still open.");
                QueueNormalizeSkillsWhenReady();
            }
        } else {
            logger::warn("[EA] OnDataLoaded: UI singleton null — CharCreateWatcher NOT registered.");
        }

        logger::info("[EA] All systems initialised and ready.");
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    InitializeLog();
    logger::info("[EA] SimpleAlternateLevelling loaded successfully. Version 0.1.0");

    SKSE::Init(a_skse);

    // Register messaging listener — hooks must wait for kDataLoaded
    auto* messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            OnDataLoaded();
        }
        if (msg->type == SKSE::MessagingInterface::kNewGame) {
            // New character — arm the CharCreateWatcher to fire on RaceMenu close.
            s_awaitingCharCreate = true;
            s_skillsNormalized   = false;
            s_normalizeTaskQueued = false;
            logger::info("[EA] kNewGame: awaiting RaceMenu close to normalize skills.");
            QueueNormalizeSkillsWhenReady();
        }
        // kPostLoadGame: no skill-reset logic here.
        // CharCreateWatcher fires before any save exists, so kPostLoadGame is
        // not involved in the new-game skill reset path.
    });

    // Register cosave serialization
    auto* serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID(kEASaveID);
    serialization->SetSaveCallback(OnGameSave);
    serialization->SetLoadCallback(OnGameLoad);
    serialization->SetRevertCallback(OnGameRevert);
    logger::info("[EA] Cosave serialization registered.");

    EA::Config::Load();  // Config loads immediately; hooks wait for kDataLoaded

    return true;
}
