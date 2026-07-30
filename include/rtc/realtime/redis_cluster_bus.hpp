#pragma once

#include <memory>
#include <string>

#include "rtc/realtime/cluster_bus.hpp"

namespace rtc::realtime {

// Redis Pub/Sub implementation of IClusterBus.
//
// Uses the PIMPL idiom for exactly the reason RedisCacheStore does: this header
// must not drag redis-plus-plus into every translation unit that includes the
// connection manager. The concrete client lives entirely in the .cpp, compiled
// only when the build is configured with -DRTC_WITH_REDIS=ON. Built without it,
// the constructor throws and the composition root falls back to NullClusterBus —
// so a single-instance deployment needs no Redis at all.
//
// Threading: one dedicated subscriber thread runs the Redis subscribe loop and
// invokes handlers. Publishing uses a separate connection, because a Redis
// connection in subscribe mode cannot issue other commands. The subscriber loop
// reconnects with backoff on failure, so a Redis restart degrades delivery
// temporarily instead of permanently.
class RedisClusterBus final : public IClusterBus {
public:
    RedisClusterBus(const std::string& uri, std::string node_id);
    ~RedisClusterBus() override;

    RedisClusterBus(const RedisClusterBus&) = delete;
    RedisClusterBus& operator=(const RedisClusterBus&) = delete;

    void publish(std::string_view channel, const nlohmann::json& body) noexcept override;
    void subscribe(std::string_view channel, Handler handler) override;
    void start() override;
    void stop() override;

    [[nodiscard]] std::string_view node_id() const noexcept override;
    [[nodiscard]] bool is_distributed() const noexcept override { return true; }
    [[nodiscard]] std::uint64_t published_count() const noexcept override;
    [[nodiscard]] std::uint64_t received_count() const noexcept override;

    // True when the binary was compiled with Redis support.
    [[nodiscard]] static bool available() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rtc::realtime
