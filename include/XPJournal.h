#pragma once

#include "RewardRules.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/sinks/ringbuffer_sink.h>

// Session-only record of XP rewards for the in-game Stats and XP Log pages.
// Written on the game's main thread, read by the pages under a mutex. Cleared
// on every load, revert, and new game with the other transient reward state.
namespace EA::XPJournal {
    enum class EntryKind : std::uint8_t {
        kAward,  // XP was granted
        kNote    // a reward was skipped or merged; see noteKey
    };

    struct Entry {
        std::string                 time;        // local wall-clock time, HH:MM:SS
        EntryKind                   kind{ EntryKind::kAward };
        RewardRules::RewardSource   source{ RewardRules::RewardSource::kQuest };
        std::string                 subject;     // in-game name (actor, quest, book, place)
        std::string                 subjectKey;  // translation key used instead of subject when set
        std::string                 noteKey;     // translation key explaining a note
        int                         enemyLevel{ 0 };
        int                         count{ 0 };  // objectives merged into one reward
        float                       baseXP{ 0.0f };
        double                      scale{ 1.0 };
        float                       xp{ 0.0f };
        int                         playerLevel{ 0 };
    };

    struct Snapshot {
        std::uint64_t                         version{ 0 };
        std::vector<Entry>                    entries;  // oldest first
        RewardRules::SessionStats             stats;
        std::chrono::steady_clock::time_point sessionStart{};
    };

    inline constexpr std::size_t kMaxEntries = 500;

    void RecordAward(Entry entry);
    void RecordNote(RewardRules::RewardSource source, std::string subject, std::string noteKey, int count = 0);
    void ResetSession();

    // Monotonic counter bumped by every change; lets pages skip copying.
    [[nodiscard]] std::uint64_t Version();
    [[nodiscard]] Snapshot Copy();

    // Recent info-and-above plugin log lines (never trace spam).
    void SetDiagnosticsSink(std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink);
    [[nodiscard]] std::vector<std::string> RecentDiagnostics();
}
