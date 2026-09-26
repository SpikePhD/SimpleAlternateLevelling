#include "PCH.h"
#include "Integration.h"

#include "IntegrationRules.h"

#include "Leveling.h"
#include "RewardRules.h"
#include "SAL_API.h"
#include "SkillMenu.h"

#include <limits>

namespace EA::Integration {
    namespace {
        using MultiplierProvider = float (*)();
        using LevelUpStep = bool (*)(std::uint32_t);
        using CharacterCreatedCallback = void (*)();
        using SkillPointBonus = std::int32_t (*)(std::uint32_t);

        using IntegrationRules::CallbackSlot;
        using IntegrationRules::XPMultiplierProvider;

        CallbackSlot<MultiplierProvider>       s_multiplierProvider;
        CallbackSlot<LevelUpStep>              s_levelUpStep;
        CallbackSlot<LevelUpStep>              s_preSkillMenuStep;
        CallbackSlot<SkillPointBonus>          s_skillPointBonus;
        CallbackSlot<CharacterCreatedCallback> s_characterCreated;
        CallbackSlot<XPMultiplierProvider>     s_xpMultiplier;
        bool                                   s_broadcast{ false };

        // Each slot allows one registrant; the first valid one wins.
        template <class Callback>
        bool RegisterSlot(CallbackSlot<Callback>& slot, Callback callback, std::string_view name)
        {
            switch (slot.Register(callback)) {
                case IntegrationRules::RegisterResult::kNull:
                    logger::warn("[EA] Integration: rejected null {} registration.", name);
                    return false;
                case IntegrationRules::RegisterResult::kDuplicate:
                    logger::warn("[EA] Integration: rejected second {} registration; each slot allows one registrant.", name);
                    return false;
                case IntegrationRules::RegisterResult::kAccepted:
                    break;
            }
            logger::info("[EA] Integration: {} registered.", name);
            return true;
        }

        bool RegisterThresholdMultiplierApi(MultiplierProvider provider)
        {
            return RegisterSlot(s_multiplierProvider, provider, "threshold multiplier");
        }

        void RequestThresholdRefreshApi()
        {
            Leveling::RequestIntegrationRefresh();
        }

        bool RegisterLevelUpStepApi(LevelUpStep wantsStep)
        {
            return RegisterSlot(s_levelUpStep, wantsStep, "level-up step");
        }

        void ContinueLevelUpApi()
        {
            SkillMenu::RequestContinueLevelUp();
        }

        bool RegisterCharacterCreatedApi(CharacterCreatedCallback callback)
        {
            return RegisterSlot(s_characterCreated, callback, "character-created callback");
        }

        bool RegisterPreSkillMenuStepApi(LevelUpStep wantsStep)
        {
            return RegisterSlot(s_preSkillMenuStep, wantsStep, "pre-skill-menu step");
        }

        bool RegisterSkillPointBonusApi(SkillPointBonus bonus)
        {
            return RegisterSlot(s_skillPointBonus, bonus, "skill point bonus");
        }

        bool RegisterXPMultiplierApi(XPMultiplierProvider provider)
        {
            return RegisterSlot(s_xpMultiplier, provider, "XP multiplier");
        }

        bool AskStep(LevelUpStep step, std::uint32_t level, std::string_view name)
        {
            if (!step) {
                return false;
            }
            try {
                return step(level);
            } catch (...) {
                logger::error("[EA] Integration: {} threw for level {}; continuing without it.", name, level);
                return false;
            }
        }

        SAL::SALInterfaceV4 s_interface{
            {
                {
                    {
                        SAL::kInterfaceVersion4,
                        RegisterThresholdMultiplierApi,
                        RequestThresholdRefreshApi,
                        RegisterLevelUpStepApi,
                        ContinueLevelUpApi,
                        RegisterCharacterCreatedApi
                    },
                    RegisterPreSkillMenuStepApi
                },
                RegisterSkillPointBonusApi
            },
            RegisterXPMultiplierApi
        };
    }

    void Broadcast()
    {
        if (s_broadcast) {
            logger::debug("[EA] Integration: interface already broadcast.");
            return;
        }
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            logger::error("[EA] Integration: MessagingInterface unavailable; integration API not broadcast.");
            return;
        }
        s_broadcast = true;
        // SKSE reports false when no plugin listens, which is normal without a
        // companion. Call the raw dispatcher so CommonLib's wrapper does not
        // log that as a warning.
        const auto& raw = reinterpret_cast<const SKSE::Impl::SKSEMessagingInterface&>(*messaging);
        if (!raw.Dispatch(SKSE::GetPluginHandle(), SAL::kMessageInterface, &s_interface,
                static_cast<std::uint32_t>(sizeof(s_interface)), nullptr)) {
            logger::info("[EA] Integration: interface V{} broadcast; no companion plugin is listening.",
                s_interface.v3.v2.v1.version);
            return;
        }
        logger::info("[EA] Integration: interface V{} broadcast to listeners of '{}'.",
            s_interface.v3.v2.v1.version, SAL::kSenderName);
    }

    bool HasThresholdMultiplier()
    {
        return s_multiplierProvider.Get() != nullptr;
    }

    float ThresholdMultiplier()
    {
        const auto provider = s_multiplierProvider.Get();
        if (!provider) {
            return 1.0f;
        }
        try {
            return provider();
        } catch (...) {
            logger::error("[EA] Integration: threshold multiplier provider threw; ignoring it.");
            return std::numeric_limits<float>::quiet_NaN();
        }
    }

    bool HasLevelUpStep()
    {
        return s_levelUpStep.Get() != nullptr;
    }

    bool WantsLevelUpStep(std::uint32_t level)
    {
        return AskStep(s_levelUpStep.Get(), level, "level-up step");
    }

    bool HasPreSkillMenuStep()
    {
        return s_preSkillMenuStep.Get() != nullptr;
    }

    bool WantsPreSkillMenuStep(std::uint32_t level)
    {
        return AskStep(s_preSkillMenuStep.Get(), level, "pre-skill-menu step");
    }

    bool HasSkillPointBonus()
    {
        return s_skillPointBonus.Get() != nullptr;
    }

    std::int32_t SkillPointBonus(std::uint32_t level)
    {
        const auto bonus = s_skillPointBonus.Get();
        if (!bonus) {
            return 0;
        }
        try {
            return bonus(level);
        } catch (...) {
            logger::error("[EA] Integration: skill point bonus threw for level {}; using 0.", level);
            return 0;
        }
    }

    double XPMultiplier(RewardRules::RewardSource source)
    {
        static bool s_warned = false;
        bool threw = false;
        const float raw = IntegrationRules::QueryXPMultiplier(
            s_xpMultiplier, RewardRules::ToPublicXPSource(source), threw);
        const auto result = RewardRules::SanitizeXPMultiplier(raw);
        if (result.status != RewardRules::XPMultiplierStatus::kApplied && !s_warned) {
            // Once per session: a broken provider would otherwise warn on every award.
            s_warned = true;
            if (threw) {
                logger::warn("[EA] Integration: XP multiplier provider threw; using 1.0. Further warnings suppressed.");
            } else if (result.status == RewardRules::XPMultiplierStatus::kInvalid) {
                logger::warn("[EA] Integration: XP multiplier {} is invalid; using 1.0. Further warnings suppressed.", raw);
            } else {
                logger::warn("[EA] Integration: XP multiplier {} clamped to {:.0f}. Further warnings suppressed.",
                    raw, RewardRules::kMaxXPMultiplier);
            }
        }
        return result.value;
    }

    void NotifyCharacterCreated()
    {
        const auto callback = s_characterCreated.Get();
        if (!callback) {
            logger::debug("[EA] Integration: character created; no callback registered.");
            return;
        }
        logger::info("[EA] Integration: notifying character-created callback.");
        try {
            callback();
        } catch (...) {
            logger::error("[EA] Integration: character-created callback threw.");
        }
    }
}
