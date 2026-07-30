#include <gtest/gtest.h>

#include "rtc/dto/auth_dto.hpp"
#include "rtc/dto/profile_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/services/user_service.hpp"
#include "support/fake_password_hasher.hpp"
#include "support/fake_user_repository.hpp"

namespace {

using rtc::dto::RegisterRequest;
using rtc::dto::UpdateProfileRequest;
using rtc::errors::ValidationException;

class UserProfileTest : public ::testing::Test {
  protected:
    std::int64_t make_user() {
        RegisterRequest r;
        r.username = "alice";
        r.email = "alice@example.com";
        r.password = "password123";
        return service_.register_user(r).id;
    }

    rtc::testing::FakeUserRepository repo_;
    rtc::testing::FakePasswordHasher hasher_;
    rtc::services::UserService service_{repo_, hasher_};
};

TEST_F(UserProfileTest, UpdatesProvidedFieldsOnly) {
    const auto id = make_user();

    UpdateProfileRequest req;
    req.display_name_set = true;
    req.display_name = "Alice A.";
    req.bio_set = true;
    req.bio = "Hello there";

    const auto updated = service_.update_profile(id, req);
    ASSERT_TRUE(updated.display_name.has_value());
    EXPECT_EQ(*updated.display_name, "Alice A.");
    ASSERT_TRUE(updated.bio.has_value());
    EXPECT_EQ(*updated.bio, "Hello there");
    EXPECT_FALSE(updated.avatar_url.has_value());  // untouched
}

TEST_F(UserProfileTest, TrimsAndClearsBlankValues) {
    const auto id = make_user();
    UpdateProfileRequest set;
    set.display_name_set = true;
    set.display_name = "  Bob  ";
    EXPECT_EQ(*service_.update_profile(id, set).display_name, "Bob");

    UpdateProfileRequest blank;
    blank.display_name_set = true;
    blank.display_name = "   ";  // whitespace clears the field
    EXPECT_FALSE(service_.update_profile(id, blank).display_name.has_value());
}

TEST_F(UserProfileTest, ExplicitNullClearsField) {
    const auto id = make_user();
    UpdateProfileRequest set;
    set.bio_set = true;
    set.bio = "some bio";
    service_.update_profile(id, set);

    UpdateProfileRequest clear;
    clear.bio_set = true;
    clear.bio = std::nullopt;  // explicit null
    EXPECT_FALSE(service_.update_profile(id, clear).bio.has_value());
}

TEST_F(UserProfileTest, RejectsInvalidAvatarUrl) {
    const auto id = make_user();
    UpdateProfileRequest req;
    req.avatar_url_set = true;
    req.avatar_url = "not-a-url";
    EXPECT_THROW(service_.update_profile(id, req), ValidationException);
}

TEST_F(UserProfileTest, AcceptsHttpsAvatarUrl) {
    const auto id = make_user();
    UpdateProfileRequest req;
    req.avatar_url_set = true;
    req.avatar_url = "https://example.com/avatar.png";
    EXPECT_EQ(*service_.update_profile(id, req).avatar_url, "https://example.com/avatar.png");
}

TEST_F(UserProfileTest, RejectsOverlongDisplayName) {
    const auto id = make_user();
    UpdateProfileRequest req;
    req.display_name_set = true;
    req.display_name = std::string(65, 'x');
    EXPECT_THROW(service_.update_profile(id, req), ValidationException);
}

}  // namespace
