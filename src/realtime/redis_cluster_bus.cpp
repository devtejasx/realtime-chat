#include "rtc/realtime/redis_cluster_bus.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rtc/errors/exceptions.hpp"
#include "rtc/logging/logger.hpp"

#ifdef RTC_WITH_REDIS
#include <sw/redis++/redis++.h>
#endif

namespace rtc::realtime {
namespace {

// Key carrying the publishing instance's identity. Present on every message; a
// receiver drops messages bearing its own id, which is what prevents an instance
// delivering its own broadcast a second time.
constexpr const char* kOriginKey = "_node";

}  // namespace

#ifdef RTC_WITH_REDIS

struct RedisClusterBus::Impl {
    Impl(const std::string& uri, std::string id)
        : node_id(std::move(id)), publisher(uri), connection_uri(uri) {}

    // --- subscriber loop -------------------------------------------------
    //
    // Runs on its own thread. A Redis connection in subscribe mode cannot issue
    // ordinary commands, which is why publishing uses a separate connection.
    void run() {
        // Exponential backoff, capped: a Redis outage must not become a hot loop,
        // and must recover on its own once Redis returns.
        std::chrono::milliseconds backoff{200};
        constexpr std::chrono::milliseconds kMaxBackoff{5000};

        while (running.load(std::memory_order_acquire)) {
            try {
                sw::redis::Redis connection(connection_uri);
                auto subscriber = connection.subscriber();

                subscriber.on_message(
                    [this](std::string channel, std::string message) {
                        deliver(channel, message);
                    });

                {
                    std::lock_guard<std::mutex> lock(mutex);
                    for (const auto& [channel, _] : handlers) {
                        subscriber.subscribe(channel);
                    }
                }

                RTC_LOG_INFO("Cluster bus subscribed on {} channel(s) as node '{}'",
                             handlers.size(), node_id);
                backoff = std::chrono::milliseconds{200};  // connected: reset

                while (running.load(std::memory_order_acquire)) {
                    // consume() blocks until a message arrives or the connection
                    // errors; a timeout surfaces as an exception we treat as a
                    // normal wakeup so shutdown stays responsive.
                    try {
                        subscriber.consume();
                    } catch (const sw::redis::TimeoutError&) {
                        continue;
                    }
                }
            } catch (const std::exception& ex) {
                if (!running.load(std::memory_order_acquire)) {
                    break;
                }
                RTC_LOG_WARN("Cluster bus subscriber error ({}); reconnecting in {} ms", ex.what(),
                             backoff.count());
                std::this_thread::sleep_for(backoff);
                backoff = std::min(backoff * 2, kMaxBackoff);
            }
        }
        RTC_LOG_INFO("Cluster bus subscriber stopped");
    }

    void deliver(const std::string& channel, const std::string& message) {
        const auto body = nlohmann::json::parse(message, nullptr, /*allow_exceptions=*/false);
        if (body.is_discarded() || !body.is_object()) {
            RTC_LOG_WARN("Cluster bus dropped malformed message on '{}'", channel);
            return;
        }

        const auto origin_it = body.find(kOriginKey);
        const std::string origin =
            origin_it != body.end() && origin_it->is_string() ? origin_it->get<std::string>() : "";
        if (origin == node_id) {
            return;  // our own broadcast; already delivered locally
        }

        IClusterBus::Handler handler;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto it = handlers.find(channel);
            if (it == handlers.end()) {
                return;
            }
            handler = it->second;  // copy so the callback runs without the lock held
        }

        received.fetch_add(1, std::memory_order_relaxed);
        try {
            handler(origin, body);
        } catch (const std::exception& ex) {
            RTC_LOG_ERROR("Cluster bus handler for '{}' threw: {}", channel, ex.what());
        }
    }

    std::string node_id;
    sw::redis::Redis publisher;
    std::string connection_uri;

    std::mutex mutex;
    std::unordered_map<std::string, IClusterBus::Handler> handlers;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<std::uint64_t> published{0};
    std::atomic<std::uint64_t> received{0};
};

RedisClusterBus::RedisClusterBus(const std::string& uri, std::string node_id)
    : impl_(std::make_unique<Impl>(uri, std::move(node_id))) {}

RedisClusterBus::~RedisClusterBus() { stop(); }

bool RedisClusterBus::available() noexcept { return true; }

void RedisClusterBus::publish(std::string_view channel, const nlohmann::json& body) noexcept {
    try {
        nlohmann::json stamped = body;
        stamped[kOriginKey] = impl_->node_id;
        impl_->publisher.publish(std::string(channel), stamped.dump());
        impl_->published.fetch_add(1, std::memory_order_relaxed);
    } catch (const std::exception& ex) {
        // Degrade to local-only delivery. Recipients on other instances miss this
        // frame; they will re-read history on reconnect, which is why at-most-once
        // is acceptable here.
        RTC_LOG_WARN("Cluster bus publish to '{}' failed: {}", channel, ex.what());
    } catch (...) {
        RTC_LOG_WARN("Cluster bus publish to '{}' failed", channel);
    }
}

void RedisClusterBus::subscribe(std::string_view channel, Handler handler) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->handlers[std::string(channel)] = std::move(handler);
}

void RedisClusterBus::start() {
    if (impl_->running.exchange(true)) {
        return;
    }
    impl_->worker = std::thread([this] { impl_->run(); });
}

void RedisClusterBus::stop() {
    if (!impl_->running.exchange(false)) {
        return;
    }
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
}

std::string_view RedisClusterBus::node_id() const noexcept { return impl_->node_id; }

std::uint64_t RedisClusterBus::published_count() const noexcept {
    return impl_->published.load(std::memory_order_relaxed);
}

std::uint64_t RedisClusterBus::received_count() const noexcept {
    return impl_->received.load(std::memory_order_relaxed);
}

#else  // RTC_WITH_REDIS not defined — compiled but non-functional.

struct RedisClusterBus::Impl {
    std::string node_id;
};

namespace {
[[noreturn]] void unavailable() {
    throw rtc::errors::ConfigException(
        "Redis support was not compiled in; rebuild with -DRTC_WITH_REDIS=ON to enable "
        "multi-instance WebSocket fan-out");
}
}  // namespace

RedisClusterBus::RedisClusterBus(const std::string&, std::string) { unavailable(); }
RedisClusterBus::~RedisClusterBus() = default;
bool RedisClusterBus::available() noexcept { return false; }

void RedisClusterBus::publish(std::string_view, const nlohmann::json&) noexcept {}
void RedisClusterBus::subscribe(std::string_view, Handler) {}
void RedisClusterBus::start() {}
void RedisClusterBus::stop() {}
std::string_view RedisClusterBus::node_id() const noexcept { return {}; }
std::uint64_t RedisClusterBus::published_count() const noexcept { return 0; }
std::uint64_t RedisClusterBus::received_count() const noexcept { return 0; }

#endif  // RTC_WITH_REDIS

}  // namespace rtc::realtime
