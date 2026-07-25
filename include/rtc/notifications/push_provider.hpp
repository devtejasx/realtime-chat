#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace rtc::notifications {

// Outbound push-delivery abstraction. The notification service persists and
// delivers in-app notifications over WebSocket itself; this interface is the
// seam for *external* channels — Firebase Cloud Messaging, Apple Push
// Notification service, email, SMS. Concrete providers implement `send`; adding
// one requires no change to business logic, only a new implementation wired in
// the composition root.
class IPushProvider {
public:
    virtual ~IPushProvider() = default;

    // Delivers a push to a user across their registered devices/channels.
    // Implementations must be non-blocking or be invoked from the background
    // executor; they must never throw into the caller.
    virtual void send(std::int64_t user_id, std::string_view title, std::string_view body,
                      const nlohmann::json& data) noexcept = 0;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

// Default provider: does nothing. Keeps the notification pipeline fully
// functional without any external push integration configured.
class NullPushProvider final : public IPushProvider {
public:
    void send(std::int64_t, std::string_view, std::string_view,
              const nlohmann::json&) noexcept override {}
    [[nodiscard]] std::string_view name() const noexcept override { return "null"; }
};

}  // namespace rtc::notifications
