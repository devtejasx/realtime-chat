#include "rtc/controllers/admin_controller.hpp"

#include <charconv>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "rtc/dto/pagination.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/json_body.hpp"
#include "rtc/http/response.hpp"
#include "rtc/http/route_registrar.hpp"
#include "rtc/realtime/events.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::controllers {
namespace {

using security::Permission;

[[nodiscard]] std::optional<std::int64_t> optional_int_param(const crow::request& req,
                                                             const char* name) {
    const char* raw = req.url_params.get(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string_view text(raw);
    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        throw errors::ValidationException(std::string("Invalid integer for parameter: ") + name,
                                          std::string("field=") + name);
    }
    return value;
}

[[nodiscard]] std::optional<std::string> optional_string_param(const crow::request& req,
                                                               const char* name) {
    const char* raw = req.url_params.get(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    return std::string(raw);
}

[[nodiscard]] std::optional<bool> optional_bool_param(const crow::request& req, const char* name) {
    const char* raw = req.url_params.get(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string_view text(raw);
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

// Extracts a required string field from a JSON body.
[[nodiscard]] std::string require_string(const nlohmann::json& body, const char* field) {
    const auto it = body.find(field);
    if (it == body.end() || !it->is_string()) {
        throw errors::ValidationException(std::string("Missing string field: ") + field,
                                          std::string("field=") + field);
    }
    return it->get<std::string>();
}

// Public projection of an administrative user record. Note the deliberate absence
// of password_hash — the admin API must never expose credential material, even to
// a super admin.
[[nodiscard]] nlohmann::json user_to_json(const repositories::AdminUserRecord& record) {
    nlohmann::json out{
        {"id", record.user.id},
        {"username", record.user.username},
        {"email", record.user.email},
        {"role", std::string(security::to_string(record.role))},
        {"banned", record.is_banned()},
        {"created_at", utils::to_iso8601(record.user.created_at)},
        {"updated_at", utils::to_iso8601(record.user.updated_at)},
    };
    out["display_name"] = record.user.display_name.has_value()
                              ? nlohmann::json(*record.user.display_name)
                              : nlohmann::json();
    out["banned_at"] = record.banned_at.has_value()
                           ? nlohmann::json(utils::to_iso8601(*record.banned_at))
                           : nlohmann::json();
    out["ban_reason"] =
        record.ban_reason.has_value() ? nlohmann::json(*record.ban_reason) : nlohmann::json();
    out["banned_by"] =
        record.banned_by.has_value() ? nlohmann::json(*record.banned_by) : nlohmann::json();
    return out;
}

// Builds the audit filter shared by the search and summary endpoints.
[[nodiscard]] repositories::AuditLogFilter audit_filter_from(const crow::request& req) {
    repositories::AuditLogFilter filter;
    filter.actor_id = optional_int_param(req, "actor_id");
    filter.event_type = optional_string_param(req, "event_type");
    filter.target_type = optional_string_param(req, "target_type");
    filter.target_id = optional_string_param(req, "target_id");
    filter.correlation_id = optional_string_param(req, "correlation_id");
    filter.from_epoch = optional_int_param(req, "from");
    filter.to_epoch = optional_int_param(req, "to");
    return filter;
}

}  // namespace

std::int64_t AdminController::authorize(const crow::request& req,
                                        security::Permission permission) const {
    const auto claims = deps_.auth_guard->authenticate(req);
    deps_.authorization->require_permission(claims.user_id, permission);
    return claims.user_id;
}

void AdminController::register_routes(http::App& app) {
    register_user_routes(app);
    register_group_routes(app);
    register_operations_routes(app);
}

void AdminController::register_user_routes(http::App& app) {
    RTC_API_ROUTE(app, "/admin/users")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kManageUsers);

                repositories::AdminUserFilter filter;
                filter.query = optional_string_param(req, "q");
                filter.banned = optional_bool_param(req, "banned");
                if (const auto role = optional_string_param(req, "role"); role.has_value()) {
                    const auto parsed = security::parse_role(*role);
                    if (!parsed) {
                        throw errors::ValidationException("Unknown role: " + *role, "field=role");
                    }
                    filter.role = *parsed;
                }

                const auto page = dto::Pagination::from_request(req);
                nlohmann::json items = nlohmann::json::array();
                for (const auto& record : deps_.users->list(filter, page)) {
                    items.push_back(user_to_json(record));
                }
                return http::json_response(200,
                                           nlohmann::json{{"total", deps_.users->count(filter)},
                                                          {"limit", page.limit},
                                                          {"offset", page.offset},
                                                          {"users", std::move(items)}});
            });
        });

    RTC_API_ROUTE(app, "/admin/users/<int>")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req, std::int64_t user_id) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kManageUsers);
                const auto record = deps_.users->find(user_id);
                if (!record) {
                    throw errors::NotFoundException("User not found",
                                                    "user_id=" + std::to_string(user_id));
                }
                return http::json_response(200, user_to_json(*record));
            });
        });

    RTC_API_ROUTE(app, "/admin/users/<int>/role")
        .methods(crow::HTTPMethod::Put)([this](const crow::request& req, std::int64_t user_id) {
            return http::run_guarded([&] {
                const std::int64_t actor_id = authorize(req, Permission::kManageRoles);

                const auto body = http::parse_json_body(req);
                const std::string requested = require_string(body, "role");
                const auto target_role = security::parse_role(requested);
                if (!target_role) {
                    throw errors::ValidationException("Unknown role: " + requested, "field=role");
                }

                // Privilege-escalation guard: nobody may grant a role at or above
                // their own tier. Without this, an account with manage_roles could
                // mint a peer (or promote itself) and the role hierarchy would be
                // decorative.
                const security::Role actor_role = deps_.authorization->role_of(actor_id);
                if (!security::can_assign_role(actor_role, *target_role)) {
                    throw errors::AuthorizationException(
                        "Cannot assign a role at or above your own",
                        "actor_role=" + std::string(security::to_string(actor_role)) +
                            " target_role=" + std::string(security::to_string(*target_role)));
                }

                const security::Role previous = deps_.users->set_role(user_id, *target_role);
                // The cached decision must be dropped immediately, or the change
                // would not take effect for up to the cache TTL.
                deps_.authorization->invalidate(user_id);

                const events::UserRoleChanged user_role_changed_event{
                    .user_id = user_id,
                    .actor_id = actor_id,
                    .previous_role = std::string(security::to_string(previous)),
                    .new_role = std::string(security::to_string(*target_role)),
                };
                deps_.publisher->publish(user_role_changed_event.to_event());

                return http::json_response(
                    200,
                    nlohmann::json{{"user_id", user_id},
                                   {"previous_role", security::to_string(previous)},
                                   {"role", security::to_string(*target_role)}});
            });
        });

    RTC_API_ROUTE(app, "/admin/users/<int>/ban")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req, std::int64_t user_id) {
            return http::run_guarded([&] {
                const std::int64_t actor_id = authorize(req, Permission::kBanUsers);
                if (actor_id == user_id) {
                    throw errors::ValidationException("You cannot suspend your own account");
                }
                // An administrator must not be able to suspend a peer or a
                // superior — same escalation reasoning as role assignment.
                const security::Role actor_role = deps_.authorization->role_of(actor_id);
                const security::Role target_role = deps_.authorization->role_of(user_id);
                if (static_cast<int>(target_role) >= static_cast<int>(actor_role)) {
                    throw errors::AuthorizationException(
                        "Cannot suspend an account at or above your own role");
                }

                const auto body = http::parse_json_body(req);
                std::optional<std::string> reason;
                if (const auto it = body.find("reason"); it != body.end() && it->is_string()) {
                    reason = it->get<std::string>();
                }

                deps_.users->set_banned(user_id, /*banned=*/true, reason, actor_id);
                deps_.authorization->invalidate(user_id);
                // Revoke refresh tokens too: without this the account stays
                // suspended for API calls but could still mint new access tokens.
                const std::int64_t revoked = deps_.sessions->revoke_all(user_id);

                const events::AdminAction admin_action_event{
                    .actor_id = actor_id,
                    .action = "user.ban",
                    .target_type = "user",
                    .target_id = std::to_string(user_id),
                    .details = {{"reason", reason.value_or("")}, {"sessions_revoked", revoked}},
                };
                deps_.publisher->publish(admin_action_event.to_event());

                return http::json_response(
                    200,
                    nlohmann::json{
                        {"user_id", user_id}, {"banned", true}, {"sessions_revoked", revoked}});
            });
        });

    RTC_API_ROUTE(app, "/admin/users/<int>/unban")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req, std::int64_t user_id) {
            return http::run_guarded([&] {
                const std::int64_t actor_id = authorize(req, Permission::kBanUsers);
                deps_.users->set_banned(user_id, /*banned=*/false, std::nullopt, std::nullopt);
                deps_.authorization->invalidate(user_id);

                const events::AdminAction admin_action_event{
                    .actor_id = actor_id,
                    .action = "user.unban",
                    .target_type = "user",
                    .target_id = std::to_string(user_id),
                };
                deps_.publisher->publish(admin_action_event.to_event());

                return http::json_response(200,
                                           nlohmann::json{{"user_id", user_id}, {"banned", false}});
            });
        });

    RTC_API_ROUTE(app, "/admin/users/<int>/sessions")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req, std::int64_t user_id) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kViewSessions);
                nlohmann::json items = nlohmann::json::array();
                for (const auto& session : deps_.sessions->list(user_id)) {
                    items.push_back({
                        {"id", session.id},
                        {"created_at", utils::to_iso8601(session.created_at)},
                        {"last_used_at", utils::to_iso8601(session.last_used_at)},
                        {"expires_at", utils::to_iso8601(session.expires_at)},
                        {"user_agent", session.user_agent.value_or("")},
                        {"ip", session.ip.value_or("")},
                    });
                }
                return http::json_response(
                    200, nlohmann::json{{"user_id", user_id}, {"sessions", std::move(items)}});
            });
        });

    RTC_API_ROUTE(app, "/admin/users/<int>/sessions")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req, std::int64_t user_id) {
            return http::run_guarded([&] {
                const std::int64_t actor_id = authorize(req, Permission::kRevokeSessions);
                const std::int64_t revoked = deps_.sessions->revoke_all(user_id);

                const events::AdminAction admin_action_event{
                    .actor_id = actor_id,
                    .action = "session.revoke_all",
                    .target_type = "user",
                    .target_id = std::to_string(user_id),
                    .details = {{"revoked", revoked}},
                };
                deps_.publisher->publish(admin_action_event.to_event());

                return http::json_response(
                    200, nlohmann::json{{"user_id", user_id}, {"revoked", revoked}});
            });
        });
}

void AdminController::register_group_routes(http::App& app) {
    RTC_API_ROUTE(app, "/admin/conversations/<int>")
        .methods(crow::HTTPMethod::Get)(
            [this](const crow::request& req, std::int64_t conversation_id) {
                return http::run_guarded([&] {
                    (void) authorize(req, Permission::kViewAnyConversation);
                    const auto conversation = deps_.conversations->find_by_id(conversation_id);
                    if (!conversation) {
                        throw errors::NotFoundException(
                            "Conversation not found",
                            "conversation_id=" + std::to_string(conversation_id));
                    }

                    nlohmann::json participants = nlohmann::json::array();
                    for (const auto& participant :
                         deps_.conversations->list_participants(conversation_id)) {
                        participants.push_back(
                            {{"user_id", participant.user_id},
                             {"role", models::to_string(participant.role)},
                             {"joined_at", utils::to_iso8601(participant.joined_at)}});
                    }

                    return http::json_response(
                        200,
                        nlohmann::json{{"id", conversation->id},
                                       {"is_group", conversation->is_group()},
                                       {"name", conversation->name.value_or("")},
                                       {"owner_id",
                                        conversation->owner_id.has_value()
                                            ? nlohmann::json(*conversation->owner_id)
                                            : nlohmann::json()},
                                       {"created_at", utils::to_iso8601(conversation->created_at)},
                                       {"participants", std::move(participants)}});
                });
            });

    RTC_API_ROUTE(app, "/admin/conversations/<int>")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req,
                                                  std::int64_t conversation_id) {
            return http::run_guarded([&] {
                const std::int64_t actor_id = authorize(req, Permission::kManageGroups);
                const auto conversation = deps_.conversations->find_by_id(conversation_id);
                if (!conversation) {
                    throw errors::NotFoundException(
                        "Conversation not found",
                        "conversation_id=" + std::to_string(conversation_id));
                }
                // Capture the audience before deleting: after the row is gone the
                // participant list is unrecoverable.
                const auto participants =
                    deps_.conversations->list_participant_ids(conversation_id);
                deps_.conversations->remove(conversation_id);

                deps_.connections->publish(participants,
                                           realtime::events::kConversationDeleted,
                                           nlohmann::json{{"conversation_id", conversation_id}});

                const events::AdminAction admin_action_event{
                    .actor_id = actor_id,
                    .action = "conversation.delete",
                    .target_type = "conversation",
                    .target_id = std::to_string(conversation_id),
                    .details = {{"participant_count", participants.size()}},
                };
                deps_.publisher->publish(admin_action_event.to_event());

                return http::json_response(
                    200, nlohmann::json{{"conversation_id", conversation_id}, {"deleted", true}});
            });
        });
}

void AdminController::register_operations_routes(http::App& app) {
    RTC_API_ROUTE(app, "/admin/websockets")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kViewSystemMetrics);

                nlohmann::json sessions = nlohmann::json::array();
                for (const auto& session : deps_.connections->sessions().snapshot()) {
                    sessions.push_back(
                        {{"user_id", session->user_id},
                         {"username", session->username},
                         {"protocol_version",
                          realtime::protocol::to_number(session->protocol_version)},
                         {"last_activity_ms",
                          session->last_activity_ms.load(std::memory_order_relaxed)}});
                }

                // Counts are per-instance by nature: this process only knows its
                // own sockets. The node id makes that explicit so an operator
                // aggregating across replicas knows what they are looking at.
                const auto* cluster = deps_.connections->cluster_bus();
                return http::json_response(
                    200,
                    nlohmann::json{
                        {"node_id", cluster != nullptr ? std::string(cluster->node_id()) : "local"},
                        {"distributed", cluster != nullptr && cluster->is_distributed()},
                        {"cluster_published", cluster != nullptr ? cluster->published_count() : 0},
                        {"cluster_received", cluster != nullptr ? cluster->received_count() : 0},
                        {"connection_count", deps_.connections->sessions().session_count()},
                        {"connections", std::move(sessions)}});
            });
        });

    RTC_API_ROUTE(app, "/admin/cache")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kViewSystemMetrics);
                return http::json_response(
                    200,
                    nlohmann::json{{"backend", deps_.cache->store().backend_name()},
                                   {"hits", deps_.cache->hits()},
                                   {"misses", deps_.cache->misses()},
                                   {"hit_ratio", deps_.cache->hit_ratio()}});
            });
        });

    RTC_API_ROUTE(app, "/admin/jobs")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kViewSystemMetrics);
                return http::json_response(
                    200,
                    nlohmann::json{
                        {"workers", deps_.executor->worker_count()},
                        {"pending", deps_.executor->pending()},
                        {"completed", deps_.executor->completed()},
                        {"failed", deps_.executor->failed()},
                        {"event_bus",
                         {{"asynchronous", deps_.event_bus->is_asynchronous()},
                          {"subscribers", deps_.event_bus->subscriber_count()},
                          {"published", deps_.event_bus->published_count()},
                          {"dropped", deps_.event_bus->dropped_count()},
                          {"dispatched", deps_.event_bus->dispatcher().dispatched_count()},
                          {"handler_failures",
                           deps_.event_bus->dispatcher().handler_failure_count()}}}});
            });
        });

    RTC_API_ROUTE(app, "/admin/system")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kViewSystemMetrics);

                nlohmann::json roles = nlohmann::json::array();
                for (const auto& [role, count] : deps_.users->counts_by_role()) {
                    roles.push_back({{"role", security::to_string(role)}, {"count", count}});
                }

                return http::json_response(
                    200,
                    nlohmann::json{
                        {"version", RTC_VERSION},
                        {"uptime_seconds", deps_.metrics->uptime_seconds()},
                        {"http_requests_total", deps_.metrics->counter("rtc_http_requests_total")},
                        {"http_4xx_total", deps_.metrics->counter("rtc_http_4xx_total")},
                        {"http_5xx_total", deps_.metrics->counter("rtc_http_5xx_total")},
                        {"websocket_connections", deps_.connections->sessions().session_count()},
                        {"users_by_role", std::move(roles)}});
            });
        });

    RTC_API_ROUTE(app, "/admin/audit-logs")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kViewAuditLogs);
                const auto filter = audit_filter_from(req);
                const auto page = dto::Pagination::from_request(req);

                nlohmann::json items = nlohmann::json::array();
                for (const auto& record : deps_.audit->search(filter, page)) {
                    items.push_back(services::AuditService::to_json(record));
                }
                return http::json_response(200,
                                           nlohmann::json{{"total", deps_.audit->count(filter)},
                                                          {"limit", page.limit},
                                                          {"offset", page.offset},
                                                          {"records", std::move(items)}});
            });
        });

    RTC_API_ROUTE(app, "/admin/audit-logs/summary")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kViewAuditLogs);
                return http::json_response(200, deps_.audit->summary(audit_filter_from(req)));
            });
        });

    RTC_API_ROUTE(app, "/admin/features")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                (void) authorize(req, Permission::kViewSystemMetrics);
                return http::json_response(200,
                                           nlohmann::json{{"features", deps_.features->to_json()}});
            });
        });

    RTC_API_ROUTE(app, "/admin/features/<string>")
        .methods(crow::HTTPMethod::Put)([this](const crow::request& req, const std::string& name) {
            return http::run_guarded([&] {
                const std::int64_t actor_id = authorize(req, Permission::kManageFeatureFlags);

                const auto feature = features::parse_feature(name);
                if (!feature) {
                    throw errors::NotFoundException("Unknown feature flag: " + name,
                                                    "feature=" + name);
                }
                const auto body = http::parse_json_body(req);
                const auto enabled_it = body.find("enabled");
                if (enabled_it == body.end() || !enabled_it->is_boolean()) {
                    throw errors::ValidationException("Missing boolean field: enabled",
                                                      "field=enabled");
                }
                const bool enabled = enabled_it->get<bool>();
                const bool previous = deps_.features->set(*feature, enabled);

                const events::AdminAction admin_action_event{
                    .actor_id = actor_id,
                    .action = "feature.toggle",
                    .target_type = "feature",
                    .target_id = name,
                    .details = {{"previous", previous}, {"enabled", enabled}},
                };
                deps_.publisher->publish(admin_action_event.to_event());

                return http::json_response(
                    200,
                    nlohmann::json{
                        {"feature", name}, {"previous", previous}, {"enabled", enabled}});
            });
        });
}

}  // namespace rtc::controllers
