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
    // end of the level-up.
    [[nodiscard]] bool IsDeferringVanillaLevelUp();
}
