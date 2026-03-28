#include "PCH.h"
#include "SkillMenu.h"
#include "Config.h"
#include "XPManager.h"

namespace EA::SkillMenu {

    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------

    static constexpr std::string_view MENU_NAME = "EA Skill Menu";
    // BSScaleformManager::BuildFilePath prepends "Interface/" and appends ".swf" automatically.
    // Pass only the bare filename here.
    static constexpr std::string_view SWF_NAME  = "EA_SkillMenu";

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    // Points remaining to spend during the current level-up.
    static int  s_remainingPoints   = 0;
    // Carry-over stored before Open() so PostCreate() can pass it to the SWF.
    static int  s_pendingCarryOver  = 0;

    // Set when we swallow a "LevelUp Menu" kShow message so we know to
    // re-fire it when our menu closes.
    static bool s_deferredLevelUp      = false;
    // One-shot flag: set by Confirm() so the very next "LevelUp Menu" kShow
    // message is allowed through the hook unmodified.
    static bool s_allowVanillaLevelUp  = false;

    // Snapshot of base skill levels at menu open — used by Reset to undo
    // allocations made during this session only.
    static constexpr int NUM_SKILLS = 18;
    static float s_snapshotLevels[NUM_SKILLS] = {};

    // -----------------------------------------------------------------------
    // Skill table — 18 trainable skills in three display columns.
    // Columns: 0=Combat, 1=Magic, 2=Crafting/Misc
    // Names are fetched at runtime from ActorValueList (engine-localised,
    // mod-compatible). No hardcoded strings.
    // -----------------------------------------------------------------------
    struct SkillEntry {
        RE::ActorValue av;
        int            col;
        int            row;
    };

    static constexpr SkillEntry SKILLS[] = {
        // Combat (col 0)
        { RE::ActorValue::kOneHanded,  0, 0 },
        { RE::ActorValue::kTwoHanded,  0, 1 },
        { RE::ActorValue::kBlock,       0, 2 },
        { RE::ActorValue::kHeavyArmor,  0, 3 },
        { RE::ActorValue::kLightArmor,  0, 4 },
        { RE::ActorValue::kArchery,     0, 5 },
        // Magic (col 1)
        { RE::ActorValue::kAlteration,  1, 0 },
        { RE::ActorValue::kConjuration,  1, 1 },
        { RE::ActorValue::kDestruction,  1, 2 },
        { RE::ActorValue::kIllusion,     1, 3 },
        { RE::ActorValue::kRestoration,  1, 4 },
        { RE::ActorValue::kSneak,        1, 5 },
        // Crafting / Misc (col 2)
        { RE::ActorValue::kSmithing,     2, 0 },
        { RE::ActorValue::kAlchemy,      2, 1 },
        { RE::ActorValue::kEnchanting,   2, 2 },
        { RE::ActorValue::kPickpocket,   2, 3 },
        { RE::ActorValue::kLockpicking,  2, 4 },
        { RE::ActorValue::kSpeech,       2, 5 },
    };

    // -----------------------------------------------------------------------
    // Forward declarations (internal helpers)
    // -----------------------------------------------------------------------
    static void InvokeInit(RE::GFxMovieView* a_movie, int totalPoints, int carryOver);
    static void InvokeUpdateSkill(RE::GFxMovieView* a_movie, RE::ActorValue a_av, float a_level);
    static void InvokeUpdatePoints(RE::GFxMovieView* a_movie, int remaining);
    static void ResetAllocations();

    // -----------------------------------------------------------------------
    // EASkillMenu — Scaleform menu class
    // -----------------------------------------------------------------------

    class EASkillMenu : public RE::IMenu {
    public:
        static constexpr std::string_view MENU_NAME_SV = MENU_NAME;

        static RE::stl::owner<RE::IMenu*> Creator() { return new EASkillMenu(); }

        EASkillMenu() {
            // LoadMovie calls LoadMovie_Impl which handles:
            //   uiMovie creation, fxDelegate = make_gptr<FxDelegate>(),
            //   fxDelegate->RegisterHandler(this) [calls our Accept()],
            //   and SetState(kExternalInterface, fxDelegate) on the view.
            auto* sfm = RE::BSScaleformManager::GetSingleton();
            sfm->LoadMovie(this, uiMovie, SWF_NAME.data());

            menuFlags |= RE::UI_MENU_FLAGS::kPausesGame;
            menuFlags |= RE::UI_MENU_FLAGS::kModal;
            menuFlags |= RE::UI_MENU_FLAGS::kDisablePauseMenu;
            menuFlags |= RE::UI_MENU_FLAGS::kUsesCursor;
            depthPriority = 3;
            inputContext  = Context::kMenuMode;
        }

        // Called after the menu is added to the UI stack — SWF is loaded and
        // frame 1 has run, so InvokeNoReturn is safe here.
        void PostCreate() override {
            InvokeInit(uiMovie.get(), s_remainingPoints, s_pendingCarryOver);
        }

        // Register AS2 -> C++ callbacks.
        void Accept(CallbackProcessor* a_cbReg) override {
            a_cbReg->Process("EA_OnAllocate", OnAllocate);
            a_cbReg->Process("EA_OnConfirm",  OnConfirm);
            a_cbReg->Process("EA_OnReset",    OnReset);
        }

    private:
        // AS2 calls this when the player clicks ">" on a skill.
        static void OnAllocate(const RE::FxDelegateArgs& a_args) {
            if (a_args.GetArgCount() < 1) return;
            auto av = static_cast<RE::ActorValue>(
                static_cast<int>(a_args[0].GetNumber()));
            EA::SkillMenu::AllocatePoint(av);
        }

        // AS2 calls this when the player clicks "Confirm".
        static void OnConfirm(const RE::FxDelegateArgs&) {
            EA::SkillMenu::Confirm();
        }

        // AS2 calls this when the player clicks "Reset".
        static void OnReset(const RE::FxDelegateArgs&) {
            EA::SkillMenu::ResetAllocations();
        }
    };

    // -----------------------------------------------------------------------
    // MenuOpenCloseEvent sink — intercepts LevelUp Menu opening
    //
    // Uses an event sink instead of write_branch<5> on UIMessageQueue::AddMessage
    // to avoid collision with STB_Widgets (and any other mod that hooks AddMessage).
    // Two write_branch<5> on the same function corrupt each other's relative jumps.
    //
    // When the LevelUp Menu opens, we immediately close it and open our skill menu.
    // A one-shot s_allowVanillaLevelUp flag lets Confirm() pass the vanilla menu
    // through after skill allocation is complete.
    // -----------------------------------------------------------------------

    struct LevelUpMenuWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        static LevelUpMenuWatcher* GetSingleton() {
            static LevelUpMenuWatcher singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!a_event)
                return RE::BSEventNotifyControl::kContinue;

            if (a_event->menuName != RE::LevelUpMenu::MENU_NAME || !a_event->opening)
                return RE::BSEventNotifyControl::kContinue;

            if (s_allowVanillaLevelUp) {
                // Confirm() set this — let the vanilla menu through once.
                s_allowVanillaLevelUp = false;
                logger::info("[EA] SkillMenu watcher: Allowing vanilla LevelUp Menu through.");
                return RE::BSEventNotifyControl::kContinue;
            }

            if (!s_deferredLevelUp) {
                s_deferredLevelUp = true;
                logger::info("[EA] SkillMenu watcher: LevelUp Menu intercepted — closing it and opening skill menu.");

                // Defer to avoid modifying UI state inside the event handler.
                SKSE::GetTaskInterface()->AddTask([]() {
                    RE::UIMessageQueue::GetSingleton()->AddMessage(
                        RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    EA::SkillMenu::Open();
                });
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    void Register() {
        // Register the custom menu with the UI system.
        RE::UI::GetSingleton()->Register(MENU_NAME, EASkillMenu::Creator);
        logger::info("[EA] SkillMenu: '{}' registered.", MENU_NAME);

        // Register MenuOpenCloseEvent sink to intercept LevelUp Menu.
        // Uses event sink instead of write_branch<5> on UIMessageQueue::AddMessage
        // to avoid collision with STB_Widgets which also hooks that function.
        RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(
            LevelUpMenuWatcher::GetSingleton());
        logger::info("[EA] SkillMenu: LevelUpMenuWatcher event sink registered.");
    }

    void Open() {
        // Snapshot current skill levels so Reset can restore them.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto* avo = static_cast<RE::Actor*>(player)->AsActorValueOwner();
            for (int i = 0; i < NUM_SKILLS; ++i) {
                s_snapshotLevels[i] = avo->GetBaseActorValue(SKILLS[i].av);
            }
        }

        // Compute total available points: new grant + carry-over from cosave.
        s_pendingCarryOver = EA::XPManager::GetPendingSkillPoints();
        s_remainingPoints  = EA::Config::skillPointsPerLevel + s_pendingCarryOver;

        logger::info("[EA] SkillMenu::Open — totalPoints={} (new={} + carry={})",
            s_remainingPoints, EA::Config::skillPointsPerLevel, s_pendingCarryOver);

        RE::UIMessageQueue::GetSingleton()->AddMessage(
            MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    }

    void AllocatePoint(RE::ActorValue a_skill) {
        if (s_remainingPoints <= 0) {
            logger::warn("[EA] SkillMenu::AllocatePoint: no points remaining.");
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto* avo = static_cast<RE::Actor*>(player)->AsActorValueOwner();

        float current = avo->GetBaseActorValue(a_skill);
        if (current >= 100.0f) {
            logger::info("[EA] SkillMenu::AllocatePoint: AV={} already at 100.",
                static_cast<int>(a_skill));
            return;
        }

        avo->SetBaseActorValue(a_skill, current + 1.0f);
        --s_remainingPoints;

        logger::info("[EA] SkillMenu: Allocated point to AV={}. {:.0f} -> {:.0f}. Remaining: {}",
            static_cast<int>(a_skill), current, current + 1.0f, s_remainingPoints);

        // Reflect the change in the SWF.
        auto* ui   = RE::UI::GetSingleton();
        auto  menu = ui->GetMenu(MENU_NAME);
        if (menu && menu->uiMovie) {
            InvokeUpdateSkill(menu->uiMovie.get(), a_skill, current + 1.0f);
            InvokeUpdatePoints(menu->uiMovie.get(), s_remainingPoints);
        }

        if (s_remainingPoints <= 0) {
            Confirm();
        }
    }

    void Confirm() {
        // Persist unspent points to cosave for next level-up.
        EA::XPManager::SetPendingSkillPoints(s_remainingPoints);

        logger::info("[EA] SkillMenu::Confirm — carry-over saved: {} points.", s_remainingPoints);

        // Close our menu.
        RE::UIMessageQueue::GetSingleton()->AddMessage(
            MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);

        // Re-open the vanilla LevelUp Menu, bypassing our hook.
        if (s_deferredLevelUp) {
            s_deferredLevelUp     = false;
            s_allowVanillaLevelUp = true;
            RE::UIMessageQueue::GetSingleton()->AddMessage(
                RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
            logger::info("[EA] SkillMenu::Confirm — vanilla LevelUp Menu queued.");
        }
    }

    static void ResetAllocations() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto* avo = static_cast<RE::Actor*>(player)->AsActorValueOwner();

        // Restore all skills to their snapshot values.
        for (int i = 0; i < NUM_SKILLS; ++i) {
            avo->SetBaseActorValue(SKILLS[i].av, s_snapshotLevels[i]);
        }

        // Restore points.
        s_remainingPoints = EA::Config::skillPointsPerLevel + s_pendingCarryOver;

        logger::info("[EA] SkillMenu::ResetAllocations — all points restored ({})", s_remainingPoints);

        // Update SWF.
        auto* ui   = RE::UI::GetSingleton();
        auto  menu = ui->GetMenu(MENU_NAME);
        if (menu && menu->uiMovie) {
            for (int i = 0; i < NUM_SKILLS; ++i) {
                InvokeUpdateSkill(menu->uiMovie.get(), SKILLS[i].av, s_snapshotLevels[i]);
            }
            InvokeUpdatePoints(menu->uiMovie.get(), s_remainingPoints);
        }
    }

    // -----------------------------------------------------------------------
    // Internal: Scaleform call helpers
    // -----------------------------------------------------------------------

    static void InvokeInit(RE::GFxMovieView* a_movie, int totalPoints, int carryOver) {
        if (!a_movie) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto* avo = static_cast<RE::Actor*>(player)->AsActorValueOwner();

        // Build skillData array: [{name, actorValue, currentLevel, column, row}, ...]
        RE::GFxValue skillArr;
        a_movie->CreateArray(&skillArr);

        auto* avList = RE::ActorValueList::GetSingleton();

        for (const auto& entry : SKILLS) {
            RE::GFxValue obj;
            a_movie->CreateObject(&obj);

            // Fetch engine-localised skill name (respects mods that rename skills).
            auto*       avInfo    = avList ? avList->GetActorValue(entry.av) : nullptr;
            const char* skillName = (avInfo && avInfo->fullName.data() && avInfo->fullName.data()[0])
                                    ? avInfo->fullName.data()
                                    : "???";

            obj.SetMember("name",         RE::GFxValue(skillName));
            obj.SetMember("actorValue",   RE::GFxValue(static_cast<double>(
                                              static_cast<int>(entry.av))));
            obj.SetMember("currentLevel", RE::GFxValue(static_cast<double>(
                                              avo->GetBaseActorValue(entry.av))));
            obj.SetMember("column",       RE::GFxValue(static_cast<double>(entry.col)));
            obj.SetMember("row",          RE::GFxValue(static_cast<double>(entry.row)));

            skillArr.PushBack(obj);
        }

        RE::GFxValue args[15];
        args[0] = skillArr;
        args[1] = RE::GFxValue(static_cast<double>(totalPoints));
        args[2] = RE::GFxValue(static_cast<double>(carryOver));
        args[3] = RE::GFxValue(static_cast<double>(EA::Config::menuPanelWidth));
        args[4] = RE::GFxValue(static_cast<double>(EA::Config::menuPanelHeight));
        args[5] = RE::GFxValue(static_cast<double>(EA::Config::menuPanelYOffset));
        args[6] = RE::GFxValue(static_cast<double>(EA::Config::menuSkillRowGap));
        args[7] = RE::GFxValue(static_cast<double>(EA::Config::menuSkillColumnGap));
        args[8] = RE::GFxValue(static_cast<double>(EA::Config::menuSkillLabelValueGap));
        args[9] = RE::GFxValue(static_cast<double>(EA::Config::menuSkillValueArrowGap));
        args[10] = RE::GFxValue(static_cast<double>(EA::Config::menuSkillButtonTopGap));
        args[11] = RE::GFxValue(static_cast<double>(EA::Config::menuSkillButtonRowOffset));
        args[12] = RE::GFxValue(static_cast<double>(EA::Config::menuSkillButtonGap));
        args[13] = RE::GFxValue(static_cast<double>(EA::Config::menuFontSize));
        args[14] = RE::GFxValue(static_cast<double>(EA::Config::menuHeaderFontSize));

        a_movie->InvokeNoReturn("EA_Init", args, 15);

        logger::info("[EA] SkillMenu: EA_Init called (totalPoints={}, carryOver={}, yOffset={}, rowGap={}, colGap={}, buttonRowOffset={}).",
            totalPoints, carryOver, EA::Config::menuPanelYOffset,
            EA::Config::menuSkillRowGap, EA::Config::menuSkillColumnGap,
            EA::Config::menuSkillButtonRowOffset);
    }

    static void InvokeUpdateSkill(RE::GFxMovieView* a_movie, RE::ActorValue a_av, float a_level) {
        RE::GFxValue args[2];
        args[0] = RE::GFxValue(static_cast<double>(static_cast<int>(a_av)));
        args[1] = RE::GFxValue(static_cast<double>(a_level));
        a_movie->InvokeNoReturn("EA_UpdateSkill", args, 2);
    }

    static void InvokeUpdatePoints(RE::GFxMovieView* a_movie, int remaining) {
        RE::GFxValue arg(static_cast<double>(remaining));
        a_movie->InvokeNoReturn("EA_UpdatePoints", &arg, 1);
    }

}  // namespace EA::SkillMenu
