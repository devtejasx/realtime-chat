#pragma once

#include <string>
#include <string_view>

#include "rtc/security/password_hasher.hpp"

namespace rtc::security {

// bcrypt-based IPasswordHasher.
//
// bcrypt is deliberately slow and memory-independent, with a tunable work
// factor ("cost"). The default cost of 12 is a reasonable production baseline;
// raise it as hardware improves. Cost is validated to bcrypt's supported range.
class BcryptPasswordHasher final : public IPasswordHasher {
public:
    static constexpr int kDefaultCost = 12;
    static constexpr int kMinCost = 4;
    static constexpr int kMaxCost = 31;

    explicit BcryptPasswordHasher(int cost = kDefaultCost);

    [[nodiscard]] std::string hash(std::string_view plaintext) const override;
    [[nodiscard]] bool verify(std::string_view plaintext,
                              std::string_view hash) const override;

    [[nodiscard]] int cost() const noexcept { return cost_; }

private:
    int cost_;
};

}  // namespace rtc::security
