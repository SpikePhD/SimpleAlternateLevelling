#include "PCH.h"
#include "UIText.h"

#include <SKSE/Translation.h>

#include <mutex>
#include <unordered_map>

namespace EA::UIText {
    namespace {
        std::mutex                                   s_mutex;
        std::unordered_map<std::string, std::string> s_cache;  // node-based: references stay valid
        bool                                         s_parsed = false;
    }

    const std::string& Get(const std::string& key)
    {
        std::lock_guard lock(s_mutex);
        if (!s_parsed) {
            SKSE::Translation::ParseTranslation("SimpleAlternateLevelling");
            s_parsed = true;
        }
        if (const auto it = s_cache.find(key); it != s_cache.end()) {
            return it->second;
        }
        std::string value;
        if (!SKSE::Translation::Translate(key, value) || value.empty()) {
            value = key;
        }
        return s_cache.emplace(key, std::move(value)).first->second;
    }

    std::string Format(std::string text, std::initializer_list<std::pair<std::string_view, std::string>> values)
    {
        for (const auto& [name, value] : values) {
            const auto token = "{" + std::string(name) + "}";
            for (auto pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + value.size())) {
                text.replace(pos, token.size(), value);
            }
        }
        return text;
    }
}
