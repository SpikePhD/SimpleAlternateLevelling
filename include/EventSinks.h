#pragma once

#include <string_view>

namespace EA::EventSinks {
    void Register();
    void ResetRewardState();

    // Records which locations are already ever-cleared and which books are
    // already read. Call after a save has loaded and on new game, so only
    // later clears and first reads award XP.
    void SnapshotSavedFlags(std::string_view reason);

    // Awards book XP, on the next frame, for every book whose read flag was
    // set since the snapshot. Covers world, inventory, and container reads.
    void QueueReadBookCheck(std::string_view trigger);
}
