#pragma once

// Simple Alternate Levelling (SAL) integration API, version 4.
//
// Copy this header into a companion SKSE plugin. It has no dependencies
// beyond <cstdint> and uses only plain C types and function pointers, so it
// is safe across separately built DLLs.
//
// Handshake: during SKSEPlugin_Load, register a messaging listener for the
// sender SAL::kSenderName. At kPostPostLoad SAL broadcasts a message of type
// SAL::kMessageInterface whose data points to a static SALInterfaceV4 that
// stays valid for the life of the process. It begins with a SALInterfaceV3,
// which begins with a SALInterfaceV2, which begins with a SALInterfaceV1, so
// V1-V3 consumers keep working unchanged:
//
//     messaging->RegisterListener(SAL::kSenderName, [](SKSE::MessagingInterface::Message* msg) {
//         if (!msg || msg->type != SAL::kMessageInterface || msg->dataLen < sizeof(SAL::SALInterfaceV1)) {
//             return;
//         }
//         const auto* v1 = static_cast<const SAL::SALInterfaceV1*>(msg->data);
//         if (v1->version >= SAL::kInterfaceVersion4 && msg->dataLen >= sizeof(SAL::SALInterfaceV4)) {
//             const auto* v4 = static_cast<const SAL::SALInterfaceV4*>(msg->data);
//             /* store v4, register callbacks including RegisterXPMultiplier */
//         } else if (v1->version >= SAL::kInterfaceVersion3 && msg->dataLen >= sizeof(SAL::SALInterfaceV3)) {
//             const auto* v3 = static_cast<const SAL::SALInterfaceV3*>(msg->data);
//             /* store v3, register callbacks including RegisterSkillPointBonus */
//         } else if (v1->version >= SAL::kInterfaceVersion2 && msg->dataLen >= sizeof(SAL::SALInterfaceV2)) {
//             const auto* v2 = static_cast<const SAL::SALInterfaceV2*>(msg->data);
//             /* store v2, register callbacks including RegisterPreSkillMenuStep */
//         } else if (v1->version >= SAL::kInterfaceVersion1) {
//             /* V1-only SAL: store v1, register callbacks */
//         }
//     });
//
// Contract:
// - Each Register* slot accepts one registrant. A second registration, or a
//   null callback, is rejected (returns false) and logged by SAL.
// - Register during the kPostPostLoad listener callback or later.
// - SAL invokes every callback on the main thread. Callbacks must not throw
//   and must not block.
// - ContinueLevelUp and RequestThresholdRefresh may be called from any
//   thread; SAL performs the work on the main thread.
// - Level-up sequence after SAL intercepts the vanilla LevelUp Menu:
//     pre-skill-menu step (V2) -> skill point bonus (V3) -> SAL skill menu
//     -> level-up step (V1) -> vanilla LevelUp Menu, opened once.
//   Each step is optional and waits independently. ContinueLevelUp resumes
//   whichever step is waiting, so when both steps are registered each owner
//   must call it exactly once per wait.
// - XP multiplier (V4): applied to every XP award as it is earned, outside
//   the level-up sequence. It never changes the XP needed per level.
// - Nothing registered here is persisted. SAL's cosave is unchanged.

#include <cstdint>

namespace SAL {
    // SKSE messaging sender name (SAL's plugin name).
    inline constexpr const char* kSenderName = "SimpleAlternateLevelling";
    // SKSE message type carrying a pointer to the interface.
    inline constexpr std::uint32_t kMessageInterface = 0x53414C00u;  // 'SAL\0'
    inline constexpr std::uint32_t kInterfaceVersion1 = 1;
    inline constexpr std::uint32_t kInterfaceVersion2 = 2;
    inline constexpr std::uint32_t kInterfaceVersion3 = 3;
    inline constexpr std::uint32_t kInterfaceVersion4 = 4;

    // XP source categories passed to the V4 XP multiplier provider. The
    // values are stable and new categories are only ever appended; treat an
    // unknown category like any other.
    inline constexpr std::uint32_t kXPSourceQuest = 0;        // quests and objectives
    inline constexpr std::uint32_t kXPSourceKill = 1;
    inline constexpr std::uint32_t kXPSourceExploration = 2;  // discovering and clearing locations
    inline constexpr std::uint32_t kXPSourceLock = 3;
    inline constexpr std::uint32_t kXPSourceBook = 4;
    inline constexpr std::uint32_t kXPSourcePickpocket = 5;

    struct SALInterfaceV1 {
        // Interface version of the broadcast: 1 for a V1-only SAL, 2, 3, or 4
        // when this struct is the prefix of a SALInterfaceV2, SALInterfaceV3,
        // or SALInterfaceV4.
        // Later versions only append members, so check version >= the
        // version you need.
        std::uint32_t version;

        // Threshold multiplier: the returned value multiplies the XP needed
        // for the current level. SAL ignores non-finite or <= 0 values
        // (treated as 1.0) and clamps the result to
        // [integration.threshold_multiplier_floor, 1.0]. The provider is
        // called whenever SAL refreshes the threshold (data load, game load,
        // new game, settings change, after each level-up, and on request).
        // It never affects reward scaling.
        bool (*RegisterThresholdMultiplier)(float (*provider)());

        // Ask SAL to recompute the threshold after the provider's value
        // changes. During an in-progress level-up the request is folded into
        // the refresh SAL already performs when the LevelUp Menu closes.
        void (*RequestThresholdRefresh)();

        // Level-up step between SAL's skill menu and the vanilla LevelUp
        // Menu. When SAL is about to open the vanilla menu it calls
        // wantsStep(level) with the player's current level. Returning true
        // makes SAL wait; the step owner must then call ContinueLevelUp on
        // every exit path. Returning false continues immediately.
        bool (*RegisterLevelUpStep)(bool (*wantsStep)(std::uint32_t level));

        // Resumes whichever step is waiting: after the pre-skill-menu step
        // SAL opens its skill menu; after the level-up step it opens the
        // vanilla LevelUp Menu once. Idempotent; ignored when nothing waits.
        // As a fail-safe, SAL continues by itself when the game has stayed
        // unpaused for 10 seconds while a step waits, so keep a pausing menu
        // open during the step.
        void (*ContinueLevelUp)();

        // Called once per new character, after character creation closes and
        // SAL has applied its starting skills (immediately after creation in
        // Vanilla starting-skills mode). Not called for loaded saves.
        bool (*RegisterCharacterCreated)(void (*callback)());
    };

    struct SALInterfaceV2 {
        // Layout-compatible prefix. v1.version is 2 in a V2 broadcast, 3 in
        // a V3 broadcast, and 4 in a V4 broadcast.
        SALInterfaceV1 v1;

        // Pre-skill-menu step. When SAL has intercepted the vanilla LevelUp
        // Menu and is about to open its skill menu, it calls wantsStep(level)
        // with the player's current level, on every intercepted level-up and
        // regardless of how many skill points SAL grants. Returning true
        // makes SAL wait; the step owner must call v1.ContinueLevelUp on
        // every exit path, after which SAL opens its skill menu (or continues
        // as it does today when there are no skill points). Returning false
        // continues immediately.
        bool (*RegisterPreSkillMenuStep)(bool (*wantsStep)(std::uint32_t level));
    };

    struct SALInterfaceV3 {
        // Layout-compatible prefix. v2.v1.version is 3 in a V3 broadcast and
        // 4 in a V4 broadcast.
        SALInterfaceV2 v2;

        // Skill point bonus. SAL calls bonus(level) exactly once per level-up
        // it intercepts, after the pre-skill-menu step has finished (or right
        // away when there is none) and before it computes the points for its
        // skill menu; never for stray or re-opened menus. `level` is the
        // player's current level. The return value is added to that
        // level-up's grant: total = pending + points_per_level + bonus.
        // Negative values count as 0, values above 1000 are clamped to 1000.
        // If the skill menu cannot open or a commit is rejected, the bonus is
        // kept in SAL's pending points like the base grant. SAL shows it in
        // the skill menu as a bonus from other mods.
        bool (*RegisterSkillPointBonus)(std::int32_t (*bonus)(std::uint32_t level));
    };

    struct SALInterfaceV4 {
        // Layout-compatible prefix. v3.v2.v1.version is 4 in a V4 broadcast.
        SALInterfaceV3 v3;

        // XP reward multiplier. SAL calls provider(sourceCategory) on the
        // main thread once per XP award, with no caching, and multiplies that
        // award by the result: amount = base * level scaling * multiplier.
        // sourceCategory is one of the kXPSource* constants. Because the value
        // is read live, a temporary buff affects exactly the awards made while
        // it is active; keep the provider cheap. Finite values in (0, 1)
        // reduce XP. Non-finite or <= 0 values count as 1.0; values above 100
        // are clamped to 100. The result is not rounded. It never affects the
        // XP needed per level (see RegisterThresholdMultiplier) or SAL's
        // level scaling of rewards.
        bool (*RegisterXPMultiplier)(float (*provider)(std::uint32_t sourceCategory));
    };
}
