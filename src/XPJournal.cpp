#include "PCH.h"
#include "XPJournal.h"

#include <ctime>
#include <deque>
#include <mutex>

namespace EA::XPJournal {
    namespace {
        std::mutex                                        s_mutex;
        std::deque<Entry>                                 s_entries;
        RewardRules::SessionStats                         s_stats;
        std::chrono::steady_clock::time_point             s_sessionStart = std::chrono::steady_clock::now();
        std::uint64_t                                     s_version = 1;
        std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> s_diagnostics;

        std::string Now()
        {
            const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local{};
            localtime_s(&local, &time);
            return std::format("{:02}:{:02}:{:02}", local.tm_hour, local.tm_min, local.tm_sec);
        }

        void Push(Entry entry)
        {
            entry.time = Now();
            s_entries.push_back(std::move(entry));
            while (s_entries.size() > kMaxEntries) {
                s_entries.pop_front();
            }
            ++s_version;
        }
    }

    void RecordAward(Entry entry)
    {
        std::lock_guard lock(s_mutex);
        entry.kind = EntryKind::kAward;
        s_stats.Add(entry.source, entry.xp);
        Push(std::move(entry));
    }

    void RecordNote(RewardRules::RewardSource source, std::string subject, std::string noteKey, int count)
    {
        std::lock_guard lock(s_mutex);
        Entry entry;
        entry.kind = EntryKind::kNote;
        entry.source = source;
        entry.subject = std::move(subject);
        entry.noteKey = std::move(noteKey);
        entry.count = count;
        Push(std::move(entry));
    }

    void ResetSession()
    {
        std::lock_guard lock(s_mutex);
        s_entries.clear();
        s_stats.Reset();
        s_sessionStart = std::chrono::steady_clock::now();
        ++s_version;
    }

    std::uint64_t Version()
    {
        std::lock_guard lock(s_mutex);
        return s_version;
    }

    Snapshot Copy()
    {
        std::lock_guard lock(s_mutex);
        Snapshot snapshot;
        snapshot.version = s_version;
        snapshot.entries.assign(s_entries.begin(), s_entries.end());
        snapshot.stats = s_stats;
        snapshot.sessionStart = s_sessionStart;
        return snapshot;
    }

    void SetDiagnosticsSink(std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink)
    {
        std::lock_guard lock(s_mutex);
        s_diagnostics = std::move(sink);
    }

    std::vector<std::string> RecentDiagnostics()
    {
        std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
        {
            std::lock_guard lock(s_mutex);
            sink = s_diagnostics;
        }
        if (!sink) return {};
        auto lines = sink->last_formatted();
        for (auto& line : lines) {
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        }
        return lines;
    }
}
