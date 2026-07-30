#include "rtc/services/search_service.hpp"

#include <string>
#include <utility>

#include "rtc/errors/exceptions.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::services {
namespace {

[[nodiscard]] std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

}  // namespace

std::string SearchService::validate_term(const std::string& raw) {
    const std::string term = trim(raw);
    if (term.size() < kMinTermLength) {
        throw errors::ValidationException(
            "Search term must be at least " + std::to_string(kMinTermLength) + " characters",
            "field=q");
    }
    if (term.size() > kMaxTermLength) {
        throw errors::ValidationException(
            "Search term must be at most " + std::to_string(kMaxTermLength) + " characters",
            "field=q");
    }
    return term;
}

nlohmann::json SearchService::search_messages(std::int64_t actor_id,
                                              const repositories::MessageSearchQuery& query,
                                              const dto::Pagination& page) {
    flags_.require(features::Feature::kSearch);

    repositories::MessageSearchQuery normalised = query;
    normalised.term = validate_term(query.term);

    const auto hits = messages_.search(actor_id, normalised, page);
    const std::int64_t total = messages_.count(actor_id, normalised);

    nlohmann::json results = nlohmann::json::array();
    for (const auto& hit : hits) {
        results.push_back({
            {"message_id", hit.message.id},
            {"conversation_id", hit.message.conversation_id},
            {"sender_id", hit.message.sender_id},
            {"content", hit.message.content},
            {"created_at", utils::to_iso8601(hit.message.created_at)},
            {"rank", hit.rank},
            {"highlight", hit.headline},
            {"fuzzy_match", hit.fuzzy_match},
        });
    }

    return nlohmann::json{
        {"query", normalised.term},
        // `total` counts exact matches only. A fuzzy fallback result set is
        // reported with its own size so a client is never told there are more
        // pages of approximate hits than actually exist.
        {"total", total},
        {"returned", results.size()},
        {"fuzzy_available", messages_.fuzzy_available()},
        {"limit", page.limit},
        {"offset", page.offset},
        {"results", std::move(results)},
    };
}

}  // namespace rtc::services
