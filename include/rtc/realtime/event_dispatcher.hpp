#pragma once

#include <crow/websocket.h>

#include <cstdint>
#include <string>

#include "rtc/events/event_bus.hpp"
#include "rtc/features/feature_flags.hpp"
#include "rtc/realtime/connection_manager.hpp"
#include "rtc/realtime/protocol.hpp"
#include "rtc/services/conversation_service.hpp"
#include "rtc/services/message_service.hpp"
#include "rtc/services/presence_service.hpp"
#include "rtc/services/read_receipt_service.hpp"

namespace rtc::realtime {

// Routes WebSocket lifecycle and inbound frames to the appropriate services.
//
// This is the only realtime component that knows the wire protocol; it decodes
// frames (either protocol version — see rtc/realtime/protocol.hpp) and calls the
// *same* service methods the REST layer uses, so business logic is never
// duplicated. It also owns the connect/disconnect side effects: room
// subscription, presence transitions, and cleanup. All handlers are
// exception-safe — a failure becomes an `error` event, never a crash of the I/O
// thread, and a v2 client's error frame echoes the `request_id` of the command
// that failed so a client can correlate the two.
class EventDispatcher {
  public:
    EventDispatcher(ConnectionManager& connections,
                    services::PresenceService& presence,
                    services::ConversationService& conversations,
                    services::MessageService& messages,
                    services::ReadReceiptService& receipts) noexcept
        : connections_(connections),
          presence_(presence),
          conversations_(conversations),
          messages_(messages),
          receipts_(receipts) {}

    // Enables runtime feature gating for typing and read receipts. Optional and
    // injected after construction so existing callers and tests are unaffected;
    // without it every capability is treated as enabled.
    void set_feature_flags(const features::FeatureFlags& flags) noexcept { flags_ = &flags; }

    // Publishes domain events (presence transitions) when set. Optional for the
    // same reason.
    // Note the leading `::` — inside rtc::realtime, an unqualified `events::`
    // resolves to rtc::realtime::events (the wire-event name constants), not the
    // domain-event namespace. Fully qualifying it removes the ambiguity.
    void set_event_publisher(::rtc::events::IEventPublisher& publisher) noexcept {
        publisher_ = &publisher;
    }

    // Called after a successful authenticated handshake. `version` is the wire
    // protocol negotiated during the upgrade.
    void on_open(crow::websocket::connection& conn,
                 std::int64_t user_id,
                 const std::string& username,
                 protocol::Version version = protocol::kDefaultVersion);

    // Called for each inbound text frame.
    void on_message(crow::websocket::connection& conn, const std::string& data);

    // Called when a connection closes (or errors).
    void on_close(crow::websocket::connection& conn);

  private:
    void handle_typing(crow::websocket::connection& conn,
                       std::int64_t user_id,
                       const std::string& username,
                       const nlohmann::json& data,
                       bool starting);
    void handle_send_message(std::int64_t user_id, const nlohmann::json& data);
    void handle_mark_delivered(std::int64_t user_id, const nlohmann::json& data);
    void handle_mark_read(std::int64_t user_id, const nlohmann::json& data);

    void send_error(crow::websocket::connection& conn,
                    std::string_view code,
                    std::string_view message,
                    const protocol::Envelope& envelope);

    // True when `feature` is on, or when no flag store has been attached.
    [[nodiscard]] bool enabled(features::Feature feature) const noexcept;

    ConnectionManager& connections_;
    services::PresenceService& presence_;
    services::ConversationService& conversations_;
    services::MessageService& messages_;
    services::ReadReceiptService& receipts_;
    const features::FeatureFlags* flags_ = nullptr;
    ::rtc::events::IEventPublisher* publisher_ = nullptr;
};

}  // namespace rtc::realtime
