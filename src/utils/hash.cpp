#include "rtc/utils/hash.hpp"

#include <openssl/sha.h>

#include <array>
#include <cstdint>

namespace rtc::utils {

std::string sha256_hex(std::string_view data) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest.data());

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const unsigned char byte : digest) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

}  // namespace rtc::utils
