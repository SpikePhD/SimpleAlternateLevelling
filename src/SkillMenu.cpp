#include "PCH.h"
#include "SkillMenu.h"

#include "Config.h"
#include "UIRules.h"
#include "XPManager.h"

#include <SKSE/Translation.h>

#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <string>

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

        UIRules::AllocationSession s_session;
        std::atomic<std::uint64_t>  s_generation{ 1 };
        RE::GFxMovieView*           s_activeMovie{ nullptr };
        bool                        s_registered{ false };
        bool                        s_deferredLevelUp{ false };
        bool                        s_allowVanillaLevelUp{ false };
        bool                        s_vanillaContinuationQueued{ false };
        bool                        s_movieLoaded{ false };
        int                         s_pendingCarryOver{ 0 };

        std::string s_pointsLabel{ "Skill points to distribute:" };
        std::string s_confirmLabel{ "Confirm" };
        std::string s_resetLabel{ "Reset" };

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
            QueueVanillaContinuation(reason);
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

            std::array<RE::GFxValue, 18> args;
            args[0] = skillArray;
            args[1] = s_session.TotalPoints();
            args[2] = s_pendingCarryOver;
            args[3] = Config::menuPanelWidth;
            args[4] = Config::menuPanelHeight;
            args[5] = Config::menuPanelYOffset;
            args[6] = Config::menuSkillRowGap;
            args[7] = Config::menuSkillColumnGap;
            args[8] = Config::menuSkillLabelValueGap;
            args[9] = Config::menuSkillValueArrowGap;
            args[10] = Config::menuSkillButtonTopGap;
            args[11] = Config::menuSkillButtonRowOffset;
            args[12] = Config::menuSkillButtonGap;
            args[13] = Config::menuFontSize;
            args[14] = Config::menuHeaderFontSize;
            args[15] = s_pointsLabel;
            args[16] = s_confirmLabel;
            args[17] = s_resetLabel;

            if (!movie->Invoke("EA_Init", nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
                logger::error("[EA] SkillMenu: required EA_Init function is missing from the SWF.");
                return false;
            }
            logger::info("[EA] SkillMenu: transactional session initialized with {} points (carry={}).",
                s_session.TotalPoints(), s_pendingCarryOver);
            return true;
        }

        void InvokeUpdateSkill(std::size_t index)
        {
            auto* movie = ActiveMovie();
            if (!movie || index >= kSkills.size()) {
                return;
            }
            std::array<RE::GFxValue, 2> args{
                RE::GFxValue(static_cast<int>(kSkills[index].actorValue)),
                RE::GFxValue(s_session.Preview(index))
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
                if (s_allowVanillaLevelUp) {
                    s_allowVanillaLevelUp = false;
                    s_vanillaContinuationQueued = false;
                    s_deferredLevelUp = false;
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
                    Open();
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
        const auto total = UIRules::CheckedPointTotal(s_pendingCarryOver, Config::skillPointsPerLevel);
        if (!total) {
            logger::error("[EA] SkillMenu: pending points plus level grant overflowed; preserving pending={} and continuing vanilla.",
                s_pendingCarryOver);
            s_session.Cancel();
            QueueVanillaContinuation("point-total-overflow");
            return;
        }
        if (*total == 0) {
            s_session.Cancel();
            QueueVanillaContinuation("no-points");
            return;
        }

        std::array<float, UIRules::kSkillCount> snapshot{};
        RE::ActorValueOwner* owner = nullptr;
        if (!ReadCurrentSkillValues(snapshot, owner) || !s_session.Begin(*total, Config::skillCap, snapshot)) {
            logger::warn("[EA] SkillMenu: unable to start a safe allocation session.");
            s_session.Cancel();
            QueueVanillaContinuation("invalid-session-input");
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

    void ResetState()
    {
        s_generation.fetch_add(1);
        s_session.Cancel();
        s_activeMovie = nullptr;
        s_deferredLevelUp = false;
        s_allowVanillaLevelUp = false;
        s_vanillaContinuationQueued = false;
        s_movieLoaded = false;
        s_pendingCarryOver = 0;

        auto* ui = RE::UI::GetSingleton();
        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (ui && queue && ui->IsMenuOpen(kMenuName)) {
            queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
        logger::debug("[EA] SkillMenu: lifecycle state and deferred work reset.");
    }
}
