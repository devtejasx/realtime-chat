#include "rtc/services/user_service.hpp"

#include <gtest/gtest.h>

#include "rtc/dto/auth_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "support/fake_password_hasher.hpp"
#include "support/fake_user_repository.hpp"

namespace {

using rtc::dto::LoginRequest;
using rtc::dto::RegisterRequest;
using rtc::errors::AuthenticationException;
using rtc::errors::ConflictException;
using rtc::errors::NotFoundException;
using rtc::services::UserService;
using rtc::testing::FakePasswordHasher;
using rtc::testing::FakeUserRepository;

RegisterRequest make_register(std::string username, std::string email, std::string password) {
    RegisterRequest r;
    r.username = std::move(username);
    r.email = std::move(email);
    r.password = std::move(password);
    return r;
}

class UserServiceTest : public ::testing::Test {
protected:
    FakeUserRepository repo_;
    FakePasswordHasher hasher_;
    UserService service_{repo_, hasher_};
};

TEST_F(UserServiceTest, RegisterCreatesUserWithHashedPassword) {
    const auto user = service_.register_user(make_register("alice", "alice@example.com", "pw12345678"));
    EXPECT_EQ(user.id, 1);
    EXPECT_EQ(user.username, "alice");
    EXPECT_EQ(user.password_hash, "fakehash:pw12345678");
    EXPECT_EQ(repo_.count(), 1U);
}

TEST_F(UserServiceTest, RegisterDuplicateUsernameThrowsConflict) {
    service_.register_user(make_register("alice", "alice@example.com", "pw12345678"));
    EXPECT_THROW(service_.register_user(make_register("alice", "other@example.com", "pw12345678")),
                 ConflictException);
}

TEST_F(UserServiceTest, AuthenticateWithCorrectPasswordSucceeds) {
    service_.register_user(make_register("bob", "bob@example.com", "sup3rsecret"));

    LoginRequest login;
    login.identifier = "bob";
    login.password = "sup3rsecret";
    const auto user = service_.authenticate(login);
    EXPECT_EQ(user.username, "bob");
}

TEST_F(UserServiceTest, AuthenticateByEmailSucceeds) {
    service_.register_user(make_register("bob", "bob@example.com", "sup3rsecret"));

    LoginRequest login;
    login.identifier = "bob@example.com";
    login.password = "sup3rsecret";
    EXPECT_NO_THROW(service_.authenticate(login));
}

TEST_F(UserServiceTest, AuthenticateWithWrongPasswordThrows) {
    service_.register_user(make_register("bob", "bob@example.com", "sup3rsecret"));

    LoginRequest login;
    login.identifier = "bob";
    login.password = "wrong";
    EXPECT_THROW(service_.authenticate(login), AuthenticationException);
}

TEST_F(UserServiceTest, AuthenticateUnknownUserThrows) {
    LoginRequest login;
    login.identifier = "ghost";
    login.password = "whatever";
    EXPECT_THROW(service_.authenticate(login), AuthenticationException);
}

TEST_F(UserServiceTest, GetByIdReturnsUserOrThrows) {
    const auto created = service_.register_user(make_register("carol", "c@example.com", "pw12345678"));
    EXPECT_EQ(service_.get_by_id(created.id).username, "carol");
    EXPECT_THROW(service_.get_by_id(9999), NotFoundException);
}

}  // namespace
