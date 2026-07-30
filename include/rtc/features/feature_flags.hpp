#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "rtc/errors/exceptions.hpp"

namespace rtc::features {

// Runtime-togglable capabilities.
//
// A closed enum rather than free-form strings: a typo in a flag name becomes a
// compile error instead of a silently-always-off feature, and the flag set is
// self-documenting. Keep kCount last — it sizes the storage array.
enum class Feature : std::size_t {
    kReactions,
    kUploads,
    kNotifications,
    kTyping,
    kSearch,
    kReadReceipts,
    kPresence,
    kAuditLog,
    kCount,
};

inline constexpr std::size_t kFeatureCount = static_cast<std::size_t>(Feature::kCount);

// Static description of one flag: its stable API name, the environment variable
// that seeds it, and the default when that variable is unset.
struct FeatureDescriptor {
    Feature feature;
    std::string_view name;      // "reactions" — the name used in the admin API
    std::string_view env_var;   // "ENABLE_REACTIONS"
    bool default_enabled;
    std::string_view description;
};

// The registry. Defaults are deliberately "on" for everything the service
// already shipped, so introducing flags changes no existing behaviour.
inline constexpr std::array<FeatureDescriptor, kFeatureCount> kFeatureRegistry{{
    {Feature::kReactions, "reactions", "ENABLE_REACTIONS", true,
     "Message reactions (add/remove emoji)"},
    {Feature::kUploads, "uploads", "ENABLE_UPLOADS", true, "Attachment upload and download"},
    {Feature::kNotifications, "notifications", "ENABLE_NOTIFICATIONS", true,
     "Notification creation, listing and push delivery"},
    {Feature::kTyping, "typing", "ENABLE_TYPING", true, "Typing indicators over WebSocket"},
    {Feature::kSearch, "search", "ENABLE_SEARCH", true, "Full-text message and user search"},
    {Feature::kReadReceipts, "read_receipts", "ENABLE_READ_RECEIPTS", true,
     "Delivery and read receipts"},
    {Feature::kPresence, "presence", "ENABLE_PRESENCE", true, "Online/offline presence tracking"},
    {Feature::kAuditLog, "audit_log", "ENABLE_AUDIT_LOG", true,
     "Persisting audit records for security-relevant actions"},
}};

[[nodiscard]] constexpr const FeatureDescriptor& descriptor(Feature feature) noexcept {
    return kFeatureRegistry[static_cast<std::size_t>(feature)];
}

[[nodiscard]] constexpr std::string_view name_of(Feature feature) noexcept {
    return descriptor(feature).name;
}

// Resolves an API flag name to its enum value; nullopt when unknown.
[[nodiscard]] std::optional<Feature> parse_feature(std::string_view name) noexcept;

// 404 raised when a request targets a disabled capability. A disabled feature
// should look *absent* rather than forbidden — 403 would tell a caller the
// endpoint exists and they merely lack rights, which is misleading here.
class FeatureDisabledException : public errors::AppException {
public:
    explicit FeatureDisabledException(std::string_view feature_name)
        : errors::AppException(errors::ErrorType::kNotFound,
                               "Feature is disabled: " + std::string(feature_name),
                               "feature=" + std::string(feature_name)) {}
};

// Thread-safe, runtime-mutable feature-flag store.
//
// Reads are a single relaxed atomic load, so a flag check is cheap enough to sit
// on the hot path of every request. Writes come from the admin API and are rare.
// Relaxed ordering is correct here: a flag is an independent boolean with no
// happens-before relationship to other state, so the only guarantee needed is
// that a read eventually observes the write — which relaxed provides.
//
// Injected by reference wherever needed (it is not a singleton); Application
// owns the single instance.
class FeatureFlags {
public:
    // All flags at their registry defaults.
    FeatureFlags() noexcept;

    // Non-copyable and non-movable: std::atomic is neither, and a flag store is
    // a single shared authority anyway — it is always used by reference.
    FeatureFlags(const FeatureFlags&) = delete;
    FeatureFlags& operator=(const FeatureFlags&) = delete;

    // Seeds every flag from its environment variable, falling back to the
    // registry default. Accepts 1/true/yes/on (case-insensitive) as enabled.
    // Called once at startup; safe to call again to re-read the environment.
    void load_from_env();

    [[nodiscard]] bool is_enabled(Feature feature) const noexcept {
        return flags_[static_cast<std::size_t>(feature)].load(std::memory_order_relaxed);
    }

    // Sets a flag at runtime. Returns the previous value.
    bool set(Feature feature, bool enabled) noexcept {
        return flags_[static_cast<std::size_t>(feature)].exchange(enabled,
                                                                  std::memory_order_relaxed);
    }

    // Throws FeatureDisabledException unless the feature is enabled. The guard
    // controllers use so a disabled capability fails uniformly.
    void require(Feature feature) const {
        if (!is_enabled(feature)) {
            throw FeatureDisabledException(name_of(feature));
        }
    }

    // Snapshot of every flag, for the admin API and diagnostics:
    //   [{"name":"reactions","enabled":true,"env":"ENABLE_REACTIONS","description":"..."}]
    [[nodiscard]] nlohmann::json to_json() const;

    // Names of the currently-enabled features, for startup logging.
    [[nodiscard]] std::vector<std::string_view> enabled_names() const;

private:
    // std::atomic<bool> is not copyable, so the array is built in place.
    std::array<std::atomic<bool>, kFeatureCount> flags_;
};

}  // namespace rtc::features
