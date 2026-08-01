#pragma once

#include <functional>

#include "rtc/cache/invalidation.hpp"
#include "rtc/realtime/cluster_bus.hpp"

namespace rtc::realtime {

// Carries cache-invalidation notices over the cluster bus.
//
// This is the single point where the caching layer and the messaging layer meet,
// and the dependency deliberately points this way: rtc::realtime knows about
// rtc::cache::IInvalidationPublisher, never the reverse. The cache keeps working
// — and keeps being testable — with no Redis, no WebSockets and no bus at all.
//
// Both halves live here because they are two ends of one wire:
//
//   ClusterInvalidationPublisher   local eviction -> the bus
//   subscribe_to_invalidations()   the bus        -> local eviction
//
// Splitting them across files would let one be changed without the other, and
// the wire format is the thing that has to agree.
class ClusterInvalidationPublisher final : public cache::IInvalidationPublisher {
  public:
    explicit ClusterInvalidationPublisher(IClusterBus& bus) noexcept : bus_(bus) {}

    void publish_invalidation(const cache::InvalidationEvent& event) noexcept override {
        // IClusterBus::publish is itself noexcept and swallows transport
        // failures: a cache notice that cannot be sent degrades to TTL-bounded
        // staleness, which is strictly better than failing the write that
        // triggered it.
        bus_.publish(cluster_channels::kCacheInvalidate, event.to_json());
    }

  private:
    IClusterBus& bus_;
};

// Registers the receiving half.
//
// `apply` is invoked on the bus's subscriber thread for every *remote*
// invalidation — the bus drops self-originated messages before handlers run, so
// this never sees an echo of its own publish. Implementations must therefore be
// thread-safe, and must call the *_local variants so that handling a notice does
// not rebroadcast it.
//
// Takes a callback rather than references to the caches themselves so that this
// header stays ignorant of which caches exist; the composition root decides what
// a scope means.
inline void subscribe_to_invalidations(IClusterBus& bus,
                                       std::function<void(const cache::InvalidationEvent&)> apply) {
    bus.subscribe(
        cluster_channels::kCacheInvalidate,
        [handler = std::move(apply)](std::string_view /*origin_node*/, const nlohmann::json& body) {
            handler(cache::InvalidationEvent::from_json(body));
        });
}

}  // namespace rtc::realtime
