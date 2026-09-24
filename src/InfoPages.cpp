#include "PCH.h"
#include "InfoPages.h"

#include "Config.h"
#include "Progression.h"
#include "RewardRules.h"
#include "UIText.h"
#include "XPJournal.h"

// The vendored framework header declares ImGuiTextFilter as both struct and class.
#pragma warning(push)
#pragma warning(disable : 4099)
#include "SKSEMenuFramework.h"
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>

// Stats and XP Log pages. Journal data is copied under XPJournal's mutex only
// when its version changes. Player level/XP/threshold are read directly for
// display; these are plain floats/ints, so a torn frame is harmless here.
namespace EA::InfoPages {
    namespace {
        namespace ImGui = ImGuiMCP;
        using RewardRules::RewardSource;

        constexpr ImGui::ImVec4 kGold{ 0.94f, 0.80f, 0.35f, 1.0f };
        constexpr ImGui::ImVec4 kMuted{ 0.60f, 0.58f, 0.52f, 1.0f };
        constexpr std::size_t kSourceCount = static_cast<std::size_t>(RewardSource::kCount);

        XPJournal::Snapshot s_snapshot;
        int s_sourceFilter = 0;  // 0 = all, otherwise RewardSource + 1
        bool s_showNotes = true;
        char s_search[128] = {};
        bool s_registered = false;

        const std::string& T(const std::string& key) { return UIText::Get(key); }

        const std::string& SourceLabel(RewardSource source)
        {
            const auto index = static_cast<std::size_t>(source);
            std::string key(Config::kRewardWeightKeys[std::min(index, Config::kRewardWeightKeys.size() - 1)]);
            for (auto& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return T("$SAL_SETTING_LEVELING_REWARD_WEIGHTS_" + key);
        }

        void RefreshSnapshot()
        {
            if (XPJournal::Version() != s_snapshot.version) {
                s_snapshot = XPJournal::Copy();
            }
        }

        double ScaleFor(RewardSource source, std::uint32_t level)
        {
            const auto weight = Config::rewardWeights[static_cast<std::size_t>(source)];
            return Progression::RewardScale(level,
                { Config::xpBase, Config::xpIncrease, Config::xpCap }, Config::rewardScaling * weight);
        }

        bool ContainsInsensitive(std::string_view text, std::string_view needle)
        {
            if (needle.empty()) return true;
            const auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                });
            return it != text.end();
        }

        std::string Subject(const XPJournal::Entry& entry)
        {
            return entry.subjectKey.empty() ? entry.subject : T(entry.subjectKey);
        }

        // -------------------------------------------------------------------
        // Stats
        // -------------------------------------------------------------------

        void DrawCurrentLevel(RE::PlayerCharacter* player)
        {
            auto* skills = player ? player->GetInfoRuntimeData().skills : nullptr;
            if (!skills || !skills->data) {
                ImGui::TextWrapped("%s", T("$SAL_STATS_NO_PLAYER").c_str());
                return;
            }
            const auto level = static_cast<std::uint32_t>(player->GetLevel());
            const double xp = std::max(0.0f, skills->data->xp);
            const double threshold = std::max(1.0f, skills->data->levelThreshold);

            ImGui::SeparatorText(T("$SAL_STATS_THIS_LEVEL").c_str());
            ImGui::TextColored(kGold, "%s %u", T("$SAL_ALLOC_LEVEL").c_str(), level);
            const auto overlay = std::format("{:.0f} / {:.0f} XP", xp, threshold);
            ImGui::ProgressBar(static_cast<float>(std::clamp(xp / threshold, 0.0, 1.0)), ImGui::ImVec2{ -1.0f, 0.0f },
                overlay.c_str());
            const double remaining = std::max(0.0, threshold - xp);
            ImGui::Text("%s: %.0f XP", T("$SAL_STATS_REMAINING").c_str(), remaining);

            const double perKill = Config::xpKillHumanoid * Config::xpKillGlobalMultiplier * ScaleFor(RewardSource::kKill, level);
            const double perQuest = Config::xpQuestSide * ScaleFor(RewardSource::kQuest, level);
            if (perKill > 0.0 && perQuest > 0.0) {
                ImGui::TextWrapped("%s", UIText::Format(T("$SAL_STATS_ESTIMATE"), {
                    { "kills", std::format("{:.0f}", std::ceil(remaining / perKill)) },
                    { "quests", std::format("{:.1f}", remaining / perQuest) } }).c_str());
            }

            ImGui::Spacing();
            ImGui::SeparatorText(T("$SAL_STATS_MULTIPLIERS").c_str());
            if (ImGui::BeginTable("multipliers", 2, ImGui::ImGuiTableFlags_SizingStretchProp)) {
                for (std::size_t i = 0; i < kSourceCount; ++i) {
                    const auto source = static_cast<RewardSource>(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(SourceLabel(source).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("x%.2f", ScaleFor(source, level));
                }
                ImGui::EndTable();
            }
        }

        void DrawSession()
        {
            ImGui::Spacing();
            ImGui::SeparatorText(T("$SAL_STATS_SESSION").c_str());
            const auto& stats = s_snapshot.stats;
            if (stats.TotalCount() == 0) {
                ImGui::TextDisabled("%s", T("$SAL_STATS_EMPTY").c_str());
                return;
            }
            const auto elapsed = std::chrono::steady_clock::now() - s_snapshot.sessionStart;
            const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
            const double hours = std::chrono::duration<double, std::ratio<3600>>(elapsed).count();

            ImGui::Text("%s: %.0f", T("$SAL_STATS_TOTAL").c_str(), stats.TotalXP());
            ImGui::Text("%s: %u", T("$SAL_STATS_AWARDS").c_str(), stats.TotalCount());
            ImGui::Text("%s: %lld:%02lld", T("$SAL_STATS_DURATION").c_str(),
                static_cast<long long>(minutes / 60), static_cast<long long>(minutes % 60));
            if (hours >= 1.0 / 60.0) {
                ImGui::Text("%s: %.0f", T("$SAL_STATS_RATE").c_str(), stats.TotalXP() / hours);
            }

            constexpr auto flags = ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_BordersInnerH |
                                   ImGui::ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("sources", 4, flags)) {
                ImGui::TableSetupColumn(T("$SAL_STATS_COL_SOURCE").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableSetupColumn(T("$SAL_STATS_COL_XP").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.15f);
                ImGui::TableSetupColumn(T("$SAL_STATS_COL_COUNT").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.15f);
                ImGui::TableSetupColumn(T("$SAL_STATS_COL_SHARE").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < kSourceCount; ++i) {
                    const auto source = static_cast<RewardSource>(i);
                    const auto& totals = stats.For(source);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(SourceLabel(source).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f", totals.xp);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", totals.count);
                    ImGui::TableNextColumn();
                    const auto share = stats.Share(source);
                    const auto label = std::format("{:.0f}%", share * 100.0);
                    ImGui::ProgressBar(static_cast<float>(share), ImGui::ImVec2{ -1.0f, 0.0f }, label.c_str());
                }
                ImGui::EndTable();
            }
        }

        void __stdcall RenderStats()
        {
            RefreshSnapshot();
            DrawCurrentLevel(RE::PlayerCharacter::GetSingleton());
            DrawSession();
        }

        // -------------------------------------------------------------------
        // XP Log
        // -------------------------------------------------------------------

        void DrawFilters()
        {
            const auto current = s_sourceFilter == 0 ? T("$SAL_LOG_ALL")
                                                     : SourceLabel(static_cast<RewardSource>(s_sourceFilter - 1));
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("##source", current.c_str())) {
                if (ImGui::Selectable(T("$SAL_LOG_ALL").c_str(), s_sourceFilter == 0)) s_sourceFilter = 0;
                for (std::size_t i = 0; i < kSourceCount; ++i) {
                    const int value = static_cast<int>(i) + 1;
                    if (ImGui::Selectable(SourceLabel(static_cast<RewardSource>(i)).c_str(), s_sourceFilter == value)) {
                        s_sourceFilter = value;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##search", T("$SAL_LOG_SEARCH").c_str(), s_search, sizeof(s_search));
            ImGui::SameLine();
            ImGui::Checkbox(T("$SAL_LOG_SHOW_NOTES").c_str(), &s_showNotes);
        }

        bool Visible(const XPJournal::Entry& entry, const std::string& subject)
        {
            if (entry.kind == XPJournal::EntryKind::kNote && !s_showNotes) return false;
            if (s_sourceFilter != 0 && static_cast<int>(entry.source) != s_sourceFilter - 1) return false;
            return ContainsInsensitive(subject, s_search) || ContainsInsensitive(SourceLabel(entry.source), s_search);
        }

        void DrawEntries()
        {
            if (s_snapshot.entries.empty()) {
                ImGui::TextDisabled("%s", T("$SAL_LOG_EMPTY").c_str());
                return;
            }
            constexpr auto flags = ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_BordersInnerH |
                                   ImGui::ImGuiTableFlags_ScrollY | ImGui::ImGuiTableFlags_SizingStretchProp;
            if (!ImGui::BeginTable("entries", 5, flags, ImGui::ImVec2{ 0.0f, 380.0f })) return;
            ImGui::TableSetupColumn(T("$SAL_LOG_COL_TIME").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.12f);
            ImGui::TableSetupColumn(T("$SAL_STATS_COL_SOURCE").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.14f);
            ImGui::TableSetupColumn(T("$SAL_LOG_COL_WHAT").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.34f);
            ImGui::TableSetupColumn(T("$SAL_STATS_COL_XP").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.1f);
            ImGui::TableSetupColumn(T("$SAL_LOG_COL_DETAIL").c_str(), ImGui::ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableHeadersRow();

            for (auto it = s_snapshot.entries.rbegin(); it != s_snapshot.entries.rend(); ++it) {
                const auto& entry = *it;
                const auto subject = Subject(entry);
                if (!Visible(entry, subject)) continue;
                const bool note = entry.kind == XPJournal::EntryKind::kNote;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", entry.time.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(SourceLabel(entry.source).c_str());
                ImGui::TableNextColumn();
                if (note) ImGui::TextColored(kMuted, "%s", subject.c_str());
                else ImGui::TextUnformatted(subject.c_str());
                ImGui::TableNextColumn();
                if (note) ImGui::TextColored(kMuted, "-");
                else ImGui::TextColored(kGold, "+%.1f", entry.xp);
                ImGui::TableNextColumn();
                if (note) {
                    ImGui::TextColored(kMuted, "%s", UIText::Format(T(entry.noteKey), {
                        { "count", std::to_string(entry.count) } }).c_str());
                } else {
                    auto detail = UIText::Format(T("$SAL_LOG_DETAIL"), {
                        { "base", std::format("{:g}", entry.baseXP) },
                        { "scale", std::format("{:.2f}", entry.scale) } });
                    if (entry.enemyLevel > 0) {
                        detail += "  " + UIText::Format(T("$SAL_LOG_ENEMY_LEVEL"), {
                            { "level", std::to_string(entry.enemyLevel) } });
                    }
                    ImGui::TextDisabled("%s", detail.c_str());
                }
            }
            ImGui::EndTable();
        }

        void DrawDiagnostics()
        {
            ImGui::Spacing();
            if (!ImGui::CollapsingHeader(T("$SAL_LOG_DIAGNOSTICS").c_str())) return;
            const auto lines = XPJournal::RecentDiagnostics();
            if (ImGui::BeginChild("diagnostics", ImGui::ImVec2{ 0.0f, 260.0f }, ImGui::ImGuiChildFlags_Border)) {
                for (const auto& line : lines) {
                    if (ContainsInsensitive(line, s_search)) ImGui::TextUnformatted(line.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }

        void __stdcall RenderLog()
        {
            RefreshSnapshot();
            DrawFilters();
            ImGui::Spacing();
            DrawEntries();
            DrawDiagnostics();
        }
    }

    void Register()
    {
        if (s_registered || !SKSEMenuFramework::IsInstalled()) return;
        SKSEMenuFramework::SetSection("Simple Alternate Levelling");
        SKSEMenuFramework::AddSectionItem(T("$SAL_PAGE_STATS"), RenderStats);
        SKSEMenuFramework::AddSectionItem(T("$SAL_PAGE_LOG"), RenderLog);
        s_registered = true;
        logger::info("[EA] Info pages: Stats and XP Log registered in SKSE Menu Framework.");
    }
}
