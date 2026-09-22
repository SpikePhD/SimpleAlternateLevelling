#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace EA::Leveling {
    void ApplyGameSettings(std::string_view reason);
    bool RefreshThreshold(std::string_view reason, std::uint32_t expectedLevel = 0);
    void QueueThresholdRefresh(std::uint32_t expectedLevel, std::string reason);
    void ResetState();
}
