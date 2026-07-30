#include "rtc/features/feature_flags.hpp"

#include <cctype>
#include <string>

#include "rtc/utils/env.hpp"

namespace rtc::features {
namespace {

// Accepts the same truthy spellings as config::Config so operators only have to
// learn one convention across every environment variable this service reads.
[[nodiscard]] bool parse_bool(std::string_view raw, bool fallback) {
    if (raw.empty()) {
        return fallback;
    }
    std::string lowered;
    lowered.reserve(raw.size());
    for (const char c : raw) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

}  // namespace

std::optional<Feature> parse_feature(std::string_view name) noexcept {
    for (const auto& entry : kFeatureRegistry) {
        if (entry.name == name) {
            return entry.feature;
        }
    }
    return std::nullopt;
}

FeatureFlags::FeatureFlags() noexcept {
    for (const auto& entry : kFeatureRegistry) {
        flags_[static_cast<std::size_t>(entry.feature)].store(entry.default_enabled,
                                                              std::memory_order_relaxed);
    }
}

void FeatureFlags::load_from_env() {
    for (const auto& entry : kFeatureRegistry) {
        const std::string env_var(entry.env_var);
        const std::string raw = utils::get_env_or(env_var.c_str(), "");
        set(entry.feature, parse_bool(raw, entry.default_enabled));
    }
}

nlohmann::json FeatureFlags::to_json() const {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& entry : kFeatureRegistry) {
        out.push_back(nlohmann::json{
            {"name", std::string(entry.name)},
            {"enabled", is_enabled(entry.feature)},
            {"env", std::string(entry.env_var)},
            {"default", entry.default_enabled},
            {"description", std::string(entry.description)},
        });
    }
    return out;
}

std::vector<std::string_view> FeatureFlags::enabled_names() const {
    std::vector<std::string_view> out;
    out.reserve(kFeatureCount);
    for (const auto& entry : kFeatureRegistry) {
        if (is_enabled(entry.feature)) {
            out.push_back(entry.name);
        }
    }
    return out;
}

}  // namespace rtc::features
