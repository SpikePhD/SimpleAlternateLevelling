#include "PCH.h"
#include "Leveling.h"

#include "Config.h"
#include "Progression.h"

#include <atomic>

namespace EA::Leveling {
    namespace {
        std::atomic<std::uint64_t> s_generation{ 1 };
    }

    void ResetState()
    {
        s_generation.fetch_add(1);
        logger::debug("[EA] Leveling: invalidated deferred threshold tasks.");
    }

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
        const auto threshold = Progression::CalculateThreshold(finalizedLevel,
            { Config::xpBase, Config::xpIncrease, Config::xpCap });
        const auto previous = skills->data->levelThreshold;
        skills->data->levelThreshold = threshold;

        logger::info("[EA] Threshold refresh (reason={}): expectedLevel={} finalizedLevel={} {:.1f} -> {:.1f}.",
            reason, expectedLevel, finalizedLevel, previous, threshold);
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
}
