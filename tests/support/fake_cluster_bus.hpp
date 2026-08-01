#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "rtc/realtime/cluster_bus.hpp"

namespace rtc::testing {

// An in-process stand-in for Redis Pub/Sub.
//
// Several instances of this class attach to one shared Fabric, which delivers
// each publish to every *other* attached bus — the same visibility a real Redis
// channel provides, minus the network. That is what makes cross-instance
// behaviour testable in CI: spinning up a real Redis (and two real servers) for
// a unit test would make the suite depend on a broker being present, and these
// assertions are about the delivery contract, not about Redis.
//
// Loop suppression is reproduced faithfully rather than assumed: a bus never
// hands a message back to its own subscribers, which is exactly how
// RedisClusterBus filters on node id. Tests can therefore prove the absence of
// duplicate delivery instead of taking it on trust.
//
// Delivery is synchronous and on the calling thread, so tests are deterministic.
// The real bus dispatches on its own subscriber thread; handlers must be
// thread-safe either way, and nothing here depends on the difference.
class ClusterFabric {
  public:
    // Registers a bus. Returns a token used to skip the sender on delivery.
    void attach(const std::string& node_id, realtime::IClusterBus* bus) {
        std::lock_guard<std::mutex> lock(mutex_);
        members_[node_id] = bus;
    }

    void detach(const std::string& node_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        members_.erase(node_id);
    }

    // Records a handler for a channel on one member.
    void subscribe(const std::string& node_id,
                   const std::string& channel,
                   realtime::IClusterBus::Handler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_[node_id][channel].push_back(std::move(handler));
    }

    // Delivers to every member except the origin.
    void deliver(const std::string& origin_node,
                 const std::string& channel,
                 const nlohmann::json& body) {
        std::vector<realtime::IClusterBus::Handler> targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [node, by_channel] : handlers_) {
                if (node == origin_node) {
                    continue;  // loop suppression, as RedisClusterBus does
                }
                const auto it = by_channel.find(channel);
                if (it != by_channel.end()) {
                    targets.insert(targets.end(), it->second.begin(), it->second.end());
                }
            }
        }
        // Invoked outside the lock so a handler may itself publish without
        // deadlocking.
        for (auto& handler : targets) {
            handler(origin_node, body);
        }
    }

  private:
    std::mutex mutex_;
    std::map<std::string, realtime::IClusterBus*> members_;
    std::map<std::string, std::map<std::string, std::vector<realtime::IClusterBus::Handler>>>
        handlers_;
};

// One instance's view of the shared fabric.
class FakeClusterBus final : public realtime::IClusterBus {
  public:
    FakeClusterBus(std::shared_ptr<ClusterFabric> fabric, std::string node_id)
        : fabric_(std::move(fabric)), node_id_(std::move(node_id)) {
        fabric_->attach(node_id_, this);
    }

    ~FakeClusterBus() override { fabric_->detach(node_id_); }

    FakeClusterBus(const FakeClusterBus&) = delete;
    FakeClusterBus& operator=(const FakeClusterBus&) = delete;

    void publish(std::string_view channel, const nlohmann::json& body) noexcept override {
        try {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                published_.push_back({std::string(channel), body});
            }
            published_count_.fetch_add(1);
            fabric_->deliver(node_id_, std::string(channel), body);
        } catch (...) {
            // Matches the real bus: publishing never throws into the caller.
        }
    }

    void subscribe(std::string_view channel, Handler handler) override {
        auto counting = [this, inner = std::move(handler)](std::string_view origin,
                                                           const nlohmann::json& body) {
            received_count_.fetch_add(1);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                received_.push_back({std::string(origin), body});
            }
            inner(origin, body);
        };
        fabric_->subscribe(node_id_, std::string(channel), std::move(counting));
    }

    void start() override {}
    void stop() override {}

    [[nodiscard]] std::string_view node_id() const noexcept override { return node_id_; }
    [[nodiscard]] bool is_distributed() const noexcept override { return true; }
    [[nodiscard]] std::uint64_t published_count() const noexcept override {
        return published_count_.load();
    }
    [[nodiscard]] std::uint64_t received_count() const noexcept override {
        return received_count_.load();
    }

    struct Published {
        std::string channel;
        nlohmann::json body;
    };
    struct Received {
        std::string origin;
        nlohmann::json body;
    };

    [[nodiscard]] std::vector<Published> published() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return published_;
    }
    [[nodiscard]] std::vector<Received> received() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return received_;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        published_.clear();
        received_.clear();
    }

  private:
    std::shared_ptr<ClusterFabric> fabric_;
    std::string node_id_;
    mutable std::mutex mutex_;
    std::vector<Published> published_;
    std::vector<Received> received_;
    std::atomic<std::uint64_t> published_count_{0};
    std::atomic<std::uint64_t> received_count_{0};
};

}  // namespace rtc::testing
