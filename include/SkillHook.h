#pragma once

namespace EA::SkillHook {
    // Installs the skill-XP trampoline and the collision-free book Activate
    // vtable hook used for exact successful-read rewards.
    // Must be called during SKSEPlugin_Load, after SKSE::Init(), on kDataLoaded.
    void Install();
}
