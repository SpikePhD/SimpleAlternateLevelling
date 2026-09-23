#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace EA {
    enum class SettingSection {
        Progression, Quests, Kills, Exploration, Locks, Books, Pickpocket,
        Starting, Allocation, Notifications, Interface, Advanced
    };

    enum class SettingKind { Number, Integer, Toggle, StartingMode };

    struct SettingDescriptor {
        std::string key;
        std::string translationKey;
        SettingSection section;
        SettingKind kind;
        double minimum{ 0 };
        double maximum{ 0 };
        double step{ 1 };
    };

    class SettingsModel {
    public:
        using Json = nlohmann::json;
        static constexpr int kSchemaVersion = 2;

        // Shipped JSON defines the defaults. Unknown overrides and invalid values
        // are ignored independently, so one damaged setting cannot poison others.
        bool Load(Json shipped, Json user, std::string* warning = nullptr);
        void Begin();
        void Cancel();
        bool Set(std::size_t index, const Json& value);
        void ResetSection(SettingSection section);
        void ResetAll();
        bool ApplyPreset(std::string_view name);
        void Accept();
        [[nodiscard]] Json Overrides() const;
        [[nodiscard]] const Json& Effective() const { return effective_; }
        [[nodiscard]] const Json& Draft() const { return draft_; }
        [[nodiscard]] const Json& Shipped() const { return shipped_; }
        [[nodiscard]] const std::vector<SettingDescriptor>& Registry() const { return registry_; }
        [[nodiscard]] bool Dirty() const;
        [[nodiscard]] static bool ValidHotkey(double value);

    private:
        Json shipped_ = Json::object();
        Json effective_ = Json::object();
        Json draft_ = Json::object();
        std::vector<SettingDescriptor> registry_;

        void BuildRegistry();
        [[nodiscard]] bool Valid(const SettingDescriptor& descriptor, const Json& value) const;
    };
}
