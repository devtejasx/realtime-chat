#pragma once

#include <cstdint>
#include <string>

#include <crow/websocket.h>

#include "rtc/realtime/connection_manager.hpp"
#include "rtc/services/conversation_service.hpp"
#include "rtc/services/message_service.hpp"
#include "rtc/services/presence_service.hpp"
#include "rtc/services/read_receipt_service.hpp"

namespace rtc::realtime {

// Routes WebSocket lifecycle and inbound frames to the appropriate services.
//
// This is the only realtime component that knows the wire protocol; it decodes
// {type, data} envelopes and calls the *same* service methods the REST layer
// uses, so business logic is never duplicated. It also owns the connect/
// disconnect side effects: room subscription, presence transitions, and
// cleanup. All handlers are exception-safe — a failure becomes an `error`
// event, never a crash of the I/O thread.
class EventDispatcher {
public:
    EventDispatcher(ConnectionManager& connections, services::PresenceService& presence,
                    services::ConversationService& conversations,
                    services::MessageService& messages,
                    services::ReadReceiptService& receipts) noexcept
        : connections_(connections),
          presence_(presence),
          conversations_(conversations),
          messages_(messages),
          receipts_(receipts) {}

    // Called after a successful authenticated handshake.
    void on_open(crow::websocket::connection& conn, std::int64_t user_id,
                 const std::string& username);

    // Called for each inbound text frame.
    void on_message(crow::websocket::connection& conn, const std::string& data);

    // Called when a connection closes (or errors).
    void on_close(crow::websocket::connection& conn);

private:
    void handle_typing(crow::websocket::connection& conn, std::int64_t user_id,
                       const std::string& username, const nlohmann::json& data, bool starting);
    void handle_send_message(std::int64_t user_id, const nlohmann::json& data);
    void handle_mark_delivered(std::int64_t user_id, const nlohmann::json& data);
    void handle_mark_read(std::int64_t user_id, const nlohmann::json& data);

    void send_error(crow::websocket::connection& conn, std::string_view code,
                    std::string_view message);

    ConnectionManager& connections_;
    services::PresenceService& presence_;
    services::ConversationService& conversations_;
    services::MessageService& messages_;
    services::ReadReceiptService& receipts_;
};

}  // namespace rtc::realtime
