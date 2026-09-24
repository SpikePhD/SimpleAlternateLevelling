#pragma once

#include <RE/A/ActorValues.h>

namespace EA::SkillMenu {
    [[nodiscard]] bool Register();
    void Open();
    void AllocatePoint(RE::ActorValue skill);
    void DeallocatePoint(RE::ActorValue skill);
    void Confirm();
    void ResetAllocations();
    void ResetState();

    // True while a vanilla LevelUp Menu has been intercepted and its
    // continuation has not reopened it yet. Its interim close is not the
    // end of the level-up. Stays true while an integration step (pre- or
    // post-skill-menu) is waiting for ContinueLevelUp.
    [[nodiscard]] bool IsDeferringVanillaLevelUp();

    // Ends whichever integration step is waiting: the pre-skill-menu step
    // continues to the skill menu, the post-skill-menu step queues the
    // vanilla LevelUp Menu once. Main thread only; ignored when nothing waits.
    void ContinueLevelUp();
    // Thread-safe variant for the integration API: runs ContinueLevelUp on
    // the main thread unless lifecycle state is invalidated first.
    void RequestContinueLevelUp();
}
