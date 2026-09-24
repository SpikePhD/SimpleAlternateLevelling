#include "UIRules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace EA::UIRules {
    IntegerValidation ValidateInteger(
        double candidate, int defaultValue, int minimum, int maximum)
    {
        if (!std::isfinite(candidate) || std::trunc(candidate) != candidate ||
            candidate < static_cast<double>(minimum) || candidate > static_cast<double>(maximum)) {
            return { defaultValue, true };
        }
        return { static_cast<int>(candidate), false };
    }

    FloatValidation ValidateFloat(
        double candidate, float defaultValue, float minimum, float maximum)
    {
        if (!std::isfinite(candidate) || candidate < static_cast<double>(minimum) ||
            candidate > static_cast<double>(maximum)) {
            return { defaultValue, true };
        }
        return { static_cast<float>(candidate), false };
    }

    std::optional<int> CheckedPointTotal(int pendingPoints, int grant)
    {
        if (pendingPoints < 0 || grant < 0) {
            return std::nullopt;
        }
        const auto total = static_cast<std::int64_t>(pendingPoints) + grant;
        if (total > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return static_cast<int>(total);
    }

    std::optional<int> ParseIntegralIdentifier(double value)
    {
        if (!std::isfinite(value) || std::trunc(value) != value ||
            value < static_cast<double>(std::numeric_limits<int>::min()) ||
            value > static_cast<double>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }
        return static_cast<int>(value);
    }

    std::optional<std::size_t> FindWhitelistedIdentifier(
        int value, std::span<const int> whitelist)
    {
        const auto found = std::find(whitelist.begin(), whitelist.end(), value);
        if (found == whitelist.end()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(std::distance(whitelist.begin(), found));
    }

    bool AllocationSession::BeginOpening() noexcept
    {
        if (_state != SessionState::kIdle) {
            return false;
        }
        _state = SessionState::kOpening;
        return true;
    }

    bool AllocationSession::Begin(
        int totalPoints,
        float cap,
        const std::array<float, kSkillCount>& snapshot)
    {
        if (_state == SessionState::kIdle) {
            _state = SessionState::kOpening;
        }
        if (_state != SessionState::kOpening) {
            return false;
        }
        if (totalPoints < 0 || !std::isfinite(cap) || cap < 1.0f || cap > 1000.0f) {
            Cancel();
            return false;
        }
        for (const auto value : snapshot) {
            if (!std::isfinite(value)) {
                Cancel();
                return false;
            }
        }

        _totalPoints = totalPoints;
        _remainingPoints = totalPoints;
        _cap = cap;
        _snapshot = snapshot;
        _preview = snapshot;
        _deltas.fill(0);
        _state = SessionState::kActive;
        return true;
    }

    AllocationResult AllocationSession::Allocate(std::size_t skillIndex)
    {
        if (_state != SessionState::kActive) {
            return AllocationResult::kInvalidState;
        }
        if (skillIndex >= kSkillCount) {
            return AllocationResult::kInvalidSkill;
        }
        if (_remainingPoints <= 0) {
            return AllocationResult::kNoPoints;
        }
        const auto current = _preview[skillIndex];
        if (!std::isfinite(current)) {
            return AllocationResult::kInvalidValue;
        }
        if (current >= _cap) {
            return AllocationResult::kAtCap;
        }

        _preview[skillIndex] = std::min(_cap, current + 1.0f);
        ++_deltas[skillIndex];
        --_remainingPoints;
        return AllocationResult::kAllocated;
    }

    AllocationResult AllocationSession::Deallocate(std::size_t skillIndex)
    {
        if (_state != SessionState::kActive) {
            return AllocationResult::kInvalidState;
        }
        if (skillIndex >= kSkillCount) {
            return AllocationResult::kInvalidSkill;
        }
        if (_deltas[skillIndex] == 0) {
            return AllocationResult::kNothingToRemove;
        }

        --_deltas[skillIndex];
        ++_remainingPoints;
        // Recompute from the snapshot so a fractional start (e.g. 199.5 capped
        // to 200 by Allocate) returns exactly to its original value.
        _preview[skillIndex] = _deltas[skillIndex] == 0
            ? _snapshot[skillIndex]
            : std::min(_cap, _snapshot[skillIndex] + static_cast<float>(_deltas[skillIndex]));
        return AllocationResult::kAllocated;
    }

    bool AllocationSession::Reset()
    {
        if (_state != SessionState::kActive) {
            return false;
        }
        _preview = _snapshot;
        _deltas.fill(0);
        _remainingPoints = _totalPoints;
        return true;
    }

    CommitPlan AllocationSession::PrepareCommit(
        const std::array<float, kSkillCount>& currentValues)
    {
        CommitPlan plan;
        plan.pendingPoints = _remainingPoints;
        if (_state != SessionState::kActive) {
            return plan;
        }
        _state = SessionState::kCommitting;

        for (std::size_t index = 0; index < kSkillCount; ++index) {
            if (!std::isfinite(currentValues[index])) {
                plan.status = CommitStatus::kInvalidValue;
                return plan;
            }
            if (currentValues[index] != _snapshot[index]) {
                plan.status = CommitStatus::kSnapshotDrift;
                return plan;
            }
        }

        plan.status = CommitStatus::kReady;
        plan.finalValues = _preview;
        return plan;
    }

    void AllocationSession::MarkClosing() noexcept
    {
        if (_state != SessionState::kIdle) {
            _state = SessionState::kClosing;
        }
    }

    void AllocationSession::Cancel() noexcept
    {
        _state = SessionState::kIdle;
        _totalPoints = 0;
        _remainingPoints = 0;
        _cap = 1.0f;
        _snapshot.fill(0.0f);
        _preview.fill(0.0f);
        _deltas.fill(0);
    }

    bool AllocationSession::HasChanges() const noexcept
    {
        return std::any_of(_deltas.begin(), _deltas.end(), [](auto delta) { return delta != 0; });
    }

    HandoffDecision LevelUpHandoff::Begin(bool stepRegistered, bool wantsStep) noexcept
    {
        if (_waiting) {
            return HandoffDecision::kAwaitStep;
        }
        if (!stepRegistered || !wantsStep) {
            return HandoffDecision::kContinueNow;
        }
        _waiting = true;
        _unpausedSince.reset();
        return HandoffDecision::kAwaitStep;
    }

    bool LevelUpHandoff::Continue() noexcept
    {
        if (!_waiting) {
            return false;
        }
        Reset();
        return true;
    }

    bool LevelUpHandoff::ObserveSample(bool gamePaused, double nowSeconds, double graceSeconds) noexcept
    {
        if (!_waiting || !std::isfinite(nowSeconds)) {
            return false;
        }
        if (gamePaused) {
            _unpausedSince.reset();
            return false;
        }
        if (!_unpausedSince || nowSeconds < *_unpausedSince) {
            _unpausedSince = nowSeconds;
        }
        const double grace = std::isfinite(graceSeconds) && graceSeconds >= 0.0 ? graceSeconds : kDefaultGraceSeconds;
        return nowSeconds - *_unpausedSince >= grace;
    }

    void LevelUpHandoff::Reset() noexcept
    {
        _waiting = false;
        _unpausedSince.reset();
    }

    void CharacterCreatedSignal::Arm() noexcept
    {
        _armed = true;
        _menuClosed = false;
    }

    void CharacterCreatedSignal::ObserveCreationMenuClosed() noexcept
    {
        if (_armed) {
            _menuClosed = true;
        }
    }

    bool CharacterCreatedSignal::TryFire(bool creationMenuOpen, bool skillsSettled) noexcept
    {
        if (!_armed || !_menuClosed || creationMenuOpen || !skillsSettled) {
            return false;
        }
        Reset();
        return true;
    }

    void CharacterCreatedSignal::Reset() noexcept
    {
        _armed = false;
        _menuClosed = false;
    }
}
