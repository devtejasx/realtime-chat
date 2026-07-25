#pragma once

#include <string>
#include <string_view>

namespace rtc::utils {

// Returns the lowercase hex SHA-256 digest of `data`. Backed by OpenSSL (already
// a dependency via jwt-cpp). Used for attachment checksums and for hashing
// refresh tokens before they are persisted — the raw token is never stored.
[[nodiscard]] std::string sha256_hex(std::string_view data);

}  // namespace rtc::utils
