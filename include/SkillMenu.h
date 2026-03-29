#pragma once

namespace EA::SkillMenu {

    // Register the EA Skill Menu with the UI system and install the
    // UIMessageQueue::AddMessage hook that intercepts the LevelUp Menu open.
    // Call from OnDataLoaded(), after EventSinks::Register().
    void Register();

    // Open the skill allocation menu. Called by the AddMessage hook when
    // the engine tries to open the LevelUp Menu.
    void Open();

    // Award one skill point to the given actor value. Called from the AS2
    // EA_OnAllocate callback. Sends EA_UpdateSkill and EA_UpdatePoints back
    // to the SWF; auto-confirms when points reach zero.
    void AllocatePoint(RE::ActorValue a_skill);

    // Close our menu and re-open the vanilla LevelUp Menu. Called from the
    // AS2 EA_OnConfirm callback and auto-triggered at zero points.
    void Confirm();

}  // namespace EA::SkillMenu
