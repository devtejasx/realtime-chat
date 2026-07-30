#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "rtc/dto/pagination.hpp"
#include "rtc/features/feature_flags.hpp"
#include "rtc/repositories/message_search_repository.hpp"

namespace rtc::services {

// Full-text search over messages the caller can see.
//
// Thin by design: the repository owns the SQL (including the visibility join that
// makes the query safe), and this layer owns input validation, the feature gate
// and the response shape. Keeping ranking and highlighting in SQL is deliberate —
// PostgreSQL does both far better than post-processing in C++ could, and doing it
// in the database avoids shipping rows only to discard them.
class SearchService {
  public:
    // Bounds on the search term. A one-character term matches almost everything
    // and is pure load with no useful result, so it is rejected rather than
    // served; the upper bound stops a pathological query being sent to the parser.
    static constexpr std::size_t kMinTermLength = 2;
    static constexpr std::size_t kMaxTermLength = 256;

    SearchService(repositories::IMessageSearchRepository& messages,
                  const features::FeatureFlags& flags) noexcept
        : messages_(messages), flags_(flags) {}

    SearchService(const SearchService&) = delete;
    SearchService& operator=(const SearchService&) = delete;

    // Runs a search and returns the full response envelope:
    //   { "query", "total", "fuzzy_available", "results": [ ... ] }
    //
    // Throws ValidationException on an unusable term and FeatureDisabledException
    // when search is switched off.
    [[nodiscard]] nlohmann::json search_messages(std::int64_t actor_id,
                                                 const repositories::MessageSearchQuery& query,
                                                 const dto::Pagination& page);

    // Validates and normalises a raw term (trims, checks length).
    [[nodiscard]] static std::string validate_term(const std::string& raw);

  private:
    repositories::IMessageSearchRepository& messages_;
    const features::FeatureFlags& flags_;
};

}  // namespace rtc::services
