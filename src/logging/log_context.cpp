#include "rtc/logging/log_context.hpp"

namespace rtc::logging {
namespace {

// Function-local rather than a namespace-scope thread_local: guarantees
// initialisation before first use regardless of static initialisation order,
// which matters because logging can be reached from a static constructor.
[[nodiscard]] std::string& request_id_slot() noexcept {
    static thread_local std::string slot;
    return slot;
}

}  // namespace

std::string_view current_request_id() noexcept {
    return request_id_slot();
}

void set_request_id(std::string request_id) noexcept {
    request_id_slot() = std::move(request_id);
}

void clear_request_id() noexcept {
    request_id_slot().clear();
}

RequestIdScope::RequestIdScope(std::string request_id) noexcept
    : previous_(std::move(request_id_slot())) {
    request_id_slot() = std::move(request_id);
}

RequestIdScope::~RequestIdScope() {
    request_id_slot() = std::move(previous_);
}

}  // namespace rtc::logging
