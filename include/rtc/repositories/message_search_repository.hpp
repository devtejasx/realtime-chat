#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rtc/dto/pagination.hpp"
#include "rtc/models/message.hpp"

namespace rtc::repositories {

// A full-text search request over messages.
//
// `term` is raw user input and is *never* interpolated into SQL: it is bound as a
// parameter and handed to websearch_to_tsquery, which parses Google-style syntax
// ("foo bar", "foo OR bar", -excluded, "quoted phrase") and — crucially — cannot
// raise a syntax error on malformed input the way to_tsquery does. That property
// matters when the input comes from a text box.
struct MessageSearchQuery {
    std::string term;
    // Restrict to a single conversation. When unset, every conversation the
    // caller participates in is searched.
    std::optional<std::int64_t> conversation_id;
    std::optional<std::int64_t> sender_id;
    // Inclusive time window as Unix epoch seconds.
    std::optional<std::int64_t> from_epoch;
    std::optional<std::int64_t> to_epoch;
    // Fall back to trigram similarity when the exact query matches nothing, so a
    // typo still returns results. Requires pg_trgm; silently ignored without it.
    bool fuzzy = true;
    // Produce an ts_headline snippet with <mark> markers around matches.
    bool highlight = true;
};

// One search result: the message, its relevance score, and an optional snippet.
struct MessageSearchHit {
    models::Message message;
    double rank = 0.0;
    // Highlighted excerpt when highlighting was requested, otherwise empty.
    std::string headline;
    // True when this hit came from the fuzzy fallback rather than an exact
    // full-text match. Surfaced to clients so a UI can label approximate results.
    bool fuzzy_match = false;
};

// Search boundary for messages.
//
// Deliberately a *new* interface rather than methods added to IMessageRepository:
// that interface is implemented by fakes across the existing test suite, and
// extending it would break every one of them. Separation also reflects reality —
// search has its own indexes, its own ranking concerns and its own failure modes,
// and none of that belongs in the CRUD repository.
//
// Authorisation is enforced *inside* the query, not by the caller. Every method
// takes `actor_id` and joins against conversation_participants, so it is
// structurally impossible to search messages the caller cannot see. Passing a
// pre-computed list of visible conversation ids would be both slower (a large IN
// list) and less safe (the caller could get it wrong).
class IMessageSearchRepository {
public:
    virtual ~IMessageSearchRepository() = default;

    [[nodiscard]] virtual std::vector<MessageSearchHit> search(std::int64_t actor_id,
                                                              const MessageSearchQuery& query,
                                                              const dto::Pagination& page) = 0;

    // Total matches, for the paginated envelope. Counted with the same predicate
    // as search() so the total and the page can never disagree.
    [[nodiscard]] virtual std::int64_t count(std::int64_t actor_id,
                                            const MessageSearchQuery& query) = 0;

    // Whether trigram (fuzzy) support is actually available on this database.
    // Probed once and cached; lets the API tell clients what it can do instead of
    // silently ignoring the `fuzzy` flag.
    [[nodiscard]] virtual bool fuzzy_available() = 0;
};

}  // namespace rtc::repositories
