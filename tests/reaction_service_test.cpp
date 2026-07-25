#include "rtc/services/reaction_service.hpp"

#include <gtest/gtest.h>

#include "rtc/errors/exceptions.hpp"
#include "rtc/notifications/notification_dispatcher.hpp"
#include "rtc/repositories/message_repository.hpp"
#include "support/fake_conversation_repository.hpp"
#include "support/fake_message_repository.hpp"
#include "support/fake_reaction_repository.hpp"
#include "support/recording_broadcaster.hpp"

namespace {

using rtc::errors::NotFoundException;
using rtc::errors::ValidationException;

class ReactionServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        conversation_id_ = conversations_.create_or_get_direct(alice_, bob_).id;
        rtc::repositories::NewMessage m;
        m.conversation_id = conversation_id_;
        m.sender_id = alice_;
        m.content = "hello";
        message_id_ = messages_.create(m).id;
    }

    std::int64_t alice_ = 1;
    std::int64_t bob_ = 2;
    std::int64_t carol_ = 3;
    std::int64_t conversation_id_ = 0;
    std::int64_t message_id_ = 0;

    rtc::testing::FakeReactionRepository reactions_;
    rtc::testing::FakeMessageRepository messages_;
    rtc::testing::FakeConversationRepository conversations_;
    rtc::testing::RecordingBroadcaster broadcaster_;
    rtc::notifications::NullNotificationDispatcher notifications_;
    rtc::services::ReactionService service_{reactions_, messages_, conversations_, broadcaster_,
                                            notifications_};
};

TEST_F(ReactionServiceTest, ReactPersistsAndBroadcasts) {
    const auto reaction = service_.react(bob_, message_id_, "👍");
    EXPECT_EQ(reaction.emoji, "👍");
    ASSERT_EQ(broadcaster_.count(), 1U);
    EXPECT_EQ(broadcaster_.last().type, "reaction.added");
}

TEST_F(ReactionServiceTest, RejectsDisallowedEmoji) {
    EXPECT_THROW(service_.react(bob_, message_id_, "🦄"), ValidationException);
}

TEST_F(ReactionServiceTest, NonParticipantCannotReact) {
    EXPECT_THROW(service_.react(carol_, message_id_, "❤️"), NotFoundException);
}

TEST_F(ReactionServiceTest, ChangingReactionReplacesEmoji) {
    service_.react(bob_, message_id_, "👍");
    service_.react(bob_, message_id_, "🔥");
    const auto reactions = service_.list(alice_, message_id_);
    ASSERT_EQ(reactions.size(), 1U);
    EXPECT_EQ(reactions.front().emoji, "🔥");
}

TEST_F(ReactionServiceTest, UnreactRemovesAndBroadcasts) {
    service_.react(bob_, message_id_, "👍");
    broadcaster_.clear();
    service_.unreact(bob_, message_id_);
    EXPECT_TRUE(service_.list(alice_, message_id_).empty());
    ASSERT_EQ(broadcaster_.count(), 1U);
    EXPECT_EQ(broadcaster_.last().type, "reaction.removed");
}

}  // namespace
