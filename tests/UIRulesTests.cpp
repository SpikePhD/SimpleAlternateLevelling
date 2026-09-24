#ifdef NDEBUG
#undef NDEBUG
#endif

#include "UIRules.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

using namespace EA::UIRules;

namespace {
    std::array<float, kSkillCount> Levels(float value = 15.0f)
    {
        std::array<float, kSkillCount> result{};
        result.fill(value);
        return result;
    }
}

int main()
{
    assert(ValidateInteger(0.0, 10, 0, 1000).value == 0);
    assert(ValidateInteger(1000.0, 10, 0, 1000).value == 1000);
    assert(ValidateInteger(-360.0, -90, -360, 360).value == -360);
    assert(ValidateInteger(360.0, -90, -360, 360).value == 360);
    assert(ValidateInteger(-361.0, -90, -360, 360).replaced);
    assert(ValidateInteger(361.0, -90, -360, 360).replaced);
    assert(ValidateInteger(1.5, 10, 0, 1000).replaced);
    assert(ValidateInteger(std::numeric_limits<double>::infinity(), 10, 0, 1000).replaced);
    assert(!ValidateFloat(200.5, 200.0f, 1.0f, 1000.0f).replaced);
    assert(!ValidateFloat(1.0, 200.0f, 1.0f, 1000.0f).replaced);
    assert(!ValidateFloat(1000.0, 200.0f, 1.0f, 1000.0f).replaced);
    assert(ValidateFloat(0.0, 200.0f, 1.0f, 1000.0f).replaced);
    assert(ValidateFloat(std::nan(""), 200.0f, 1.0f, 1000.0f).replaced);

    assert(CheckedPointTotal(10, 5) == 15);
    assert(!CheckedPointTotal(-1, 5));
    assert(!CheckedPointTotal(std::numeric_limits<int>::max(), 1));
    assert(ParseIntegralIdentifier(42.0) == 42);
    assert(!ParseIntegralIdentifier(42.5));
    assert(!ParseIntegralIdentifier(std::nan("")));
    constexpr std::array whitelist{ 6, 7, 8 };
    assert(FindWhitelistedIdentifier(7, whitelist) == 1);
    assert(!FindWhitelistedIdentifier(9, whitelist));

    AllocationSession session;
    auto levels = Levels();
    assert(session.BeginOpening());
    assert(session.State() == SessionState::kOpening);
    assert(session.Begin(2, 16.0f, levels));
    assert(session.State() == SessionState::kActive);
    assert(session.Allocate(0) == AllocationResult::kAllocated);
    assert(session.Preview(0) == 16.0f);
    assert(session.Allocate(0) == AllocationResult::kAtCap);
    assert(session.RemainingPoints() == 1);
    assert(session.HasChanges());
    assert(session.Reset());
    assert(session.Preview(0) == 15.0f);
    assert(session.RemainingPoints() == 2);
    assert(!session.HasChanges());

    assert(session.Allocate(1) == AllocationResult::kAllocated);
    auto plan = session.PrepareCommit(levels);
    assert(plan.Ready());
    assert(plan.finalValues[1] == 16.0f);
    assert(plan.pendingPoints == 1);
    assert(session.PrepareCommit(levels).status == CommitStatus::kInvalidState);
    assert(!session.Reset());
    assert(session.Allocate(1) == AllocationResult::kInvalidState);

    session.Cancel();
    assert(session.Begin(3, 200.0f, levels));
    assert(session.Allocate(2) == AllocationResult::kAllocated);
    auto drifted = levels;
    drifted[2] += 1.0f;
    assert(session.PrepareCommit(drifted).status == CommitStatus::kSnapshotDrift);

    session.Cancel();
    assert(session.Begin(0, 200.0f, levels));
    assert(session.Allocate(0) == AllocationResult::kNoPoints);

    session.Cancel();
    auto fractional = Levels(199.5f);
    assert(session.Begin(1, 200.0f, fractional));
    assert(session.Allocate(0) == AllocationResult::kAllocated);
    assert(session.Preview(0) == 200.0f);
    session.MarkClosing();
    assert(session.State() == SessionState::kClosing);
    assert(session.Allocate(0) == AllocationResult::kInvalidState);

    session.Cancel();
    assert(session.Begin(3, 200.0f, levels));
    assert(session.Deallocate(0) == AllocationResult::kNothingToRemove);
    assert(session.Allocate(0) == AllocationResult::kAllocated);
    assert(session.Allocate(0) == AllocationResult::kAllocated);
    assert(session.Deallocate(0) == AllocationResult::kAllocated);
    assert(session.Preview(0) == 16.0f);
    assert(session.Delta(0) == 1);
    assert(session.RemainingPoints() == 2);
    assert(session.Deallocate(0) == AllocationResult::kAllocated);
    assert(session.Preview(0) == 15.0f);
    assert(!session.HasChanges());
    assert(session.Deallocate(0) == AllocationResult::kNothingToRemove);
    assert(session.Deallocate(kSkillCount) == AllocationResult::kInvalidSkill);

    session.Cancel();
    auto nearCap = Levels(199.5f);
    assert(session.Begin(2, 200.0f, nearCap));
    assert(session.Allocate(0) == AllocationResult::kAllocated);
    assert(session.Preview(0) == 200.0f);
    assert(session.Deallocate(0) == AllocationResult::kAllocated);
    assert(session.Preview(0) == 199.5f);
    session.MarkClosing();
    assert(session.Deallocate(0) == AllocationResult::kInvalidState);

    session.Cancel();
    levels[4] = std::numeric_limits<float>::quiet_NaN();
    assert(!session.Begin(1, 200.0f, levels));
    assert(session.State() == SessionState::kIdle);

    // Level-up step hand-off.
    LevelUpHandoff handoff;
    assert(handoff.Begin(false, true) == HandoffDecision::kContinueNow);
    assert(handoff.Begin(true, false) == HandoffDecision::kContinueNow);
    assert(!handoff.Waiting());
    assert(!handoff.Continue());
    assert(handoff.Begin(true, true) == HandoffDecision::kAwaitStep);
    assert(handoff.Waiting());
    assert(handoff.Begin(true, false) == HandoffDecision::kAwaitStep);
    assert(handoff.Continue());
    assert(!handoff.Continue());
    assert(!handoff.Waiting());

    assert(handoff.Begin(true, true) == HandoffDecision::kAwaitStep);
    handoff.Reset();
    assert(!handoff.Waiting());
    assert(!handoff.Continue());

    // Fail-safe: only continuous unpaused time counts.
    assert(!handoff.ObserveSample(false, 0.0, 10.0));
    assert(handoff.Begin(true, true) == HandoffDecision::kAwaitStep);
    assert(!handoff.ObserveSample(true, 0.0, 10.0));
    assert(!handoff.ObserveSample(true, 100.0, 10.0));
    assert(!handoff.ObserveSample(false, 100.0, 10.0));
    assert(!handoff.ObserveSample(false, 109.0, 10.0));
    assert(!handoff.ObserveSample(true, 109.5, 10.0));
    assert(!handoff.ObserveSample(false, 110.0, 10.0));
    assert(!handoff.ObserveSample(false, 119.9, 10.0));
    assert(handoff.ObserveSample(false, 120.0, 10.0));
    assert(handoff.Waiting());
    assert(handoff.Continue());
    assert(!handoff.ObserveSample(false, 500.0, 10.0));
    assert(handoff.Begin(true, true) == HandoffDecision::kAwaitStep);
    assert(!handoff.ObserveSample(false, std::nan(""), 10.0));
    assert(!handoff.ObserveSample(false, 1.0, std::nan("")));
    assert(handoff.ObserveSample(false, 1.0 + LevelUpHandoff::kDefaultGraceSeconds, std::nan("")));
    handoff.Reset();

    // Level-up sequence: neither step registered.
    LevelUpFlow flow;
    assert(flow.Stage() == LevelUpStage::kIdle);
    assert(flow.Begin(false, false) == FlowAction::kOpenSkillMenu);
    assert(flow.Stage() == LevelUpStage::kSkillMenu);
    assert(flow.FinishSkillMenu(false, false) == FlowAction::kOpenVanilla);
    assert(flow.Stage() == LevelUpStage::kVanilla);
    assert(flow.Continue() == FlowAction::kNone);
    flow.Reset();
    assert(flow.Stage() == LevelUpStage::kIdle);

    // Registered steps that decline behave as unregistered.
    assert(flow.Begin(true, false) == FlowAction::kOpenSkillMenu);
    assert(flow.FinishSkillMenu(true, false) == FlowAction::kOpenVanilla);
    flow.Reset();

    // Pre-step only.
    assert(flow.Begin(true, true) == FlowAction::kAwaitStep);
    assert(flow.Stage() == LevelUpStage::kPreStep);
    assert(flow.Waiting());
    assert(flow.FinishSkillMenu(false, false) == FlowAction::kNone);
    assert(flow.Continue() == FlowAction::kOpenSkillMenu);
    assert(flow.Stage() == LevelUpStage::kSkillMenu);
    assert(flow.Continue() == FlowAction::kNone);
    assert(flow.FinishSkillMenu(false, false) == FlowAction::kOpenVanilla);
    flow.Reset();

    // Post-step (V1) only.
    assert(flow.Begin(false, false) == FlowAction::kOpenSkillMenu);
    assert(flow.Continue() == FlowAction::kNone);
    assert(flow.FinishSkillMenu(true, true) == FlowAction::kAwaitStep);
    assert(flow.Stage() == LevelUpStage::kPostStep);
    assert(flow.Continue() == FlowAction::kOpenVanilla);
    assert(flow.Stage() == LevelUpStage::kVanilla);
    assert(flow.Continue() == FlowAction::kNone);
    flow.Reset();

    // Both steps: each waits independently.
    assert(flow.Begin(true, true) == FlowAction::kAwaitStep);
    assert(flow.Continue() == FlowAction::kOpenSkillMenu);
    assert(flow.FinishSkillMenu(true, true) == FlowAction::kAwaitStep);
    assert(flow.Stage() == LevelUpStage::kPostStep);
    assert(flow.Continue() == FlowAction::kOpenVanilla);
    assert(flow.Continue() == FlowAction::kNone);

    // Fail-safe applies to the pre-step and resumes to the skill menu.
    assert(flow.Begin(true, true) == FlowAction::kAwaitStep);
    assert(!flow.ObserveSample(false, 0.0, 10.0));
    assert(!flow.ObserveSample(true, 5.0, 10.0));
    assert(!flow.ObserveSample(false, 6.0, 10.0));
    assert(flow.ObserveSample(false, 16.0, 10.0));
    assert(flow.Continue() == FlowAction::kOpenSkillMenu);
    assert(!flow.ObserveSample(false, 100.0, 10.0));

    // Reset discards a pending wait; Begin restarts from any stage.
    assert(flow.FinishSkillMenu(true, true) == FlowAction::kAwaitStep);
    flow.Reset();
    assert(!flow.Waiting());
    assert(flow.Continue() == FlowAction::kNone);
    assert(flow.Begin(true, true) == FlowAction::kAwaitStep);
    assert(flow.Begin(false, false) == FlowAction::kOpenSkillMenu);
    assert(!flow.Waiting());
    flow.Reset();

    // Character-created callback.
    CharacterCreatedSignal created;
    created.ObserveCreationMenuClosed();
    assert(!created.TryFire(false, true));
    created.Arm();
    assert(created.Armed());
    assert(!created.TryFire(false, true));
    created.ObserveCreationMenuClosed();
    assert(!created.TryFire(true, true));
    assert(!created.TryFire(false, false));
    assert(created.TryFire(false, true));
    assert(!created.Armed());
    assert(!created.TryFire(false, true));
    created.ObserveCreationMenuClosed();
    assert(!created.TryFire(false, true));
    created.Arm();
    created.ObserveCreationMenuClosed();
    created.Reset();
    assert(!created.TryFire(false, true));
    created.Arm();
    assert(!created.CreationMenuClosed());
    return 0;
}
