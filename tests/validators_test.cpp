#include "rtc/validation/validators.hpp"

#include <string>

#include <gtest/gtest.h>

#include "rtc/dto/auth_dto.hpp"
#include "rtc/errors/exceptions.hpp"

namespace {

using rtc::errors::ValidationException;
namespace validation = rtc::validation;

TEST(ValidatorsTest, TrimStripsSurroundingWhitespace) {
    EXPECT_EQ(validation::trim("  hello \t"), "hello");
    EXPECT_EQ(validation::trim("   "), "");
    EXPECT_EQ(validation::trim("nospace"), "nospace");
}

TEST(ValidatorsTest, AcceptsValidUsername) {
    EXPECT_NO_THROW(validation::validate_username("alice_01"));
    EXPECT_NO_THROW(validation::validate_username("Bob.the-builder"));
}

TEST(ValidatorsTest, RejectsShortUsername) {
    EXPECT_THROW(validation::validate_username("ab"), ValidationException);
}

TEST(ValidatorsTest, RejectsUsernameStartingWithSymbol) {
    EXPECT_THROW(validation::validate_username("_leading"), ValidationException);
}

TEST(ValidatorsTest, RejectsUsernameWithIllegalChars) {
    EXPECT_THROW(validation::validate_username("bad name"), ValidationException);
    EXPECT_THROW(validation::validate_username("bad$name"), ValidationException);
}

TEST(ValidatorsTest, AcceptsValidEmail) {
    EXPECT_NO_THROW(validation::validate_email("user@example.com"));
}

TEST(ValidatorsTest, RejectsInvalidEmail) {
    EXPECT_THROW(validation::validate_email("not-an-email"), ValidationException);
    EXPECT_THROW(validation::validate_email("missing@tld"), ValidationException);
    EXPECT_THROW(validation::validate_email("@example.com"), ValidationException);
}

TEST(ValidatorsTest, RejectsShortPassword) {
    EXPECT_THROW(validation::validate_password("short"), ValidationException);
}

TEST(ValidatorsTest, RejectsPasswordOver72Bytes) {
    EXPECT_THROW(validation::validate_password(std::string(73, 'x')), ValidationException);
}

TEST(ValidatorsTest, NormalizesRegisterRequest) {
    rtc::dto::RegisterRequest request;
    request.username = "  Alice  ";
    request.email = "  Alice@Example.COM ";
    request.password = "password123";

    validation::validate_and_normalize(request);

    EXPECT_EQ(request.username, "Alice");
    EXPECT_EQ(request.email, "alice@example.com");  // trimmed + lower-cased
}

TEST(ValidatorsTest, RejectsEmptyLoginIdentifier) {
    rtc::dto::LoginRequest request;
    request.identifier = "   ";
    request.password = "password123";
    EXPECT_THROW(validation::validate_and_normalize(request), ValidationException);
}

}  // namespace
