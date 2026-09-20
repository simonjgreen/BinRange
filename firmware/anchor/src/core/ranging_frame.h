#pragma once

#include <cstddef>

inline constexpr bool ranging_poll_length_valid(std::size_t length) {
    return length == 12;
}

inline constexpr bool ranging_final_length_valid(std::size_t length) {
    return length == 24 || length == 26 || length == 29 || length == 33;
}
