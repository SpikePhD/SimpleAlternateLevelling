#include "SettingsModel.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <functional>

namespace EA {
    namespace {
        using Json = SettingsModel::Json;

        const Json* Find(const Json& root, std::string_view path)
        {
            const Json* node = &root;
            std::size_t start = 0;
            while (start < path.size()) {
                const auto end = path.find('.', start);
                const std::string key(path.substr(start, end == std::string_view::npos ? end : end - start));
                if (!node->is_object() || !node->contains(key)) return nullptr;
                node = &(*node)[key];
                if (end == std::string_view::npos) break;
                start = end + 1;
            }
            return node;
        }

        void Put(Json& root, std::string_view path, const Json& value)
        {
            Json* node = &root;
            std::size_t start = 0;
            while (true) {
                const auto end = path.find('.', start);
                const std::string key(path.substr(start, end == std::string_view::npos ? end : end - start));
                if (end == std::string_view::npos) {
                    (*node)[key] = value;
                    return;
                }
                node = &(*node)[key];
                start = end + 1;
            }
        }

        // The menu sends every number as a double. Store integer settings as
        // JSON integers so the user file keeps their type (20, not 20.0).
        // Callers validate first, so the value is integral and in range.
        Json Normalized(const SettingDescriptor& descriptor, const Json& value)
        {
            if (descriptor.kind == SettingKind::Integer && value.is_number_float()) {
                return Json(static_cast<std::int64_t>(value.get<double>()));
            }
            return value;
        }

        SettingSection Section(std::string_view key)
        {
            if (key.starts_with("leveling.")) return SettingSection::Progression;
            if (key.starts_with("xp_sources.quest.")) return SettingSection::Quests;
            if (key.starts_with("xp_sources.kill.")) return SettingSection::Kills;
            if (key.starts_with("xp_sources.location.")) return SettingSection::Exploration;
            if (key.starts_with("xp_sources.lockpick.")) return SettingSection::Locks;
            if (key.starts_with("xp_sources.book.")) return SettingSection::Books;
            if (key.starts_with("xp_sources.pickpocket.")) return SettingSection::Pickpocket;
            if (key.starts_with("starting_skills.")) return SettingSection::Starting;
            if (key == "skill_allocation.points_per_level" || key == "skill_allocation.skill_cap") return SettingSection::Allocation;
            if (key.starts_with("skill_allocation.") || key.starts_with("interface.")) return SettingSection::Interface;
            if (key.starts_with("notifications.")) return SettingSection::Notifications;
            return SettingSection::Advanced;
        }

        std::string TranslationKey(std::string_view key)
        {
            std::string result = "$SAL_SETTING_";
            for (const char c : key) {
                result += c == '.' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return result;
        }

        void Migrate(Json& root)
        {
            if (!root.is_object()) return;
            if (root.contains("reset_skills_on_new_game") &&
                (!root.contains("starting_skills") || !root["starting_skills"].is_object() ||
                 !root["starting_skills"].contains("mode"))) {
                if (root["reset_skills_on_new_game"].is_boolean()) {
                    if (!root.contains("starting_skills") || !root["starting_skills"].is_object())
                        root["starting_skills"] = Json::object();
                    root["starting_skills"]["mode"] = root["reset_skills_on_new_game"].get<bool>() ? "zero" : "vanilla";
                }
            }
            root.erase("reset_skills_on_new_game");
        }
    }

    bool SettingsModel::ValidHotkey(double value)
    {
        return std::isfinite(value) && std::trunc(value) == value && value >= 0 && value <= 255;
    }

    void SettingsModel::BuildRegistry()
    {
        registry_.clear();
        std::function<void(const Json&, const std::string&)> visit = [&](const Json& node, const std::string& prefix) {
            if (!node.is_object()) return;
            for (auto it = node.begin(); it != node.end(); ++it) {
                if (it.key().starts_with('_') || it.key() == "config_version") continue;
                const auto key = prefix.empty() ? it.key() : prefix + "." + it.key();
                if (key == "notifications.messages") continue; // JSON text remains user-editable.
                if (it.value().is_object()) { visit(it.value(), key); continue; }
                if (!it.value().is_number() && !it.value().is_boolean() && key != "starting_skills.mode") continue;

                SettingDescriptor descriptor{ key, TranslationKey(key), Section(key), SettingKind::Number, 0, 1000000, 1 };
                if (it.value().is_boolean()) descriptor.kind = SettingKind::Toggle;
                else if (key == "starting_skills.mode") descriptor.kind = SettingKind::StartingMode;
                else if (it.value().is_number_integer()) descriptor.kind = SettingKind::Integer;
                if (key.starts_with("leveling.")) {
                    descriptor.minimum = key.ends_with("xp_increase") ? 0 : 0.01;
                    descriptor.maximum = key.ends_with("xp_cap") ? 10000000 : 1000000;
                    descriptor.step = key.ends_with("xp_increase") ? 1 : 5;
                } else if (key.starts_with("starting_skills.")) {
                    descriptor.maximum = 1000;
                } else if (key == "skill_allocation.points_per_level") {
                    descriptor.maximum = 1000;
                } else if (key == "skill_allocation.skill_cap") {
                    descriptor.minimum = 1;
                    descriptor.maximum = 1000;
                } else if (key == "interface.settings_hotkey") {
                    descriptor.maximum = 255;
                } else if (key == "debug.max_log_files") {
                    descriptor.maximum = 1000;
                } else if (key.starts_with("skill_allocation.")) {
                    if (key == "skill_allocation.panel_width") { descriptor.minimum = 480; descriptor.maximum = 1280; }
                    else if (key == "skill_allocation.panel_height") descriptor.maximum = 720;
                    else if (key == "skill_allocation.panel_y_offset") { descriptor.minimum = -360; descriptor.maximum = 360; }
                    else if (key == "skill_allocation.row_gap") { descriptor.minimum = 24; descriptor.maximum = 72; }
                    else if (key == "skill_allocation.column_gap") descriptor.maximum = 200;
                    else if (key == "skill_allocation.button_row_offset") { descriptor.minimum = -72; descriptor.maximum = 120; }
                    else if (key == "skill_allocation.font_size") { descriptor.minimum = 8; descriptor.maximum = 40; }
                    else if (key == "skill_allocation.header_font_size") { descriptor.minimum = 10; descriptor.maximum = 48; }
                    else descriptor.maximum = 120;
                } else if (key.starts_with("xp_sources.")) {
                    descriptor.maximum = 100000;
                    descriptor.step = key.ends_with("multiplier") || key.ends_with("factor") ? 0.1 : 1;
                }
                registry_.push_back(std::move(descriptor));
            }
        };
        visit(shipped_, "");
        std::stable_sort(registry_.begin(), registry_.end(), [](const auto& left, const auto& right) {
            return static_cast<int>(left.section) < static_cast<int>(right.section);
        });
    }

    bool SettingsModel::Valid(const SettingDescriptor& descriptor, const Json& value) const
    {
        if (descriptor.kind == SettingKind::Toggle) return value.is_boolean();
        if (descriptor.kind == SettingKind::StartingMode) {
            return value.is_string() && (value == "vanilla" || value == "zero" || value == "uniform" || value == "custom");
        }
        if (!value.is_number()) return false;
        double number;
        try { number = value.get<double>(); }
        catch (const Json::exception&) { return false; }
        if (!std::isfinite(number) || number < descriptor.minimum || number > descriptor.maximum) return false;
        if (descriptor.key == "skill_allocation.panel_height" && number != 0 && number < 300) return false;
        return descriptor.kind != SettingKind::Integer || std::trunc(number) == number;
    }

    bool SettingsModel::Load(Json shipped, Json user, std::string* warning)
    {
        if (!shipped.is_object()) return false;
        Migrate(shipped);
        Migrate(user);
        shipped_ = std::move(shipped);
        shipped_["config_version"] = kSchemaVersion;
        effective_ = shipped_;
        BuildRegistry();
        if (user.is_object()) {
            int version = 1;
            if (user.contains("config_version")) {
                if (!user["config_version"].is_number_integer()) {
                    if (warning) *warning = "invalid user config version ignored";
                    user = Json::object();
                } else {
                    try { version = user["config_version"].get<int>(); }
                    catch (const Json::exception&) {
                        if (warning) *warning = "invalid user config version ignored";
                        user = Json::object();
                    }
                }
            }
            if (version > kSchemaVersion) {
                if (warning) *warning = "future user config version ignored";
            } else {
                for (const auto& descriptor : registry_) {
                    if (const auto* candidate = Find(user, descriptor.key)) {
                        if (Valid(descriptor, *candidate)) Put(effective_, descriptor.key, Normalized(descriptor, *candidate));
                        else if (warning) *warning = "invalid override ignored: " + descriptor.key;
                    }
                }
                if (const auto* messages = Find(user, "notifications.messages"); messages && messages->is_object()) {
                    for (auto it = messages->begin(); it != messages->end(); ++it) {
                        const auto* base = Find(shipped_, "notifications.messages." + it.key());
                        if (base && it.value().is_string() && it.value().get<std::string>().size() <= 512) {
                            effective_["notifications"]["messages"][it.key()] = it.value();
                        }
                    }
                }
            }
        }
        draft_ = effective_;
        return true;
    }

    void SettingsModel::Begin() { draft_ = effective_; }
    void SettingsModel::Cancel() { draft_ = effective_; }

    bool SettingsModel::Set(std::size_t index, const Json& value)
    {
        if (index >= registry_.size() || !Valid(registry_[index], value)) return false;
        Put(draft_, registry_[index].key, Normalized(registry_[index], value));
        return true;
    }

    void SettingsModel::ResetSection(SettingSection section)
    {
        for (const auto& descriptor : registry_) {
            if (descriptor.section == section) Put(draft_, descriptor.key, *Find(shipped_, descriptor.key));
        }
        if (section == SettingSection::Notifications && shipped_.contains("notifications") &&
            shipped_["notifications"].is_object() && shipped_["notifications"].contains("messages"))
            draft_["notifications"]["messages"] = shipped_["notifications"]["messages"];
    }

    void SettingsModel::ResetAll()
    {
        draft_ = shipped_;
    }

    bool SettingsModel::ApplyPreset(std::string_view name)
    {
        if (name == "default") { ResetAll(); return true; }
        if (name == "zero") { Put(draft_, "starting_skills.mode", "zero"); return true; }
        if (name == "vanilla_curve") {
            Put(draft_, "leveling.xp_base", 75.0);
            Put(draft_, "leveling.xp_increase", 25.0);
            Put(draft_, "leveling.xp_cap", 10000000.0);
            return true;
        }
        if (name == "faster" || name == "slower") {
            const double factor = name == "faster" ? 0.75 : 1.35;
            for (const auto& descriptor : registry_) {
                if (descriptor.section != SettingSection::Progression) continue;
                const auto* base = Find(shipped_, descriptor.key);
                if (base && base->is_number()) {
                    const auto value = std::clamp(base->get<double>() * factor, descriptor.minimum, descriptor.maximum);
                    Put(draft_, descriptor.key, value);
                }
            }
            return true;
        }
        if (name == "custom") { Put(draft_, "starting_skills.mode", "custom"); return true; }
        return false;
    }

    void SettingsModel::Accept() { effective_ = draft_; }
    bool SettingsModel::Dirty() const { return draft_ != effective_; }

    SettingsModel::Json SettingsModel::Overrides() const
    {
        Json result = { { "config_version", kSchemaVersion } };
        for (const auto& descriptor : registry_) {
            const auto* value = Find(draft_, descriptor.key);
            const auto* base = Find(shipped_, descriptor.key);
            if (value && base && *value != *base) Put(result, descriptor.key, *value);
        }
        if (const auto* messages = Find(draft_, "notifications.messages"); messages && messages->is_object()) {
            for (auto it = messages->begin(); it != messages->end(); ++it) {
                const auto* base = Find(shipped_, "notifications.messages." + it.key());
                if (base && *base != it.value()) result["notifications"]["messages"][it.key()] = it.value();
            }
        }
        return result;
    }
}
