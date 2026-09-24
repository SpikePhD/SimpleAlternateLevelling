#include "PCH.h"
#include "SettingsPage.h"

#include "Config.h"
#include "Leveling.h"
#include "SettingsModel.h"
#include "UIText.h"

#include <SKSE/Translation.h>

// The vendored framework header declares ImGuiTextFilter as both struct and class.
#pragma warning(push)
#pragma warning(disable : 4099)
#include "SKSEMenuFramework.h"
#pragma warning(pop)

#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Settings page in SKSE Menu Framework's Mod Control Panel.
//
// Rendering may happen off the game's main thread, so every access to the
// settings model goes through s_mutex, and saving/applying (which rebuilds the
// Config globals read by event sinks) is queued to the main thread.
namespace EA::SettingsPage {
    namespace {
        using Json = nlohmann::json;
        namespace ImGui = ImGuiMCP;

        constexpr double kUncappedXP = 10000000.0;
        constexpr double kCappedFallbackXP = 1000.0;
        constexpr auto kConfirmWindow = std::chrono::seconds(4);
        constexpr ImGui::ImVec4 kChangedColor{ 0.94f, 0.80f, 0.35f, 1.0f };
        constexpr ImGui::ImVec4 kErrorColor{ 0.95f, 0.40f, 0.40f, 1.0f };

        constexpr std::array<std::string_view, 35> kLocationTypes{
            "default", "city", "town", "settlement", "castle", "fort", "military_camp", "camp",
            "cave", "mine", "nordic_ruin", "nordic_tower", "nordic_dwelling", "dwemer_ruin",
            "dragon_lair", "giant_camp", "orc_stronghold", "imperial_tower", "daedric_shrine",
            "doomstone", "shipwreck", "lighthouse", "docks", "grove", "landmark", "clearing",
            "pass", "altar", "rock", "farm", "wood_mill", "wheat_mill", "smelter", "stable", "shack"
        };

        constexpr std::array<std::string_view, 18> kSkillKeys{
            "one_handed", "two_handed", "block", "heavy_armor", "light_armor", "archery",
            "alteration", "conjuration", "destruction", "illusion", "restoration", "sneak",
            "smithing", "alchemy", "enchanting", "pickpocket", "lockpicking", "speech"
        };

        constexpr std::array<std::string_view, 4> kModes{ "vanilla", "zero", "uniform", "custom" };
        constexpr std::array<std::string_view, 6> kPresets{
            "default", "faster", "slower", "vanilla_curve", "zero", "custom"
        };

        std::mutex s_mutex;
        std::unordered_map<std::string, std::size_t> s_indexByKey;
        std::unordered_map<std::string, double> s_editing;  // in-progress numeric edits
        std::string s_status;
        bool s_statusIsError = false;
        int s_presetIndex = 0;
        std::chrono::steady_clock::time_point s_resetAllArmedUntil{};
        bool s_registered = false;

        // -------------------------------------------------------------------
        // Text
        // -------------------------------------------------------------------

        const std::string& T(const std::string& key) { return UIText::Get(key); }

        std::string SettingToken(std::string_view key)
        {
            std::string token;
            for (const char c : key) {
                token += c == '.' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return token;
        }

        const std::string& Label(std::string_view key) { return T("$SAL_SETTING_" + SettingToken(key)); }

        const std::string* Description(std::string_view key)
        {
            const auto& text = T("$SAL_DESC_" + SettingToken(key));
            return text.starts_with("$SAL_") ? nullptr : &text;
        }

        const std::string& ModeLabel(std::string_view mode)
        {
            if (mode == "zero") return T("$SAL_MODE_ZERO");
            if (mode == "uniform") return T("$SAL_MODE_UNIFORM");
            if (mode == "custom") return T("$SAL_MODE_CUSTOM");
            return T("$SAL_MODE_VANILLA");
        }

        const std::string& PresetLabel(std::size_t index)
        {
            static const std::array<std::string, 6> keys{
                "$SAL_PRESET_DEFAULT", "$SAL_PRESET_FASTER", "$SAL_PRESET_SLOWER",
                "$SAL_PRESET_VANILLA_CURVE", "$SAL_PRESET_ZERO", "$SAL_PRESET_CUSTOM"
            };
            return T(keys[index]);
        }

        std::string FormatValue(const Json& value)
        {
            if (value.is_boolean()) return value.get<bool>() ? T("$SAL_PAGE_ON") : T("$SAL_PAGE_OFF");
            if (value.is_string()) return std::string(ModeLabel(value.get<std::string>()));
            if (value.is_number()) return std::format("{:g}", value.get<double>());
            return {};
        }

        // -------------------------------------------------------------------
        // Model access (callers hold s_mutex)
        // -------------------------------------------------------------------

        Json::json_pointer Pointer(std::string_view key)
        {
            std::string path = "/";
            for (const char c : key) path += c == '.' ? '/' : c;
            return Json::json_pointer(path);
        }

        Json DraftValue(std::string_view key)
        {
            const auto& draft = Config::Settings().Draft();
            const auto pointer = Pointer(key);
            return draft.contains(pointer) ? draft.at(pointer) : Json();
        }

        Json ShippedValue(std::string_view key)
        {
            const auto& shipped = Config::Settings().Shipped();
            const auto pointer = Pointer(key);
            return shipped.contains(pointer) ? shipped.at(pointer) : Json();
        }

        void SetStatus(std::string text, bool error)
        {
            s_status = std::move(text);
            s_statusIsError = error;
        }

        // Saves the draft and applies it on the main thread.
        void QueueSave(std::string reason)
        {
            auto apply = [reason = std::move(reason)]() {
                std::lock_guard lock(s_mutex);
                std::string error;
                if (!Config::SaveDraftAndApply(error)) {
                    logger::error("[EA] Settings page: save failed ({}): {}.", reason, error);
                    SetStatus(T("$SAL_SAVE_FAILED"), true);
                    return;
                }
                Leveling::ApplyGameSettings("settings-page");
                Leveling::RefreshThreshold("settings-page");
                logger::info("[EA] Settings page: saved and applied ({}).", reason);
                SetStatus({}, false);
            };
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask(std::move(apply));
            } else {
                logger::warn("[EA] Settings page: task interface unavailable; changes stay unsaved.");
            }
        }

        void Commit(std::string_view key, const Json& value)
        {
            const auto it = s_indexByKey.find(std::string(key));
            if (it == s_indexByKey.end()) {
                logger::warn("[EA] Settings page: unknown setting '{}'.", key);
                return;
            }
            if (DraftValue(key) == value) {
                return;
            }
            if (!Config::Settings().Set(it->second, value)) {
                logger::info("[EA] Settings page: '{}' = {} rejected (out of range).", key, value.dump());
                SetStatus(Label(key) + ": " + T("$SAL_PAGE_INVALID"), true);
                return;
            }
            logger::info("[EA] Settings page: '{}' = {}.", key, value.dump());
            QueueSave(std::string(key));
        }

        // -------------------------------------------------------------------
        // Widgets
        // -------------------------------------------------------------------

        void Tooltip(std::string_view key)
        {
            if (!ImGui::IsItemHovered()) return;
            std::string text;
            if (const auto* description = Description(key)) {
                text = *description + "\n\n";
            }
            text += T("$SAL_PAGE_DEFAULT") + ": " + FormatValue(ShippedValue(key));
            ImGui::SetTooltip("%s", text.c_str());
        }

        void DrawLabel(std::string_view key, const std::string& text)
        {
            ImGui::AlignTextToFramePadding();
            if (DraftValue(key) != ShippedValue(key)) {
                ImGui::TextColored(kChangedColor, "%s", text.c_str());
            } else {
                ImGui::TextUnformatted(text.c_str());
            }
            Tooltip(key);
        }

        // Numeric input that commits when editing finishes (Enter, focus
        // loss, or a +/- click), so partial typing is never saved.
        void DrawNumber(std::string_view key, bool integer, double step)
        {
            const std::string id(key);
            const auto current = DraftValue(key);
            double value = s_editing.contains(id) ? s_editing[id] : (current.is_number() ? current.get<double>() : 0.0);
            ImGui::SetNextItemWidth(-1.0f);
            bool changed;
            if (integer) {
                int asInt = static_cast<int>(std::llround(value));
                changed = ImGui::InputInt("##value", &asInt, 1, 10);
                value = asInt;
            } else {
                changed = ImGui::InputDouble("##value", &value, step, step * 10.0, "%g");
            }
            if (changed) s_editing[id] = value;
            if (ImGui::IsItemDeactivatedAfterEdit() && s_editing.contains(id)) {
                const double edited = s_editing[id];
                s_editing.erase(id);
                Commit(key, integer ? Json(static_cast<std::int64_t>(std::llround(edited))) : Json(edited));
            }
            Tooltip(key);
        }

        void DrawSetting(std::string_view key, bool disabled = false)
        {
            const auto it = s_indexByKey.find(std::string(key));
            if (it == s_indexByKey.end()) return;
            const auto& descriptor = Config::Settings().Registry()[it->second];

            ImGui::PushID(descriptor.key.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawLabel(key, Label(key));
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(disabled);
            switch (descriptor.kind) {
                case SettingKind::Toggle: {
                    bool value = DraftValue(key).is_boolean() && DraftValue(key).get<bool>();
                    if (ImGui::Checkbox("##value", &value)) Commit(key, Json(value));
                    Tooltip(key);
                    break;
                }
                case SettingKind::StartingMode: {
                    const auto current = DraftValue(key).is_string() ? DraftValue(key).get<std::string>() : std::string("vanilla");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##value", ModeLabel(current).c_str())) {
                        for (const auto mode : kModes) {
                            if (ImGui::Selectable(ModeLabel(mode).c_str(), mode == current)) {
                                Commit(key, Json(std::string(mode)));
                            }
                        }
                        ImGui::EndCombo();
                    }
                    Tooltip(key);
                    break;
                }
                case SettingKind::Integer:
                    DrawNumber(key, true, 1.0);
                    break;
                case SettingKind::Number:
                    DrawNumber(key, false, descriptor.step);
                    break;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        bool BeginSettingsTable(const char* id)
        {
            if (!ImGui::BeginTable(id, 2, ImGui::ImGuiTableFlags_SizingStretchProp)) return false;
            ImGui::TableSetupColumn("label", ImGui::ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("value", ImGui::ImGuiTableColumnFlags_WidthStretch, 0.4f);
            return true;
        }

        void DrawSection(const char* titleKey, SettingSection section, std::initializer_list<std::string_view> keys,
            bool defaultOpen = false)
        {
            if (!ImGui::CollapsingHeader(T(titleKey).c_str(), defaultOpen ? ImGui::ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                return;
            }
            ImGui::PushID(titleKey);
            if (BeginSettingsTable("settings")) {
                for (const auto key : keys) DrawSetting(key);
                ImGui::EndTable();
            }
            if (ImGui::SmallButton(T("$SAL_RESET_SECTION").c_str())) {
                Config::Settings().ResetSection(section);
                logger::info("[EA] Settings page: section {} reset to defaults.", static_cast<int>(section));
                QueueSave("reset-section");
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        // -------------------------------------------------------------------
        // Sections with custom layout
        // -------------------------------------------------------------------

        void DrawProgression()
        {
            if (!ImGui::CollapsingHeader(T("$SAL_SECTION_PROGRESSION").c_str(), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }
            ImGui::PushID("progression");
            const auto cap = DraftValue("leveling.xp_cap");
            const bool uncapped = cap.is_number() && cap.get<double>() >= kUncappedXP;
            if (BeginSettingsTable("settings")) {
                DrawSetting("leveling.xp_base");
                DrawSetting("leveling.xp_increase");
                DrawSetting("leveling.xp_cap", uncapped);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                bool noCap = uncapped;
                if (ImGui::Checkbox(T("$SAL_PAGE_NO_CAP").c_str(), &noCap)) {
                    Commit("leveling.xp_cap", Json(noCap ? kUncappedXP : kCappedFallbackXP));
                }
                DrawSetting("leveling.reward_scaling");
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Spacing();
                ImGui::TextDisabled("%s", T("$SAL_PAGE_WEIGHTS").c_str());
                for (const auto source : Config::kRewardWeightKeys) {
                    DrawSetting("leveling.reward_weights." + std::string(source));
                }
                ImGui::EndTable();
            }
            if (ImGui::SmallButton(T("$SAL_RESET_SECTION").c_str())) {
                Config::Settings().ResetSection(SettingSection::Progression);
                QueueSave("reset-section");
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        void DrawExploration()
        {
            if (!ImGui::CollapsingHeader(T("$SAL_SECTION_EXPLORATION").c_str())) return;
            ImGui::PushID("exploration");
            constexpr auto flags = ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_BordersInnerH |
                                   ImGui::ImGuiTableFlags_ScrollY | ImGui::ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("locations", 3, flags, ImGui::ImVec2{ 0.0f, 360.0f })) {
                ImGui::TableSetupColumn(T("$SAL_PAGE_COL_LOCATION").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.5f);
                ImGui::TableSetupColumn(T("$SAL_PAGE_COL_DISCOVER").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.25f);
                ImGui::TableSetupColumn(T("$SAL_PAGE_COL_CLEAR").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.25f);
                ImGui::TableHeadersRow();
                for (const auto type : kLocationTypes) {
                    const std::string name(type);
                    ImGui::PushID(name.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const auto discover = "xp_sources.location.discovery." + name;
                    const auto clear = "xp_sources.location.clearing." + name;
                    const bool changed = DraftValue(discover) != ShippedValue(discover) || DraftValue(clear) != ShippedValue(clear);
                    ImGui::AlignTextToFramePadding();
                    const auto& label = T("$SAL_LOCTYPE_" + SettingToken(name));
                    if (changed) ImGui::TextColored(kChangedColor, "%s", label.c_str());
                    else ImGui::TextUnformatted(label.c_str());
                    ImGui::TableNextColumn();
                    ImGui::PushID("discover");
                    DrawNumber(discover, false, 1.0);
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::PushID("clear");
                    DrawNumber(clear, false, 1.0);
                    ImGui::PopID();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::SmallButton(T("$SAL_RESET_SECTION").c_str())) {
                Config::Settings().ResetSection(SettingSection::Exploration);
                QueueSave("reset-section");
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        void DrawBooks()
        {
            if (!ImGui::CollapsingHeader(T("$SAL_SECTION_BOOKS").c_str())) return;
            ImGui::PushID("books");
            const auto useValue = DraftValue("xp_sources.book.use_value_reward");
            const bool valueMode = useValue.is_boolean() && useValue.get<bool>();
            if (BeginSettingsTable("settings")) {
                DrawSetting("xp_sources.book.new_book", valueMode);
                DrawSetting("xp_sources.book.skill_book");
                DrawSetting("xp_sources.book.use_value_reward");
                DrawSetting("xp_sources.book.value_multiplier", !valueMode);
                DrawSetting("xp_sources.book.reading_multiplier");
                ImGui::EndTable();
            }
            if (ImGui::SmallButton(T("$SAL_RESET_SECTION").c_str())) {
                Config::Settings().ResetSection(SettingSection::Books);
                QueueSave("reset-section");
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        void DrawStartingSkills()
        {
            if (!ImGui::CollapsingHeader(T("$SAL_SECTION_STARTING").c_str())) return;
            ImGui::PushID("starting");
            ImGui::TextWrapped("%s", T("$SAL_PAGE_NEW_CHARACTERS").c_str());
            const auto mode = DraftValue("starting_skills.mode");
            const auto modeName = mode.is_string() ? mode.get<std::string>() : std::string("vanilla");
            if (BeginSettingsTable("settings")) {
                DrawSetting("starting_skills.mode");
                if (modeName == "uniform") DrawSetting("starting_skills.all_value");
                ImGui::EndTable();
            }
            if (modeName == "custom" &&
                ImGui::BeginTable("custom", 6, ImGui::ImGuiTableFlags_SizingStretchProp)) {
                for (std::size_t i = 0; i < kSkillKeys.size(); ++i) {
                    const auto key = "starting_skills.custom." + std::string(kSkillKeys[i]);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextColumn();
                    DrawLabel(key, Label(key));
                    ImGui::TableNextColumn();
                    DrawNumber(key, true, 1.0);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::SmallButton(T("$SAL_RESET_SECTION").c_str())) {
                Config::Settings().ResetSection(SettingSection::Starting);
                QueueSave("reset-section");
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        // -------------------------------------------------------------------
        // Page
        // -------------------------------------------------------------------

        void DrawToolbar()
        {
            ImGui::TextWrapped("%s", T("$SAL_PAGE_INTRO").c_str());
            ImGui::Spacing();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(T("$SAL_PAGE_PRESET").c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("##preset", PresetLabel(static_cast<std::size_t>(s_presetIndex)).c_str())) {
                for (std::size_t i = 0; i < kPresets.size(); ++i) {
                    if (ImGui::Selectable(PresetLabel(i).c_str(), static_cast<int>(i) == s_presetIndex)) {
                        s_presetIndex = static_cast<int>(i);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button(T("$SAL_PAGE_APPLY_PRESET").c_str())) {
                const auto preset = kPresets[static_cast<std::size_t>(s_presetIndex)];
                if (Config::Settings().ApplyPreset(preset)) {
                    logger::info("[EA] Settings page: preset '{}' applied.", preset);
                    QueueSave("preset");
                }
            }

            ImGui::SameLine();
            const auto now = std::chrono::steady_clock::now();
            const bool armed = now < s_resetAllArmedUntil;
            if (ImGui::Button(armed ? T("$SAL_PAGE_CONFIRM").c_str() : T("$SAL_RESET_ALL").c_str())) {
                if (armed) {
                    s_resetAllArmedUntil = {};
                    Config::Settings().ResetAll();
                    logger::info("[EA] Settings page: all settings reset to defaults.");
                    QueueSave("reset-all");
                } else {
                    s_resetAllArmedUntil = now + kConfirmWindow;
                }
            }

            if (!s_status.empty()) {
                ImGui::TextColored(s_statusIsError ? kErrorColor : kChangedColor, "%s", s_status.c_str());
            }
            ImGui::Separator();
            ImGui::Spacing();
        }

        void __stdcall Render()
        {
            std::lock_guard lock(s_mutex);
            if (s_indexByKey.empty()) {
                ImGui::TextColored(kErrorColor, "%s", T("$SAL_SAVE_FAILED").c_str());
                return;
            }
            DrawToolbar();
            DrawProgression();
            DrawSection("$SAL_SECTION_QUESTS", SettingSection::Quests, {
                "xp_sources.quest.main", "xp_sources.quest.side", "xp_sources.quest.misc",
                "xp_sources.quest.objectives", "xp_sources.quest.college", "xp_sources.quest.companions",
                "xp_sources.quest.thieves", "xp_sources.quest.brotherhood", "xp_sources.quest.daedric",
                "xp_sources.quest.civil_war", "xp_sources.quest.dawnguard", "xp_sources.quest.dragonborn",
                "xp_sources.quest.other" });
            DrawSection("$SAL_SECTION_KILLS", SettingSection::Kills, {
                "xp_sources.kill.base_humanoid", "xp_sources.kill.base_animal", "xp_sources.kill.base_creature",
                "xp_sources.kill.base_undead", "xp_sources.kill.base_daedra", "xp_sources.kill.base_dragon",
                "xp_sources.kill.base_default", "xp_sources.kill.level_scale_factor",
                "xp_sources.kill.global_multiplier" });
            DrawExploration();
            DrawSection("$SAL_SECTION_LOCKS", SettingSection::Locks, {
                "xp_sources.lockpick.novice", "xp_sources.lockpick.apprentice", "xp_sources.lockpick.adept",
                "xp_sources.lockpick.expert", "xp_sources.lockpick.master" });
            DrawBooks();
            DrawSection("$SAL_SECTION_PICKPOCKET", SettingSection::Pickpocket, { "xp_sources.pickpocket.base" });
            DrawStartingSkills();
            DrawSection("$SAL_SECTION_ALLOCATION", SettingSection::Allocation, {
                "skill_allocation.points_per_level", "skill_allocation.skill_cap" });
            DrawSection("$SAL_SECTION_NOTIFICATIONS", SettingSection::Notifications, { "notifications.enabled" });
            DrawSection("$SAL_SECTION_ADVANCED", SettingSection::Advanced, { "debug.verbose", "debug.max_log_files" });
        }
    }

    bool Register()
    {
        if (s_registered) return true;
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::info("[EA] Settings page: SKSE Menu Framework not installed; edit SimpleAlternateLevelling.user.json instead.");
            return false;
        }
        {
            std::lock_guard lock(s_mutex);
            const auto& registry = Config::Settings().Registry();
            for (std::size_t i = 0; i < registry.size(); ++i) {
                s_indexByKey.emplace(registry[i].key, i);
            }
            Config::Settings().Begin();
        }
        SKSEMenuFramework::SetSection("Simple Alternate Levelling");
        SKSEMenuFramework::AddSectionItem(T("$SAL_PAGE_SETTINGS"), Render);
        s_registered = true;
        logger::info("[EA] Settings page: registered in SKSE Menu Framework ({} settings).", s_indexByKey.size());
        return true;
    }
}
