#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr bool ranging_poll_length_valid(std::size_t length) {
    return length == 12;
}

inline constexpr bool ranging_final_length_valid(std::size_t length) {
    return length == 24 || length == 26 || length == 29 || length == 33 || length == 41;
}

struct TipTelemetry {
    bool has_tip = false;
    bool tip_ready = false;
    uint32_t tip_count = 0;
    uint32_t tip_age_s = UINT32_MAX;
};

// Length includes the radio's two-byte FCS. Never interpret older FCS bytes
// as extension data. Unknown fields also clear any earlier report.
inline TipTelemetry ranging_tip_telemetry(const uint8_t *frame, std::size_t length) {
    TipTelemetry tip;
    if (!frame || length != 41) return tip;
    tip.has_tip = true;
    tip.tip_ready = (frame[24] & 0x04) && !(frame[24] & 0x02);
    tip.tip_count = static_cast<uint32_t>(frame[31]) |
        (static_cast<uint32_t>(frame[32]) << 8) |
        (static_cast<uint32_t>(frame[33]) << 16) |
        (static_cast<uint32_t>(frame[34]) << 24);
    tip.tip_age_s = static_cast<uint32_t>(frame[35]) |
        (static_cast<uint32_t>(frame[36]) << 8) |
        (static_cast<uint32_t>(frame[37]) << 16) |
        (static_cast<uint32_t>(frame[38]) << 24);
    return tip;
}
