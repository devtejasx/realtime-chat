#include "rtc/security/bcrypt_password_hasher.hpp"

#include <algorithm>
#include <string>

#include <bcrypt.h>

#include "rtc/errors/exceptions.hpp"

namespace rtc::security {

BcryptPasswordHasher::BcryptPasswordHasher(int cost)
    : cost_(std::clamp(cost, kMinCost, kMaxCost)) {}

std::string BcryptPasswordHasher::hash(std::string_view plaintext) const {
    try {
        // Bcrypt.cpp works with std::string; construct once from the view.
        return bcrypt::generateHash(std::string(plaintext), cost_);
    } catch (const std::exception& ex) {
        throw rtc::errors::InternalException("Password hashing failed", ex.what());
    }
}

bool BcryptPasswordHasher::verify(std::string_view plaintext, std::string_view hash) const {
    if (hash.empty()) {
        return false;
    }
    try {
        return bcrypt::validatePassword(std::string(plaintext), std::string(hash));
    } catch (const std::exception&) {
        // A malformed stored hash must never crash auth; treat it as a mismatch.
        return false;
    }
}

}  // namespace rtc::security
