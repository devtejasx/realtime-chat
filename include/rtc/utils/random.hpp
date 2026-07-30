#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace rtc::utils {

// Generates a cryptographically-seeded random hex token of `bytes` bytes
// (2*bytes hex chars). Used for storage keys, session ids, and opaque handles.
// Seeded from std::random_device and mixed per call so tokens are unpredictable
// and collision-resistant for these purposes.
[[nodiscard]] inline std::string generate_hex_token(std::size_t bytes = 16) {
    static thread_local std::mt19937_64 engine(
        std::random_device{}() ^ static_cast<std::uint64_t>(std::random_device{}()) << 1);
    static constexpr std::array<char, 16> kHex{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        const auto byte = static_cast<unsigned>(engine() & 0xFF);
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

}  // namespace rtc::utils
