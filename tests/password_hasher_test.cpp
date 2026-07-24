#include "rtc/security/bcrypt_password_hasher.hpp"

#include <gtest/gtest.h>

namespace {

using rtc::security::BcryptPasswordHasher;

// Use the minimum bcrypt cost to keep the suite fast; production uses 12.
constexpr int kTestCost = BcryptPasswordHasher::kMinCost;

TEST(BcryptPasswordHasherTest, HashIsVerifiable) {
    const BcryptPasswordHasher hasher(kTestCost);
    const std::string hash = hasher.hash("correct horse battery staple");
    EXPECT_TRUE(hasher.verify("correct horse battery staple", hash));
}

TEST(BcryptPasswordHasherTest, WrongPasswordFailsVerification) {
    const BcryptPasswordHasher hasher(kTestCost);
    const std::string hash = hasher.hash("real-password");
    EXPECT_FALSE(hasher.verify("wrong-password", hash));
}

TEST(BcryptPasswordHasherTest, HashesAreSaltedAndUnique) {
    const BcryptPasswordHasher hasher(kTestCost);
    const std::string a = hasher.hash("same-input");
    const std::string b = hasher.hash("same-input");
    EXPECT_NE(a, b);  // distinct salts => distinct hashes
    EXPECT_TRUE(hasher.verify("same-input", a));
    EXPECT_TRUE(hasher.verify("same-input", b));
}

TEST(BcryptPasswordHasherTest, MalformedHashReturnsFalseNotThrow) {
    const BcryptPasswordHasher hasher(kTestCost);
    EXPECT_FALSE(hasher.verify("whatever", "not-a-real-bcrypt-hash"));
    EXPECT_FALSE(hasher.verify("whatever", ""));
}

TEST(BcryptPasswordHasherTest, CostIsClampedToValidRange) {
    EXPECT_EQ(BcryptPasswordHasher(2).cost(), BcryptPasswordHasher::kMinCost);
    EXPECT_EQ(BcryptPasswordHasher(99).cost(), BcryptPasswordHasher::kMaxCost);
}

}  // namespace
