#pragma once

#include <string>
#include <string_view>

namespace rtc::security {

// Abstraction over password hashing so services depend on the capability, not
// a concrete algorithm. This keeps the domain testable (a fake hasher in unit
// tests) and lets the hashing scheme evolve without touching business logic.
class IPasswordHasher {
  public:
    virtual ~IPasswordHasher() = default;

    // Produces a self-describing salted hash for `plaintext`. Throws
    // rtc::errors::InternalException if hashing fails.
    [[nodiscard]] virtual std::string hash(std::string_view plaintext) const = 0;

    // Returns true iff `plaintext` matches the previously produced `hash`.
    // Never throws for a malformed hash; returns false instead.
    [[nodiscard]] virtual bool verify(std::string_view plaintext, std::string_view hash) const = 0;
};

}  // namespace rtc::security
