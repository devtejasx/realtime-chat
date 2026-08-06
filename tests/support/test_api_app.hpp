#pragma once

#include <crow/http_request.h>
#include <crow/http_response.h>

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "rtc/cache/in_memory_cache_store.hpp"
#include "rtc/config/config.hpp"
#include "rtc/controllers/api_fallback_controller.hpp"
#include "rtc/controllers/auth_controller.hpp"
#include "rtc/controllers/conversation_controller.hpp"
#include "rtc/controllers/health_controller.hpp"
#include "rtc/controllers/message_controller.hpp"
#include "rtc/controllers/reaction_controller.hpp"
#include "rtc/controllers/search_controller.hpp"
#include "rtc/controllers/session_controller.hpp"
#include "rtc/controllers/user_controller.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/features/feature_flags.hpp"
#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/notifications/notification_dispatcher.hpp"
#include "rtc/ratelimit/rate_limiter.hpp"
#include "rtc/repositories/message_search_repository.hpp"
#include "rtc/security/jwt_token_service.hpp"
#include "rtc/services/attachment_linker.hpp"
#include "rtc/services/auth_service.hpp"
#include "rtc/services/conversation_service.hpp"
#include "rtc/services/message_service.hpp"
#include "rtc/services/reaction_service.hpp"
#include "rtc/services/search_service.hpp"
#include "rtc/services/session_service.hpp"
#include "rtc/services/user_service.hpp"
#include "support/fake_conversation_repository.hpp"
#include "support/fake_message_repository.hpp"
#include "support/fake_password_hasher.hpp"
#include "support/fake_reaction_repository.hpp"
#include "support/fake_session_repository.hpp"
#include "support/fake_user_repository.hpp"
#include "support/recording_broadcaster.hpp"

namespace rtc::testing {

// Returns canned hits so the HTTP layer can be exercised without PostgreSQL's
// full-text machinery. Mirrors the fake in search_service_test.cpp; kept here so
// the API-level tests do not depend on that file's internals.
class StubMessageSearchRepository final : public repositories::IMessageSearchRepository {
  public:
    [[nodiscard]] std::vector<repositories::MessageSearchHit> search(
        std::int64_t, const repositories::MessageSearchQuery&, const dto::Pagination&) override {
        return hits;
    }

    [[nodiscard]] std::int64_t count(std::int64_t,
                                     const repositories::MessageSearchQuery&) override {
        return static_cast<std::int64_t>(hits.size());
    }

    [[nodiscard]] bool fuzzy_available() override { return true; }

    std::vector<repositories::MessageSearchHit> hits;
};

// A fully wired HTTP surface backed by in-memory fakes.
//
// Composes the same controllers the production composition root does — over fake
// repositories rather than PostgreSQL — so tests can drive the real route table,
// the real middleware stack and the real DTO parsing without any external
// dependency. Requests go through Crow's handle_full(), which runs
// handle_initial() (routing) and then handle() (middleware + handler) in exactly
// the order crow::http_connection uses on a live socket. That ordering is the
// whole point: the versioned-route regression lived in the gap between those two
// calls, so a test that skipped routing would not have caught it.
//
// Deliberately mirrors the production registration *order*, including the
// terminal ApiFallbackController, because Crow resolves ambiguity by lowest rule
// index — a fixture that registered them differently would not be testing the
// route table the service actually serves.
class TestApiApp {
  public:
    TestApiApp() {
        conversations_service_.set_event_publisher(publisher_);
        messages_service_.set_event_publisher(publisher_);

        health_.register_routes(app_);
        auth_.register_routes(app_);
        users_.register_routes(app_);
        conversations_.register_routes(app_);
        messages_.register_routes(app_);
        reactions_.register_routes(app_);
        sessions_.register_routes(app_);
        search_.register_routes(app_);
        controllers::ApiFallbackController::register_routes(app_);
        app_.validate();
    }

    // Drives one request through the real route table.
    //
    // Note on scope: handle_full() performs handle_initial() (routing) followed
    // by the rule dispatch, which is what makes it a faithful guard for route
    // registration. It does *not* run the global middleware chain — Crow invokes
    // that from http_connection, not from the app — so assertions about
    // X-API-Version or the unsupported-version rejection belong in the live
    // socket suite (http_server_live_test.cpp) instead.
    crow::response request(crow::HTTPMethod method,
                           const std::string& url,
                           const std::string& body = {},
                           const std::string& bearer = {}) {
        crow::request req;
        req.method = method;
        req.raw_url = url;
        // A live connection hands the router a path with the query string
        // already split off. Reproducing that here matters: leaving "?q=..."
        // attached makes every query-bearing route miss.
        const auto query_at = url.find('?');
        if (query_at == std::string::npos) {
            req.url = url;
        } else {
            req.url = url.substr(0, query_at);
            req.url_params = crow::query_string(url);
        }
        req.body = body;
        if (!body.empty()) {
            req.headers.emplace("Content-Type", "application/json");
        }
        if (!bearer.empty()) {
            req.headers.emplace("Authorization", "Bearer " + bearer);
        }
        crow::response res;
        app_.handle_full(req, res);
        return res;
    }

    [[nodiscard]] static nlohmann::json body_of(const crow::response& res) {
        return nlohmann::json::parse(res.body, nullptr, /*allow_exceptions=*/false);
    }

    // Registers a user and returns its access token.
    std::string register_user(const std::string& username, const std::string& email) {
        const auto res = request(
            crow::HTTPMethod::Post,
            "/api/v1/auth/register",
            nlohmann::json{
                {"username", username}, {"email", email}, {"password", "correct-horse-battery"}}
                .dump());
        const auto json = body_of(res);
        if (json.is_discarded() || !json.contains("tokens")) {
            return {};
        }
        return json.at("tokens").at("access_token").get<std::string>();
    }

    http::App& app() noexcept { return app_; }
    StubMessageSearchRepository& search_repository() noexcept { return search_repo_; }

  private:
    config::Config config_{};
    http::App app_{};

    FakeUserRepository user_repo_{};
    FakePasswordHasher hasher_{};
    FakeSessionRepository session_repo_{};
    FakeConversationRepository conversation_repo_{};
    FakeMessageRepository message_repo_{};
    FakeReactionRepository reaction_repo_{};
    StubMessageSearchRepository search_repo_{};

    RecordingBroadcaster broadcaster_{};
    notifications::NullNotificationDispatcher dispatcher_{};
    services::NullAttachmentLinker attachments_{};
    events::NullEventPublisher& publisher_{events::NullEventPublisher::instance()};
    features::FeatureFlags flags_{};

    security::JwtTokenService tokens_{security::JwtTokenService::Options{
        .secret = "http-integration-test-secret", .issuer = "realtime-chat-test"}};

    services::UserService user_service_{user_repo_, hasher_};
    services::AuthService auth_service_{user_service_, tokens_};
    services::SessionService session_service_{session_repo_, tokens_, 1209600};
    services::ConversationService conversations_service_{
        conversation_repo_, user_repo_, broadcaster_, dispatcher_};
    services::MessageService messages_service_{
        message_repo_, conversation_repo_, broadcaster_, dispatcher_, attachments_};
    services::ReactionService reactions_service_{
        reaction_repo_, message_repo_, conversation_repo_, broadcaster_, dispatcher_};
    services::SearchService search_service_{search_repo_, flags_};

    middlewares::AuthMiddleware guard_{tokens_};

    // Disabled: these tests drive many requests through the same in-process
    // limiter, and the default per-window budgets would start returning 429 part
    // way through a suite. Enforcement itself is covered by rate_limiter_test.
    cache::InMemoryCacheStore rate_limit_store_{};
    ratelimit::RateLimiter rate_limiter_{rate_limit_store_, /*enabled=*/false};

    controllers::HealthController health_{config_};
    controllers::AuthController auth_{
        auth_service_, user_service_, session_service_, guard_, rate_limiter_, config_};
    controllers::UserController users_{user_service_, guard_};
    controllers::ConversationController conversations_{conversations_service_, guard_};
    controllers::MessageController messages_{messages_service_, guard_, rate_limiter_, config_};
    controllers::ReactionController reactions_{reactions_service_, guard_};
    controllers::SessionController sessions_{session_service_, guard_};
    controllers::SearchController search_{search_service_, guard_};
};

}  // namespace rtc::testing
