#pragma once

#include <string_view>

namespace rtc::realtime::events {

// Canonical event-type names shared by the service layer (producers) and the
// WebSocket layer (transport). Centralising them keeps client and server in
// lock-step and avoids stringly-typed drift across the codebase.

// ---- Server -> client -----------------------------------------------------
inline constexpr std::string_view kMessageCreated = "message.created";
inline constexpr std::string_view kMessageUpdated = "message.updated";
inline constexpr std::string_view kMessageDeleted = "message.deleted";
inline constexpr std::string_view kConversationCreated = "conversation.created";
inline constexpr std::string_view kConversationDeleted = "conversation.deleted";
inline constexpr std::string_view kMemberAdded = "conversation.member_added";
inline constexpr std::string_view kMemberRemoved = "conversation.member_removed";
inline constexpr std::string_view kTypingStart = "typing.start";
inline constexpr std::string_view kTypingStop = "typing.stop";
inline constexpr std::string_view kPresenceUpdate = "presence.update";
inline constexpr std::string_view kReceiptUpdate = "receipt.update";
inline constexpr std::string_view kReadUpdate = "read.update";
inline constexpr std::string_view kPong = "pong";
inline constexpr std::string_view kError = "error";
inline constexpr std::string_view kReady = "ready";

// ---- Client -> server -----------------------------------------------------
inline constexpr std::string_view kClientPing = "ping";
inline constexpr std::string_view kClientSendMessage = "message.send";
inline constexpr std::string_view kClientTypingStart = "typing.start";
inline constexpr std::string_view kClientTypingStop = "typing.stop";
inline constexpr std::string_view kClientMarkRead = "mark_read";
inline constexpr std::string_view kClientMarkDelivered = "mark_delivered";

}  // namespace rtc::realtime::events
