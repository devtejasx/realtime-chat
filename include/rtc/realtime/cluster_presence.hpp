#pragma once

#include "rtc/realtime/cluster_bus.hpp"
#include "rtc/services/presence_publisher.hpp"
#include "rtc/services/presence_service.hpp"

namespace rtc::realtime {

// Carries presence deltas over the cluster bus.
//
// Mirrors ClusterInvalidationPublisher: rtc::realtime depends on the service
// layer's IPresencePublisher, never the reverse, so PresenceService remains
// constructible and testable with no bus at all.
class ClusterPresencePublisher final : public services::IPresencePublisher {
  public:
    explicit ClusterPresencePublisher(IClusterBus& bus) noexcept : bus_(bus) {}

    void publish_presence(const services::PresenceDelta& delta) noexcept override {
        bus_.publish(cluster_channels::kPresence, delta.to_json());
    }

  private:
    IClusterBus& bus_;
};

// Registers the receiving half: another instance's delta updates this
// instance's view of that node.
//
// Note what this deliberately does *not* do — emit a client-facing presence
// frame. The instance where the transition happened already published one
// through ConnectionManager::publish, which is itself cluster-wide, so peers
// everywhere have been told. Announcing again here would deliver one frame per
// replica for a single transition.
//
// Runs on the bus's subscriber thread; PresenceService is mutex-guarded, and
// apply_remote does not re-publish.
inline void subscribe_to_presence(IClusterBus& bus, services::PresenceService& presence) {
    bus.subscribe(cluster_channels::kPresence,
                  [&presence](std::string_view origin_node, const nlohmann::json& body) {
                      presence.apply_remote(origin_node, services::PresenceDelta::from_json(body));
                  });
}

}  // namespace rtc::realtime
