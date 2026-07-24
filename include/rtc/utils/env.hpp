#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace rtc::utils {

// Thin, testable wrappers around environment-variable access. These never
// throw: parsing/typing decisions belong to the caller (e.g. Config), which
// can apply defaults and raise a domain-specific ConfigException.

// Returns the raw value of `name`, or std::nullopt when the variable is unset.
[[nodiscard]] inline std::optional<std::string> get_env(const char* name) {
#if defined(_MSC_VER)
    // std::getenv triggers a deprecation warning under MSVC; use the safe
    // duplicate-environment-string API instead.
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return std::nullopt;
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    return std::string(raw);
#endif
}

// Returns the value of `name`, or `fallback` when unset or empty.
[[nodiscard]] inline std::string get_env_or(const char* name, std::string_view fallback) {
    auto value = get_env(name);
    if (!value || value->empty()) {
        return std::string(fallback);
    }
    return *value;
}

}  // namespace rtc::utils
