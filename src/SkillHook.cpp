#include "PCH.h"
#include "SkillHook.h"
#include "Config.h"
#include "RewardRules.h"
#include "XPManager.h"

namespace EA::SkillHook {
    static bool s_installed = false;

    static std::string DescribeSkill(RE::ActorValue a_skill) {
        auto* avList = RE::ActorValueList::GetSingleton();
        auto* avInfo  = avList ? RE::ActorValueList::GetActorValueInfo(a_skill) : nullptr;
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

    struct BookActivateHook {
        static bool thunk(
            RE::TESObjectBOOK* a_book,
            RE::TESObjectREFR* a_targetRef,
            RE::TESObjectREFR* a_activatorRef,
            std::uint8_t       a_arg3,
            RE::TESBoundObject* a_object,
            std::int32_t       a_targetCount)
        {
            const bool alreadyRead = a_book && a_book->IsRead();
            const bool skillBook = a_book && a_book->TeachesSkill();
            const bool playerActivated = a_activatorRef && a_activatorRef->IsPlayerRef();
            const auto formID = a_book ? a_book->GetFormID() : RE::FormID{ 0 };
            const std::string title = a_book && a_book->GetFullName() && a_book->GetFullName()[0]
                ? a_book->GetFullName()
                : "Book";

            float xp = Config::xpBookNew;
            if (skillBook) {
                xp = Config::xpBookSkill;
            } else if (a_book && Config::bookUseValueReward) {
                xp = static_cast<float>(std::max(1, a_book->GetGoldValue())) * Config::bookValueMultiplier;
            }
            xp *= Config::bookReadingMultiplier;

            const bool activated = func(
                a_book, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);

            if (!RewardRules::ShouldRewardBook(activated, playerActivated, alreadyRead)) {
                logger::debug("[EA] Book reward skipped: '{}' ({:08X}) success={} player={} alreadyRead={}.",
                    title, formID, activated, playerActivated, alreadyRead);
                return activated;
            }

            auto* task = SKSE::GetTaskInterface();
            if (!task) {
                logger::error("[EA] Book reward: task interface unavailable for '{}' ({:08X}); reward skipped.",
                    title, formID);
                return activated;
            }

            const auto generation = XPManager::GetRewardGeneration();
            task->AddTask([title, formID, skillBook, xp, generation]() {
                if (XPManager::GetRewardGeneration() != generation) {
                    logger::debug("[EA] Book reward: stale deferred task for '{}' ({:08X}) discarded after state reset.",
                        title, formID);
                    return;
                }
                if (!XPManager::RegisterBookRead(formID)) {
                    logger::debug("[EA] Book reward: duplicate activation for '{}' ({:08X}) skipped.",
                        title, formID);
                    return;
                }

                XPManager::AwardXP(
                    xp,
                    XPManager::MakeBookContext(title, formID, skillBook, false));
            });
            return activated;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void Install() {
        if (s_installed) {
            logger::debug("[EA] SkillHook: hooks already installed.");
            return;
        }
        auto& trampoline = SKSE::GetTrampoline();
        trampoline.create(64);

        REL::Relocation<std::uintptr_t> skillTarget{ RELOCATION_ID(39413, 40488) };
        AddSkillExperienceHook::func =
            trampoline.write_branch<5>(skillTarget.address(), AddSkillExperienceHook::thunk);
        logger::info("[EA] SkillHook: AddSkillExperienceHook installed.");

        REL::Relocation<std::uintptr_t> bookVtable{ RE::VTABLE_TESObjectBOOK[0] };
        BookActivateHook::func = bookVtable.write_vfunc(0x37, BookActivateHook::thunk);
        logger::info("[EA] SkillHook: TESObjectBOOK::Activate vtable hook installed (slot 0x37).");
        s_installed = true;
    }
}
