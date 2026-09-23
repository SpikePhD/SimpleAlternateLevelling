#pragma once

#include <string_view>

namespace EA::EventSinks {
    void Register();
    void ResetRewardState();

    // Records which locations are already ever-cleared. Call after a save has
    // loaded and on new game, so only later clears award XP.
    void SnapshotClearedLocations(std::string_view reason);
}
