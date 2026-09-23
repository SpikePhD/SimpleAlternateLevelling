#include "RewardRules.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

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

    void TestTransitionsAndEligibility()
    {
        using namespace EA::RewardRules;
        Check(IsObjectiveCompletionTransition(1, 2), "displayed to completed awards");
        Check(IsObjectiveCompletionTransition(1, 3), "displayed to completed-displayed awards");
        Check(!IsObjectiveCompletionTransition(2, 3), "completed presentation change does not duplicate");
        Check(!IsObjectiveCompletionTransition(4, 5), "failed transition does not award");
        Check(IsObjectiveCompletionTransition(4, 2), "failed objective can later complete");

        Check(ShouldRewardBook(true, true, false), "successful first player read awards");
        Check(!ShouldRewardBook(false, true, false), "failed activation rejected");
        Check(!ShouldRewardBook(true, false, false), "non-player activation rejected");
        Check(!ShouldRewardBook(true, true, true), "reread rejected");

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
    TestTransitionsAndEligibility();
    TestKillRewards();
    TestMappings();
    if (failures == 0) {
        std::cout << "All reward-rule tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
