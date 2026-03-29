#include "PCH.h"
#include "SkillHook.h"
#include "Config.h"

namespace EA::SkillHook {

    static std::string DescribeSkill(RE::ActorValue a_skill) {
        auto* avList = RE::ActorValueList::GetSingleton();
        auto* avInfo  = avList ? avList->GetActorValue(a_skill) : nullptr;
        if (avInfo && avInfo->fullName.data() && avInfo->fullName.data()[0]) {
            return avInfo->fullName.data();
        }
        return std::format("AV={}", static_cast<std::uint32_t>(a_skill));
    }

    struct AddSkillExperienceHook {
        static void thunk(
            RE::PlayerCharacter* a_player,
            RE::ActorValue       a_skill,
            float                a_experience)
        {
            if (EA::Config::verbose) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* skills  = player ? player->GetInfoRuntimeData().skills : nullptr;
                float engineXP = (skills && skills->data) ? skills->data->xp : -1.0f;
                float threshold = (skills && skills->data) ? skills->data->levelThreshold : -1.0f;

                logger::trace("[EA] SkillHook: AddSkillExperience intercepted | skill='{}' ({}) points={:.2f} | engine_xp={:.1f} threshold={:.1f} -> discarded.",
                    DescribeSkill(a_skill),
                    static_cast<std::uint32_t>(a_skill),
                    a_experience,
                    engineXP,
                    threshold);
            }
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void Install() {
        auto& trampoline = SKSE::GetTrampoline();
        trampoline.create(64);

        REL::Relocation<std::uintptr_t> skillTarget{ REL::ID(40488) };
        AddSkillExperienceHook::func =
            trampoline.write_branch<5>(skillTarget.address(), AddSkillExperienceHook::thunk);
        logger::info("[EA] SkillHook: AddSkillExperienceHook installed (ID 40488).");
    }
}
