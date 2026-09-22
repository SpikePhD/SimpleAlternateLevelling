#include "LogPolicy.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace EA::LogPolicy {
    namespace {
        constexpr std::string_view kPrefix = "SimpleAlternateLevelling_";
        constexpr std::string_view kSuffix = ".log";
        constexpr std::size_t kTimestampLength = 19;

        bool IsDigit(char value) noexcept {
            return value >= '0' && value <= '9';
        }
    }

    int ValidateMaxLogFiles(std::optional<std::int64_t> value) noexcept {
        if (!value || *value < 0 || *value > kMaximumMaxLogFiles) {
            return kDefaultMaxLogFiles;
        }
        return static_cast<int>(*value);
    }

    bool IsSessionLogName(std::string_view name) noexcept {
        if (!name.starts_with(kPrefix) || !name.ends_with(kSuffix) ||
            name.size() != kPrefix.size() + kTimestampLength + kSuffix.size()) {
            return false;
        }

        const auto timestamp = name.substr(kPrefix.size(), kTimestampLength);
        constexpr std::array separators{
            std::pair{ 4u, '-' }, std::pair{ 7u, '-' }, std::pair{ 10u, '_' },
            std::pair{ 13u, '-' }, std::pair{ 16u, '-' }
        };
        for (std::size_t index = 0; index < timestamp.size(); ++index) {
            const auto separator = std::ranges::find_if(separators, [index](const auto& entry) {
                return entry.first == index;
            });
            if (separator != separators.end()) {
                if (timestamp[index] != separator->second) {
                    return false;
                }
            } else if (!IsDigit(timestamp[index])) {
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> SelectLogsToDelete(
        std::vector<std::string> names,
        int                      maxFiles) {
        if (maxFiles == 0) {
            return {};
        }

        std::erase_if(names, [](const auto& name) {
            return !IsSessionLogName(name);
        });
        std::ranges::sort(names);
        if (names.size() <= static_cast<std::size_t>(maxFiles)) {
            return {};
        }
        names.resize(names.size() - static_cast<std::size_t>(maxFiles));
        return names;
    }
}
