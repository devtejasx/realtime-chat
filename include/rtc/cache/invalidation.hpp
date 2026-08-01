#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace rtc::cache {

// Cross-instance cache invalidation, expressed as an interface the cache layer
// owns rather than a dependency on the messaging layer.
//
// Why an interface here instead of handing CacheService an IClusterBus: cache
// coherence is a caching concern, and cluster transport is a realtime concern.
// Pointing rtc::cache at rtc::realtime would invert that and make the cache
// unusable (and untestable) without the whole WebSocket stack. The composition
// root supplies an adapter that happens to be backed by Redis Pub/Sub; the
// caching code only knows that *something* will carry the notice.
//
// Fire-and-forget by design. A dropped invalidation degrades to the behaviour
// this system already had before it was distributed — an entry that lives out
// its TTL — so a publish failure must never propagate into the write that
// triggered it. Hence noexcept.

// Names the cached thing to drop. Kept coarse deliberately: a receiver acts on
// it without knowing how the publisher stores anything.
struct InvalidationEvent {
    // Which cache is affected — see invalidation_scopes below.
    std::string scope;
    // Scope-specific identifier. For kAuthorization, the user id as a string;
    // for kNamespacedKey, the cache namespace.
    std::string key;
    // Optional second component, used where a scope needs a namespace *and* a
    // key. Empty when the scope does not need it.
    std::string sub_key;

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json out{{"scope", scope}, {"key", key}};
        if (!sub_key.empty()) {
            out["sub_key"] = sub_key;
        }
        return out;
    }

    [[nodiscard]] static InvalidationEvent from_json(const nlohmann::json& body) {
        InvalidationEvent event;
        if (const auto it = body.find("scope"); it != body.end() && it->is_string()) {
            event.scope = it->get<std::string>();
        }
        if (const auto it = body.find("key"); it != body.end() && it->is_string()) {
            event.key = it->get<std::string>();
        }
        if (const auto it = body.find("sub_key"); it != body.end() && it->is_string()) {
            event.sub_key = it->get<std::string>();
        }
        return event;
    }
};

namespace invalidation_scopes {
// Role and ban state for one user. The security-critical one: without it, a ban
// applied on one replica stays invisible to the others until their cached entry
// expires, so the "authorization is database-authoritative behind a short-TTL
// cache" guarantee holds only on a single instance.
inline constexpr std::string_view kAuthorization = "authorization";
// A single CacheService entry, identified by namespace (key) + entry (sub_key).
inline constexpr std::string_view kNamespacedKey = "cache";
}  // namespace invalidation_scopes

class IInvalidationPublisher {
  public:
    virtual ~IInvalidationPublisher() = default;
    virtual void publish_invalidation(const InvalidationEvent& event) noexcept = 0;
};

// Used when the process is running standalone, where local eviction already
// reaches every cache there is. A no-op is the correct behaviour, not a stub.
class NullInvalidationPublisher final : public IInvalidationPublisher {
  public:
    void publish_invalidation(const InvalidationEvent& /*event*/) noexcept override {}

    // Shared singleton: holders take a reference and there is no state to keep
    // per owner, which keeps the "no publisher configured" path allocation-free.
    [[nodiscard]] static NullInvalidationPublisher& instance() noexcept {
        static NullInvalidationPublisher singleton;
        return singleton;
    }
};

}  // namespace rtc::cache
