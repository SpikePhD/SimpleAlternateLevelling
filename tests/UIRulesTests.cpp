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
    assert(ValidatePanelHeight(0.0, 0).value == 0);
    assert(ValidatePanelHeight(300.0, 0).value == 300);
    assert(ValidatePanelHeight(720.0, 0).value == 720);
    assert(ValidatePanelHeight(299.0, 0).replaced);
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
    levels[4] = std::numeric_limits<float>::quiet_NaN();
    assert(!session.Begin(1, 200.0f, levels));
    assert(session.State() == SessionState::kIdle);
    return 0;
}
