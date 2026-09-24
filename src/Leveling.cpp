#include "PCH.h"
#include "Leveling.h"

#include "Config.h"
#include "Integration.h"
#include "Progression.h"
#include "SkillMenu.h"

#include <atomic>

namespace EA::Leveling {
    namespace {
        std::atomic<std::uint64_t> s_generation{ 1 };
        bool                       s_levelIncreasePending{ false };

        std::atomic<float>  s_appliedConfigured{ 0.0f };
        std::atomic<float>  s_appliedEffective{ 0.0f };
        std::atomic<double> s_appliedMultiplier{ 1.0 };

        [[nodiscard]] bool IsLevelUpInProgress()
        {
            if (s_levelIncreasePending || SkillMenu::IsDeferringVanillaLevelUp()) {
                return true;
            }
            auto* ui = RE::UI::GetSingleton();
            return ui && ui->IsMenuOpen(RE::LevelUpMenu::MENU_NAME);
        }
    }

    void ResetState()
    {
        s_generation.fetch_add(1);
        s_levelIncreasePending = false;
        logger::debug("[EA] Leveling: invalidated deferred threshold tasks.");
    }

    // The native curve settings stay unmultiplied: the engine's own
    // recalculation at level-up completion is always replaced by
    // RefreshThreshold when the LevelUp Menu closes, which applies the
    // integration multiplier in one place.
    void ApplyGameSettings(std::string_view reason)
    {
        auto* settings = RE::GameSettingCollection::GetSingleton();
        if (!settings) {
            logger::warn("[EA] Leveling: GameSettingCollection is null (reason={}).", reason);
            return;
        }

        auto* base = settings->GetSetting("fXPLevelUpBase");
        auto* mult = settings->GetSetting("fXPLevelUpMult");
        if (base) {
            base->data.f = Config::xpBase;
        } else {
            logger::warn("[EA] Leveling: fXPLevelUpBase not found (reason={}).", reason);
        }
        if (mult) {
            mult->data.f = Config::xpIncrease;
        } else {
            logger::warn("[EA] Leveling: fXPLevelUpMult not found (reason={}).", reason);
        }

        logger::info("[EA] Leveling settings applied (reason={}): base={:.1f}, increase={:.1f}, cap={:.1f}.",
            reason, Config::xpBase, Config::xpIncrease, Config::xpCap);
    }

    bool RefreshThreshold(std::string_view reason, std::uint32_t expectedLevel)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("[EA] Leveling: PlayerCharacter is null (reason={}).", reason);
            return false;
        }

        auto* skills = player->GetInfoRuntimeData().skills;
        if (!skills || !skills->data) {
            logger::warn("[EA] Leveling: PlayerSkills or data is null (reason={}).", reason);
            return false;
        }

        const auto finalizedLevel = static_cast<std::uint32_t>(player->GetLevel());
        const auto configured = Progression::CalculateThreshold(finalizedLevel,
            { Config::xpBase, Config::xpIncrease, Config::xpCap });
        const bool hasProvider = Integration::HasThresholdMultiplier();
        const float providerValue = hasProvider ? Integration::ThresholdMultiplier() : 1.0f;
        const auto modified = Progression::ApplyThresholdMultiplier(
            configured, providerValue, Config::thresholdMultiplierFloor);
        if (hasProvider && modified.rejected) {
            logger::warn("[EA] Leveling: integration threshold multiplier {} is invalid; using 1.0 (reason={}).",
                providerValue, reason);
        } else if (modified.clamped) {
            logger::info("[EA] Leveling: integration threshold multiplier {:.3f} clamped to {:.3f} (floor={:.2f}).",
                providerValue, modified.multiplier, Config::thresholdMultiplierFloor);
        }

        const auto previous = skills->data->levelThreshold;
        skills->data->levelThreshold = modified.threshold;
        s_appliedConfigured.store(configured);
        s_appliedEffective.store(modified.threshold);
        s_appliedMultiplier.store(modified.multiplier);

        logger::info("[EA] Threshold refresh (reason={}): expectedLevel={} finalizedLevel={} {:.1f} -> {:.1f} (curve={:.1f}, multiplier={:.3f}).",
            reason, expectedLevel, finalizedLevel, previous, modified.threshold, configured, modified.multiplier);
        return true;
    }

    void QueueThresholdRefresh(std::uint32_t expectedLevel, std::string reason)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::warn("[EA] Leveling: TaskInterface is null; applying threshold immediately (reason={}).", reason);
            RefreshThreshold(reason, expectedLevel);
            return;
        }

        const auto generation = s_generation.load();
        tasks->AddTask([expectedLevel, reason = std::move(reason), generation]() {
            if (s_generation.load() != generation) {
                logger::debug("[EA] Leveling: stale threshold task discarded (reason={}).", reason);
                return;
            }
            RefreshThreshold(reason, expectedLevel);
        });
    }

    void RequestIntegrationRefresh()
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::warn("[EA] Leveling: TaskInterface is null; integration threshold refresh dropped.");
            return;
        }
        const auto generation = s_generation.load();
        tasks->AddTask([generation]() {
            if (s_generation.load() != generation) {
                logger::debug("[EA] Leveling: stale integration refresh discarded.");
                return;
            }
            if (IsLevelUpInProgress()) {
                logger::info("[EA] Leveling: integration refresh folded into the LevelUp Menu close refresh.");
                return;
            }
            RefreshThreshold("integration-request");
        });
    }

    void MarkLevelIncrease()
    {
        s_levelIncreasePending = true;
    }

    void MarkLevelUpFinished()
    {
        s_levelIncreasePending = false;
    }

    AppliedThreshold LastApplied()
    {
        return { s_appliedConfigured.load(), s_appliedEffective.load(), s_appliedMultiplier.load() };
    }
}
