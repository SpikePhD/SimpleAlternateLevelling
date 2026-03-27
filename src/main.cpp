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

        // Set up spdlog with the timestamped file
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto log  = std::make_shared<spdlog::logger>("EA", std::move(sink));
        log->set_level(spdlog::level::trace);
        log->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(std::move(log));

        // Log rotation: delete oldest EA session logs if over the limit
        int maxFiles = ReadMaxLogFiles();
        if (maxFiles > 0) {
            std::vector<std::filesystem::path> logFiles;
            for (const auto& entry : std::filesystem::directory_iterator(spikeDir)) {
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
        }
    }

    // -----------------------------------------------------------------------
    // Cosave callbacks
    // -----------------------------------------------------------------------

    constexpr std::uint32_t kEASaveID  = 'EAXP';
    constexpr std::uint32_t kEAVersion = 2;  // v2: adds trackedLevel + pendingLevelUps

    void OnGameSave(SKSE::SerializationInterface* intfc) {
        if (!intfc->OpenRecord(kEASaveID, kEAVersion)) {
            logger::error("[EA] Cosave: Failed to open write record.");
            return;
        }
        float xp      = EA::XPManager::GetCurrentXP();
        int   level   = EA::XPManager::GetTrackedLevel();
        int   pending = EA::XPManager::GetPendingLevelUps();

        intfc->WriteRecordData(&xp,      sizeof(xp));
        intfc->WriteRecordData(&level,   sizeof(level));
        intfc->WriteRecordData(&pending, sizeof(pending));

        logger::info("[EA] Cosave: Saved XP={:.1f}, trackedLevel={}, pending={}.",
                     xp, level, pending);
    }

    void OnGameLoad(SKSE::SerializationInterface* intfc) {
        // Reset guards on every load — FormIDs from the previous session
        // are invalid in the new save's worldspace.
        EA::XPManager::ResetKillGuard();
        EA::XPManager::ResetQuestGuard();
        EA::XPManager::SetLevelUpInProgress(false);

        std::uint32_t type, version, length;
        while (intfc->GetNextRecordInfo(type, version, length)) {
            if (type == kEASaveID) {
                float xp      = 0.0f;
                int   level   = 1;
                int   pending = 0;

                intfc->ReadRecordData(&xp, sizeof(xp));

                if (version >= 2) {
                    intfc->ReadRecordData(&level,   sizeof(level));
                    intfc->ReadRecordData(&pending, sizeof(pending));
                } else {
                    // Upgrading from v1 cosave: initialize level from the game.
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    level = player ? static_cast<int>(player->GetLevel()) : 1;
                }

                EA::XPManager::SetCurrentXP(xp);
                EA::XPManager::SetTrackedLevel(level);
                EA::XPManager::SetPendingLevelUps(pending);

                logger::info("[EA] Cosave: Loaded XP={:.1f}, trackedLevel={}, pending={}.",
                             xp, level, pending);

                // Restart the chain if level-ups were pending at save time.
                // FirePendingLevelUp triggers TriggerLevelUp once; the engine then
                // fires "Level Increases" which chains the rest via EventSinks.
                if (EA::XPManager::GetPendingLevelUps() > 0) {
                    logger::info("[EA] Cosave: {} pending level-up(s) found on load. "
                                 "Resuming chain.",
                                 EA::XPManager::GetPendingLevelUps());
                    EA::XPManager::FirePendingLevelUp();
                }
            } else {
                logger::warn("[EA] Cosave: Unknown record type {:#010x} — skipped.", type);
            }
        }
    }

    void OnGameRevert(SKSE::SerializationInterface*) {
        EA::XPManager::SetCurrentXP(0.0f);
        EA::XPManager::SetTrackedLevel(1);
        EA::XPManager::SetPendingLevelUps(0);
        EA::XPManager::SetLevelUpInProgress(false);
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

        // Sync our level tracker with the game's current level.
        // This is the correct starting point for a fresh new game.
        // For a loaded save, OnGameLoad fires shortly after and overwrites this.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            int gameLevel = static_cast<int>(player->GetLevel());
            EA::XPManager::SetTrackedLevel(gameLevel);
            logger::info("[EA] OnDataLoaded: s_trackedLevel initialized to {}.", gameLevel);
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
