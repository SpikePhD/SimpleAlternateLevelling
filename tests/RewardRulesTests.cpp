#include "RewardRules.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
    int failures = 0;

    void Check(bool condition, std::string_view message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    void TestQuestLifecycle()
    {
        using namespace EA::RewardRules;
        QuestLifecycle lifecycle;
        Check(lifecycle.Observe(0x1234, QuestSignal::kCompleted), "first completion accepted");
        Check(!lifecycle.Observe(0x1234, QuestSignal::kCompleted), "duplicate completion rejected");
        Check(!lifecycle.Observe(0x1234, QuestSignal::kReset), "reset does not award");
        Check(lifecycle.Observe(0x1234, QuestSignal::kCompleted), "reset re-arms completion");
        Check(!lifecycle.Observe(0x1234, QuestSignal::kStarted), "start does not award");
        Check(lifecycle.Observe(0x1234, QuestSignal::kCompleted), "start re-arms completion");
        lifecycle.Reset();
        Check(lifecycle.Observe(0x1234, QuestSignal::kCompleted), "global reset clears lifecycle");
        Check(!lifecycle.Observe(0, QuestSignal::kCompleted), "zero quest id rejected");
    }

    void TestNewlyFlaggedTracker()
    {
        using EA::RewardRules::NewlyFlaggedTracker;
        using Ids = std::vector<std::uint32_t>;
        NewlyFlaggedTracker tracker;
        Check(!tracker.Ready(), "tracker starts without a snapshot");
        Check(tracker.Observe(Ids{ 0x10, 0x20 }).empty(), "observation without snapshot awards nothing");
        Check(tracker.Ready(), "first observation becomes the baseline");
        Check(tracker.Observe(Ids{ 0x10, 0x20, 0x30 }) == Ids{ 0x30 }, "remote clear detected by location");
        Check(tracker.Observe(Ids{ 0x10, 0x20, 0x30 }).empty(), "same clear never awards twice");

        tracker.Snapshot(Ids{ 0xA, 0xB });
        Check(tracker.Observe(Ids{ 0xA, 0xB }).empty(), "locations cleared before load do not award");
        Check(tracker.Observe(Ids{ 0xA, 0xB, 0xC, 0xD }) == (Ids{ 0xC, 0xD }), "simultaneous clears each award");
        Check(tracker.Observe(Ids{ 0, 0xA }).empty(), "zero id ignored");

        tracker.Invalidate();
        Check(!tracker.Ready(), "invalidate clears readiness");
        Check(tracker.Observe(Ids{ 0xA, 0xE }).empty(), "invalidated tracker does not mass-award");
    }

    void TestObjectiveBatcher()
    {
        using EA::RewardRules::ObjectiveBatch;
        using EA::RewardRules::ObjectiveBatcher;
        ObjectiveBatcher batcher;
        Check(batcher.Empty(), "batcher starts empty");
        Check(batcher.Add(0x100, 10), "first completion asks for a flush");
        Check(!batcher.Add(0x100, 20), "same-frame completion joins the batch");
        Check(!batcher.Add(0x200, 5), "another quest in the same frame needs no second flush");
        Check(!batcher.Add(0x100, 30), "third objective of the first quest joins its batch");
        const auto flushed = batcher.Flush();
        Check(flushed.size() == 2, "one entry per quest");
        Check(flushed.size() == 2 && flushed[0] == (ObjectiveBatch{ 0x100, 10, 3 }),
            "wrap-up batch keeps first objective and counts all");
        Check(flushed.size() == 2 && flushed[1] == (ObjectiveBatch{ 0x200, 5, 1 }), "single completion stays single");
        Check(batcher.Empty(), "flush clears the batch");
        Check(batcher.Add(0x100, 40), "next frame starts a new batch");
        batcher.Reset();
        Check(batcher.Empty() && batcher.Flush().empty(), "reset drops pending completions");
    }

    void TestSessionStats()
    {
        using namespace EA::RewardRules;
        SessionStats stats;
        Check(stats.TotalXP() == 0.0 && stats.Share(RewardSource::kKill) == 0.0, "empty stats have no share");
        stats.Add(RewardSource::kKill, 30.0);
        stats.Add(RewardSource::kKill, 10.0);
        stats.Add(RewardSource::kQuest, 60.0);
        stats.Add(RewardSource::kBook, 0.0);
        stats.Add(RewardSource::kBook, std::nan(""));
        stats.Add(RewardSource::kCount, 50.0);
        Check(stats.TotalXP() == 100.0, "total sums valid awards only");
        Check(stats.TotalCount() == 3, "count ignores invalid awards");
        Check(stats.For(RewardSource::kKill).count == 2 && stats.For(RewardSource::kKill).xp == 40.0, "per-source totals");
        Check(stats.Share(RewardSource::kQuest) == 0.6, "share is a fraction of all XP");
        stats.Reset();
        Check(stats.TotalXP() == 0.0 && stats.TotalCount() == 0, "reset clears totals");
    }

    void TestTransitionsAndEligibility()
    {
        using namespace EA::RewardRules;
        Check(IsObjectiveCompletionTransition(1, 2), "displayed to completed awards");
        Check(IsObjectiveCompletionTransition(1, 3), "displayed to completed-displayed awards");
        Check(!IsObjectiveCompletionTransition(2, 3), "completed presentation change does not duplicate");
        Check(!IsObjectiveCompletionTransition(4, 5), "failed transition does not award");
        Check(IsObjectiveCompletionTransition(4, 2), "failed objective can later complete");

        Check(ShouldRewardPickpocket(1), "single item pickpocket awards");
        Check(ShouldRewardPickpocket(50), "stack still produces one eligible event");
        Check(!ShouldRewardPickpocket(0), "empty pickpocket event rejected");

        Check(ShouldRewardKill(true, false, false), "player-credited kill awards");
        Check(!ShouldRewardKill(false, false, false), "uncredited kill rejected");
        Check(!ShouldRewardKill(true, true, false), "player death rejected");
        Check(!ShouldRewardKill(true, false, true), "player's own minion rejected");
    }

    void TestKillRewards()
    {
        using EA::RewardRules::CalculateKillReward;
        Check(CalculateKillReward(5.0f, 5, 10, 1.0f, 1.0f) == 5.0f, "lower-level kill gets base");
        Check(CalculateKillReward(5.0f, 10, 10, 2.0f, 1.0f) == 5.0f, "equal-level kill gets base");
        Check(CalculateKillReward(5.0f, 15, 10, 2.0f, 1.5f) == 22.5f, "higher-level bonus and multiplier");
        Check(std::isnan(CalculateKillReward(
            std::numeric_limits<float>::max(), 100, 1,
            std::numeric_limits<float>::max(), std::numeric_limits<float>::max())),
            "overflow-prone reward is invalidated");
        Check(std::isfinite(CalculateKillReward(
            1.0f, std::numeric_limits<int>::max(), std::numeric_limits<int>::min(),
            1.0f, 1.0f)),
            "extreme level delta does not overflow integer arithmetic");
    }

    void TestMappings()
    {
        using namespace EA::RewardRules;
        Check(ClassifyRewardSource("quest_main") == RewardSource::kQuest, "quest type maps to quests");
        Check(ClassifyRewardSource("quest_objectives") == RewardSource::kQuest, "misc objectives map to quests");
        Check(ClassifyRewardSource("kill") == RewardSource::kKill, "kills map to kills");
        Check(ClassifyRewardSource("location_discovery") == RewardSource::kExploration, "discovery maps to exploration");
        Check(ClassifyRewardSource("location_cleared") == RewardSource::kExploration, "clearing maps to exploration");
        Check(ClassifyRewardSource("lock_picked") == RewardSource::kLock, "locks map to locks");
        Check(ClassifyRewardSource("book_read") == RewardSource::kBook, "books map to books");
        Check(ClassifyRewardSource("book_skill") == RewardSource::kBook, "skill books map to books");
        Check(ClassifyRewardSource("pickpocket") == RewardSource::kPickpocket, "pickpocket maps to pickpocket");
        Check(ClassifyRewardSource("something_new") == RewardSource::kQuest, "unknown source uses quest growth");
        Check(ClassifyMarkerType(16) == "military_camp", "imperial camp mapping");
        Check(ClassifyMarkerType(29) == "giant_camp", "giant camp mapping");
        Check(ClassifyMarkerType(53) == "daedric_shrine", "Miraak Temple mapping");
        Check(ClassifyMarkerType(54) == "town", "Raven Rock mapping");
        Check(ClassifyMarkerType(55) == "doomstone", "Solstheim standing stone mapping");
        Check(ClassifyMarkerType(56) == "landmark", "Telvanni tower mapping");
        Check(ClassifyMarkerType(57) == "docks" && ClassifyMarkerType(58) == "docks", "travel marker mapping");
        Check(ClassifyMarkerType(59) == "castle", "Castle Karstaag mapping");
        Check(ClassifyMarkerType(999) == "default", "unknown marker fallback");

        Check(ClassifyLockLevel(0) == "novice", "novice lock mapping");
        Check(ClassifyLockLevel(1) == "apprentice", "apprentice lock mapping");
        Check(ClassifyLockLevel(2) == "adept", "adept lock mapping");
        Check(ClassifyLockLevel(3) == "expert", "expert lock mapping");
        Check(ClassifyLockLevel(4) == "master", "master lock mapping");
        Check(ClassifyLockLevel(99) == "novice", "unknown lock fallback");
    }
}

int main()
{
    TestQuestLifecycle();
    TestNewlyFlaggedTracker();
    TestObjectiveBatcher();
    TestSessionStats();
    TestTransitionsAndEligibility();
    TestKillRewards();
    TestMappings();
    if (failures == 0) {
        std::cout << "All reward-rule tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
