#include "PCH.h"
#include "Config.h"
#include "SkillHook.h"
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
            auto configPath = GetPluginsDir() / "ExperienceAndAttributes.json";
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

        // Build timestamped filename: ExperienceAndAttributes_2026-03-27_10-26-21.log
        auto now  = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream ts;
        ts << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
        auto logFilename = std::format("ExperienceAndAttributes_{}.log", ts.str());
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
                if (name.starts_with("ExperienceAndAttributes_") && name.ends_with(".log")) {
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
    // Cosave callbacks
    // -----------------------------------------------------------------------

    constexpr std::uint32_t kEASaveID  = 'EAXP';
    constexpr std::uint32_t kEAVersion = 3;  // v3: xp only (trackedLevel + pendingLevelUps removed)

    void OnGameSave(SKSE::SerializationInterface* intfc) {
        if (!intfc->OpenRecord(kEASaveID, kEAVersion)) {
            logger::error("[EA] Cosave: Failed to open write record.");
            return;
        }
        float xp = EA::XPManager::GetCurrentXP();
        intfc->WriteRecordData(&xp, sizeof(xp));
        logger::info("[EA] Cosave: Saved XP={:.1f}.", xp);
    }

    void OnGameLoad(SKSE::SerializationInterface* intfc) {
        // Reset guards on every load — FormIDs from the previous session
        // are invalid in the new save's worldspace.
        EA::XPManager::ResetKillGuard();
        EA::XPManager::ResetQuestGuard();

        std::uint32_t type, version, length;
        while (intfc->GetNextRecordInfo(type, version, length)) {
            if (type == kEASaveID) {
                float xp = 0.0f;
                intfc->ReadRecordData(&xp, sizeof(xp));
                // v1/v2 cosaves had additional fields (trackedLevel, pendingLevelUps)
                // after xp — they are intentionally not read; the engine is now
                // authoritative for level state.

                EA::XPManager::SetCurrentXP(xp);

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

                logger::info("[EA] Cosave: Loaded XP={:.1f}.", xp);
            } else {
                logger::warn("[EA] Cosave: Unknown record {:#010x} — skipped.", type);
            }
        }
    }

    void OnGameRevert(SKSE::SerializationInterface*) {
        EA::XPManager::SetCurrentXP(0.0f);
        EA::XPManager::ResetKillGuard();
        EA::XPManager::ResetQuestGuard();
        logger::info("[EA] Cosave: Reverted — all state reset.");
    }

    // -----------------------------------------------------------------------
    // kDataLoaded callback — all hooks and sinks registered here
    // -----------------------------------------------------------------------
    void OnDataLoaded() {
        EA::SkillHook::Install();
        EA::EventSinks::Register();

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

        logger::info("[EA] All systems initialised and ready.");
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    InitializeLog();
    logger::info("[EA] ExperienceAndAttributes loaded successfully. Version 0.1.0");

    SKSE::Init(a_skse);

    // Register messaging listener — hooks must wait for kDataLoaded
    auto* messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            OnDataLoaded();
        }
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
