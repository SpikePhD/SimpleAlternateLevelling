#include "PCH.h"
#include "SkillMenu.h"

#include "Config.h"
#include "Integration.h"
#include "UIRules.h"
#include "XPManager.h"

#include <SKSE/Translation.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>
#include <thread>

namespace EA::SkillMenu {
    namespace {
        constexpr std::string_view kMenuName = "EA Skill Menu";
        constexpr std::string_view kSwfName = "EA_SkillMenu";

        struct SkillEntry {
            RE::ActorValue actorValue;
            int            column;
            int            row;
        };

        constexpr std::array<SkillEntry, UIRules::kSkillCount> kSkills{{
            { RE::ActorValue::kOneHanded, 0, 0 },
            { RE::ActorValue::kTwoHanded, 0, 1 },
            { RE::ActorValue::kBlock, 0, 2 },
            { RE::ActorValue::kHeavyArmor, 0, 3 },
            { RE::ActorValue::kLightArmor, 0, 4 },
            { RE::ActorValue::kArchery, 0, 5 },
            { RE::ActorValue::kAlteration, 1, 0 },
            { RE::ActorValue::kConjuration, 1, 1 },
            { RE::ActorValue::kDestruction, 1, 2 },
            { RE::ActorValue::kIllusion, 1, 3 },
            { RE::ActorValue::kRestoration, 1, 4 },
            { RE::ActorValue::kSneak, 1, 5 },
            { RE::ActorValue::kSmithing, 2, 0 },
            { RE::ActorValue::kAlchemy, 2, 1 },
            { RE::ActorValue::kEnchanting, 2, 2 },
            { RE::ActorValue::kPickpocket, 2, 3 },
            { RE::ActorValue::kLockpicking, 2, 4 },
            { RE::ActorValue::kSpeech, 2, 5 }
        }};

        constexpr auto MakeActorValueWhitelist()
        {
            std::array<int, UIRules::kSkillCount> result{};
            for (std::size_t index = 0; index < kSkills.size(); ++index) {
                result[index] = static_cast<int>(kSkills[index].actorValue);
            }
            return result;
        }

        constexpr auto kActorValueWhitelist = MakeActorValueWhitelist();

        constexpr auto kContinuationSampleInterval = std::chrono::milliseconds(250);

        UIRules::AllocationSession s_session;
        UIRules::LevelUpFlow        s_flow;
        std::atomic<std::uint64_t>  s_generation{ 1 };
        std::atomic<std::uint64_t>  s_continuationWatch{ 0 };
        RE::GFxMovieView*           s_activeMovie{ nullptr };
        bool                        s_registered{ false };
        bool                        s_deferredLevelUp{ false };
        bool                        s_allowVanillaLevelUp{ false };
        bool                        s_vanillaContinuationQueued{ false };
        bool                        s_movieLoaded{ false };
        int                         s_pendingCarryOver{ 0 };
        int                         s_levelBonus{ 0 };

        std::string s_pointsLabel{ "Distribute Skill Points" };
        std::string s_confirmLabel{ "Confirm" };
        std::string s_resetLabel{ "Reset" };
        std::string s_levelLabel{ "Level" };
        std::string s_remainingLabel{ "points remaining" };
        std::string s_carriedLabel{ "carried over from earlier levels" };
        std::string s_bonusLabel{ "bonus from other mods" };
        std::string s_maxLabel{ "Max" };
        std::string s_combatLabel{ "Combat" };
        std::string s_magicLabel{ "Magic" };
        std::string s_stealthLabel{ "Stealth" };
        std::string s_hintLabel{ "Arrows: select    Enter or +: add    Backspace or -: remove    R: reset    C or Esc: confirm" };

        [[nodiscard]] bool ReadCurrentSkillValues(
            std::array<float, UIRules::kSkillCount>& values,
            RE::ActorValueOwner*& owner)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                logger::warn("[EA] SkillMenu: PlayerCharacter is unavailable.");
                return false;
            }
            owner = static_cast<RE::Actor*>(player)->AsActorValueOwner();
            if (!owner) {
                logger::warn("[EA] SkillMenu: player ActorValueOwner is unavailable.");
                return false;
            }
            for (std::size_t index = 0; index < kSkills.size(); ++index) {
                values[index] = owner->GetBaseActorValue(kSkills[index].actorValue);
                if (!std::isfinite(values[index])) {
                    logger::warn("[EA] SkillMenu: skill AV={} has invalid native value {}.",
                        static_cast<int>(kSkills[index].actorValue), values[index]);
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] RE::GFxMovieView* ActiveMovie()
        {
            if (s_activeMovie) {
                return s_activeMovie;
            }
            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                return nullptr;
            }
            const auto menu = ui->GetMenu(kMenuName);
            return menu && menu->uiMovie ? menu->uiMovie.get() : nullptr;
        }

        void QueueVanillaContinuation(std::string_view reason)
        {
            if (!s_deferredLevelUp || s_vanillaContinuationQueued) {
                return;
            }
            auto* queue = RE::UIMessageQueue::GetSingleton();
            if (!queue) {
                logger::error("[EA] SkillMenu: UIMessageQueue unavailable; cannot continue vanilla LevelUp Menu (reason={}).",
                    reason);
                s_deferredLevelUp = false;
                return;
            }
            s_vanillaContinuationQueued = true;
            s_allowVanillaLevelUp = true;
            queue->AddMessage(RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
            logger::info("[EA] SkillMenu: vanilla LevelUp Menu queued once (reason={}).", reason);
        }

        [[nodiscard]] std::uint32_t CurrentLevel()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player ? static_cast<std::uint32_t>(player->GetLevel()) : 0u;
        }

        // Asks the integration provider once per intercepted level-up (see
        // UIRules::LevelUpFlow::TakeBonusCall) and returns the clamped bonus.
        [[nodiscard]] int TakeSkillPointBonus()
        {
            if (!s_flow.TakeBonusCall() || !Integration::HasSkillPointBonus()) {
                return 0;
            }
            const auto level = CurrentLevel();
            const auto raw = Integration::SkillPointBonus(level);
            const auto bonus = UIRules::ClampSkillPointBonus(raw);
            if (bonus.negative) {
                logger::warn("[EA] SkillMenu: negative skill point bonus {} from integration counts as 0.", raw);
            } else if (bonus.clamped) {
                logger::warn("[EA] SkillMenu: skill point bonus {} from integration clamped to {}.",
                    raw, UIRules::kMaxSkillPointBonus);
            }
            logger::info("[EA] SkillMenu: skill point bonus from integration: {} (level {}).", bonus.value, level);
            return bonus.value;
        }

        [[nodiscard]] std::string_view WaitingStepName()
        {
            return s_flow.Stage() == UIRules::LevelUpStage::kPreStep ? "pre-skill-menu step" : "level-up step";
        }

        void HandOffOrContinue(std::string_view reason);

        // Resumes whichever integration step is waiting: the pre-step goes on
        // to the skill menu, the post-step to the vanilla LevelUp Menu.
        void ContinueFromStep(std::string_view reason)
        {
            switch (s_flow.Continue()) {
                case UIRules::FlowAction::kOpenSkillMenu:
                    s_continuationWatch.fetch_add(1);
                    logger::info("[EA] SkillMenu: pre-skill-menu step finished (reason={}).", reason);
                    if (s_session.State() == UIRules::SessionState::kOpening) {
                        Open();
                    } else {
                        logger::warn("[EA] SkillMenu: allocation session was lost during the pre-skill-menu step; skipping the skill menu.");
                        HandOffOrContinue("pre-step-session-lost");
                    }
                    return;
                case UIRules::FlowAction::kOpenVanilla:
                    s_continuationWatch.fetch_add(1);
                    logger::info("[EA] SkillMenu: level-up step finished (reason={}).", reason);
                    QueueVanillaContinuation(reason);
                    return;
                default:
                    logger::debug("[EA] SkillMenu: continuation ignored; no integration step is waiting (reason={}).", reason);
                    return;
            }
        }

        // Runs on the main thread for each fail-safe sample while a level-up
        // step is waiting.
        void SampleContinuation(std::uint64_t watch, std::uint64_t generation)
        {
            if (s_continuationWatch.load() != watch || s_generation.load() != generation || !s_flow.Waiting()) {
                return;
            }
            auto* ui = RE::UI::GetSingleton();
            const bool paused = ui && ui->GameIsPaused();
            const double now = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (!s_flow.ObserveSample(paused, now)) {
                return;
            }
            logger::warn("[EA] SkillMenu: {} did not call ContinueLevelUp within {:.0f}s of unpaused play; continuing.",
                WaitingStepName(), UIRules::LevelUpHandoff::kDefaultGraceSeconds);
            ContinueFromStep("continuation-fail-safe");
        }

        // A background ticker posts one sample task per interval; it stops as
        // soon as the wait ends or lifecycle state is invalidated.
        void StartContinuationWatch()
        {
            const auto watch = s_continuationWatch.fetch_add(1) + 1;
            const auto generation = s_generation.load();
            try {
                std::thread([watch, generation]() {
                    while (s_continuationWatch.load() == watch && s_generation.load() == generation) {
                        std::this_thread::sleep_for(kContinuationSampleInterval);
                        auto* tasks = SKSE::GetTaskInterface();
                        if (!tasks) {
                            return;
                        }
                        tasks->AddTask([watch, generation]() { SampleContinuation(watch, generation); });
                    }
                }).detach();
            } catch (const std::system_error& error) {
                logger::warn("[EA] SkillMenu: continuation fail-safe unavailable: {}.", error.what());
            }
        }

        // Starts the sequence once the vanilla menu is hidden: a registered
        // pre-skill-menu step may run before SAL's skill menu.
        void BeginLevelUpFlow()
        {
            const bool registered = Integration::HasPreSkillMenuStep();
            const auto level = CurrentLevel();
            const bool wantsStep = registered && Integration::WantsPreSkillMenuStep(level);
            if (s_flow.Begin(registered, wantsStep) == UIRules::FlowAction::kAwaitStep) {
                logger::info("[EA] SkillMenu: waiting for integration pre-skill-menu step at level {}.", level);
                StartContinuationWatch();
                return;
            }
            Open();
        }

        // Every path that ends SAL's part of a level-up comes through here:
        // a registered integration step may run before the vanilla menu.
        void HandOffOrContinue(std::string_view reason)
        {
            if (!s_deferredLevelUp || s_vanillaContinuationQueued || s_flow.Waiting()) {
                return;
            }
            const bool registered = Integration::HasLevelUpStep();
            const auto level = CurrentLevel();
            const bool wantsStep = registered && Integration::WantsLevelUpStep(level);
            switch (s_flow.FinishSkillMenu(registered, wantsStep)) {
                case UIRules::FlowAction::kAwaitStep:
                    logger::info("[EA] SkillMenu: waiting for integration level-up step at level {} (reason={}).",
                        level, reason);
                    StartContinuationWatch();
                    return;
                case UIRules::FlowAction::kOpenVanilla:
                    QueueVanillaContinuation(reason);
                    return;
                default:
                    return;
            }
        }

        void CloseCustomAndContinue(std::string_view reason)
        {
            s_session.MarkClosing();
            if (auto* movie = ActiveMovie()) {
                movie->Invoke("EA_SetClosing", nullptr, nullptr, 0);
            }
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            } else {
                logger::warn("[EA] SkillMenu: UIMessageQueue unavailable while closing custom menu.");
            }
            HandOffOrContinue(reason);
        }

        void PreserveAllAndContinue(std::string_view reason)
        {
            if (s_session.State() == UIRules::SessionState::kActive ||
                s_session.State() == UIRules::SessionState::kCommitting) {
                XPManager::SetPendingSkillPoints(s_session.TotalPoints());
                logger::warn("[EA] SkillMenu: commit rejected; preserving all {} session points (reason={}).",
                    s_session.TotalPoints(), reason);
            }
            CloseCustomAndContinue(reason);
        }

        [[nodiscard]] bool InvokeInit(RE::GFxMovieView* movie)
        {
            if (!movie || s_session.State() != UIRules::SessionState::kActive) {
                return false;
            }

            RE::GFxValue skillArray;
            movie->CreateArray(&skillArray);
            auto* actorValues = RE::ActorValueList::GetSingleton();
            for (std::size_t index = 0; index < kSkills.size(); ++index) {
                const auto& entry = kSkills[index];
                RE::GFxValue object;
                movie->CreateObject(&object);
                auto* info = actorValues ? RE::ActorValueList::GetActorValueInfo(entry.actorValue) : nullptr;
                const char* name = info && info->fullName.data() && info->fullName.data()[0]
                    ? info->fullName.data()
                    : "???";
                object.SetMember("name", RE::GFxValue(name));
                object.SetMember("actorValue", RE::GFxValue(static_cast<int>(entry.actorValue)));
                object.SetMember("currentLevel", RE::GFxValue(s_session.Preview(index)));
                object.SetMember("skillCap", RE::GFxValue(Config::skillCap));
                object.SetMember("column", RE::GFxValue(entry.column));
                object.SetMember("row", RE::GFxValue(entry.row));
                skillArray.PushBack(object);
            }

            // Presentation strings and context for the redesigned layout.
            RE::GFxValue info;
            movie->CreateObject(&info);
            auto* player = RE::PlayerCharacter::GetSingleton();
            info.SetMember("level", RE::GFxValue(player ? static_cast<int>(player->GetLevel()) : 0));
            info.SetMember("levelLabel", RE::GFxValue(s_levelLabel.c_str()));
            info.SetMember("remainingLabel", RE::GFxValue(s_remainingLabel.c_str()));
            info.SetMember("carriedLabel", RE::GFxValue(s_carriedLabel.c_str()));
            info.SetMember("bonus", RE::GFxValue(s_levelBonus));
            info.SetMember("bonusLabel", RE::GFxValue(s_bonusLabel.c_str()));
            info.SetMember("maxLabel", RE::GFxValue(s_maxLabel.c_str()));
            info.SetMember("combatLabel", RE::GFxValue(s_combatLabel.c_str()));
            info.SetMember("magicLabel", RE::GFxValue(s_magicLabel.c_str()));
            info.SetMember("stealthLabel", RE::GFxValue(s_stealthLabel.c_str()));
            info.SetMember("hint", RE::GFxValue(s_hintLabel.c_str()));

            std::array<RE::GFxValue, 7> args;
            args[0] = skillArray;
            args[1] = s_session.TotalPoints();
            args[2] = s_pendingCarryOver;
            args[3] = s_pointsLabel;
            args[4] = s_confirmLabel;
            args[5] = s_resetLabel;
            args[6] = info;

            if (!movie->Invoke("EA_Init", nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
                logger::error("[EA] SkillMenu: required EA_Init function is missing from the SWF.");
                return false;
            }
            logger::info("[EA] SkillMenu: transactional session initialized with {} points (carry={}, bonus={}).",
                s_session.TotalPoints(), s_pendingCarryOver, s_levelBonus);
            return true;
        }

        void InvokeUpdateSkill(std::size_t index)
        {
            auto* movie = ActiveMovie();
            if (!movie || index >= kSkills.size()) {
                return;
            }
            std::array<RE::GFxValue, 3> args{
                RE::GFxValue(static_cast<int>(kSkills[index].actorValue)),
                RE::GFxValue(s_session.Preview(index)),
                RE::GFxValue(static_cast<double>(s_session.Delta(index)))
            };
            if (!movie->Invoke("EA_UpdateSkill", nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
                logger::warn("[EA] SkillMenu: EA_UpdateSkill is missing from the active SWF.");
            }
        }

        void InvokeUpdatePoints()
        {
            auto* movie = ActiveMovie();
            if (!movie) {
                return;
            }
            RE::GFxValue remaining(s_session.RemainingPoints());
            if (!movie->Invoke("EA_UpdatePoints", nullptr, &remaining, 1)) {
                logger::warn("[EA] SkillMenu: EA_UpdatePoints is missing from the active SWF.");
            }
        }

        [[nodiscard]] bool IsValidCallback(const RE::FxDelegateArgs& args, std::uint32_t expectedArgs)
        {
            if (s_session.State() != UIRules::SessionState::kActive ||
                !s_activeMovie || args.GetMovie() != s_activeMovie || args.GetArgCount() != expectedArgs) {
                logger::warn("[EA] SkillMenu: rejected stale or malformed Scaleform callback.");
                return false;
            }
            return true;
        }

        void LoadTranslations()
        {
            SKSE::Translation::ParseTranslation("SimpleAlternateLevelling");
            const auto translate = [](std::string_view key, std::string_view fallback) {
                std::string result;
                if (!SKSE::Translation::Translate(std::string(key), result) || result.empty()) {
                    logger::warn("[EA] SkillMenu: translation {} unavailable; using English fallback.", key);
                    return std::string(fallback);
                }
                return result;
            };
            s_pointsLabel = translate("$SAL_SKILL_POINTS_LABEL", "Skill points to distribute:");
            s_confirmLabel = translate("$SAL_CONFIRM", "Confirm");
            s_resetLabel = translate("$SAL_RESET", "Reset");
            s_levelLabel = translate("$SAL_ALLOC_LEVEL", "Level");
            s_remainingLabel = translate("$SAL_ALLOC_REMAINING", "points remaining");
            s_carriedLabel = translate("$SAL_ALLOC_CARRIED", "carried over from earlier levels");
            s_bonusLabel = translate("$SAL_ALLOC_BONUS", "bonus from other mods");
            s_maxLabel = translate("$SAL_ALLOC_MAX", "Max");
            s_combatLabel = translate("$SAL_GROUP_COMBAT", "Combat");
            s_magicLabel = translate("$SAL_GROUP_MAGIC", "Magic");
            s_stealthLabel = translate("$SAL_GROUP_STEALTH", "Stealth");
            s_hintLabel = translate("$SAL_ALLOC_HINT", s_hintLabel);
        }

        class EASkillMenu final : public RE::IMenu {
        public:
            static RE::stl::owner<RE::IMenu*> Creator() { return new EASkillMenu(); }

            EASkillMenu()
            {
                auto* scaleform = RE::BSScaleformManager::GetSingleton();
                s_movieLoaded = scaleform && scaleform->LoadMovie(this, uiMovie, kSwfName.data());
                if (!s_movieLoaded) {
                    logger::error("[EA] SkillMenu: failed to load Interface/EA_SkillMenu.swf.");
                }
                menuFlags |= RE::UI_MENU_FLAGS::kPausesGame;
                menuFlags |= RE::UI_MENU_FLAGS::kModal;
                menuFlags |= RE::UI_MENU_FLAGS::kDisablePauseMenu;
                menuFlags |= RE::UI_MENU_FLAGS::kUsesCursor;
                depthPriority = 3;
                inputContext = Context::kMenuMode;
            }

            void PostCreate() override
            {
                s_activeMovie = uiMovie.get();
                if (!s_movieLoaded || !s_activeMovie || !InvokeInit(s_activeMovie)) {
                    PreserveAllAndContinue("swf-initialization-failed");
                }
            }

            void Accept(CallbackProcessor* callbacks) override
            {
                if (!callbacks) {
                    logger::error("[EA] SkillMenu: Scaleform callback processor is null.");
                    return;
                }
                callbacks->Process("EA_OnAllocate", OnAllocate);
                callbacks->Process("EA_OnDeallocate", OnDeallocate);
                callbacks->Process("EA_OnConfirm", OnConfirm);
                callbacks->Process("EA_OnReset", OnReset);
            }

        private:
            static void OnAllocate(const RE::FxDelegateArgs& args)
            {
                if (!IsValidCallback(args, 1) || !args[0].IsNumber()) {
                    return;
                }
                const auto identifier = UIRules::ParseIntegralIdentifier(args[0].GetNumber());
                const auto index = identifier
                    ? UIRules::FindWhitelistedIdentifier(*identifier, kActorValueWhitelist)
                    : std::nullopt;
                if (!index) {
                    logger::warn("[EA] SkillMenu: rejected non-skill allocation identifier.");
                    return;
                }
                AllocatePoint(kSkills[*index].actorValue);
            }

            static void OnDeallocate(const RE::FxDelegateArgs& args)
            {
                if (!IsValidCallback(args, 1) || !args[0].IsNumber()) {
                    return;
                }
                const auto identifier = UIRules::ParseIntegralIdentifier(args[0].GetNumber());
                const auto index = identifier
                    ? UIRules::FindWhitelistedIdentifier(*identifier, kActorValueWhitelist)
                    : std::nullopt;
                if (!index) {
                    logger::warn("[EA] SkillMenu: rejected non-skill deallocation identifier.");
                    return;
                }
                DeallocatePoint(kSkills[*index].actorValue);
            }

            static void OnConfirm(const RE::FxDelegateArgs& args)
            {
                if (IsValidCallback(args, 0)) {
                    Confirm();
                }
            }

            static void OnReset(const RE::FxDelegateArgs& args)
            {
                if (IsValidCallback(args, 0)) {
                    ResetAllocations();
                }
            }
        };

        struct MenuWatcher final : RE::BSTEventSink<RE::MenuOpenCloseEvent> {
            static MenuWatcher* GetSingleton()
            {
                static MenuWatcher singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::MenuOpenCloseEvent* event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!event) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (event->menuName == kMenuName && !event->opening) {
                    s_activeMovie = nullptr;
                    if (s_session.State() == UIRules::SessionState::kActive) {
                        const auto generation = s_generation.load();
                        if (auto* tasks = SKSE::GetTaskInterface()) {
                            tasks->AddTask([generation]() {
                                if (s_generation.load() == generation &&
                                    s_session.State() == UIRules::SessionState::kActive) {
                                    Confirm();
                                }
                            });
                        } else {
                            logger::warn("[EA] SkillMenu: task interface unavailable after unexpected menu close; committing immediately.");
                            Confirm();
                        }
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (event->menuName != RE::LevelUpMenu::MENU_NAME || !event->opening) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (s_flow.Waiting() && s_flow.Stage() == UIRules::LevelUpStage::kPreStep) {
                    // Something else opened the vanilla menu before the skill
                    // menu ran. Hide it again and continue to the skill menu
                    // so this level's skill points are not lost.
                    logger::warn("[EA] SkillMenu: vanilla LevelUp Menu opened while the pre-skill-menu step was waiting; hiding it and continuing.");
                    const auto generation = s_generation.load();
                    if (auto* tasks = SKSE::GetTaskInterface()) {
                        tasks->AddTask([generation]() {
                            if (s_generation.load() != generation) {
                                return;
                            }
                            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                                queue->AddMessage(RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                            }
                            ContinueFromStep("vanilla-opened-during-pre-step");
                        });
                    } else {
                        ContinueFromStep("vanilla-opened-during-pre-step");
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (s_flow.Waiting()) {
                    // Something else opened the vanilla menu while the
                    // post-skill-menu step was pending; treat it as the
                    // continuation.
                    logger::warn("[EA] SkillMenu: vanilla LevelUp Menu opened while a level-up step was waiting; ending the wait.");
                    s_continuationWatch.fetch_add(1);
                    s_allowVanillaLevelUp = true;
                }
                if (s_allowVanillaLevelUp) {
                    s_allowVanillaLevelUp = false;
                    s_vanillaContinuationQueued = false;
                    s_deferredLevelUp = false;
                    s_flow.Reset();
                    s_session.Cancel();
                    logger::info("[EA] SkillMenu: allowing one vanilla LevelUp Menu open.");
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (s_deferredLevelUp || !s_session.BeginOpening()) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                s_deferredLevelUp = true;
                const auto generation = s_generation.load();
                auto* tasks = SKSE::GetTaskInterface();
                if (!tasks) {
                    logger::warn("[EA] SkillMenu: task interface unavailable; vanilla LevelUp Menu was not intercepted.");
                    s_deferredLevelUp = false;
                    s_session.Cancel();
                    return RE::BSEventNotifyControl::kContinue;
                }
                tasks->AddTask([generation]() {
                    if (s_generation.load() != generation ||
                        s_session.State() != UIRules::SessionState::kOpening) {
                        return;
                    }
                    auto* queue = RE::UIMessageQueue::GetSingleton();
                    if (!queue) {
                        logger::warn("[EA] SkillMenu: UIMessageQueue unavailable; leaving vanilla menu untouched.");
                        s_deferredLevelUp = false;
                        s_session.Cancel();
                        return;
                    }
                    queue->AddMessage(RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    BeginLevelUpFlow();
                });
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    bool Register()
    {
        if (s_registered) {
            logger::debug("[EA] SkillMenu: registration already completed.");
            return true;
        }
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::error("[EA] SkillMenu: UI singleton unavailable; custom allocation disabled.");
            return false;
        }
        LoadTranslations();
        ui->Register(kMenuName, EASkillMenu::Creator);
        ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatcher::GetSingleton());
        s_registered = true;
        logger::info("[EA] SkillMenu: custom menu and watcher registered.");
        return true;
    }

    void Open()
    {
        if (s_session.State() != UIRules::SessionState::kOpening) {
            logger::warn("[EA] SkillMenu: ignored Open outside the opening state.");
            return;
        }
        s_pendingCarryOver = XPManager::GetPendingSkillPoints();
        // Both terms are validated to at most 1000, so the grant cannot overflow.
        s_levelBonus = TakeSkillPointBonus();
        const auto total = UIRules::CheckedPointTotal(s_pendingCarryOver, Config::skillPointsPerLevel + s_levelBonus);
        if (!total) {
            logger::error("[EA] SkillMenu: pending points plus level grant overflowed; preserving pending={} and continuing vanilla (bonus={} lost).",
                s_pendingCarryOver, s_levelBonus);
            s_session.Cancel();
            HandOffOrContinue("point-total-overflow");
            return;
        }
        if (*total == 0) {
            s_session.Cancel();
            HandOffOrContinue("no-points");
            return;
        }

        std::array<float, UIRules::kSkillCount> snapshot{};
        RE::ActorValueOwner* owner = nullptr;
        if (!ReadCurrentSkillValues(snapshot, owner) || !s_session.Begin(*total, Config::skillCap, snapshot)) {
            // Keep this level's grant (including any integration bonus the
            // provider has already counted as given) for the next level-up.
            XPManager::SetPendingSkillPoints(*total);
            logger::warn("[EA] SkillMenu: unable to start a safe allocation session; preserving all {} points.", *total);
            s_session.Cancel();
            HandOffOrContinue("invalid-session-input");
            return;
        }
        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (!queue) {
            PreserveAllAndContinue("custom-menu-queue-unavailable");
            return;
        }
        queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    }

    void AllocatePoint(RE::ActorValue skill)
    {
        const auto index = UIRules::FindWhitelistedIdentifier(
            static_cast<int>(skill), kActorValueWhitelist);
        if (!index) {
            logger::warn("[EA] SkillMenu: native allocation request rejected for non-skill AV={}.",
                static_cast<int>(skill));
            return;
        }
        const auto result = s_session.Allocate(*index);
        if (result != UIRules::AllocationResult::kAllocated) {
            logger::debug("[EA] SkillMenu: allocation rejected for AV={} (reason={}).",
                static_cast<int>(skill), static_cast<int>(result));
            return;
        }
        InvokeUpdateSkill(*index);
        InvokeUpdatePoints();
        logger::info("[EA] SkillMenu: previewed AV={} at {:.1f}; {} points remain.",
            static_cast<int>(skill), s_session.Preview(*index), s_session.RemainingPoints());
    }

    void DeallocatePoint(RE::ActorValue skill)
    {
        const auto index = UIRules::FindWhitelistedIdentifier(
            static_cast<int>(skill), kActorValueWhitelist);
        if (!index) {
            logger::warn("[EA] SkillMenu: native deallocation request rejected for non-skill AV={}.",
                static_cast<int>(skill));
            return;
        }
        const auto result = s_session.Deallocate(*index);
        if (result != UIRules::AllocationResult::kAllocated) {
            logger::debug("[EA] SkillMenu: deallocation rejected for AV={} (reason={}).",
                static_cast<int>(skill), static_cast<int>(result));
            return;
        }
        InvokeUpdateSkill(*index);
        InvokeUpdatePoints();
        logger::info("[EA] SkillMenu: removed a point from AV={}, now {:.1f}; {} points remain.",
            static_cast<int>(skill), s_session.Preview(*index), s_session.RemainingPoints());
    }

    void Confirm()
    {
        if (s_session.State() != UIRules::SessionState::kActive) {
            logger::debug("[EA] SkillMenu: duplicate or stale confirm ignored.");
            return;
        }

        std::array<float, UIRules::kSkillCount> current{};
        RE::ActorValueOwner* owner = nullptr;
        if (!ReadCurrentSkillValues(current, owner)) {
            PreserveAllAndContinue("native-values-unavailable");
            return;
        }
        const auto plan = s_session.PrepareCommit(current);
        if (!plan.Ready()) {
            PreserveAllAndContinue(plan.status == UIRules::CommitStatus::kSnapshotDrift
                ? "snapshot-drift"
                : "invalid-commit-values");
            return;
        }

        for (std::size_t index = 0; index < kSkills.size(); ++index) {
            if (s_session.Delta(index) != 0) {
                owner->SetBaseActorValue(kSkills[index].actorValue, plan.finalValues[index]);
            }
        }
        XPManager::SetPendingSkillPoints(plan.pendingPoints);
        logger::info("[EA] SkillMenu: committed allocation transaction; carry-over={}.", plan.pendingPoints);
        CloseCustomAndContinue("confirmed");
    }

    void ResetAllocations()
    {
        if (!s_session.Reset()) {
            logger::debug("[EA] SkillMenu: stale reset ignored.");
            return;
        }
        for (std::size_t index = 0; index < kSkills.size(); ++index) {
            InvokeUpdateSkill(index);
        }
        InvokeUpdatePoints();
        logger::info("[EA] SkillMenu: preview transaction reset; {} points restored.",
            s_session.RemainingPoints());
    }

    bool IsDeferringVanillaLevelUp()
    {
        return s_deferredLevelUp;
    }

    void ContinueLevelUp()
    {
        ContinueFromStep("integration-continue");
    }

    void RequestContinueLevelUp()
    {
        const auto generation = s_generation.load();
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::warn("[EA] SkillMenu: TaskInterface unavailable; ContinueLevelUp request dropped.");
            return;
        }
        tasks->AddTask([generation]() {
            if (s_generation.load() != generation) {
                logger::debug("[EA] SkillMenu: stale ContinueLevelUp request discarded.");
                return;
            }
            ContinueLevelUp();
        });
    }

    void ResetState()
    {
        s_generation.fetch_add(1);
        s_session.Cancel();
        s_flow.Reset();
        s_continuationWatch.fetch_add(1);
        s_activeMovie = nullptr;
        s_deferredLevelUp = false;
        s_allowVanillaLevelUp = false;
        s_vanillaContinuationQueued = false;
        s_movieLoaded = false;
        s_pendingCarryOver = 0;
        s_levelBonus = 0;

        auto* ui = RE::UI::GetSingleton();
        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (ui && queue && ui->IsMenuOpen(kMenuName)) {
            queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
        logger::debug("[EA] SkillMenu: lifecycle state and deferred work reset.");
    }
}
