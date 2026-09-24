#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

// Translated text for the SKSE Menu Framework pages. Keys are the "$SAL_*"
// entries in Interface/Translations/SimpleAlternateLevelling_<LANGUAGE>.txt.
namespace EA::UIText {
    // Cached and thread-safe; a missing key returns the key itself.
    [[nodiscard]] const std::string& Get(const std::string& key);

    // Replaces "{name}" placeholders, e.g. Format(text, { { "count", "3" } }).
    [[nodiscard]] std::string Format(
        std::string text, std::initializer_list<std::pair<std::string_view, std::string>> values);
}
