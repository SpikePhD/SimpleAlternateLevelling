#include "PCH.h"
#include "SkillHook.h"
#include "Config.h"
#include "XPManager.h"

namespace EA::SkillHook {

    // -----------------------------------------------------------------------
    // ADDRESS VERIFICATION
    // Source: CommonLibSSE-NG submodule, src/RE/P/PlayerCharacter.cpp
    //   PlayerCharacter::AddSkillExperience -> RELOCATION_ID(39413, 40488)
    //   SE ID: 39413  |  AE ID: 40488  (we target AE 1.6.1170, so AE ID is used)
    //
    // This is the function entry point (non-virtual, resolved via REL::Relocation).
    // Hook method: write_branch<5>  (patches the function prologue, not a call site)
    //
    // Signature in CommonLibSSE-NG:
    //   void PlayerCharacter::AddSkillExperience(RE::ActorValue a_skill, float a_experience)
    // -----------------------------------------------------------------------

    struct AddSkillExperienceHook {

        static void thunk(
            RE::PlayerCharacter* a_player,
            RE::ActorValue       a_skill,
            float                a_experience)
        {
            // EA: Discard all organic skill XP. Do nothing and return.
            // Skills can still be raised via console/trainers (those paths bypass this function).
            if (EA::Config::verbose) {
                logger::trace("[EA] SkillHook: Intercepted skill XP gain. Skill={}, Points={:.2f} -> Discarded.",
                    static_cast<std::uint32_t>(a_skill), a_experience);
            }
        }

        // Stores the original function pointer.
        // Required by the trampoline pattern even though we never call the original.
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // -----------------------------------------------------------------------
    // BOOK READ HOOK — TESObjectBOOK::Read
    //
    // Intercepts the moment a book is read by the player.
    // We check IsRead() BEFORE calling the original — if false, this is the
    // first time the player has read this book, so we award XP.
    // Vanilla sets the read flag during the original call, so our check
    // correctly fires only once per unique book.
    //
    // Skill books: handled via "Skill Books Read" TrackedStat in EventSinks.
    // We skip them here (GetSkill() != kNone) to avoid double-awarding.
    //
    // Address: RELOCATION_ID(17439, 17842)
    // Verified via: CommonLibSSE-NG src/RE/T/TESObjectBOOK.cpp line 60
    // Return type: bool (must match — vanilla returns true on success)
    // -----------------------------------------------------------------------
    struct BookReadHook {

        static bool thunk(
            RE::TESObjectBOOK* a_book,
            RE::TESObjectREFR* a_reader)
        {
            if (a_book && a_reader && a_reader->IsPlayerRef()) {
                bool isSkillBook = (a_book->GetSkill() != RE::ActorValue::kNone);
                bool alreadyRead = a_book->IsRead();

                if (!alreadyRead && !isSkillBook) {
                    logger::info("[EA] BookHook: First read of '{}' (FormID={:08X}). "
                                 "Awarding {:.1f} XP.",
                                 a_book->GetFullName(),
                                 a_book->GetFormID(),
                                 EA::Config::xpBookNew);
                    EA::XPManager::AwardXP(EA::Config::xpBookNew, "book_read");
                } else if (EA::Config::verbose) {
                    if (alreadyRead) {
                        logger::trace("[EA] BookHook: '{}' already read — skipped.",
                                      a_book->GetFullName());
                    }
                    // Skill books: no trace needed, TrackedStat handles logging
                }
            }

            // Always call original — vanilla sets the read flag, teaches spells,
            // and handles the book-reading animation/UI
            return func(a_book, a_reader);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void Install() {
        auto& trampoline = SKSE::GetTrampoline();
        trampoline.create(128);  // 128 bytes: enough for two write_branch<5> hooks

        // AE Address ID 40488 = PlayerCharacter::AddSkillExperience
        // Verified via CommonLibSSE-NG: RELOCATION_ID(39413, 40488)
        REL::Relocation<std::uintptr_t> skillTarget{ REL::ID(40488) };
        AddSkillExperienceHook::func =
            trampoline.write_branch<5>(skillTarget.address(), AddSkillExperienceHook::thunk);
        logger::info("[EA] SkillHook: AddSkillExperienceHook installed (ID 40488).");

        // AE Address ID 17842 = TESObjectBOOK::Read
        // Verified via CommonLibSSE-NG: RELOCATION_ID(17439, 17842)
        REL::Relocation<std::uintptr_t> bookTarget{ REL::ID(17842) };
        BookReadHook::func =
            trampoline.write_branch<5>(bookTarget.address(), BookReadHook::thunk);
        logger::info("[EA] SkillHook: BookReadHook installed (ID 17842).");
    }
}
