#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "rtc/cache/invalidation.hpp"
#include "rtc/realtime/cluster_bus.hpp"
#include "rtc/realtime/cluster_invalidation.hpp"
#include "rtc/realtime/cluster_presence.hpp"
#include "rtc/realtime/connection_manager.hpp"
#include "rtc/services/presence_service.hpp"
#include "support/fake_cluster_bus.hpp"

// Cross-instance delivery: the contract between replicas.
//
// Scope, stated plainly. These tests assert what one instance puts on the bus
// and what another does with it. They do not assert that bytes reach a browser,
// because that requires a live crow::websocket::connection, and a fake pointer
// would be dereferenced on send. The socket write is the one link already
// covered by the single-instance suite; what was untested — and what horizontal
// scaling actually depends on — is everything between two processes.
//
// Delivery uses an in-process fabric rather than a real Redis so the suite has
// no broker dependency. Loop suppression is reproduced rather than stubbed, so
// "no duplicate delivery" is proven rather than assumed.

namespace {

using nlohmann::json;
using rtc::realtime::ConnectionManager;
using rtc::realtime::cluster_channels::kCacheInvalidate;
using rtc::realtime::cluster_channels::kPresence;
using rtc::realtime::cluster_channels::kRoomBroadcast;
using rtc::realtime::cluster_channels::kUserBroadcast;
using rtc::testing::ClusterFabric;
using rtc::testing::FakeClusterBus;

// Two replicas sharing one bus fabric.
class TwoInstances : public ::testing::Test {
  protected:
    void SetUp() override {
        fabric_ = std::make_shared<ClusterFabric>();
        bus_a_ = std::make_unique<FakeClusterBus>(fabric_, "node-a");
        bus_b_ = std::make_unique<FakeClusterBus>(fabric_, "node-b");
        a_.set_cluster_bus(*bus_a_);
        b_.set_cluster_bus(*bus_b_);
    }

    // The single message A published on `channel`, or a failure.
    [[nodiscard]] json only_published_on(const FakeClusterBus& bus,
                                         std::string_view channel) const {
        std::vector<json> matches;
        for (const auto& entry : bus.published()) {
            if (entry.channel == channel) {
                matches.push_back(entry.body);
            }
        }
        EXPECT_EQ(matches.size(), 1U) << "expected exactly one publish on " << channel;
        return matches.empty() ? json::object() : matches.front();
    }

    std::shared_ptr<ClusterFabric> fabric_;
    std::unique_ptr<FakeClusterBus> bus_a_;
    std::unique_ptr<FakeClusterBus> bus_b_;
    ConnectionManager a_;
    ConnectionManager b_;
};

// --- messages -------------------------------------------------------------

TEST_F(TwoInstances, UserPublishCrossesToTheOtherInstance) {
    a_.publish({7, 9}, "message.new", json{{"id", 1}, {"content", "hi"}});

    const auto body = only_published_on(*bus_a_, kUserBroadcast);
    EXPECT_EQ(body.at("event"), "message.new");
    EXPECT_EQ(body.at("user_ids"), (std::vector<std::int64_t>{7, 9}));
    EXPECT_EQ(body.at("data").at("content"), "hi");

    // B received it and decoded it without complaint.
    ASSERT_EQ(bus_b_->received().size(), 1U) << "the peer never saw the message";
    EXPECT_EQ(bus_b_->received().front().origin, "node-a");
    EXPECT_EQ(bus_b_->received().front().body.at("event"), "message.new");
}

TEST_F(TwoInstances, RoomBroadcastCrossesToTheOtherInstance) {
    a_.broadcast_to_room(42, "typing.start", json{{"user_id", 7}});

    const auto body = only_published_on(*bus_a_, kRoomBroadcast);
    EXPECT_EQ(body.at("event"), "typing.start");
    EXPECT_EQ(body.at("conversation_id"), 42);

    ASSERT_EQ(bus_b_->received().size(), 1U);
    EXPECT_EQ(bus_b_->received().front().body.at("conversation_id"), 42);
}

// --- no duplicate delivery ------------------------------------------------

TEST_F(TwoInstances, AnInstanceNeverReceivesItsOwnBroadcast) {
    // The property that keeps one send from becoming two deliveries: the sender
    // already delivered locally, so hearing its own publish would double up.
    a_.publish({7}, "message.new", json{{"id", 1}});
    EXPECT_TRUE(bus_a_->received().empty())
        << "instance received its own broadcast; every event would be delivered twice";
    EXPECT_EQ(bus_b_->received().size(), 1U);
}

TEST_F(TwoInstances, ReceivingAClusterMessageDoesNotRebroadcastIt) {
    // If the inbound handler used publish() instead of deliver_local(), the
    // message would circulate between instances without end.
    a_.publish({7}, "message.new", json{{"id", 1}});
    EXPECT_TRUE(bus_b_->published().empty())
        << "peer rebroadcast an inbound message; this loops forever across the cluster";
}

TEST_F(TwoInstances, LocalOnlyDeliveryIsNotForwarded) {
    a_.deliver_local({7}, "message.new", json{{"id", 1}});
    EXPECT_TRUE(bus_a_->published().empty())
        << "deliver_local must stay local; forwarding it is what creates the loop";
    EXPECT_TRUE(bus_b_->received().empty());
}

// --- malformed input ------------------------------------------------------

TEST_F(TwoInstances, MalformedClusterMessagesAreIgnored) {
    // A peer running a different build may publish a shape this one does not
    // understand. The handler runs on the bus thread, where an exception would
    // take down cross-instance delivery for everything.
    for (const auto& body : {json::object(),
                             json{{"event", 42}},                       // wrong type
                             json{{"event", "x"}},                      // no user_ids
                             json{{"user_ids", json::array({1})}},      // no event
                             json{{"event", "x"}, {"user_ids", 7}}}) {  // user_ids not an array
        EXPECT_NO_THROW(bus_a_->publish(kUserBroadcast, body));
    }
    for (const auto& body : {json{{"event", "x"}},  // no room
                             json{{"event", "x"}, {"conversation_id", "no"}}}) {
        EXPECT_NO_THROW(bus_a_->publish(kRoomBroadcast, body));
    }
}

// --- counters -------------------------------------------------------------

TEST_F(TwoInstances, CountersReflectTraffic) {
    // Surfaced on /metrics as rtc_cluster_published/received. A published count
    // that climbs while received stays at zero is how a misconfigured fan-out
    // announces itself.
    a_.publish({7}, "message.new", json{{"id", 1}});
    a_.broadcast_to_room(42, "typing.start", json{{"user_id", 7}});

    EXPECT_EQ(bus_a_->published_count(), 2U);
    EXPECT_EQ(bus_a_->received_count(), 0U);
    EXPECT_EQ(bus_b_->received_count(), 2U);
    EXPECT_EQ(bus_b_->published_count(), 0U);
}

// --- notifications --------------------------------------------------------

// Notifications reach other instances over the *user* channel, not a dedicated
// one: NotificationService delivers through IEventBroadcaster::publish, which is
// already cluster-wide. This test pins that down, because the property is easy
// to break by "optimising" a notification into a local-only send.
TEST_F(TwoInstances, NotificationDeliveryCrossesInstances) {
    const json payload{{"id", 5}, {"type", "message.new"}, {"read", false}};
    a_.publish({7}, "notification", payload);

    const auto body = only_published_on(*bus_a_, kUserBroadcast);
    EXPECT_EQ(body.at("event"), "notification");
    EXPECT_EQ(body.at("user_ids"), (std::vector<std::int64_t>{7}));
    EXPECT_EQ(body.at("data").at("id"), 5);
    ASSERT_EQ(bus_b_->received().size(), 1U)
        << "a notification created on one instance never reached the other";
}

// --- presence over the real bus -------------------------------------------

TEST_F(TwoInstances, PresenceDeltasReachTheOtherInstance) {
    rtc::services::PresenceService presence_a;
    rtc::services::PresenceService presence_b;
    rtc::realtime::ClusterPresencePublisher pub_a(*bus_a_);
    presence_a.set_publisher(pub_a);
    rtc::realtime::subscribe_to_presence(*bus_b_, presence_b);

    ASSERT_TRUE(presence_a.on_connect(7));

    EXPECT_TRUE(presence_b.is_online(7)) << "B cannot see a user connected to A";
    EXPECT_FALSE(presence_b.is_online_locally(7));
    EXPECT_EQ(presence_b.known_peer_count(), 1U);

    ASSERT_TRUE(presence_a.on_disconnect(7));
    EXPECT_FALSE(presence_b.is_online(7));
}

// --- cache invalidation over the real bus ---------------------------------

TEST_F(TwoInstances, CacheInvalidationReachesTheOtherInstance) {
    std::vector<rtc::cache::InvalidationEvent> applied;
    rtc::realtime::subscribe_to_invalidations(
        *bus_b_,
        [&applied](const rtc::cache::InvalidationEvent& event) { applied.push_back(event); });

    rtc::realtime::ClusterInvalidationPublisher publisher(*bus_a_);
    publisher.publish_invalidation(rtc::cache::InvalidationEvent{
        .scope = std::string(rtc::cache::invalidation_scopes::kAuthorization),
        .key = "42",
        .sub_key = {}});

    ASSERT_EQ(applied.size(), 1U) << "a ban applied on one instance did not evict on the other";
    EXPECT_EQ(applied.front().scope, "authorization");
    EXPECT_EQ(applied.front().key, "42");

    // And the publisher does not evict itself twice via the bus.
    EXPECT_TRUE(bus_a_->received().empty());
}

TEST_F(TwoInstances, NamespacedCacheInvalidationCarriesBothKeyParts) {
    std::vector<rtc::cache::InvalidationEvent> applied;
    rtc::realtime::subscribe_to_invalidations(
        *bus_b_,
        [&applied](const rtc::cache::InvalidationEvent& event) { applied.push_back(event); });

    rtc::realtime::ClusterInvalidationPublisher publisher(*bus_a_);
    publisher.publish_invalidation(rtc::cache::InvalidationEvent{
        .scope = std::string(rtc::cache::invalidation_scopes::kNamespacedKey),
        .key = "conversations",
        .sub_key = "42"});

    ASSERT_EQ(applied.size(), 1U);
    EXPECT_EQ(applied.front().key, "conversations");
    EXPECT_EQ(applied.front().sub_key, "42");
}

TEST_F(TwoInstances, InvalidationEventSurvivesAJsonRoundTrip) {
    const rtc::cache::InvalidationEvent sent{.scope = "cache", .key = "users", .sub_key = "7"};
    const auto received = rtc::cache::InvalidationEvent::from_json(sent.to_json());
    EXPECT_EQ(received.scope, sent.scope);
    EXPECT_EQ(received.key, sent.key);
    EXPECT_EQ(received.sub_key, sent.sub_key);
}

TEST_F(TwoInstances, InvalidationEventWithoutSubKeyOmitsItOnTheWire) {
    const rtc::cache::InvalidationEvent sent{.scope = "authorization", .key = "7", .sub_key = {}};
    EXPECT_FALSE(sent.to_json().contains("sub_key"));
}

// --- three instances ------------------------------------------------------

TEST(ThreeInstances, EveryPeerReceivesExactlyOnce) {
    // deploy/k8s runs replicas: 3. One publish must produce exactly one delivery
    // per peer — not one per peer per hop, and not none.
    auto fabric = std::make_shared<ClusterFabric>();
    FakeClusterBus bus_a(fabric, "node-a");
    FakeClusterBus bus_b(fabric, "node-b");
    FakeClusterBus bus_c(fabric, "node-c");

    ConnectionManager a;
    ConnectionManager b;
    ConnectionManager c;
    a.set_cluster_bus(bus_a);
    b.set_cluster_bus(bus_b);
    c.set_cluster_bus(bus_c);

    a.publish({7}, "message.new", json{{"id", 1}});

    EXPECT_EQ(bus_b.received().size(), 1U);
    EXPECT_EQ(bus_c.received().size(), 1U);
    EXPECT_TRUE(bus_a.received().empty());
    EXPECT_TRUE(bus_b.published().empty()) << "a peer rebroadcast; delivery would amplify";
    EXPECT_TRUE(bus_c.published().empty());
}

// --- single instance ------------------------------------------------------

TEST(SingleInstance, NoBusMeansNoClusterTraffic) {
    // The default path. Local delivery already reaches every connection, so the
    // manager must not require a bus to function.
    ConnectionManager solo;
    EXPECT_EQ(solo.cluster_bus(), nullptr);
    EXPECT_NO_THROW(solo.publish({7}, "message.new", json{{"id", 1}}));
    EXPECT_NO_THROW(solo.broadcast_to_room(42, "typing.start", json{{"user_id", 7}}));
}

}  // namespace
