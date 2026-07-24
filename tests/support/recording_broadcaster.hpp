#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rtc/realtime/event_broadcaster.hpp"

namespace rtc::testing {

// IEventBroadcaster that records every published event, so service tests can
// assert that the correct real-time notifications were emitted (and to whom).
class RecordingBroadcaster final : public realtime::IEventBroadcaster {
public:
    struct Event {
        std::vector<std::int64_t> user_ids;
        std::string type;
        nlohmann::json data;
    };

    void publish(const std::vector<std::int64_t>& user_ids, std::string_view type,
                 const nlohmann::json& data) override {
        events_.push_back(Event{user_ids, std::string(type), data});
    }

    [[nodiscard]] const std::vector<Event>& events() const noexcept { return events_; }
    [[nodiscard]] std::size_t count() const noexcept { return events_.size(); }
    void clear() { events_.clear(); }

    [[nodiscard]] const Event& last() const { return events_.back(); }

private:
    std::vector<Event> events_;
};

}  // namespace rtc::testing
