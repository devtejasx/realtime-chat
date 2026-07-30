#pragma once

#include <cstdint>
#include <vector>

#include "rtc/cache/cache_store.hpp"

namespace rtc::cache {

// Cross-instance online-user tracking backed by the shared cache.
//
// In a multi-instance deployment each node knows only about its own sockets;
// this cache is the shared source of truth for "who is online" globally. It
// reference-counts a user's connections across the whole fleet so a user is
// reported offline only when their last connection (on any node) closes.
class PresenceCache {
  public:
    explicit PresenceCache(ICacheStore& store) noexcept : store_(store) {}

    // Registers a connection for the user; returns true if this was their first
    // connection fleet-wide (offline -> online).
    [[nodiscard]] bool add_connection(std::int64_t user_id);

    // Deregisters a connection; returns true if it was their last (online ->
    // offline). Records last-seen implicitly via the offline transition.
    [[nodiscard]] bool remove_connection(std::int64_t user_id);

    [[nodiscard]] bool is_online(std::int64_t user_id);
    [[nodiscard]] std::vector<std::int64_t> online_user_ids();
    [[nodiscard]] std::size_t online_count();

  private:
    ICacheStore& store_;
};

}  // namespace rtc::cache
