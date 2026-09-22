#pragma once

#include <RE/A/ActorValues.h>

namespace EA::SkillMenu {
    [[nodiscard]] bool Register();
    void Open();
    void AllocatePoint(RE::ActorValue skill);
    void Confirm();
    void ResetAllocations();
    void ResetState();
}
