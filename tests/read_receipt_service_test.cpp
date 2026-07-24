#include "rtc/services/read_receipt_service.hpp"

#include <gtest/gtest.h>

#include "rtc/errors/exceptions.hpp"
#include "rtc/repositories/message_repository.hpp"
#include "support/fake_conversation_repository.hpp"
#include "support/fake_message_repository.hpp"
#include "support/fake_read_receipt_repository.hpp"
#include "support/recording_broadcaster.hpp"

namespace {

using rtc::errors::NotFoundException;

class ReadReceiptServiceTest : public ::testing::Test {
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

    rtc::testing::FakeReadReceiptRepository receipts_;
    rtc::testing::FakeConversationRepository conversations_;
    rtc::testing::FakeMessageRepository messages_;
    rtc::testing::RecordingBroadcaster broadcaster_;
    rtc::services::ReadReceiptService service_{receipts_, conversations_, messages_, broadcaster_};
};

TEST_F(ReadReceiptServiceTest, MarkDeliveredPersistsAndBroadcasts) {
    service_.mark_delivered(bob_, message_id_);
    const auto stored = receipts_.list_for_message(message_id_);
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_EQ(stored.front().state, rtc::models::ReceiptState::kDelivered);
    ASSERT_EQ(broadcaster_.count(), 1U);
    EXPECT_EQ(broadcaster_.last().type, "receipt.update");
}

TEST_F(ReadReceiptServiceTest, MarkDeliveredRejectsNonParticipant) {
    EXPECT_THROW(service_.mark_delivered(carol_, message_id_), NotFoundException);
}

TEST_F(ReadReceiptServiceTest, MarkReadUpdatesMarkerAndBroadcasts) {
    service_.mark_read(bob_, conversation_id_, message_id_);
    EXPECT_EQ(receipts_.last_mark_user(), bob_);
    EXPECT_EQ(receipts_.last_mark_up_to(), message_id_);
    ASSERT_EQ(broadcaster_.count(), 1U);
    EXPECT_EQ(broadcaster_.last().type, "read.update");
}

TEST_F(ReadReceiptServiceTest, MarkReadRejectsNonParticipant) {
    EXPECT_THROW(service_.mark_read(carol_, conversation_id_, message_id_), NotFoundException);
}

TEST_F(ReadReceiptServiceTest, ReceiptStateNeverRegresses) {
    service_.mark_read(bob_, conversation_id_, message_id_);
    (void) receipts_.upsert_state(message_id_, bob_, rtc::models::ReceiptState::kRead);
    // A later "delivered" must not downgrade a "read".
    const auto receipt =
        receipts_.upsert_state(message_id_, bob_, rtc::models::ReceiptState::kDelivered);
    EXPECT_EQ(receipt.state, rtc::models::ReceiptState::kRead);
}

}  // namespace
