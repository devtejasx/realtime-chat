#include "rtc/controllers/conversation_controller.hpp"

#include <cstdint>

#include <nlohmann/json.hpp>

#include "rtc/dto/conversation_dto.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/json_body.hpp"
#include "rtc/http/response.hpp"

namespace rtc::controllers {
namespace {

// Builds the JSON body for a conversation, including its participants.
[[nodiscard]] nlohmann::json to_body(services::ConversationService& service,
                                     const models::Conversation& conversation) {
    return dto::ConversationResponse::from(conversation, service.participants(conversation.id))
        .to_json();
}

}  // namespace

void ConversationController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/api/conversations")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto request =
                    dto::CreateConversationRequest::from_json(http::parse_json_body(req));
                const auto conversation = conversations_.create(claims.user_id, request);
                return http::json_response(201, to_body(conversations_, conversation));
            });
        });

    CROW_ROUTE(app, "/api/conversations")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto page = dto::Pagination::from_request(req);
                const auto conversations = conversations_.list(claims.user_id, page);
                nlohmann::json items = nlohmann::json::array();
                for (const auto& conversation : conversations) {
                    items.push_back(to_body(conversations_, conversation));
                }
                return http::json_response(200, nlohmann::json{{"conversations", items}});
            });
        });

    CROW_ROUTE(app, "/api/conversations/<int>")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto conversation = conversations_.get(claims.user_id, id);
                return http::json_response(200, to_body(conversations_, conversation));
            });
        });

    CROW_ROUTE(app, "/api/conversations/<int>")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                conversations_.remove(claims.user_id, id);
                return http::json_response(200, nlohmann::json{{"deleted", true}});
            });
        });

    CROW_ROUTE(app, "/api/conversations/<int>/name")
        .methods(crow::HTTPMethod::Patch)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto request =
                    dto::RenameGroupRequest::from_json(http::parse_json_body(req));
                const auto conversation = conversations_.rename_group(claims.user_id, id, request);
                return http::json_response(200, to_body(conversations_, conversation));
            });
        });

    CROW_ROUTE(app, "/api/conversations/<int>/members")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto request = dto::AddMemberRequest::from_json(http::parse_json_body(req));
                const auto participant =
                    conversations_.add_member(claims.user_id, id, request.user_id);
                return http::json_response(201, dto::ParticipantResponse::from(participant).to_json());
            });
        });

    CROW_ROUTE(app, "/api/conversations/<int>/members/<int>")
        .methods(crow::HTTPMethod::Delete)(
            [this](const crow::request& req, std::int64_t id, std::int64_t user_id) {
                return http::run_guarded([&] {
                    const auto claims = auth_guard_.authenticate(req);
                    conversations_.remove_member(claims.user_id, id, user_id);
                    return http::json_response(200, nlohmann::json{{"removed", true}});
                });
            });

    CROW_ROUTE(app, "/api/conversations/<int>/leave")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                conversations_.leave(claims.user_id, id);
                return http::json_response(200, nlohmann::json{{"left", true}});
            });
        });
}

}  // namespace rtc::controllers
