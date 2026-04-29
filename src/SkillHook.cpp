#include "PCH.h"
#include "SkillHook.h"
#include "Config.h"
#include "XPManager.h"

namespace EA::SkillHook {

    // -----------------------------------------------------------------------
    // Book Activate Hook — vtable slot 37
    // Uses write_vfunc to avoid collision with PapyrusExtender which hooks
    // TESObjectBOOK::Read (ID 17842) with write_branch<5>.
    // XP award is deferred via task interface because DebugNotification
    // cannot be called from inside TESObjectBOOK::Activate's call stack.
    // -----------------------------------------------------------------------
    struct BookActivateHook {
        static void thunk(RE::TESObjectBOOK* a_book, RE::TESBoundObject* a_boundObject, RE::TESObject_REFR* a_refr) {
            // Call original first
            func(a_book, a_boundObject, a_refr);

            // Defer book XP processing to avoid call stack issues
            SKSE::GetTaskInterface()->AddTask([a_book]() {
                ProcessBookXP(a_book);
            });
        }

        static void ProcessBookXP(RE::TESObjectBOOK* a_book) {
            if (!a_book) return;

            auto  formID = a_book->GetFormID();
            auto  title = a_book->GetFullName();
            bool  alreadyRead = a_book->IsRead();
            bool  skillBook = a_book->TeachesSkill();

            // Skip if this book was already awarded XP this session
            if (!XPManager::RegisterBookRead(formID)) {
                logger::debug("[EA] Book hook: FormID {:08X} already awarded XP - skipped.", formID);
                return;
            }

            float xp = Config::xpBookNew;
            if (skillBook) {
                xp = Config::xpBookSkill;
            } else if (Config::bookUseValueReward) {
                xp = static_cast<float>(std::max(1, a_book->GetGoldValue())) * Config::bookValueMultiplier;
            }

            xp *= Config::bookReadingMultiplier;

            XPManager::AwardXP(
                xp,
                XPManager::MakeBookContext(title, formID, skillBook, alreadyRead));
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

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

        // Install AddSkillExperience hook to discard organic skill XP
        REL::Relocation<std::uintptr_t> skillTarget{ REL::ID(40488) };
        AddSkillExperienceHook::func =
            trampoline.write_branch<5>(skillTarget.address(), AddSkillExperienceHook::thunk);
        logger::info("[EA] SkillHook: AddSkillExperienceHook installed (ID 40488).");

        // Install TESObjectBOOK::Activate vtable hook for book XP
        // Slot 37, using write_vfunc to avoid collision with PapyrusExtender
        auto& vtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_TESObjectBOOK[0] };
        BookActivateHook::func = vtable.write_vfunc(0x25, BookActivateHook::thunk);  // 0x25 = 37 in decimal
        logger::info("[EA] SkillHook: BookActivateHook installed (vtable slot 37).");
    }
}
