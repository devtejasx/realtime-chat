#include "rtc/services/conversation_service.hpp"

#include <gtest/gtest.h>

#include "rtc/dto/conversation_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/models/conversation.hpp"
#include "rtc/notifications/notification_dispatcher.hpp"
#include "support/fake_conversation_repository.hpp"
#include "support/fake_user_repository.hpp"
#include "support/recording_broadcaster.hpp"

namespace {

using rtc::dto::CreateConversationRequest;
using rtc::dto::RenameGroupRequest;
using rtc::errors::AuthorizationException;
using rtc::errors::ConflictException;
using rtc::errors::NotFoundException;
using rtc::errors::ValidationException;
using rtc::models::ConversationType;

class ConversationServiceTest : public ::testing::Test {
protected:
    std::int64_t make_user(const std::string& name) {
        return users_.create({name, name + "@example.com", "hash"}).id;
    }

    CreateConversationRequest direct_to(std::int64_t other) {
        CreateConversationRequest r;
        r.type = ConversationType::kDirect;
        r.participant_ids = {other};
        return r;
    }

    CreateConversationRequest group(std::string name, std::vector<std::int64_t> members) {
        CreateConversationRequest r;
        r.type = ConversationType::kGroup;
        r.name = std::move(name);
        r.participant_ids = std::move(members);
        return r;
    }

    rtc::testing::FakeConversationRepository conversations_;
    rtc::testing::FakeUserRepository users_;
    rtc::testing::RecordingBroadcaster broadcaster_;
    rtc::notifications::NullNotificationDispatcher notifications_;
    rtc::services::ConversationService service_{conversations_, users_, broadcaster_,
                                                notifications_};
};

TEST_F(ConversationServiceTest, CreatesDirectConversationAndBroadcasts) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");

    const auto conversation = service_.create(alice, direct_to(bob));
    EXPECT_EQ(conversation.type, ConversationType::kDirect);
    EXPECT_EQ(conversation.direct_key, "1:2");
    ASSERT_EQ(broadcaster_.count(), 1U);
    EXPECT_EQ(broadcaster_.last().type, "conversation.created");
}

TEST_F(ConversationServiceTest, DirectConversationsAreDeduplicated) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");

    const auto first = service_.create(alice, direct_to(bob));
    const auto second = service_.create(bob, direct_to(alice));  // reversed order
    EXPECT_EQ(first.id, second.id);
}

TEST_F(ConversationServiceTest, RejectsDirectWithSelf) {
    const auto alice = make_user("alice");
    EXPECT_THROW(service_.create(alice, direct_to(alice)), ValidationException);
}

TEST_F(ConversationServiceTest, RejectsDirectWithUnknownUser) {
    const auto alice = make_user("alice");
    EXPECT_THROW(service_.create(alice, direct_to(9999)), NotFoundException);
}

TEST_F(ConversationServiceTest, CreatesGroupWithOwnerAndMembers) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");
    const auto carol = make_user("carol");

    const auto conversation = service_.create(alice, group("Team", {bob, carol}));
    EXPECT_EQ(conversation.type, ConversationType::kGroup);
    EXPECT_EQ(conversation.owner_id, alice);
    EXPECT_EQ(service_.participants(conversation.id).size(), 3U);
}

TEST_F(ConversationServiceTest, GroupRequiresName) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");
    CreateConversationRequest r;
    r.type = ConversationType::kGroup;
    r.participant_ids = {bob};
    EXPECT_THROW(service_.create(alice, r), ValidationException);
}

TEST_F(ConversationServiceTest, GetHidesConversationFromNonParticipants) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");
    const auto carol = make_user("carol");
    const auto conversation = service_.create(alice, direct_to(bob));

    EXPECT_NO_THROW(service_.get(alice, conversation.id));
    EXPECT_THROW(service_.get(carol, conversation.id), NotFoundException);
}

TEST_F(ConversationServiceTest, OnlyOwnerCanRenameGroup) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");
    const auto conversation = service_.create(alice, group("Old", {bob}));

    RenameGroupRequest rename{"New Name"};
    EXPECT_THROW(service_.rename_group(bob, conversation.id, rename), AuthorizationException);
    const auto updated = service_.rename_group(alice, conversation.id, rename);
    EXPECT_EQ(updated.name, "New Name");
}

TEST_F(ConversationServiceTest, AddMemberRejectsDuplicates) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");
    const auto carol = make_user("carol");
    const auto conversation = service_.create(alice, group("Team", {bob}));

    service_.add_member(alice, conversation.id, carol);
    EXPECT_THROW(service_.add_member(alice, conversation.id, carol), ConflictException);
}

TEST_F(ConversationServiceTest, CannotRemoveOwner) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");
    const auto conversation = service_.create(alice, group("Team", {bob}));
    EXPECT_THROW(service_.remove_member(alice, conversation.id, alice), ValidationException);
}

TEST_F(ConversationServiceTest, OwnerLeavingTransfersOwnership) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");
    const auto conversation = service_.create(alice, group("Team", {bob}));

    service_.leave(alice, conversation.id);
    const auto reloaded = service_.get(bob, conversation.id);
    EXPECT_EQ(reloaded.owner_id, bob);
}

TEST_F(ConversationServiceTest, LastMemberLeavingDeletesGroup) {
    const auto alice = make_user("alice");
    const auto conversation = service_.create(alice, group("Solo", {}));
    service_.leave(alice, conversation.id);
    EXPECT_THROW(service_.get(alice, conversation.id), NotFoundException);
}

TEST_F(ConversationServiceTest, NonOwnerCannotDeleteGroup) {
    const auto alice = make_user("alice");
    const auto bob = make_user("bob");
    const auto conversation = service_.create(alice, group("Team", {bob}));
    EXPECT_THROW(service_.remove(bob, conversation.id), AuthorizationException);
}

}  // namespace
