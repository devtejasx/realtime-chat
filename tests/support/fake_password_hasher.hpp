#pragma once

#include <string>
#include <string_view>

#include "rtc/security/password_hasher.hpp"

namespace rtc::testing {

// Deterministic, fast IPasswordHasher for unit tests. Avoids bcrypt's
// intentional slowness while preserving the hash/verify contract: a value
// hashes to a stable string and verifies only against its own plaintext.
class FakePasswordHasher final : public security::IPasswordHasher {
public:
    [[nodiscard]] std::string hash(std::string_view plaintext) const override {
        return kPrefix + std::string(plaintext);
    }

    [[nodiscard]] bool verify(std::string_view plaintext,
                              std::string_view hash) const override {
        return hash == (kPrefix + std::string(plaintext));
    }

private:
    static constexpr const char* kPrefix = "fakehash:";
};

}  // namespace rtc::testing
