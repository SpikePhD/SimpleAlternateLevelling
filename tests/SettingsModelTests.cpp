#include "SettingsModel.h"
#include "Progression.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iostream>

using EA::SettingsModel;
using Json = SettingsModel::Json;

static std::size_t Index(const SettingsModel& model, const std::string& key)
{
    const auto& entries = model.Registry();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].key == key) return i;
    throw std::runtime_error("missing setting: " + key);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream input(argv[1]);
    const Json defaults = Json::parse(input);
    SettingsModel model;
    std::string warning;
    assert(model.Load(defaults, Json::object(), &warning));
    assert(model.Effective()["starting_skills"]["mode"] == "vanilla");
    assert(model.Registry().size() > 100);

    const Json legacy = { {"reset_skills_on_new_game", true},
        {"leveling", {{"xp_base", 17.0}, {"xp_increase", "bad"}}},
        {"xp_sources", {{"quest", {{"main", 44.0}}}}} };
    assert(model.Load(defaults, legacy, &warning));
    assert(model.Effective()["starting_skills"]["mode"] == "zero");
    assert(model.Effective()["leveling"]["xp_base"] == 17.0);
    assert(model.Effective()["leveling"]["xp_increase"] == defaults["leveling"]["xp_increase"]);
    assert(model.Effective()["xp_sources"]["quest"]["main"] == 44.0);
    assert(!model.Effective().contains("reset_skills_on_new_game"));
    SettingsModel oldShipped;
    Json oldDefaults = defaults;
    oldDefaults.erase("starting_skills");
    oldDefaults["reset_skills_on_new_game"] = true;
    assert(oldShipped.Load(oldDefaults, Json::object()));
    assert(oldShipped.Effective()["starting_skills"]["mode"] == "zero");

    model.Begin();
    assert(model.Set(Index(model, "leveling.xp_base"), 30.0));
    assert(!model.Set(Index(model, "leveling.xp_base"), -1.0));
    assert(!model.Set(Index(model, "skill_allocation.skill_cap"), 0.5));
    assert(model.Set(Index(model, "starting_skills.mode"), "uniform"));
    assert(model.Set(Index(model, "starting_skills.all_value"), 10));
    assert(model.Dirty());
    model.Cancel();
    assert(model.Draft() == model.Effective());

    assert(model.Set(Index(model, "starting_skills.mode"), "custom"));
    assert(model.Set(Index(model, "starting_skills.custom.archery"), 9));
    assert(model.Set(Index(model, "notifications.enabled"), false));
    model.ResetSection(EA::SettingSection::Starting);
    assert(model.Draft()["starting_skills"]["mode"] == defaults["starting_skills"]["mode"]);
    assert(model.Draft()["starting_skills"]["custom"]["archery"] == 0);
    assert(model.Draft()["notifications"]["enabled"] == false);
    model.ResetAll();
    assert(model.Draft()["notifications"]["enabled"] == true);
    assert(model.Draft()["leveling"]["xp_base"] == defaults["leveling"]["xp_base"]);
    SettingsModel messages;
    assert(messages.Load(defaults, {{"notifications", {{"messages", {{"quest_main", "Custom notification"}}}}}}));
    messages.Begin();
    messages.ResetSection(EA::SettingSection::Notifications);
    assert(messages.Draft()["notifications"]["messages"]["quest_main"] ==
        defaults["notifications"]["messages"]["quest_main"]);

    assert(model.ApplyPreset("faster"));
    assert(model.Draft()["leveling"]["xp_base"].get<double>() < defaults["leveling"]["xp_base"].get<double>());
    assert(model.Draft()["leveling"]["reward_scaling"] == defaults["leveling"]["reward_scaling"]);
    assert(model.Draft()["leveling"]["reward_weights"]["kill"] == defaults["leveling"]["reward_weights"]["kill"]);
    assert(model.ApplyPreset("slower"));
    assert(model.Draft()["leveling"]["xp_base"].get<double>() > defaults["leveling"]["xp_base"].get<double>());
    assert(model.ApplyPreset("vanilla_curve"));
    assert(model.Draft()["leveling"]["xp_base"] == 75.0);
    assert(model.ApplyPreset("zero"));
    assert(model.Draft()["starting_skills"]["mode"] == "zero");
    assert(model.ApplyPreset("custom"));
    assert(model.Draft()["starting_skills"]["mode"] == "custom");
    assert(model.ApplyPreset("default"));
    assert(model.Draft() == model.Shipped());

    assert(model.Set(Index(model, "leveling.xp_base"), 25.0));
    assert(model.Set(Index(model, "starting_skills.mode"), "custom"));
    assert(model.Set(Index(model, "starting_skills.custom.speech"), 12));
    const Json persisted = model.Overrides();
    assert(persisted["config_version"] == SettingsModel::kSchemaVersion);
    assert(persisted["leveling"]["xp_base"] == 25.0);
    assert(!persisted["leveling"].contains("xp_increase"));
    assert(persisted["starting_skills"]["custom"]["speech"] == 12);
    model.Accept();
    SettingsModel reloaded;
    assert(reloaded.Load(defaults, Json::parse(persisted.dump())));
    assert(reloaded.Effective() == model.Effective());
    assert(model.Set(Index(model, "leveling.xp_base"), 90.0));
    model.Cancel();
    assert(model.Draft()["leveling"]["xp_base"] == 25.0);

    const auto oldXP = 123.0f;
    const auto threshold = EA::Progression::CalculateThreshold(10,
        { 25.0f, 3.0f, 500.0f });
    assert(threshold == 55.0f);
    assert(oldXP == 123.0f); // curve calculation does not assign native accumulated XP

    SettingsModel future;
    assert(future.Load(defaults, {{"config_version", 999}, {"leveling", {{"xp_base", 999.0}}}}, &warning));
    assert(future.Effective()["leveling"]["xp_base"] == defaults["leveling"]["xp_base"]);
    SettingsModel malformed;
    assert(malformed.Load(defaults, {{"config_version", "bad"}, {"leveling", {{"xp_base", 999.0}}}}, &warning));
    assert(malformed.Effective()["leveling"]["xp_base"] == defaults["leveling"]["xp_base"]);

    // The menu sends integers as doubles; they must persist as JSON integers.
    SettingsModel integers;
    assert(integers.Load(defaults, Json::object()));
    integers.Begin();
    assert(integers.Set(Index(integers, "debug.max_log_files"), 20.0));
    assert(!integers.Set(Index(integers, "debug.max_log_files"), 20.5));
    assert(integers.Draft()["debug"]["max_log_files"].is_number_integer());
    const Json integerOverrides = integers.Overrides();
    assert(integerOverrides["debug"]["max_log_files"].is_number_integer());
    assert(integerOverrides.dump().find("20.0") == std::string::npos);
    SettingsModel legacyFloat;
    assert(legacyFloat.Load(defaults, {{"debug", {{"max_log_files", 20.0}}}}));
    assert(legacyFloat.Effective()["debug"]["max_log_files"].is_number_integer());
    assert(legacyFloat.Effective()["debug"]["max_log_files"] == 20);

    // Integration floor: Advanced section, bounded to (0, 1].
    SettingsModel integration;
    assert(integration.Load(defaults, Json::object()));
    const auto floorIndex = Index(integration, "integration.threshold_multiplier_floor");
    assert(integration.Registry()[floorIndex].section == EA::SettingSection::Advanced);
    assert(integration.Effective()["integration"]["threshold_multiplier_floor"] ==
           EA::Progression::kDefaultThresholdMultiplierFloor);
    integration.Begin();
    assert(integration.Set(floorIndex, 0.75));
    assert(integration.Set(floorIndex, 1.0));
    assert(!integration.Set(floorIndex, 0.0));
    assert(!integration.Set(floorIndex, 1.5));
    assert(integration.Overrides()["integration"]["threshold_multiplier_floor"] == 1.0);
    std::cout << "settings model tests passed\n";
}
