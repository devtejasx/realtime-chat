#include "rtc/services/message_service.hpp"

#include <gtest/gtest.h>

#include "rtc/dto/message_dto.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/errors/exceptions.hpp"
#include "support/fake_conversation_repository.hpp"
#include "support/fake_message_repository.hpp"
#include "support/recording_broadcaster.hpp"

namespace {

using rtc::dto::MessageQuery;
using rtc::dto::Pagination;
using rtc::dto::SendMessageRequest;
using rtc::dto::UpdateMessageRequest;
using rtc::errors::AuthorizationException;
using rtc::errors::NotFoundException;
using rtc::errors::ValidationException;

class MessageServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Two users in a direct conversation.
        conversation_id_ = conversations_.create_or_get_direct(alice_, bob_).id;
    }

    SendMessageRequest send_req(const std::string& content) {
        SendMessageRequest r;
        r.conversation_id = conversation_id_;
        r.content = content;
        return r;
    }

    std::int64_t alice_ = 1;
    std::int64_t bob_ = 2;
    std::int64_t carol_ = 3;
    std::int64_t conversation_id_ = 0;

    rtc::testing::FakeMessageRepository messages_;
    rtc::testing::FakeConversationRepository conversations_;
    rtc::testing::RecordingBroadcaster broadcaster_;
    rtc::services::MessageService service_{messages_, conversations_, broadcaster_};
};

TEST_F(MessageServiceTest, SendPersistsThenBroadcasts) {
    const auto message = service_.send(alice_, send_req("hello world"));
    EXPECT_GT(message.id, 0);
    EXPECT_EQ(messages_.count(), 1U);
    ASSERT_EQ(broadcaster_.count(), 1U);
    EXPECT_EQ(broadcaster_.last().type, "message.created");
    // Broadcast targets both participants.
    EXPECT_EQ(broadcaster_.last().user_ids.size(), 2U);
}

TEST_F(MessageServiceTest, SendTrimsAndRejectsEmptyContent) {
    EXPECT_THROW(service_.send(alice_, send_req("   ")), ValidationException);
}

TEST_F(MessageServiceTest, NonParticipantCannotSend) {
    EXPECT_THROW(service_.send(carol_, send_req("hi")), NotFoundException);
}

TEST_F(MessageServiceTest, OnlyAuthorCanEdit) {
    const auto message = service_.send(alice_, send_req("original"));
    UpdateMessageRequest edit{"edited"};
    EXPECT_THROW(service_.edit(bob_, message.id, edit), AuthorizationException);

    const auto updated = service_.edit(alice_, message.id, edit);
    EXPECT_EQ(updated.content, "edited");
    EXPECT_TRUE(updated.is_edited());
}

TEST_F(MessageServiceTest, DeleteSoftDeletesAndBroadcasts) {
    const auto message = service_.send(alice_, send_req("to delete"));
    broadcaster_.clear();

    const auto deleted = service_.remove(alice_, message.id);
    EXPECT_TRUE(deleted.is_deleted());
    ASSERT_EQ(broadcaster_.count(), 1U);
    EXPECT_EQ(broadcaster_.last().type, "message.deleted");
}

TEST_F(MessageServiceTest, ListReturnsNewestFirst) {
    service_.send(alice_, send_req("first"));
    service_.send(bob_, send_req("second"));
    service_.send(alice_, send_req("third"));

    MessageQuery query;
    query.conversation_id = conversation_id_;
    const auto page = Pagination{};
    const auto results = service_.list(alice_, query, page);
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results.front().content, "third");  // newest first
}

TEST_F(MessageServiceTest, ListFiltersBySenderAndKeyword) {
    service_.send(alice_, send_req("apple pie"));
    service_.send(bob_, send_req("banana bread"));
    service_.send(alice_, send_req("apple tart"));

    MessageQuery by_sender;
    by_sender.conversation_id = conversation_id_;
    by_sender.sender_id = bob_;
    EXPECT_EQ(service_.list(alice_, by_sender, Pagination{}).size(), 1U);

    MessageQuery by_keyword;
    by_keyword.conversation_id = conversation_id_;
    by_keyword.keyword = "apple";
    EXPECT_EQ(service_.list(alice_, by_keyword, Pagination{}).size(), 2U);
}

}  // namespace
