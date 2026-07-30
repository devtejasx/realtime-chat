#include "rtc/repositories/pg_message_search_repository.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>
#include <string>
#include <utility>

#include "rtc/logging/logger.hpp"
#include "rtc/tracing/scoped_span.hpp"

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

// Minimum trigram similarity for a fuzzy hit. 0.3 is PostgreSQL's own default
// for the % operator and empirically the point where results stay relevant: much
// lower and unrelated messages appear, much higher and genuine typos are missed.
constexpr double kFuzzySimilarityThreshold = 0.3;

constexpr const char* kMessageColumns =
    "m.id, m.conversation_id, m.sender_id, m.type, m.content, "
    "EXTRACT(EPOCH FROM m.created_at)::bigint AS created_epoch, "
    "EXTRACT(EPOCH FROM m.updated_at)::bigint AS updated_epoch, "
    "EXTRACT(EPOCH FROM m.edited_at)::bigint  AS edited_epoch, "
    "EXTRACT(EPOCH FROM m.deleted_at)::bigint AS deleted_epoch";

// The visibility join. This is the security boundary: a message is reachable only
// through a conversation_participants row for the actor, enforced by the query
// rather than by the caller remembering to filter.
//
// Shared predicate parameters, in bind order:
//   $1 actor_id, $2 term, $3 conversation_id, $4 sender_id, $5 from_epoch,
//   $6 to_epoch
constexpr const char* kVisibilityAndFilters =
    " FROM messages m "
    " JOIN conversation_participants cp "
    "      ON cp.conversation_id = m.conversation_id AND cp.user_id = $1 "
    " WHERE m.deleted_at IS NULL "
    "   AND ($3::bigint IS NULL OR m.conversation_id = $3) "
    "   AND ($4::bigint IS NULL OR m.sender_id = $4) "
    "   AND ($5::bigint IS NULL OR m.created_at >= to_timestamp($5)) "
    "   AND ($6::bigint IS NULL OR m.created_at <= to_timestamp($6)) ";

[[nodiscard]] std::optional<utils::TimePoint> read_opt_time(const pqxx::field& field) {
    if (field.is_null())
        return std::nullopt;
    return Clock::from_time_t(field.as<std::time_t>());
}

[[nodiscard]] models::Message map_message(const pqxx::row& row) {
    models::Message m;
    m.id = row["id"].as<std::int64_t>();
    m.conversation_id = row["conversation_id"].as<std::int64_t>();
    m.sender_id = row["sender_id"].as<std::int64_t>();
    m.type = models::message_type_from_string(row["type"].as<std::string>())
                 .value_or(models::MessageType::kText);
    m.content = row["content"].as<std::string>();
    m.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    m.updated_at = Clock::from_time_t(row["updated_epoch"].as<std::time_t>());
    m.edited_at = read_opt_time(row["edited_epoch"]);
    m.deleted_at = read_opt_time(row["deleted_epoch"]);
    return m;
}

}  // namespace

bool PgMessageSearchRepository::fuzzy_available() {
    // std::call_once rather than a plain bool: the probe is a database round-trip
    // and multiple request threads can arrive here simultaneously on the first
    // search. Doing it once, with correct publication, avoids both a race and a
    // burst of redundant queries.
    std::call_once(trgm_probe_, [this] {
        bool available = false;
        try {
            available = with_transaction([](pqxx::work& txn) -> bool {
                const auto row = txn.exec1(
                    "SELECT EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_trgm') "
                    "AS present");
                return row["present"].as<bool>();
            });
        } catch (const std::exception& ex) {
            RTC_LOG_WARN("Could not determine pg_trgm availability ({}); fuzzy search disabled",
                         ex.what());
        }
        trgm_available_.store(available, std::memory_order_release);
        RTC_LOG_INFO("Full-text search: fuzzy matching {}",
                     available ? "enabled (pg_trgm)" : "disabled (pg_trgm not installed)");
    });
    return trgm_available_.load(std::memory_order_acquire);
}

std::vector<MessageSearchHit> PgMessageSearchRepository::search(std::int64_t actor_id,
                                                                const MessageSearchQuery& query,
                                                                const dto::Pagination& page) {
    const bool want_fuzzy = query.fuzzy && fuzzy_available();
    auto scope = tracing::db_scope("message.search");
    scope.span().set_attribute("rtc.search.fuzzy", want_fuzzy);

    return with_transaction([&](pqxx::work& txn) -> std::vector<MessageSearchHit> {
        // websearch_to_tsquery never raises on malformed input (unlike
        // to_tsquery), which is essential for a term typed by a user.
        //
        // ts_rank_cd is chosen over ts_rank because it accounts for the proximity
        // of matched terms — for chat messages, two search words appearing next to
        // each other is a much better hit than the same words paragraphs apart.
        //
        // ts_headline produces the snippet. StartSel/StopSel emit <mark> so a
        // client can render highlighting without re-implementing the match logic.
        const std::string sql =
            std::string("SELECT ") + kMessageColumns +
            ", ts_rank_cd(m.search_vector, websearch_to_tsquery('english', $2)) AS rank"
            ", CASE WHEN $7::boolean THEN ts_headline('english', m.content, "
            "        websearch_to_tsquery('english', $2), "
            "        'StartSel=<mark>, StopSel=</mark>, MaxWords=32, MinWords=8, "
            "         ShortWord=3, HighlightAll=FALSE, MaxFragments=2, "
            "         FragmentDelimiter= ... ') "
            "   ELSE '' END AS headline"
            ", FALSE AS fuzzy_match" +
            kVisibilityAndFilters +
            "   AND m.search_vector @@ websearch_to_tsquery('english', $2) "
            " ORDER BY rank DESC, m.id DESC LIMIT $8 OFFSET $9";

        auto result = txn.exec_params(sql,
                                      actor_id,
                                      query.term,
                                      query.conversation_id,
                                      query.sender_id,
                                      query.from_epoch,
                                      query.to_epoch,
                                      query.highlight,
                                      page.limit,
                                      page.offset);

        // Fuzzy fallback, deliberately only when the exact search found nothing.
        // Running both always would be slower and would let low-similarity noise
        // dilute good exact matches; a typo, meanwhile, produces exactly this
        // empty-result case.
        if (result.empty() && want_fuzzy) {
            const std::string fuzzy_sql =
                std::string("SELECT ") + kMessageColumns +
                ", similarity(m.content, $2) AS rank"
                ", CASE WHEN $7::boolean THEN m.content ELSE '' END AS headline"
                ", TRUE AS fuzzy_match" +
                kVisibilityAndFilters +
                "   AND similarity(m.content, $2) >= $10 "
                " ORDER BY rank DESC, m.id DESC LIMIT $8 OFFSET $9";
            result = txn.exec_params(fuzzy_sql,
                                     actor_id,
                                     query.term,
                                     query.conversation_id,
                                     query.sender_id,
                                     query.from_epoch,
                                     query.to_epoch,
                                     query.highlight,
                                     page.limit,
                                     page.offset,
                                     kFuzzySimilarityThreshold);
        }

        std::vector<MessageSearchHit> hits;
        hits.reserve(static_cast<std::size_t>(result.size()));
        for (const auto& row : result) {
            MessageSearchHit hit;
            hit.message = map_message(row);
            hit.rank = row["rank"].as<double>();
            hit.headline = row["headline"].as<std::string>();
            hit.fuzzy_match = row["fuzzy_match"].as<bool>();
            hits.push_back(std::move(hit));
        }
        return hits;
    });
}

std::int64_t PgMessageSearchRepository::count(std::int64_t actor_id,
                                              const MessageSearchQuery& query) {
    return with_transaction([&](pqxx::work& txn) -> std::int64_t {
        // Same predicate as search()'s exact branch, so the reported total always
        // agrees with the page that was returned.
        const std::string sql = std::string("SELECT COUNT(*) AS total") + kVisibilityAndFilters +
                                "   AND m.search_vector @@ websearch_to_tsquery('english', $2)";
        const auto row = txn.exec_params(sql,
                                         actor_id,
                                         query.term,
                                         query.conversation_id,
                                         query.sender_id,
                                         query.from_epoch,
                                         query.to_epoch)
                             .front();
        return row["total"].as<std::int64_t>();
    });
}

}  // namespace rtc::repositories
