#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace EA::LogPolicy {

    inline constexpr int kDefaultMaxLogFiles = 10;
    inline constexpr int kMaximumMaxLogFiles = 1000;

    [[nodiscard]] int ValidateMaxLogFiles(
        std::optional<std::int64_t> value) noexcept;

    [[nodiscard]] bool IsSessionLogName(std::string_view name) noexcept;

    [[nodiscard]] std::vector<std::string> SelectLogsToDelete(
        std::vector<std::string> names,
        int                      maxFiles);
}
