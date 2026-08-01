#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rtc/services/presence_publisher.hpp"
#include "rtc/services/presence_service.hpp"

// Cross-instance presence.
//
// The bug these tests pin down is not a missing feature but a wrong answer.
// Presence used to be counted from this process's sessions alone, so with a user
// connected to two replicas:
//
//   - the second replica reported their first connection as a fresh "online",
//     duplicating an announcement the first replica had already made, and
//   - when that tab closed, the second replica reported "offline" while the user
//     was still connected to the first.
//
// The false "offline" is the damaging one: peers mark someone away while they
// are sitting in the conversation, reading messages.
//
// PresenceService is exercised directly rather than through Redis. The transport
// is not what was broken — ConnectionManager::publish was already cluster-wide —
// and a test that needed a live Redis could not run in CI. What is asserted here
// is the state machine that decides whether a transition is globally true.

namespace {

using rtc::services::NullPresencePublisher;
using rtc::services::PresenceDelta;
using rtc::services::PresenceService;

// Captures what this instance would have put on the bus.
class RecordingPresencePublisher final : public rtc::services::IPresencePublisher {
  public:
    void publish_presence(const PresenceDelta& delta) noexcept override { deltas.push_back(delta); }
    std::vector<PresenceDelta> deltas;
};

// Two PresenceService instances wired to each other, standing in for two
// replicas. Delivery is manual so tests control ordering, and each side's
// publisher feeds the other's apply_remote — the same path the cluster
// subscriber takes.
class ClusterFixture : public ::testing::Test {
  protected:
    void flush_a_to_b() {
        for (const auto& delta : pub_a_.deltas) {
            b_.apply_remote("node-a", delta);
        }
        pub_a_.deltas.clear();
    }
    void flush_b_to_a() {
        for (const auto& delta : pub_b_.deltas) {
            a_.apply_remote("node-b", delta);
        }
        pub_b_.deltas.clear();
    }

    void SetUp() override {
        a_.set_publisher(pub_a_);
        b_.set_publisher(pub_b_);
    }

    PresenceService a_;
    PresenceService b_;
    RecordingPresencePublisher pub_a_;
    RecordingPresencePublisher pub_b_;
};

// --- the regression --------------------------------------------------------

TEST_F(ClusterFixture, DisconnectOnOneInstanceDoesNotReportOfflineWhileAnotherHoldsTheUser) {
    ASSERT_TRUE(a_.on_connect(7));  // first session anywhere
    flush_a_to_b();

    // The same user opens a second device, load-balanced to the other replica.
    // B must not treat this as a fresh "online" — A already announced it.
    EXPECT_FALSE(b_.on_connect(7));
    flush_b_to_a();

    // The tab on B closes. B still has no sessions of its own, but A does.
    EXPECT_FALSE(b_.on_disconnect(7))
        << "reported a global offline while the user was still connected to another instance";
    EXPECT_TRUE(b_.is_online(7)) << "B lost sight of the session A is holding";
    flush_b_to_a();

    // Only when the last session anywhere closes is the user offline.
    EXPECT_TRUE(a_.on_disconnect(7));
    EXPECT_FALSE(a_.is_online(7));
}

TEST_F(ClusterFixture, ConnectOnASecondInstanceDoesNotAnnounceOnlineTwice) {
    ASSERT_TRUE(a_.on_connect(7));
    flush_a_to_b();
    EXPECT_FALSE(b_.on_connect(7)) << "duplicate online announcement across replicas";
}

// --- visibility ------------------------------------------------------------

TEST_F(ClusterFixture, EachInstanceSeesUsersConnectedToTheOther) {
    ASSERT_TRUE(a_.on_connect(1));
    ASSERT_TRUE(b_.on_connect(2));
    flush_a_to_b();
    flush_b_to_a();

    EXPECT_TRUE(a_.is_online(1));
    EXPECT_TRUE(a_.is_online(2)) << "A cannot see a user connected to B";
    EXPECT_TRUE(b_.is_online(1)) << "B cannot see a user connected to A";
    EXPECT_TRUE(b_.is_online(2));
}

TEST_F(ClusterFixture, LocalAndGlobalOnlinenessAreDistinguishable) {
    ASSERT_TRUE(a_.on_connect(1));
    flush_a_to_b();

    EXPECT_TRUE(b_.is_online(1)) << "globally online";
    EXPECT_FALSE(b_.is_online_locally(1)) << "but the socket is not on B";
    EXPECT_TRUE(a_.is_online_locally(1));
}

TEST_F(ClusterFixture, OnlineCountCountsDistinctUsersNotSessions) {
    ASSERT_TRUE(a_.on_connect(7));
    flush_a_to_b();
    EXPECT_FALSE(b_.on_connect(7));  // same user, second replica
    flush_b_to_a();

    EXPECT_EQ(a_.online_count(), 1U) << "one user connected twice is one online user";
    EXPECT_EQ(b_.online_count(), 1U);
    EXPECT_EQ(a_.local_online_count(), 1U);
    EXPECT_EQ(b_.local_online_count(), 1U);
}

TEST_F(ClusterFixture, LastSeenIsStampedOnlyWhenTheUserLeavesEveryInstance) {
    ASSERT_TRUE(a_.on_connect(7));
    flush_a_to_b();
    ASSERT_FALSE(b_.on_connect(7));
    flush_b_to_a();

    ASSERT_FALSE(b_.on_disconnect(7));
    flush_b_to_a();
    EXPECT_FALSE(a_.last_seen(7).has_value())
        << "last_seen stamped while the user still had a live session";

    ASSERT_TRUE(a_.on_disconnect(7));
    EXPECT_TRUE(a_.last_seen(7).has_value());
}

// --- propagation and failure handling --------------------------------------

TEST_F(ClusterFixture, LocalTransitionsArePublishedEvenWhenGloballyRedundant) {
    // B's connect is not a global transition, but peers still need to know B is
    // holding a session — otherwise A would think the user vanished when its own
    // session closes.
    ASSERT_TRUE(a_.on_connect(7));
    flush_a_to_b();
    ASSERT_FALSE(b_.on_connect(7));
    ASSERT_EQ(pub_b_.deltas.size(), 1U) << "B stayed silent about holding a session";
    EXPECT_TRUE(pub_b_.deltas.front().online);
    EXPECT_EQ(pub_b_.deltas.front().user_id, 7);
}

TEST_F(ClusterFixture, AdditionalSessionsOnTheSameInstanceAreNotRepublished) {
    ASSERT_TRUE(a_.on_connect(7));
    pub_a_.deltas.clear();
    EXPECT_FALSE(a_.on_connect(7));  // second tab, same replica
    EXPECT_TRUE(pub_a_.deltas.empty()) << "one delta per node transition, not per session";
}

TEST_F(ClusterFixture, ForgettingADepartedNodeClearsTheUsersItHeld) {
    ASSERT_TRUE(a_.on_connect(1));
    flush_a_to_b();
    ASSERT_TRUE(b_.is_online(1));
    EXPECT_EQ(b_.known_peer_count(), 1U);

    // A crashes without announcing anything. Its users would otherwise read as
    // online forever.
    b_.forget_node("node-a");
    EXPECT_FALSE(b_.is_online(1));
    EXPECT_EQ(b_.known_peer_count(), 0U);
}

TEST_F(ClusterFixture, RemoteOfflineForAnUnknownNodeIsIgnored) {
    // Arrives after forget_node, or from a node this instance never heard from.
    // Must not throw or resurrect state.
    b_.apply_remote("node-ghost", PresenceDelta{.user_id = 99, .online = false, .at_ms = 0});
    EXPECT_FALSE(b_.is_online(99));
    EXPECT_EQ(b_.known_peer_count(), 0U);
}

TEST_F(ClusterFixture, MalformedDeltaJsonDegradesToADefaultedValue) {
    // A peer on a newer build may publish fields this one does not know. Parsing
    // must not throw on the subscriber thread, where an exception would take the
    // bus down.
    const auto delta = PresenceDelta::from_json(nlohmann::json{{"unexpected", true}});
    EXPECT_EQ(delta.user_id, 0);
    EXPECT_FALSE(delta.online);
}

TEST_F(ClusterFixture, DeltaSurvivesAJsonRoundTrip) {
    const PresenceDelta sent{.user_id = 42, .online = true, .at_ms = 1234567890};
    const auto received = PresenceDelta::from_json(sent.to_json());
    EXPECT_EQ(received.user_id, sent.user_id);
    EXPECT_EQ(received.online, sent.online);
    EXPECT_EQ(received.at_ms, sent.at_ms);
}

// --- backward compatibility ------------------------------------------------

TEST(PresenceSingleInstance, BehaviourIsUnchangedWithoutAPublisher) {
    // The single-replica path must be exactly what it was: no publisher wired,
    // no remote nodes, transitions decided by local sessions alone.
    PresenceService presence;
    EXPECT_TRUE(presence.on_connect(1));
    EXPECT_FALSE(presence.on_connect(1));
    EXPECT_FALSE(presence.on_disconnect(1));
    EXPECT_TRUE(presence.on_disconnect(1));
    EXPECT_FALSE(presence.is_online(1));
    EXPECT_EQ(presence.known_peer_count(), 0U);
    EXPECT_TRUE(presence.last_seen(1).has_value());
}

TEST(PresenceSingleInstance, NullPublisherIsSafeToPublishThrough) {
    PresenceService presence;
    presence.set_publisher(NullPresencePublisher::instance());
    EXPECT_TRUE(presence.on_connect(1));
    EXPECT_TRUE(presence.on_disconnect(1));
}

}  // namespace
