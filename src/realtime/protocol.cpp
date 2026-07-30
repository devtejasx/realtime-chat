#include "rtc/realtime/protocol.hpp"

#include "rtc/utils/time.hpp"

namespace rtc::realtime::protocol {
namespace {

// Reads a string field, tolerating absence and a non-string type.
[[nodiscard]] std::string read_string(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

}  // namespace

std::string encode(Version version, std::string_view event, const nlohmann::json& payload,
                   const Envelope& envelope) {
    if (version == Version::kV1) {
        // Byte-for-byte the historical format. Do not add fields here: v1 clients
        // may be strict, and the whole point of negotiation is that v1 is frozen.
        return nlohmann::json{{"type", event}, {"data", payload}}.dump();
    }

    nlohmann::json frame{
        {"event", event},
        {"version", to_number(version)},
        {"timestamp", utils::to_iso8601(utils::now())},
        {"payload", payload},
    };
    // Correlation fields are always present as keys (null when unknown) so client
    // code can read them unconditionally.
    frame["request_id"] =
        envelope.request_id.empty() ? nlohmann::json() : nlohmann::json(envelope.request_id);
    frame["correlation_id"] = envelope.correlation_id.empty()
                                  ? nlohmann::json()
                                  : nlohmann::json(envelope.correlation_id);
    return frame.dump();
}

std::optional<InboundFrame> decode(std::string_view text) {
    // Non-throwing parse: a malformed client frame is routine input, not an
    // exceptional condition, and must not cost an exception per bad packet.
    const auto parsed = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return std::nullopt;
    }

    InboundFrame frame;
    // Prefer the v2 shape, fall back to v1. Detecting per frame (rather than
    // trusting the negotiated version) lets a client send either form.
    if (const auto event = read_string(parsed, "event"); !event.empty()) {
        frame.version = Version::kV2;
        frame.event = event;
        if (const auto it = parsed.find("payload"); it != parsed.end() && it->is_object()) {
            frame.payload = *it;
        }
    } else if (const auto type = read_string(parsed, "type"); !type.empty()) {
        frame.version = Version::kV1;
        frame.event = type;
        if (const auto it = parsed.find("data"); it != parsed.end() && it->is_object()) {
            frame.payload = *it;
        }
    } else {
        return std::nullopt;  // no event name — nothing dispatchable
    }

    frame.request_id = read_string(parsed, "request_id");
    frame.correlation_id = read_string(parsed, "correlation_id");
    return frame;
}

}  // namespace rtc::realtime::protocol
