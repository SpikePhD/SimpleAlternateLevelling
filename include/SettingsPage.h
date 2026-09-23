#pragma once

namespace EA::SettingsPage {
    // Registers the "Simple Alternate Levelling" page in SKSE Menu Framework's
    // Mod Control Panel. Call after all plugins have loaded (kDataLoaded).
    // Returns false, without side effects, when the framework is not installed.
    bool Register();
}
