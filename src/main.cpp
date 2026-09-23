#include "PCH.h"
#include "Config.h"
#include "SkillHook.h"
#include "SkillMenu.h"
#include "SettingsMenu.h"
#include "SettingsModel.h"
#include "EventSinks.h"
#include "XPManager.h"
#include "Leveling.h"
#include "Progression.h"
#include "LogPolicy.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <atomic>

namespace {

#ifndef SAL_VERSION
#define SAL_VERSION "unknown"
#endif

    // Returns the absolute path to the DLL's own directory (.../Data/SKSE/Plugins/).
    std::filesystem::path GetPluginsDir() {
        wchar_t buf[260] = {};
        REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buf, static_cast<std::uint32_t>(std::size(buf)));
        return std::filesystem::path(buf).parent_path();
    }

    struct BootstrapLogConfig {
        bool verbose{ false };
        int  maxLogFiles{ EA::LogPolicy::kDefaultMaxLogFiles };
    };

    // Reads only the fields required before the logger exists. Full config
    // loading and diagnostic warnings happen after logging is initialized.
    BootstrapLogConfig ReadBootstrapLogConfig() {
        BootstrapLogConfig config;
        try {
            auto configPath = GetPluginsDir() / "SimpleAlternateLevelling.json";
            std::ifstream file(configPath);
            if (!file.is_open()) return config;
            nlohmann::json j;
            file >> j;
            auto applyDebug = [&](const nlohmann::json& source) {
                if (!source.is_object() || !source.contains("debug") || !source["debug"].is_object()) return;
                const auto& debug = source["debug"];
                if (debug.contains("verbose") && debug["verbose"].is_boolean())
                    config.verbose = debug["verbose"].get<bool>();
                if (debug.contains("max_log_files") && debug["max_log_files"].is_number()) {
                    try {
                        if (const auto parsed = EA::LogPolicy::ParseMaxLogFiles(debug["max_log_files"].get<double>()))
                            config.maxLogFiles = *parsed;
                    } catch (const nlohmann::json::exception&) {}
                }
            };
            applyDebug(j);
            std::ifstream userFile(GetPluginsDir() / "SimpleAlternateLevelling.user.json");
            if (userFile) {
                try {
                    nlohmann::json user;
                    userFile >> user;
                    if (!user.contains("config_version") ||
                        (user["config_version"].is_number_integer() &&
                         user["config_version"] <= EA::SettingsModel::kSchemaVersion))
                        applyDebug(user);
                } catch (const nlohmann::json::exception&) {}
            }
        } catch (...) {}
        return config;
    }

    void InitializeLog() {
        auto logDir = logger::log_directory();
        if (!logDir) {
            SKSE::stl::report_and_fail("Failed to find SKSE log directory."sv);
        }

        const auto bootstrap = ReadBootstrapLogConfig();

        // Build timestamped filename: SimpleAlternateLevelling_2026-03-27_10-26-21.log
        auto now  = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream ts;
        ts << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
        auto logFilename = std::format("SimpleAlternateLevelling_{}.log", ts.str());
        const auto logPath = *logDir / logFilename;

        try {
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
            auto log = std::make_shared<spdlog::logger>("EA", std::move(sink));
            const auto level = bootstrap.verbose ? spdlog::level::trace : spdlog::level::info;
            log->set_level(level);
            log->flush_on(level);
            spdlog::set_default_logger(std::move(log));
        } catch (const spdlog::spdlog_ex& error) {
            SKSE::stl::report_and_fail(std::format(
                "Failed to create Simple Alternate Levelling log '{}': {}",
                logPath.string(), error.what()));
        }

        if (bootstrap.maxLogFiles == 0) {
            return;
        }

        std::error_code ec;
        std::vector<std::string> names;
        std::filesystem::directory_iterator iterator(*logDir, ec);
        const std::filesystem::directory_iterator end;
        while (!ec && iterator != end) {
            std::error_code entryError;
            if (iterator->is_regular_file(entryError) && !entryError) {
                names.push_back(iterator->path().filename().string());
            }
            iterator.increment(ec);
        }
        if (ec) {
            logger::warn("[EA] Log rotation: could not enumerate '{}': {}.",
                logDir->string(), ec.message());
            return;
        }

        for (const auto& name : EA::LogPolicy::SelectLogsToDelete(
                 std::move(names), bootstrap.maxLogFiles)) {
            ec.clear();
            if (!std::filesystem::remove(*logDir / name, ec) && ec) {
                logger::warn("[EA] Log rotation: could not remove '{}': {}.", name, ec.message());
            }
        }
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
    static EA::Config::StartingSkillsMode s_startingMode = EA::Config::StartingSkillsMode::Vanilla;
    static float s_startingUniformValue = 0.0f;
    static std::unordered_map<std::string, float> s_startingCustom;
    static bool s_normalizeTaskQueued = false;
    static bool s_charCreateWatcherRegistered = false;
    static bool s_dataLoadedHandled = false;
    static std::atomic<std::uint64_t> s_lifecycleGeneration{ 1 };

    static void InvalidateDeferredLifecycleWork() {
        s_lifecycleGeneration.fetch_add(1);
        s_normalizeTaskQueued = false;
        EA::SkillMenu::ResetState();
        EA::SettingsMenu::ResetState();
        EA::Leveling::ResetState();
    }

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
            auto* info = RE::ActorValueList::GetActorValueInfo(av);
            if (!info || !info->skill) {
                continue;
            }
            skills.push_back(av);
        }

        return skills;
    }

    static std::string_view StartingSkillKey(RE::ActorValue av) {
        switch (av) {
            case RE::ActorValue::kOneHanded: return "one_handed";
            case RE::ActorValue::kTwoHanded: return "two_handed";
            case RE::ActorValue::kBlock: return "block";
            case RE::ActorValue::kHeavyArmor: return "heavy_armor";
            case RE::ActorValue::kLightArmor: return "light_armor";
            case RE::ActorValue::kArchery: return "archery";
            case RE::ActorValue::kAlteration: return "alteration";
            case RE::ActorValue::kConjuration: return "conjuration";
            case RE::ActorValue::kDestruction: return "destruction";
            case RE::ActorValue::kIllusion: return "illusion";
            case RE::ActorValue::kRestoration: return "restoration";
            case RE::ActorValue::kSneak: return "sneak";
            case RE::ActorValue::kSmithing: return "smithing";
            case RE::ActorValue::kAlchemy: return "alchemy";
            case RE::ActorValue::kEnchanting: return "enchanting";
            case RE::ActorValue::kPickpocket: return "pickpocket";
            case RE::ActorValue::kLockpicking: return "lockpicking";
            case RE::ActorValue::kSpeech: return "speech";
            default: return "";
        }
    }

    static void NormalizeSkills() {
        if (s_startingMode == EA::Config::StartingSkillsMode::Vanilla) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("[EA] NormalizeSkills: PlayerCharacter is null.");
            return;
        }

        auto* avo = static_cast<RE::Actor*>(player)->AsActorValueOwner();
        if (!avo) {
            logger::warn("[EA] NormalizeSkills: player ActorValueOwner is null.");
            return;
        }
        auto  skills = GetSkillActorValues();
        if (skills.empty()) {
            logger::warn("[EA] NormalizeSkills: no skill actor values were discovered.");
            return;
        }

        auto* avList = RE::ActorValueList::GetSingleton();
        logger::info("[EA] NormalizeSkills: reading skill values before reset:");
        for (auto av : skills) {
            auto* info = avList ? RE::ActorValueList::GetActorValueInfo(av) : nullptr;
            const char* name = (info && info->fullName.data() && info->fullName.data()[0])
                ? info->fullName.data()
                : "???";

            float current = avo->GetBaseActorValue(av);
            if (!std::isfinite(current)) {
                logger::warn("[EA] NormalizeSkills: skipped '{}' ({}) because its base value is non-finite.",
                    name, static_cast<int>(av));
                continue;
            }
            logger::info("[EA]   skill '{}' ({}) = {:.1f}", name, static_cast<int>(av), current);
            float desired = 0.0f;
            if (s_startingMode == EA::Config::StartingSkillsMode::Uniform) desired = s_startingUniformValue;
            if (s_startingMode == EA::Config::StartingSkillsMode::Custom) {
                const auto found = s_startingCustom.find(std::string(StartingSkillKey(av)));
                if (found != s_startingCustom.end()) desired = found->second;
            }
            if (current != desired) avo->SetBaseActorValue(av, desired);

            float residual = avo->GetActorValue(av);
            if (std::isfinite(residual) && residual != desired) {
                avo->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, av, desired - residual);
            } else if (!std::isfinite(residual)) {
                logger::warn("[EA] NormalizeSkills: skipped invalid residual for '{}' ({}).",
                    name, static_cast<int>(av));
            }
        }
        s_skillsNormalized = true;
        logger::info("[EA] Starting skills applied once (mode={}).", static_cast<int>(s_startingMode));
    }

    static void QueueNormalizeSkillsWhenReady() {
        if (s_startingMode == EA::Config::StartingSkillsMode::Vanilla || !s_awaitingCharCreate || s_skillsNormalized || s_normalizeTaskQueued) {
            return;
        }

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::warn("[EA] NormalizeSkills: TaskInterface is unavailable; normalization remains armed.");
            return;
        }
        s_normalizeTaskQueued = true;
        const auto generation = s_lifecycleGeneration.load();
        tasks->AddTask([generation]() {
            if (s_lifecycleGeneration.load() != generation) {
                logger::debug("[EA] NormalizeSkills: stale deferred task discarded.");
                return;
            }
            s_normalizeTaskQueued = false;

            if (s_startingMode == EA::Config::StartingSkillsMode::Vanilla || !s_awaitingCharCreate || s_skillsNormalized) {
                return;
            }

            if (IsCreationMenuOpen()) {
                QueueNormalizeSkillsWhenReady();
                return;
            }

            s_awaitingCharCreate = false;
            logger::info("[EA] RaceSex/RaceMenu closed on new game - normalizing skills now.");
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
            if (s_startingMode == EA::Config::StartingSkillsMode::Vanilla) return RE::BSEventNotifyControl::kContinue;
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
            return RE::BSEventNotifyControl::kContinue;
        }
    };
    static CharCreateWatcher s_charCreateWatcher;

    // -----------------------------------------------------------------------
    // Cosave callbacks
    // -----------------------------------------------------------------------

    constexpr std::uint32_t kEASaveID  = 'EAXP';
    constexpr std::uint32_t kEAVersion = EA::Progression::kCosaveVersion;

    static std::optional<float> GetNativeXP() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* skills = player ? player->GetInfoRuntimeData().skills : nullptr;
        if (!skills || !skills->data || !std::isfinite(skills->data->xp) || skills->data->xp < 0.0f) {
            return std::nullopt;
        }
        return skills->data->xp;
    }

    static std::string_view DecodeStatusName(EA::Progression::DecodeStatus status) {
        using EA::Progression::DecodeStatus;
        switch (status) {
            case DecodeStatus::kSuccess: return "success";
            case DecodeStatus::kUnsupportedVersion: return "unsupported-version";
            case DecodeStatus::kInvalidLength: return "invalid-length";
            case DecodeStatus::kInvalidData: return "invalid-data";
        }
        return "unknown";
    }

    void OnGameSave(SKSE::SerializationInterface* intfc) {
        if (!intfc) {
            logger::error("[EA] Cosave: save callback received a null serialization interface.");
            return;
        }
        int pendingSkillPoints = EA::XPManager::GetPendingSkillPoints();
        if (pendingSkillPoints < 0) {
            logger::warn("[EA] Cosave: negative pendingSkillPoints={} rejected during save; writing 0.",
                pendingSkillPoints);
            pendingSkillPoints = 0;
        }

        const EA::Progression::CosaveState state{
            static_cast<std::int32_t>(pendingSkillPoints),
            s_skillsNormalized
        };
        const auto encoded = EA::Progression::EncodeCosaveV6(state);
        if (!intfc->WriteRecord(
                kEASaveID,
                kEAVersion,
                encoded.data(),
                static_cast<std::uint32_t>(encoded.size()))) {
            logger::error("[EA] Cosave: Failed to write atomic v6 record.");
            return;
        }

        const auto nativeXP = GetNativeXP();
        logger::info("[EA] Cosave v6: Saved pendingSkillPoints={}, skillsNormalized={}; native XP remains engine-owned (current={}).",
            pendingSkillPoints,
            s_skillsNormalized,
            nativeXP ? std::format("{:.1f}", *nativeXP) : "unavailable");
    }

    void OnGameLoad(SKSE::SerializationInterface* intfc) {
        InvalidateDeferredLifecycleWork();
        // Reset guards on every load — FormIDs from the previous session
        // are invalid in the new save's worldspace.
        EA::EventSinks::ResetRewardState();

        EA::XPManager::SetPendingSkillPoints(0);
        s_skillsNormalized = false;
        s_awaitingCharCreate = false;

        if (!intfc) {
            logger::error("[EA] Cosave: load callback received a null serialization interface; plugin state remains at defaults.");
            return;
        }

        const auto nativeXPBefore = GetNativeXP();
        std::optional<EA::Progression::CosaveState> acceptedState;

        std::uint32_t type, version, length;
        while (intfc->GetNextRecordInfo(type, version, length)) {
            if (type == kEASaveID) {
                if (length == 0 || length > EA::Progression::kMaxCosaveRecordSize) {
                    logger::warn("[EA] Cosave: rejected EAXP v{} record with unsafe length {}.", version, length);
                    continue;
                }

                std::vector<std::byte> recordData(length);
                const auto bytesRead = intfc->ReadRecordData(recordData.data(), length);
                if (bytesRead != length) {
                    logger::warn("[EA] Cosave: truncated EAXP v{} record (expected {}, read {}).",
                        version, length, bytesRead);
                    continue;
                }

                const auto decoded = EA::Progression::DecodeCosave(
                    version, std::span<const std::byte>{ recordData });
                if (decoded.ignoredLegacyXP) {
                    logger::info("[EA] Cosave migration: ignored legacy v{} XP={}; retained Skyrim main-save XP={}.",
                        version,
                        std::isfinite(*decoded.ignoredLegacyXP)
                            ? std::format("{:.1f}", *decoded.ignoredLegacyXP)
                            : "invalid",
                        nativeXPBefore ? std::format("{:.1f}", *nativeXPBefore) : "unavailable");
                }
                if (!decoded.Succeeded()) {
                    logger::warn("[EA] Cosave: rejected EAXP v{} record (status={}, length={}).",
                        version, DecodeStatusName(decoded.status), length);
                    continue;
                }
                if (!EA::Progression::AdoptFirstValidCosave(acceptedState, decoded)) {
                    logger::warn("[EA] Cosave: duplicate valid EAXP record ignored (version={}).", version);
                }
            } else {
                logger::warn("[EA] Cosave: Unknown record {:#010x} — skipped.", type);
            }
        }

        if (acceptedState) {
            EA::XPManager::SetPendingSkillPoints(acceptedState->pendingSkillPoints);
            s_skillsNormalized = acceptedState->skillsNormalized;
            logger::info("[EA] Cosave: Restored plugin state pendingSkillPoints={}, skillsNormalized={}.",
                acceptedState->pendingSkillPoints, acceptedState->skillsNormalized);
        } else {
            logger::warn("[EA] Cosave: No valid EAXP record found; plugin-owned state reset to defaults.");
        }

        const auto nativeXPAfter = GetNativeXP();
        logger::info("[EA] Cosave: Native XP preserved across plugin load (before={}, after={}).",
            nativeXPBefore ? std::format("{:.1f}", *nativeXPBefore) : "unavailable",
            nativeXPAfter ? std::format("{:.1f}", *nativeXPAfter) : "unavailable");

        EA::Leveling::ApplyGameSettings("game-load");
        EA::Leveling::RefreshThreshold("game-load");
    }

    void OnGameRevert(SKSE::SerializationInterface*) {
        InvalidateDeferredLifecycleWork();
        EA::XPManager::SetPendingSkillPoints(0);
        EA::EventSinks::ResetRewardState();
        s_skillsNormalized   = false;
        s_awaitingCharCreate = false;
        logger::info("[EA] Cosave: Reverted — all state reset.");
    }

    // -----------------------------------------------------------------------
    // kDataLoaded callback — all hooks and sinks registered here
    // -----------------------------------------------------------------------
    void OnDataLoaded() {
        if (s_dataLoadedHandled) {
            logger::debug("[EA] OnDataLoaded: duplicate message ignored.");
            return;
        }
        s_dataLoadedHandled = true;
        EA::SkillHook::Install();
        EA::EventSinks::Register();
        if (!EA::SkillMenu::Register()) {
            logger::warn("[EA] SkillMenu unavailable; vanilla level-up UI will remain active.");
        }
        if (!EA::SettingsMenu::Register()) {
            logger::warn("[EA] Settings: menu or input registration failed.");
        }

        // Keep the engine's native formula synchronized with the validated
        // configuration. The explicit threshold refresh below adds the cap.
        EA::Leveling::ApplyGameSettings("data-loaded");

        // Block character XP from skill rank-ups (skill books, trainers).
        // When a skill ranks up the engine calls UseSkill() which awards
        // newLevel * fXPPerSkillRank to skills->data->xp. Setting this to
        // 0 makes every rank-up contribute 0 character XP so only our
        // explicit AwardXP calls feed the level bucket.
        auto* settings = RE::GameSettingCollection::GetSingleton();
        if (settings) {
            auto* perRank = settings->GetSetting("fXPPerSkillRank");
            if (perRank) {
                perRank->data.f = 0.0f;
                logger::info("[EA] OnDataLoaded: fXPPerSkillRank set to 0.0 (blocks skill-rank character XP).");
            } else {
                logger::warn("[EA] OnDataLoaded: fXPPerSkillRank not found — skill-book/trainer XP may leak.");
            }
        } else {
            logger::warn("[EA] OnDataLoaded: GameSettingCollection is null — fXPPerSkillRank NOT applied.");
        }

        EA::Leveling::RefreshThreshold("data-loaded");

        // Register CharCreateWatcher unconditionally; ProcessEvent checks config flag at runtime.
        auto* ui = RE::UI::GetSingleton();
        if (ui && !s_charCreateWatcherRegistered) {
            ui->AddEventSink(&s_charCreateWatcher);
            s_charCreateWatcherRegistered = true;
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
    logger::info("[EA] SimpleAlternateLevelling loaded successfully. Version {}", SAL_VERSION);

    if (!a_skse) {
        logger::critical("[EA] SKSE LoadInterface is null; plugin load aborted.");
        return false;
    }
    // Keep the timestamped logger from InitializeLog(). CommonLib's default
    // InitInfo creates its own logger and would replace the default logger.
    SKSE::Init(a_skse, { .log = false });

    // Register messaging listener — hooks must wait for kDataLoaded
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::critical("[EA] SKSE MessagingInterface is unavailable; plugin load aborted.");
        return false;
    }
    if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (!msg) {
            logger::warn("[EA] SKSE messaging callback received a null message.");
            return;
        }
        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            OnDataLoaded();
        }
        if (msg->type == SKSE::MessagingInterface::kNewGame) {
            InvalidateDeferredLifecycleWork();
            // New character — arm the CharCreateWatcher to fire on RaceMenu close.
            EA::EventSinks::ResetRewardState();
            EA::EventSinks::SnapshotSavedFlags("new-game");
            s_awaitingCharCreate = true;
            s_skillsNormalized   = false;
            s_normalizeTaskQueued = false;
            s_startingMode = EA::Config::startingSkillsMode;
            s_startingUniformValue = EA::Config::startingSkillsUniformValue;
            s_startingCustom = EA::Config::startingSkillsCustom;
            logger::info("[EA] kNewGame: awaiting RaceMenu close to normalize skills.");
            EA::Leveling::QueueThresholdRefresh(1, "new-game");
            QueueNormalizeSkillsWhenReady();
        }
        // kPostLoadGame: no skill-reset logic here.
        // CharCreateWatcher fires before any save exists, so kPostLoadGame is
        // not involved in the new-game skill reset path. The loaded save's
        // location flags are final here, so take the location-clear baseline.
        if (msg->type == SKSE::MessagingInterface::kPostLoadGame) {
            EA::EventSinks::SnapshotSavedFlags("post-load-game");
        }
    })) {
        logger::critical("[EA] Failed to register the SKSE messaging listener; plugin load aborted.");
        return false;
    }

    // Register cosave serialization
    auto* serialization = SKSE::GetSerializationInterface();
    if (!serialization) {
        logger::critical("[EA] SKSE SerializationInterface is unavailable; plugin load aborted.");
        return false;
    }
    serialization->SetUniqueID(kEASaveID);
    serialization->SetSaveCallback(OnGameSave);
    serialization->SetLoadCallback(OnGameLoad);
    serialization->SetRevertCallback(OnGameRevert);
    logger::info("[EA] Cosave v6 serialization registered.");
    logger::warn("[EA] Cosave v6 is forward-only: back up saves before upgrading; downgrade to the v5 DLL is unsupported after saving.");

    EA::Config::Load();  // Config loads immediately; hooks wait for kDataLoaded

    return true;
}
