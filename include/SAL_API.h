#pragma once

// Simple Alternate Levelling (SAL) integration API, version 1.
//
// Copy this header into a companion SKSE plugin. It has no dependencies
// beyond <cstdint> and uses only plain C types and function pointers, so it
// is safe across separately built DLLs.
//
// Handshake: during SKSEPlugin_Load, register a messaging listener for the
// sender SAL::kSenderName. At kPostPostLoad SAL broadcasts a message of type
// SAL::kMessageInterface whose data points to a static SALInterfaceV1 that
// stays valid for the life of the process:
//
//     messaging->RegisterListener(SAL::kSenderName, [](SKSE::MessagingInterface::Message* msg) {
//         if (msg && msg->type == SAL::kMessageInterface &&
//             msg->dataLen >= sizeof(SAL::SALInterfaceV1)) {
//             const auto* api = static_cast<const SAL::SALInterfaceV1*>(msg->data);
//             if (api->version >= 1) { /* store api, register callbacks */ }
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
// - Nothing registered here is persisted. SAL's cosave is unchanged.

#include <cstdint>

namespace SAL {
    // SKSE messaging sender name (SAL's plugin name).
    inline constexpr const char* kSenderName = "SimpleAlternateLevelling";
    // SKSE message type carrying a pointer to SALInterfaceV1.
    inline constexpr std::uint32_t kMessageInterface = 0x53414C00u;  // 'SAL\0'
    inline constexpr std::uint32_t kInterfaceVersion1 = 1;

    struct SALInterfaceV1 {
        // Always 1 for this layout. Later versions only append members.
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

        // Resumes a waiting level-up: SAL opens the vanilla LevelUp Menu
        // once. Idempotent; ignored when SAL is not waiting. As a fail-safe,
        // SAL continues by itself when the game has stayed unpaused for 10
        // seconds while waiting, so keep a pausing menu open during the step.
        void (*ContinueLevelUp)();

        // Called once per new character, after character creation closes and
        // SAL has applied its starting skills (immediately after creation in
        // Vanilla starting-skills mode). Not called for loaded saves.
        bool (*RegisterCharacterCreated)(void (*callback)());
    };
}
