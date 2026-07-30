#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rtc/errors/exceptions.hpp"
#include "rtc/features/feature_flags.hpp"
#include "rtc/repositories/message_search_repository.hpp"
#include "rtc/services/search_service.hpp"

namespace {

// Records the query it was given and returns canned hits, so the service's own
// behaviour (validation, gating, envelope shape) is tested in isolation from SQL.
class FakeMessageSearchRepository final : public rtc::repositories::IMessageSearchRepository {
public:
    [[nodiscard]] std::vector<rtc::repositories::MessageSearchHit> search(
        std::int64_t actor_id, const rtc::repositories::MessageSearchQuery& query,
        const rtc::dto::Pagination& page) override {
        last_actor_id = actor_id;
        last_query = query;
        last_limit = page.limit;
        return hits;
    }

    [[nodiscard]] std::int64_t count(std::int64_t,
                                    const rtc::repositories::MessageSearchQuery&) override {
        return total;
    }

    [[nodiscard]] bool fuzzy_available() override { return fuzzy; }

    std::vector<rtc::repositories::MessageSearchHit> hits;
    std::int64_t total = 0;
    bool fuzzy = true;
    std::int64_t last_actor_id = 0;
    rtc::repositories::MessageSearchQuery last_query;
    int last_limit = 0;
};

[[nodiscard]] rtc::repositories::MessageSearchHit make_hit(std::int64_t id, double rank,
                                                          bool fuzzy = false) {
    rtc::repositories::MessageSearchHit hit;
    hit.message.id = id;
    hit.message.conversation_id = 5;
    hit.message.sender_id = 3;
    hit.message.content = "hello world";
    hit.rank = rank;
    hit.headline = "<mark>hello</mark> world";
    hit.fuzzy_match = fuzzy;
    return hit;
}

// --- term validation -------------------------------------------------------

TEST(SearchService, TrimsWhitespaceFromTheTerm) {
    EXPECT_EQ(rtc::services::SearchService::validate_term("  hello  "), "hello");
    EXPECT_EQ(rtc::services::SearchService::validate_term("\thi\n"), "hi");
}

TEST(SearchService, RejectsATermThatIsTooShort) {
    // A one-character term matches almost everything: pure load, no useful result.
    EXPECT_THROW((void) rtc::services::SearchService::validate_term("a"),
                 rtc::errors::ValidationException);
    EXPECT_THROW((void) rtc::services::SearchService::validate_term(""),
                 rtc::errors::ValidationException);
    EXPECT_THROW((void) rtc::services::SearchService::validate_term("   "),
                 rtc::errors::ValidationException);
}

TEST(SearchService, RejectsATermThatIsTooLong) {
    const std::string huge(rtc::services::SearchService::kMaxTermLength + 1, 'x');
    EXPECT_THROW((void) rtc::services::SearchService::validate_term(huge),
                 rtc::errors::ValidationException);
}

TEST(SearchService, AcceptsTermsAtTheBoundaries) {
    EXPECT_NO_THROW((void) rtc::services::SearchService::validate_term("ab"));
    const std::string at_limit(rtc::services::SearchService::kMaxTermLength, 'x');
    EXPECT_NO_THROW((void) rtc::services::SearchService::validate_term(at_limit));
}

TEST(SearchService, ValidationErrorNamesTheField) {
    try {
        (void) rtc::services::SearchService::validate_term("a");
        FAIL() << "expected ValidationException";
    } catch (const rtc::errors::ValidationException& ex) {
        EXPECT_EQ(ex.details(), "field=q");
    }
}

// --- feature gating --------------------------------------------------------

TEST(SearchService, ThrowsWhenSearchIsDisabled) {
    FakeMessageSearchRepository repository;
    rtc::features::FeatureFlags flags;
    flags.set(rtc::features::Feature::kSearch, false);
    rtc::services::SearchService service(repository, flags);

    rtc::repositories::MessageSearchQuery query;
    query.term = "hello";
    EXPECT_THROW((void) service.search_messages(1, query, {}),
                 rtc::features::FeatureDisabledException);
}

TEST(SearchService, GateIsCheckedBeforeTheRepositoryIsTouched) {
    // Cheap check first: a disabled feature must not cost a database round trip.
    FakeMessageSearchRepository repository;
    rtc::features::FeatureFlags flags;
    flags.set(rtc::features::Feature::kSearch, false);
    rtc::services::SearchService service(repository, flags);

    rtc::repositories::MessageSearchQuery query;
    query.term = "hello";
    EXPECT_THROW((void) service.search_messages(1, query, {}), rtc::errors::AppException);
    EXPECT_EQ(repository.last_actor_id, 0) << "repository was queried for a disabled feature";
}

// --- response envelope -----------------------------------------------------

TEST(SearchService, BuildsTheResponseEnvelope) {
    FakeMessageSearchRepository repository;
    repository.hits = {make_hit(11, 0.9), make_hit(12, 0.4)};
    repository.total = 2;
    rtc::features::FeatureFlags flags;
    rtc::services::SearchService service(repository, flags);

    rtc::repositories::MessageSearchQuery query;
    query.term = "  hello  ";
    const auto response = service.search_messages(42, query, {});

    // The term is normalised before it reaches either the response or the query.
    EXPECT_EQ(response.at("query"), "hello");
    EXPECT_EQ(repository.last_query.term, "hello");
    EXPECT_EQ(repository.last_actor_id, 42);

    EXPECT_EQ(response.at("total"), 2);
    EXPECT_EQ(response.at("returned"), 2);
    EXPECT_TRUE(response.at("fuzzy_available").get<bool>());
    ASSERT_EQ(response.at("results").size(), 2U);

    const auto& first = response.at("results").at(0);
    EXPECT_EQ(first.at("message_id"), 11);
    EXPECT_EQ(first.at("conversation_id"), 5);
    EXPECT_DOUBLE_EQ(first.at("rank").get<double>(), 0.9);
    EXPECT_EQ(first.at("highlight"), "<mark>hello</mark> world");
    EXPECT_FALSE(first.at("fuzzy_match").get<bool>());
}

TEST(SearchService, ReportsFuzzyHitsDistinctly) {
    // A client should be able to label approximate results as such.
    FakeMessageSearchRepository repository;
    repository.hits = {make_hit(11, 0.5, /*fuzzy=*/true)};
    repository.total = 0;  // no exact matches; these came from the fallback
    rtc::features::FeatureFlags flags;
    rtc::services::SearchService service(repository, flags);

    rtc::repositories::MessageSearchQuery query;
    query.term = "helo";
    const auto response = service.search_messages(1, query, {});

    EXPECT_EQ(response.at("total"), 0);
    EXPECT_EQ(response.at("returned"), 1);
    EXPECT_TRUE(response.at("results").at(0).at("fuzzy_match").get<bool>());
}

TEST(SearchService, ReportsWhenFuzzySupportIsAbsent) {
    FakeMessageSearchRepository repository;
    repository.fuzzy = false;
    rtc::features::FeatureFlags flags;
    rtc::services::SearchService service(repository, flags);

    rtc::repositories::MessageSearchQuery query;
    query.term = "hello";
    const auto response = service.search_messages(1, query, {});
    // Telling the client is better than silently ignoring the `fuzzy` parameter.
    EXPECT_FALSE(response.at("fuzzy_available").get<bool>());
}

TEST(SearchService, PassesFiltersThroughUnchanged) {
    FakeMessageSearchRepository repository;
    rtc::features::FeatureFlags flags;
    rtc::services::SearchService service(repository, flags);

    rtc::repositories::MessageSearchQuery query;
    query.term = "hello";
    query.conversation_id = 5;
    query.sender_id = 9;
    query.from_epoch = 1000;
    query.to_epoch = 2000;
    query.fuzzy = false;
    query.highlight = false;

    rtc::dto::Pagination page;
    page.limit = 7;
    (void) service.search_messages(1, query, page);

    EXPECT_EQ(repository.last_query.conversation_id, 5);
    EXPECT_EQ(repository.last_query.sender_id, 9);
    EXPECT_EQ(repository.last_query.from_epoch, 1000);
    EXPECT_EQ(repository.last_query.to_epoch, 2000);
    EXPECT_FALSE(repository.last_query.fuzzy);
    EXPECT_FALSE(repository.last_query.highlight);
    EXPECT_EQ(repository.last_limit, 7);
}

TEST(SearchService, EchoesPaginationInTheEnvelope) {
    FakeMessageSearchRepository repository;
    rtc::features::FeatureFlags flags;
    rtc::services::SearchService service(repository, flags);

    rtc::repositories::MessageSearchQuery query;
    query.term = "hello";
    rtc::dto::Pagination page;
    page.limit = 25;
    page.offset = 50;

    const auto response = service.search_messages(1, query, page);
    EXPECT_EQ(response.at("limit"), 25);
    EXPECT_EQ(response.at("offset"), 50);
}

}  // namespace
