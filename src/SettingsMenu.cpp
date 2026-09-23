#include "PCH.h"
#include "SettingsMenu.h"

#include "Config.h"
#include "Leveling.h"
#include "SettingsModel.h"

#include <SKSE/Translation.h>

#include <array>
#include <atomic>
#include <cmath>
#include <string>

namespace EA::SettingsMenu {
    namespace {
        constexpr std::string_view kMenuName = "SAL Settings Menu";
        constexpr std::string_view kSwfName = "SAL_SettingsMenu";
        std::atomic<bool> s_queued{ false };
        RE::GFxMovieView* s_movie = nullptr;
        bool s_registered = false;
        bool s_textInputEnabled = false;

        // Skyrim delivers typed characters to Scaleform only while text input
        // is allowed. ControlMap keeps a counter, so every enable must be
        // paired with exactly one disable, including when the menu closes.
        void SetTextInput(bool enabled)
        {
            if (s_textInputEnabled == enabled) return;
            auto* controlMap = RE::ControlMap::GetSingleton();
            if (!controlMap) {
                logger::warn("[EA] Settings: ControlMap unavailable; text input unchanged.");
                return;
            }
            controlMap->AllowTextInput(enabled);
            s_textInputEnabled = enabled;
            logger::debug("[EA] Settings: text input {}.", enabled ? "enabled" : "disabled");
        }

        std::string Translate(std::string_view key)
        {
            std::string value;
            if (!SKSE::Translation::Translate(std::string(key), value) || value.empty()) {
                logger::warn("[EA] Settings: missing translation {}.", key);
                return std::string(key);
            }
            return value;
        }

        RE::GFxValue Rows(RE::GFxMovieView* movie)
        {
            RE::GFxValue rows;
            movie->CreateArray(&rows);
            const auto& settings = Config::Settings();
            const auto& descriptors = settings.Registry();
            const auto& draft = settings.Draft();
            for (std::size_t i = 0; i < descriptors.size(); ++i) {
                const auto& descriptor = descriptors[i];
                RE::GFxValue item;
                movie->CreateObject(&item);
                item.SetMember("index", RE::GFxValue(static_cast<int>(i)));
                item.SetMember("key", RE::GFxValue(descriptor.key.c_str()));
                item.SetMember("section", RE::GFxValue(static_cast<int>(descriptor.section)));
                item.SetMember("kind", RE::GFxValue(static_cast<int>(descriptor.kind)));
                item.SetMember("label", RE::GFxValue(Translate(descriptor.translationKey).c_str()));
                item.SetMember("step", RE::GFxValue(descriptor.step));
                item.SetMember("min", RE::GFxValue(descriptor.minimum));
                item.SetMember("max", RE::GFxValue(descriptor.maximum));
                const auto value = draft.at(nlohmann::json::json_pointer("/" + [&]() {
                    std::string path = descriptor.key;
                    std::replace(path.begin(), path.end(), '.', '/');
                    return path;
                }()));
                if (value.is_boolean()) item.SetMember("value", RE::GFxValue(value.get<bool>()));
                else if (value.is_string()) item.SetMember("value", RE::GFxValue(value.get<std::string>().c_str()));
                else item.SetMember("value", RE::GFxValue(value.get<double>()));
                rows.PushBack(item);
            }
            return rows;
        }

        void Refresh()
        {
            if (!s_movie) return;
            auto rows = Rows(s_movie);
            if (!s_movie->Invoke("SAL_Update", nullptr, &rows, 1)) {
                logger::warn("[EA] Settings: SAL_Update is missing from the active SWF.");
            }
        }

        void Close()
        {
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }

        bool ValidCallback(const RE::FxDelegateArgs& args, std::uint32_t count)
        {
            if (!s_movie || args.GetMovie() != s_movie || args.GetArgCount() != count) {
                logger::warn("[EA] Settings: stale or malformed Scaleform callback rejected.");
                return false;
            }
            return true;
        }

        class Menu final : public RE::IMenu {
        public:
            static RE::stl::owner<RE::IMenu*> Creator() { return new Menu(); }
            Menu()
            {
                auto* scaleform = RE::BSScaleformManager::GetSingleton();
                if (!scaleform || !scaleform->LoadMovie(this, uiMovie, kSwfName.data())) {
                    logger::error("[EA] Settings: could not load SAL_SettingsMenu.swf.");
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
                s_queued = false;
                s_movie = uiMovie.get();
                if (!s_movie) { Close(); return; }
                Config::Settings().Begin();
                auto rows = Rows(s_movie);
                RE::GFxValue sections, actions, presets, modes;
                s_movie->CreateArray(&sections);
                s_movie->CreateArray(&actions);
                s_movie->CreateArray(&presets);
                s_movie->CreateArray(&modes);
                for (const auto* key : {
                    "$SAL_SECTION_PROGRESSION", "$SAL_SECTION_QUESTS", "$SAL_SECTION_KILLS",
                    "$SAL_SECTION_EXPLORATION", "$SAL_SECTION_LOCKS", "$SAL_SECTION_BOOKS",
                    "$SAL_SECTION_PICKPOCKET", "$SAL_SECTION_STARTING", "$SAL_SECTION_ALLOCATION",
                    "$SAL_SECTION_NOTIFICATIONS", "$SAL_SECTION_INTERFACE", "$SAL_SECTION_ADVANCED" }) {
                    sections.PushBack(RE::GFxValue(Translate(key).c_str()));
                }
                for (const auto* key : {
                    "$SAL_SETTINGS_TITLE", "$SAL_APPLY", "$SAL_CANCEL", "$SAL_RESET_SECTION",
                    "$SAL_RESET_ALL", "$SAL_PREVIOUS", "$SAL_NEXT", "$SAL_ON", "$SAL_OFF",
                    "$SAL_SETTINGS_HINT", "$SAL_SAVE_FAILED" }) {
                    actions.PushBack(RE::GFxValue(Translate(key).c_str()));
                }
                for (const auto* key : {
                    "$SAL_PRESET_DEFAULT", "$SAL_PRESET_FASTER", "$SAL_PRESET_SLOWER",
                    "$SAL_PRESET_VANILLA_CURVE", "$SAL_PRESET_ZERO", "$SAL_PRESET_CUSTOM" }) {
                    presets.PushBack(RE::GFxValue(Translate(key).c_str()));
                }
                for (const auto* key : {
                    "$SAL_MODE_VANILLA", "$SAL_MODE_ZERO", "$SAL_MODE_UNIFORM", "$SAL_MODE_CUSTOM" }) {
                    modes.PushBack(RE::GFxValue(Translate(key).c_str()));
                }
                std::array<RE::GFxValue, 5> args{ rows, sections, actions, presets, modes };
                if (!s_movie->Invoke("SAL_Init", nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
                    logger::error("[EA] Settings: SAL_Init missing from SWF.");
                    Close();
                }
            }
            void Accept(CallbackProcessor* callbacks) override
            {
                if (!callbacks) return;
                callbacks->Process("SAL_OnSet", OnSet);
                callbacks->Process("SAL_OnResetSection", OnResetSection);
                callbacks->Process("SAL_OnResetAll", OnResetAll);
                callbacks->Process("SAL_OnPreset", OnPreset);
                callbacks->Process("SAL_OnApply", OnApply);
                callbacks->Process("SAL_OnCancel", OnCancel);
                callbacks->Process("SAL_OnTextInput", OnTextInput);
            }
        private:
            static void OnSet(const RE::FxDelegateArgs& args)
            {
                if (!ValidCallback(args, 2) || !args[0].IsNumber() || !args[1].IsString()) return;
                const double rawIndex = args[0].GetNumber();
                if (!std::isfinite(rawIndex) || std::trunc(rawIndex) != rawIndex || rawIndex < 0) return;
                const auto& descriptors = Config::Settings().Registry();
                if (rawIndex >= static_cast<double>(descriptors.size())) return;
                const auto index = static_cast<std::size_t>(rawIndex);
                const std::string value = args[1].GetString();
                const auto& key = descriptors[index].key;
                nlohmann::json candidate;
                try {
                    switch (descriptors[index].kind) {
                        case SettingKind::Toggle:
                            if (value != "true" && value != "false") {
                                logger::info("[EA] Settings: '{}' rejected unparsable value '{}'.", key, value);
                                return;
                            }
                            candidate = value == "true";
                            break;
                        case SettingKind::StartingMode: candidate = value; break;
                        case SettingKind::Integer:
                        case SettingKind::Number: {
                            std::size_t used = 0;
                            const auto parsed = std::stod(value, &used);
                            if (used != value.size()) {
                                logger::info("[EA] Settings: '{}' rejected unparsable value '{}'.", key, value);
                                return;
                            }
                            candidate = parsed;
                            break;
                        }
                    }
                } catch (...) {
                    logger::info("[EA] Settings: '{}' rejected unparsable value '{}'.", key, value);
                    return;
                }
                const bool accepted = Config::Settings().Set(index, candidate);
                logger::info("[EA] Settings: draft '{}' = '{}' {}.", key, value,
                    accepted ? "accepted" : "rejected (out of range)");
                if (accepted &&
                    (descriptors[index].kind == SettingKind::Toggle ||
                     descriptors[index].kind == SettingKind::StartingMode)) Refresh();
            }
            static void OnResetSection(const RE::FxDelegateArgs& args)
            {
                if (!ValidCallback(args, 1) || !args[0].IsNumber()) return;
                const auto section = args[0].GetNumber();
                if (!std::isfinite(section) || std::trunc(section) != section || section < 0 || section > 11) return;
                Config::Settings().ResetSection(static_cast<SettingSection>(static_cast<int>(section)));
                Refresh();
            }
            static void OnResetAll(const RE::FxDelegateArgs& args)
            {
                if (!ValidCallback(args, 0)) return;
                Config::Settings().ResetAll();
                Refresh();
            }
            static void OnPreset(const RE::FxDelegateArgs& args)
            {
                if (!ValidCallback(args, 1) || !args[0].IsNumber()) return;
                const auto preset = args[0].GetNumber();
                if (!std::isfinite(preset) || std::trunc(preset) != preset || preset < 0 || preset > 5) return;
                constexpr std::array names{ "default", "faster", "slower", "vanilla_curve", "zero", "custom" };
                Config::Settings().ApplyPreset(names[static_cast<std::size_t>(preset)]);
                Refresh();
            }
            static void OnApply(const RE::FxDelegateArgs& args)
            {
                if (!ValidCallback(args, 0)) return;
                std::string error;
                if (!Config::SaveDraftAndApply(error)) {
                    logger::error("[EA] Settings: save failed: {}.", error);
                    RE::GFxValue message(Translate("$SAL_SAVE_FAILED").c_str());
                    s_movie->Invoke("SAL_Error", nullptr, &message, 1);
                    return;
                }
                Leveling::ApplyGameSettings("settings-apply");
                Leveling::RefreshThreshold("settings-apply");
                logger::info("[EA] Settings: applied user overrides without changing native XP or pending points.");
                Close();
            }
            static void OnCancel(const RE::FxDelegateArgs& args)
            {
                if (!ValidCallback(args, 0)) return;
                Config::Settings().Cancel();
                Close();
            }
            static void OnTextInput(const RE::FxDelegateArgs& args)
            {
                if (!ValidCallback(args, 1) || !args[0].IsBool()) return;
                SetTextInput(args[0].GetBool());
            }
        };

        struct MenuWatcher final : RE::BSTEventSink<RE::MenuOpenCloseEvent> {
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (event && event->menuName == kMenuName && !event->opening) {
                    SetTextInput(false);
                    s_movie = nullptr;
                    s_queued = false;
                    Config::Settings().Cancel();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        MenuWatcher s_menuWatcher;

        struct HotkeySink final : RE::BSTEventSink<RE::InputEvent*> {
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
                RE::BSTEventSource<RE::InputEvent*>*) override
            {
                if (!events || !*events || Config::settingsHotkey == 0) return RE::BSEventNotifyControl::kContinue;
                for (auto* event = *events; event; event = event->next) {
                    if (event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
                    const auto* button = event->AsButtonEvent();
                    if (!button || !button->IsDown() || !button->AsIDEvent() ||
                        button->AsIDEvent()->GetIDCode() != static_cast<std::uint32_t>(Config::settingsHotkey)) continue;
                    if (s_queued.exchange(true)) break;
                    if (auto* tasks = SKSE::GetTaskInterface()) {
                        tasks->AddUITask([] {
                            auto* ui = RE::UI::GetSingleton();
                            auto* queue = RE::UIMessageQueue::GetSingleton();
                            if (ui && queue && !ui->GameIsPaused() && !ui->IsModalMenuOpen() &&
                                RE::PlayerCharacter::GetSingleton() && !ui->IsMenuOpen(kMenuName)) {
                                queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                            } else {
                                s_queued = false;
                            }
                        });
                    } else {
                        s_queued = false;
                    }
                    break;
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        HotkeySink s_hotkeySink;
    }

    bool Register()
    {
        if (s_registered) return true;
        auto* ui = RE::UI::GetSingleton();
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        if (!ui || !input) return false;
        SKSE::Translation::ParseTranslation("SimpleAlternateLevelling");
        ui->Register(kMenuName, Menu::Creator);
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&s_menuWatcher);
        input->AddEventSink(&s_hotkeySink);
        s_registered = true;
        logger::info("[EA] Settings: native menu registered; hotkey scan code {}.", Config::settingsHotkey);
        return true;
    }

    void ResetState()
    {
        SetTextInput(false);
        s_queued = false;
        s_movie = nullptr;
        Config::Settings().Cancel();
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(kMenuName)) Close();
    }
}
