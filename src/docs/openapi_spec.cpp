#include <nlohmann/json.hpp>
#include <string>

#include "rtc/docs/openapi.hpp"

namespace rtc::docs {
namespace {

// The OpenAPI 3.1 document, as a raw string literal.
//
// Written by hand rather than generated from the route table. Crow registers
// routes through a macro that takes a compile-time string, so there is no
// runtime-introspectable route registry to generate from; a generator would have
// to re-declare every schema anyway. What matters is that the two stay in step,
// which tests/openapi_spec_test.cpp enforces: it parses this document and asserts
// that every path the controllers register is described here.
constexpr const char* kOpenApiJson = R"OPENAPI({
  "openapi": "3.1.0",
  "info": {
    "title": "Realtime Chat API",
    "version": "0.1.0",
    "summary": "Distributed real-time messaging platform",
    "description": "REST and WebSocket API for the realtime-chat backend.\n\n## Versioning\n\nEvery endpoint is reachable at two paths:\n\n* `/api/v1/...` — the explicit, recommended form for new clients.\n* `/api/...` — the original unversioned form, permanently supported and treated as an alias for v1.\n\nResponses carry `X-API-Version`. Requesting an unsupported version (`/api/v9/...`) returns 404 with an `unsupported_api_version` error naming the versions this build supports.\n\n## Errors\n\nEvery failure uses one envelope:\n\n```json\n{ \"error\": { \"code\": \"validation_error\", \"message\": \"...\", \"details\": \"field=username\" } }\n```\n\n`code` is stable and machine-readable; `details` is present only when there is safe extra context.\n\n## Authentication\n\nAll endpoints except registration, login, refresh, health and metrics require `Authorization: Bearer <access_token>`. Access tokens are short-lived (15 minutes by default); use `POST /api/v1/auth/refresh` with the refresh token and session id to rotate.\n\n## Correlation\n\nSend `X-Request-Id` to correlate your request with server logs, or read the one the server generates from the response header. Send W3C `traceparent` to join the server span to your trace; the response echoes it.",
    "license": { "name": "MIT", "identifier": "MIT" }
  },
  "servers": [
    { "url": "http://localhost:8080", "description": "Local development" }
  ],
  "tags": [
    { "name": "Authentication", "description": "Registration, login, token rotation and session revocation" },
    { "name": "Users", "description": "Profile read and update" },
    { "name": "Conversations", "description": "Direct conversations and group chats" },
    { "name": "Messages", "description": "Sending, listing, editing and deleting messages" },
    { "name": "Reactions", "description": "Emoji reactions on messages" },
    { "name": "Attachments", "description": "File upload, download and thumbnails" },
    { "name": "Notifications", "description": "In-app notification inbox" },
    { "name": "Sessions", "description": "The caller's own active sessions" },
    { "name": "Search", "description": "PostgreSQL full-text search over messages" },
    { "name": "Admin", "description": "Administrative operations. Each route requires a specific permission." },
    { "name": "Operations", "description": "Health probes and Prometheus metrics" }
  ],
  "components": {
    "securitySchemes": {
      "bearerAuth": {
        "type": "http",
        "scheme": "bearer",
        "bearerFormat": "JWT",
        "description": "JWT access token issued by /api/v1/auth/login or /api/v1/auth/register."
      }
    },
    "parameters": {
      "Limit": {
        "name": "limit", "in": "query", "required": false,
        "description": "Page size, clamped to [1, 100].",
        "schema": { "type": "integer", "minimum": 1, "maximum": 100, "default": 50 }
      },
      "Offset": {
        "name": "offset", "in": "query", "required": false,
        "description": "Rows to skip. Prefer keyset pagination (`before`/`after`) for deep pages.",
        "schema": { "type": "integer", "minimum": 0, "default": 0 }
      },
      "Before": {
        "name": "before", "in": "query", "required": false,
        "description": "Keyset cursor: return records with id strictly less than this.",
        "schema": { "type": "integer", "format": "int64" }
      },
      "After": {
        "name": "after", "in": "query", "required": false,
        "description": "Keyset cursor: return records with id strictly greater than this.",
        "schema": { "type": "integer", "format": "int64" }
      },
      "RequestId": {
        "name": "X-Request-Id", "in": "header", "required": false,
        "description": "Client-supplied correlation id, echoed on the response and recorded in logs and traces.",
        "schema": { "type": "string", "maxLength": 128 }
      }
    },
    "schemas": {
      "Error": {
        "type": "object",
        "required": ["error"],
        "properties": {
          "error": {
            "type": "object",
            "required": ["code", "message"],
            "properties": {
              "code": {
                "type": "string",
                "description": "Stable machine-readable code.",
                "enum": ["validation_error", "authentication_error", "authorization_error",
                         "not_found", "conflict", "payload_too_large", "unsupported_media_type",
                         "rate_limited", "database_error", "configuration_error", "internal_error",
                         "unsupported_api_version"]
              },
              "message": { "type": "string" },
              "details": { "type": "string", "description": "Safe additional context, when available." }
            }
          }
        },
        "examples": [
          { "error": { "code": "validation_error", "message": "Username is required", "details": "field=username" } }
        ]
      },
      "User": {
        "type": "object",
        "required": ["id", "username", "email", "created_at"],
        "properties": {
          "id": { "type": "integer", "format": "int64" },
          "username": { "type": "string", "minLength": 3, "maxLength": 32 },
          "email": { "type": "string", "format": "email" },
          "display_name": { "type": ["string", "null"] },
          "bio": { "type": ["string", "null"] },
          "avatar_url": { "type": ["string", "null"], "format": "uri" },
          "created_at": { "type": "string", "format": "date-time" },
          "updated_at": { "type": "string", "format": "date-time" }
        },
        "examples": [
          { "id": 1, "username": "ada", "email": "ada@example.com", "display_name": "Ada L.",
            "bio": null, "avatar_url": null,
            "created_at": "2026-07-30T09:00:00Z", "updated_at": "2026-07-30T09:00:00Z" }
        ]
      },
      "RegisterRequest": {
        "type": "object",
        "required": ["username", "email", "password"],
        "properties": {
          "username": { "type": "string", "minLength": 3, "maxLength": 32,
                        "description": "Alphanumeric plus underscore." },
          "email": { "type": "string", "format": "email" },
          "password": { "type": "string", "minLength": 8, "format": "password" }
        },
        "examples": [{ "username": "ada", "email": "ada@example.com", "password": "correct-horse-battery" }]
      },
      "LoginRequest": {
        "type": "object",
        "description": "The account is named by exactly one of `identifier`, `username` or `email` — they are accepted interchangeably and resolved against both the username and email columns. `identifier` is the canonical spelling; the other two exist for clients that model the field by what they collected. Supplying none of them is a validation error.",
        "required": ["password"],
        "properties": {
          "identifier": { "type": "string", "description": "Username or email. Preferred." },
          "username": { "type": "string", "description": "Alternative to `identifier`." },
          "email": { "type": "string", "format": "email", "description": "Alternative to `identifier`." },
          "password": { "type": "string", "format": "password" }
        },
        "anyOf": [
          { "required": ["identifier"] },
          { "required": ["username"] },
          { "required": ["email"] }
        ],
        "examples": [{ "identifier": "ada", "password": "correct-horse-battery" }]
      },
      "TokenPair": {
        "type": "object",
        "required": ["access_token", "refresh_token", "token_type"],
        "properties": {
          "access_token": { "type": "string" },
          "refresh_token": { "type": "string" },
          "token_type": { "type": "string", "const": "Bearer" },
          "access_expires_in": { "type": "integer", "description": "Access token lifetime in seconds." },
          "refresh_expires_in": { "type": "integer", "description": "Refresh token lifetime in seconds." }
        }
      },
      "AuthResponse": {
        "type": "object",
        "required": ["user", "tokens", "session_id"],
        "properties": {
          "user": { "$ref": "#/components/schemas/User" },
          "tokens": { "$ref": "#/components/schemas/TokenPair" },
          "session_id": { "type": "string", "description": "Pass to /auth/refresh and /auth/logout." }
        }
      },
      "RefreshRequest": {
        "type": "object",
        "required": ["refresh_token", "session_id"],
        "properties": {
          "refresh_token": { "type": "string" },
          "session_id": { "type": "string" }
        }
      },
      "UpdateProfileRequest": {
        "type": "object",
        "description": "Only the supplied fields are changed. An explicit null clears the field.",
        "properties": {
          "display_name": { "type": ["string", "null"], "maxLength": 64 },
          "bio": { "type": ["string", "null"], "maxLength": 280 },
          "avatar_url": { "type": ["string", "null"], "format": "uri" }
        }
      },
      "ConversationParticipant": {
        "type": "object",
        "required": ["user_id", "role", "joined_at"],
        "properties": {
          "user_id": { "type": "integer", "format": "int64" },
          "role": { "type": "string", "enum": ["member", "owner"],
                    "description": "Membership role within this conversation, distinct from the account-level RBAC role." },
          "joined_at": { "type": "string", "format": "date-time" }
        }
      },
      "Conversation": {
        "type": "object",
        "required": ["id", "type", "created_at", "updated_at", "participants"],
        "properties": {
          "id": { "type": "integer", "format": "int64" },
          "type": { "type": "string", "enum": ["direct", "group"] },
          "name": { "type": ["string", "null"], "description": "Group name; null for direct." },
          "owner_id": { "type": ["integer", "null"], "format": "int64",
                        "description": "Group owner; null for direct conversations." },
          "participants": {
            "type": "array",
            "description": "Full membership records, not bare ids.",
            "items": { "$ref": "#/components/schemas/ConversationParticipant" }
          },
          "created_at": { "type": "string", "format": "date-time" },
          "updated_at": { "type": "string", "format": "date-time" },
          "last_message_at": { "type": ["string", "null"], "format": "date-time" }
        }
      },
      "CreateConversationRequest": {
        "type": "object",
        "description": "`type` selects the shape and is always required. A direct conversation takes exactly one other participant and ignores `name`; a group takes `name` plus its initial members. The caller is always added implicitly and must not be listed in `participant_ids`. Creating a direct conversation is idempotent — an existing one between the same two users is returned rather than duplicated.",
        "required": ["type", "participant_ids"],
        "properties": {
          "type": { "type": "string", "enum": ["direct", "group"] },
          "participant_ids": {
            "type": "array",
            "description": "Other participants, excluding the caller. Exactly one entry when type is `direct`.",
            "items": { "type": "integer", "format": "int64" }
          },
          "name": { "type": "string", "maxLength": 100,
                    "description": "Group name. Ignored when type is `direct`." }
        },
        "examples": [
          { "type": "direct", "participant_ids": [2] },
          { "type": "group", "name": "release-team", "participant_ids": [2, 3, 4] }
        ]
      },
      "Message": {
        "type": "object",
        "required": ["id", "conversation_id", "sender_id", "content", "type", "created_at",
                     "updated_at", "deleted", "edited"],
        "properties": {
          "id": { "type": "integer", "format": "int64" },
          "conversation_id": { "type": "integer", "format": "int64" },
          "sender_id": { "type": "integer", "format": "int64" },
          "type": { "type": "string", "enum": ["text", "system"] },
          "content": { "type": "string", "maxLength": 4000,
                       "description": "Emptied when the message is soft-deleted." },
          "attachment_ids": { "type": "array", "items": { "type": "integer", "format": "int64" } },
          "created_at": { "type": "string", "format": "date-time" },
          "updated_at": { "type": "string", "format": "date-time" },
          "deleted": { "type": "boolean",
                       "description": "Soft-delete flag. The row is retained so conversation history stays contiguous." },
          "edited": { "type": "boolean", "description": "True once the content has been changed." },
          "edited_at": { "type": ["string", "null"], "format": "date-time",
                         "description": "Null until the first edit." }
        }
      },
      "SendMessageRequest": {
        "type": "object",
        "required": ["conversation_id", "content"],
        "properties": {
          "conversation_id": { "type": "integer", "format": "int64" },
          "content": { "type": "string", "minLength": 1, "maxLength": 4000 },
          "type": { "type": "string", "enum": ["text", "system"], "default": "text" },
          "attachment_ids": { "type": "array", "items": { "type": "integer", "format": "int64" },
                              "description": "Ids from POST /attachments, owned by the caller." }
        },
        "examples": [{ "conversation_id": 7, "content": "Ship it." }]
      },
      "UpdateMessageRequest": {
        "type": "object",
        "required": ["content"],
        "properties": { "content": { "type": "string", "minLength": 1, "maxLength": 4000 } }
      },
      "Reaction": {
        "type": "object",
        "required": ["message_id", "user_id", "emoji"],
        "properties": {
          "message_id": { "type": "integer", "format": "int64" },
          "user_id": { "type": "integer", "format": "int64" },
          "emoji": { "type": "string", "enum": ["👍", "❤️", "😂", "😮", "😢", "👏", "🔥"] },
          "created_at": { "type": "string", "format": "date-time" }
        }
      },
      "ReactionRequest": {
        "type": "object",
        "description": "Reactions are restricted to a fixed palette so every client can render them, and anything outside the enum is rejected with a validation error naming `emoji`. Send the literal emoji character, not a shortcode such as `:thumbsup:`.",
        "required": ["emoji"],
        "properties": { "emoji": { "type": "string", "enum": ["👍", "❤️", "😂", "😮", "😢", "👏", "🔥"] } },
        "examples": [{ "emoji": "👍" }]
      },
      "Attachment": {
        "type": "object",
        "properties": {
          "id": { "type": "integer", "format": "int64" },
          "owner_id": { "type": "integer", "format": "int64" },
          "message_id": { "type": ["integer", "null"], "format": "int64" },
          "filename": { "type": "string" },
          "content_type": { "type": "string" },
          "size_bytes": { "type": "integer", "format": "int64" },
          "url": { "type": "string" },
          "thumbnail_url": { "type": ["string", "null"] },
          "created_at": { "type": "string", "format": "date-time" }
        }
      },
      "Notification": {
        "type": "object",
        "properties": {
          "id": { "type": "integer", "format": "int64" },
          "user_id": { "type": "integer", "format": "int64" },
          "type": { "type": "string" },
          "payload": { "type": "object", "additionalProperties": true },
          "read_at": { "type": ["string", "null"], "format": "date-time" },
          "created_at": { "type": "string", "format": "date-time" }
        }
      },
      "Session": {
        "type": "object",
        "properties": {
          "id": { "type": "string" },
          "user_agent": { "type": ["string", "null"] },
          "ip": { "type": ["string", "null"] },
          "created_at": { "type": "string", "format": "date-time" },
          "last_used_at": { "type": "string", "format": "date-time" },
          "expires_at": { "type": "string", "format": "date-time" }
        }
      },
      "SearchResult": {
        "type": "object",
        "properties": {
          "message_id": { "type": "integer", "format": "int64" },
          "conversation_id": { "type": "integer", "format": "int64" },
          "sender_id": { "type": "integer", "format": "int64" },
          "content": { "type": "string" },
          "created_at": { "type": "string", "format": "date-time" },
          "rank": { "type": "number", "format": "double", "description": "ts_rank_cd relevance score." },
          "highlight": { "type": "string", "description": "Snippet with <mark> around matches." },
          "fuzzy_match": { "type": "boolean", "description": "True when returned by the trigram fallback." }
        }
      },
      "SearchResponse": {
        "type": "object",
        "properties": {
          "query": { "type": "string" },
          "total": { "type": "integer", "description": "Exact full-text matches." },
          "returned": { "type": "integer" },
          "fuzzy_available": { "type": "boolean", "description": "Whether pg_trgm is installed." },
          "limit": { "type": "integer" },
          "offset": { "type": "integer" },
          "results": { "type": "array", "items": { "$ref": "#/components/schemas/SearchResult" } }
        }
      },
      "AdminUser": {
        "type": "object",
        "properties": {
          "id": { "type": "integer", "format": "int64" },
          "username": { "type": "string" },
          "email": { "type": "string", "format": "email" },
          "display_name": { "type": ["string", "null"] },
          "role": { "$ref": "#/components/schemas/Role" },
          "banned": { "type": "boolean" },
          "banned_at": { "type": ["string", "null"], "format": "date-time" },
          "ban_reason": { "type": ["string", "null"] },
          "banned_by": { "type": ["integer", "null"], "format": "int64" },
          "created_at": { "type": "string", "format": "date-time" },
          "updated_at": { "type": "string", "format": "date-time" }
        }
      },
      "Role": {
        "type": "string",
        "enum": ["user", "moderator", "admin", "super_admin"],
        "description": "Roles are hierarchical. Permissions are derived from the role, never from comparing role names."
      },
      "AuditRecord": {
        "type": "object",
        "properties": {
          "id": { "type": "integer", "format": "int64" },
          "event_id": { "type": "string" },
          "event_type": { "type": "string", "example": "user.logged_in" },
          "actor_id": { "type": ["integer", "null"], "format": "int64" },
          "actor_username": { "type": ["string", "null"] },
          "target_type": { "type": ["string", "null"] },
          "target_id": { "type": ["string", "null"] },
          "ip": { "type": ["string", "null"] },
          "user_agent": { "type": ["string", "null"] },
          "correlation_id": { "type": ["string", "null"] },
          "trace_id": { "type": ["string", "null"] },
          "metadata": { "type": "object", "additionalProperties": true },
          "occurred_at": { "type": "string", "format": "date-time" },
          "created_at": { "type": "string", "format": "date-time" }
        }
      },
      "FeatureFlag": {
        "type": "object",
        "properties": {
          "name": { "type": "string" },
          "enabled": { "type": "boolean" },
          "env": { "type": "string", "description": "Environment variable seeding this flag." },
          "default": { "type": "boolean" },
          "description": { "type": "string" }
        }
      },
      "HealthStatus": {
        "type": "object",
        "properties": {
          "status": { "type": "string" },
          "checks": { "type": "object", "additionalProperties": { "type": "string" } },
          "cluster": { "type": "object", "additionalProperties": true },
          "websocket_protocol": { "type": "object", "additionalProperties": true }
        }
      }
    },
    "responses": {
      "BadRequest":   { "description": "Invalid input",         "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
      "Unauthorized": { "description": "Missing/invalid token, or suspended account", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
      "Forbidden":    { "description": "Authenticated but not permitted", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
      "NotFound":     { "description": "Resource not found, or feature disabled", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
      "Conflict":     { "description": "State conflict (e.g. duplicate)", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
      "TooLarge":     { "description": "Request or upload exceeds the limit", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
      "RateLimited":  { "description": "Rate limit exceeded",   "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
      "ServerError":  { "description": "Internal error",        "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } }
    }
  },
  "security": [{ "bearerAuth": [] }],
  "paths": {
    "/api/v1/auth/register": {
      "post": {
        "tags": ["Authentication"], "summary": "Register a new account", "security": [],
        "operationId": "register",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/RegisterRequest" } } } },
        "responses": {
          "201": { "description": "Account created", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/AuthResponse" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "409": { "$ref": "#/components/responses/Conflict" },
          "429": { "$ref": "#/components/responses/RateLimited" }
        }
      }
    },
    "/api/v1/auth/login": {
      "post": {
        "tags": ["Authentication"], "summary": "Exchange credentials for tokens", "security": [],
        "operationId": "login",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/LoginRequest" } } } },
        "responses": {
          "200": { "description": "Authenticated", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/AuthResponse" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "429": { "$ref": "#/components/responses/RateLimited" }
        }
      }
    },
    "/api/v1/auth/refresh": {
      "post": {
        "tags": ["Authentication"],
        "summary": "Rotate an access/refresh pair",
        "description": "The refresh token is single-use: rotation replaces the stored hash, so replaying an old token is rejected.",
        "security": [], "operationId": "refresh",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/RefreshRequest" } } } },
        "responses": {
          "200": { "description": "Rotated", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/TokenPair" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      }
    },
    "/api/v1/auth/logout": {
      "post": {
        "tags": ["Authentication"], "summary": "Revoke the current session", "operationId": "logout",
        "requestBody": { "required": true, "content": { "application/json": { "schema": {
          "type": "object", "required": ["session_id"],
          "properties": { "session_id": { "type": "string" } } } } } },
        "responses": {
          "200": { "description": "Revoked", "content": { "application/json": { "schema": {
            "type": "object", "properties": { "revoked": { "type": "boolean" } } } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      }
    },
    "/api/v1/auth/logout-all": {
      "post": {
        "tags": ["Authentication"], "summary": "Revoke every session for the caller",
        "operationId": "logoutAll",
        "responses": {
          "200": { "description": "Revoked", "content": { "application/json": { "schema": {
            "type": "object", "properties": { "revoked": { "type": "integer" } } } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      }
    },
    "/api/v1/auth/me": {
      "get": {
        "tags": ["Authentication"], "summary": "The authenticated user", "operationId": "me",
        "responses": {
          "200": { "description": "Current user", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/User" } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      }
    },
    "/api/v1/users/me": {
      "get": {
        "tags": ["Users"], "summary": "Read own profile", "operationId": "getOwnProfile",
        "responses": {
          "200": { "description": "Profile", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/User" } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      },
      "put": {
        "tags": ["Users"], "summary": "Update own profile", "operationId": "updateOwnProfile",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/UpdateProfileRequest" } } } },
        "responses": {
          "200": { "description": "Updated", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/User" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      }
    },
    "/api/v1/users/{id}": {
      "get": {
        "tags": ["Users"], "summary": "Read a user's public profile", "operationId": "getUser",
        "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
        "responses": {
          "200": { "description": "Profile", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/User" } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/conversations": {
      "get": {
        "tags": ["Conversations"], "summary": "List the caller's conversations", "operationId": "listConversations",
        "parameters": [
          { "$ref": "#/components/parameters/Limit" },
          { "$ref": "#/components/parameters/Offset" }
        ],
        "responses": {
          "200": { "description": "Conversations", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "conversations": { "type": "array", "items": { "$ref": "#/components/schemas/Conversation" } } } } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      },
      "post": {
        "tags": ["Conversations"], "summary": "Create a direct conversation or a group",
        "description": "Creating a direct conversation is idempotent: the existing one is returned if it already exists.",
        "operationId": "createConversation",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/CreateConversationRequest" } } } },
        "responses": {
          "201": { "description": "Created", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Conversation" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/conversations/{id}": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "get": {
        "tags": ["Conversations"], "summary": "Read a conversation", "operationId": "getConversation",
        "responses": {
          "200": { "description": "Conversation", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Conversation" } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      },
      "delete": {
        "tags": ["Conversations"], "summary": "Delete a group (owner only)", "operationId": "deleteConversation",
        "responses": {
          "200": { "description": "Deleted" },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/conversations/{id}/name": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "patch": {
        "tags": ["Conversations"], "summary": "Rename a group (owner only)", "operationId": "renameConversation",
        "requestBody": { "required": true, "content": { "application/json": { "schema": {
          "type": "object", "required": ["name"], "properties": { "name": { "type": "string", "maxLength": 100 } } } } } },
        "responses": {
          "200": { "description": "Renamed", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Conversation" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/conversations/{id}/members": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "post": {
        "tags": ["Conversations"], "summary": "Add a member to a group (owner only)", "operationId": "addMember",
        "requestBody": { "required": true, "content": { "application/json": { "schema": {
          "type": "object", "required": ["user_id"], "properties": { "user_id": { "type": "integer", "format": "int64" } } } } } },
        "responses": {
          "200": { "description": "Added" },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" },
          "409": { "$ref": "#/components/responses/Conflict" }
        }
      }
    },
    "/api/v1/conversations/{id}/members/{user_id}": {
      "parameters": [
        { "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } },
        { "name": "user_id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }
      ],
      "delete": {
        "tags": ["Conversations"], "summary": "Remove a member (owner only)", "operationId": "removeMember",
        "responses": {
          "200": { "description": "Removed" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/conversations/{id}/leave": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "post": {
        "tags": ["Conversations"], "summary": "Leave a group", "operationId": "leaveConversation",
        "responses": {
          "200": { "description": "Left" },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/messages": {
      "get": {
        "tags": ["Messages"], "summary": "List messages in a conversation", "operationId": "listMessages",
        "parameters": [
          { "name": "conversation_id", "in": "query", "required": true, "schema": { "type": "integer", "format": "int64" } },
          { "name": "sender_id", "in": "query", "required": false, "schema": { "type": "integer", "format": "int64" } },
          { "name": "q", "in": "query", "required": false, "schema": { "type": "string" },
            "description": "Simple keyword filter. For ranked search with highlighting use /api/v1/search/messages." },
          { "$ref": "#/components/parameters/Limit" },
          { "$ref": "#/components/parameters/Offset" },
          { "$ref": "#/components/parameters/Before" },
          { "$ref": "#/components/parameters/After" }
        ],
        "responses": {
          "200": { "description": "Messages, newest first", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "messages": { "type": "array", "items": { "$ref": "#/components/schemas/Message" } } } } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      },
      "post": {
        "tags": ["Messages"],
        "summary": "Send a message",
        "description": "Persists first, then broadcasts to every participant over WebSocket.",
        "operationId": "sendMessage",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/SendMessageRequest" } } } },
        "responses": {
          "201": { "description": "Sent", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Message" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "404": { "$ref": "#/components/responses/NotFound" },
          "429": { "$ref": "#/components/responses/RateLimited" }
        }
      }
    },
    "/api/v1/messages/{id}": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "patch": {
        "tags": ["Messages"], "summary": "Edit a message (author only)", "operationId": "editMessage",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/UpdateMessageRequest" } } } },
        "responses": {
          "200": { "description": "Edited", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Message" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      },
      "delete": {
        "tags": ["Messages"],
        "summary": "Soft-delete a message",
        "description": "Permitted for the author, the group owner, or a caller holding message.delete_any.",
        "operationId": "deleteMessage",
        "responses": {
          "200": { "description": "Deleted", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Message" } } } },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/messages/{id}/reactions": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "get": {
        "tags": ["Reactions"], "summary": "List reactions on a message", "operationId": "listReactions",
        "responses": {
          "200": { "description": "Reactions", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "reactions": { "type": "array", "items": { "$ref": "#/components/schemas/Reaction" } } } } } } },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      },
      "post": {
        "tags": ["Reactions"], "summary": "Add a reaction", "operationId": "addReaction",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/ReactionRequest" } } } },
        "responses": {
          "201": { "description": "Added", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Reaction" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "404": { "$ref": "#/components/responses/NotFound" },
          "409": { "$ref": "#/components/responses/Conflict" }
        }
      },
      "delete": {
        "tags": ["Reactions"], "summary": "Remove the caller's reaction", "operationId": "removeReaction",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/ReactionRequest" } } } },
        "responses": {
          "200": { "description": "Removed" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/attachments": {
      "post": {
        "tags": ["Attachments"],
        "summary": "Upload a file",
        "description": "multipart/form-data with a `file` part. The declared content type is verified against the file's magic bytes; a mismatch is rejected with 415.",
        "operationId": "uploadAttachment",
        "requestBody": { "required": true, "content": { "multipart/form-data": { "schema": {
          "type": "object", "required": ["file"],
          "properties": { "file": { "type": "string", "format": "binary" } } } } } },
        "responses": {
          "201": { "description": "Uploaded", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Attachment" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "413": { "$ref": "#/components/responses/TooLarge" },
          "415": { "description": "Unsupported or mismatched media type", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
          "429": { "$ref": "#/components/responses/RateLimited" }
        }
      }
    },
    "/api/v1/attachments/{id}": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "get": {
        "tags": ["Attachments"], "summary": "Download an attachment", "operationId": "downloadAttachment",
        "responses": {
          "200": { "description": "File bytes", "content": { "application/octet-stream": { "schema": { "type": "string", "format": "binary" } } } },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      },
      "delete": {
        "tags": ["Attachments"], "summary": "Delete an attachment (owner only)", "operationId": "deleteAttachment",
        "responses": {
          "200": { "description": "Deleted" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/attachments/{id}/thumbnail": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "get": {
        "tags": ["Attachments"], "summary": "Download an image thumbnail", "operationId": "downloadThumbnail",
        "responses": {
          "200": { "description": "Thumbnail bytes", "content": { "image/*": { "schema": { "type": "string", "format": "binary" } } } },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/notifications": {
      "get": {
        "tags": ["Notifications"], "summary": "List the caller's notifications", "operationId": "listNotifications",
        "parameters": [
          { "name": "unread_only", "in": "query", "required": false, "schema": { "type": "boolean", "default": false } },
          { "$ref": "#/components/parameters/Limit" },
          { "$ref": "#/components/parameters/Offset" }
        ],
        "responses": {
          "200": { "description": "Notifications", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "notifications": { "type": "array", "items": { "$ref": "#/components/schemas/Notification" } },
              "unread_count": { "type": "integer" } } } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      }
    },
    "/api/v1/notifications/{id}": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "delete": {
        "tags": ["Notifications"], "summary": "Delete a notification", "operationId": "deleteNotification",
        "responses": {
          "200": { "description": "Deleted" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/notifications/{id}/read": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "post": {
        "tags": ["Notifications"], "summary": "Mark one notification read", "operationId": "markNotificationRead",
        "responses": {
          "200": { "description": "Marked read" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/notifications/read-all": {
      "post": {
        "tags": ["Notifications"], "summary": "Mark every notification read", "operationId": "markAllNotificationsRead",
        "responses": {
          "200": { "description": "Marked read", "content": { "application/json": { "schema": {
            "type": "object", "properties": { "updated": { "type": "integer" } } } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      }
    },
    "/api/v1/sessions": {
      "get": {
        "tags": ["Sessions"], "summary": "List the caller's active sessions", "operationId": "listOwnSessions",
        "responses": {
          "200": { "description": "Sessions", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "sessions": { "type": "array", "items": { "$ref": "#/components/schemas/Session" } } } } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" }
        }
      }
    },
    "/api/v1/sessions/{session_id}": {
      "parameters": [{ "name": "session_id", "in": "path", "required": true, "schema": { "type": "string" } }],
      "delete": {
        "tags": ["Sessions"], "summary": "Revoke one of the caller's sessions", "operationId": "revokeOwnSession",
        "responses": {
          "200": { "description": "Revoked" },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/search/messages": {
      "get": {
        "tags": ["Search"],
        "summary": "Full-text search over messages",
        "description": "Ranked (ts_rank_cd) with highlighted snippets. Scoped to conversations the caller participates in — enforced in SQL. Google-style syntax is supported (quoted phrases, OR, -exclusion). When an exact search returns nothing and pg_trgm is installed, a trigram fallback runs so typos still match; those hits are flagged `fuzzy_match: true`.",
        "operationId": "searchMessages",
        "parameters": [
          { "name": "q", "in": "query", "required": true, "schema": { "type": "string", "minLength": 2, "maxLength": 256 } },
          { "name": "conversation_id", "in": "query", "required": false, "schema": { "type": "integer", "format": "int64" } },
          { "name": "sender_id", "in": "query", "required": false, "schema": { "type": "integer", "format": "int64" } },
          { "name": "from", "in": "query", "required": false, "schema": { "type": "integer", "format": "int64" }, "description": "Unix epoch seconds, inclusive lower bound." },
          { "name": "to", "in": "query", "required": false, "schema": { "type": "integer", "format": "int64" }, "description": "Unix epoch seconds, inclusive upper bound." },
          { "name": "fuzzy", "in": "query", "required": false, "schema": { "type": "boolean", "default": true } },
          { "name": "highlight", "in": "query", "required": false, "schema": { "type": "boolean", "default": true } },
          { "$ref": "#/components/parameters/Limit" },
          { "$ref": "#/components/parameters/Offset" }
        ],
        "responses": {
          "200": { "description": "Results", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/SearchResponse" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "404": { "description": "Search feature disabled", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } }
        }
      }
    },
    "/api/v1/admin/users": {
      "get": {
        "tags": ["Admin"], "summary": "List users", "description": "Requires user.manage.",
        "operationId": "adminListUsers",
        "parameters": [
          { "name": "q", "in": "query", "required": false, "schema": { "type": "string" }, "description": "Substring across username, email and display name." },
          { "name": "role", "in": "query", "required": false, "schema": { "$ref": "#/components/schemas/Role" } },
          { "name": "banned", "in": "query", "required": false, "schema": { "type": "boolean" } },
          { "$ref": "#/components/parameters/Limit" },
          { "$ref": "#/components/parameters/Offset" }
        ],
        "responses": {
          "200": { "description": "Users", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "total": { "type": "integer" }, "limit": { "type": "integer" }, "offset": { "type": "integer" },
              "users": { "type": "array", "items": { "$ref": "#/components/schemas/AdminUser" } } } } } } },
          "401": { "$ref": "#/components/responses/Unauthorized" },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/users/{id}": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "get": {
        "tags": ["Admin"], "summary": "Read a user", "description": "Requires user.manage.",
        "operationId": "adminGetUser",
        "responses": {
          "200": { "description": "User", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/AdminUser" } } } },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/admin/users/{id}/role": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "put": {
        "tags": ["Admin"],
        "summary": "Assign a role",
        "description": "Requires user.manage_roles (super admin). A caller may never grant a role at or above their own tier.",
        "operationId": "adminSetRole",
        "requestBody": { "required": true, "content": { "application/json": { "schema": {
          "type": "object", "required": ["role"], "properties": { "role": { "$ref": "#/components/schemas/Role" } } } } } },
        "responses": {
          "200": { "description": "Assigned", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "user_id": { "type": "integer", "format": "int64" },
              "previous_role": { "$ref": "#/components/schemas/Role" },
              "role": { "$ref": "#/components/schemas/Role" } } } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/admin/users/{id}/ban": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "post": {
        "tags": ["Admin"],
        "summary": "Suspend an account",
        "description": "Requires user.ban. Also revokes every refresh session, so the account cannot mint new access tokens. Takes effect on the target's next request.",
        "operationId": "adminBanUser",
        "requestBody": { "required": false, "content": { "application/json": { "schema": {
          "type": "object", "properties": { "reason": { "type": "string", "maxLength": 255 } } } } } },
        "responses": {
          "200": { "description": "Suspended", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "user_id": { "type": "integer", "format": "int64" },
              "banned": { "type": "boolean" },
              "sessions_revoked": { "type": "integer" } } } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/admin/users/{id}/unban": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "post": {
        "tags": ["Admin"], "summary": "Reinstate an account", "description": "Requires user.ban.",
        "operationId": "adminUnbanUser",
        "responses": {
          "200": { "description": "Reinstated" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/admin/users/{id}/sessions": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "get": {
        "tags": ["Admin"], "summary": "List a user's sessions", "description": "Requires session.view.",
        "operationId": "adminListUserSessions",
        "responses": {
          "200": { "description": "Sessions", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "user_id": { "type": "integer", "format": "int64" },
              "sessions": { "type": "array", "items": { "$ref": "#/components/schemas/Session" } } } } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      },
      "delete": {
        "tags": ["Admin"], "summary": "Revoke all of a user's sessions", "description": "Requires session.revoke.",
        "operationId": "adminRevokeUserSessions",
        "responses": {
          "200": { "description": "Revoked", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "user_id": { "type": "integer", "format": "int64" }, "revoked": { "type": "integer" } } } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/conversations/{id}": {
      "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer", "format": "int64" } }],
      "get": {
        "tags": ["Admin"], "summary": "Read any conversation", "description": "Requires conversation.view_any.",
        "operationId": "adminGetConversation",
        "responses": {
          "200": { "description": "Conversation with participants", "content": { "application/json": { "schema": { "type": "object", "additionalProperties": true } } } },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      },
      "delete": {
        "tags": ["Admin"], "summary": "Delete any conversation", "description": "Requires group.manage.",
        "operationId": "adminDeleteConversation",
        "responses": {
          "200": { "description": "Deleted" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/api/v1/admin/websockets": {
      "get": {
        "tags": ["Admin"],
        "summary": "Connected WebSocket sessions on this instance",
        "description": "Requires system.metrics. Counts are per-instance: a process only knows its own sockets, which is why `node_id` is reported.",
        "operationId": "adminListWebsockets",
        "responses": {
          "200": { "description": "Connections", "content": { "application/json": { "schema": { "type": "object", "additionalProperties": true } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/cache": {
      "get": {
        "tags": ["Admin"], "summary": "Cache backend and hit statistics", "description": "Requires system.metrics.",
        "operationId": "adminCacheStats",
        "responses": {
          "200": { "description": "Cache statistics", "content": { "application/json": { "schema": { "type": "object", "additionalProperties": true } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/jobs": {
      "get": {
        "tags": ["Admin"], "summary": "Worker pool and event bus statistics", "description": "Requires system.metrics.",
        "operationId": "adminJobStats",
        "responses": {
          "200": { "description": "Job statistics", "content": { "application/json": { "schema": { "type": "object", "additionalProperties": true } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/system": {
      "get": {
        "tags": ["Admin"], "summary": "System metrics summary", "description": "Requires system.metrics.",
        "operationId": "adminSystemInfo",
        "responses": {
          "200": { "description": "System summary", "content": { "application/json": { "schema": { "type": "object", "additionalProperties": true } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/audit-logs": {
      "get": {
        "tags": ["Admin"], "summary": "Search the audit log", "description": "Requires audit.view.",
        "operationId": "adminSearchAuditLogs",
        "parameters": [
          { "name": "actor_id", "in": "query", "required": false, "schema": { "type": "integer", "format": "int64" } },
          { "name": "event_type", "in": "query", "required": false, "schema": { "type": "string" }, "example": "user.logged_in" },
          { "name": "target_type", "in": "query", "required": false, "schema": { "type": "string" } },
          { "name": "target_id", "in": "query", "required": false, "schema": { "type": "string" } },
          { "name": "correlation_id", "in": "query", "required": false, "schema": { "type": "string" } },
          { "name": "from", "in": "query", "required": false, "schema": { "type": "integer", "format": "int64" } },
          { "name": "to", "in": "query", "required": false, "schema": { "type": "integer", "format": "int64" } },
          { "$ref": "#/components/parameters/Limit" },
          { "$ref": "#/components/parameters/Offset" }
        ],
        "responses": {
          "200": { "description": "Audit records, newest first", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "total": { "type": "integer" }, "limit": { "type": "integer" }, "offset": { "type": "integer" },
              "records": { "type": "array", "items": { "$ref": "#/components/schemas/AuditRecord" } } } } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/audit-logs/summary": {
      "get": {
        "tags": ["Admin"], "summary": "Audit event histogram", "description": "Requires audit.view.",
        "operationId": "adminAuditSummary",
        "responses": {
          "200": { "description": "Counts grouped by event type", "content": { "application/json": { "schema": { "type": "object", "additionalProperties": true } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/features": {
      "get": {
        "tags": ["Admin"], "summary": "Feature flag snapshot", "description": "Requires system.metrics.",
        "operationId": "adminListFeatures",
        "responses": {
          "200": { "description": "Flags", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "features": { "type": "array", "items": { "$ref": "#/components/schemas/FeatureFlag" } } } } } } },
          "403": { "$ref": "#/components/responses/Forbidden" }
        }
      }
    },
    "/api/v1/admin/features/{name}": {
      "parameters": [{ "name": "name", "in": "path", "required": true, "schema": { "type": "string" }, "example": "reactions" }],
      "put": {
        "tags": ["Admin"],
        "summary": "Toggle a feature flag at runtime",
        "description": "Requires system.feature_flags (super admin). Applies immediately to this instance and is not persisted — set the corresponding environment variable to make it durable.",
        "operationId": "adminToggleFeature",
        "requestBody": { "required": true, "content": { "application/json": { "schema": {
          "type": "object", "required": ["enabled"], "properties": { "enabled": { "type": "boolean" } } } } } },
        "responses": {
          "200": { "description": "Toggled", "content": { "application/json": { "schema": {
            "type": "object", "properties": {
              "feature": { "type": "string" }, "previous": { "type": "boolean" }, "enabled": { "type": "boolean" } } } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "403": { "$ref": "#/components/responses/Forbidden" },
          "404": { "$ref": "#/components/responses/NotFound" }
        }
      }
    },
    "/health": {
      "get": {
        "tags": ["Operations"], "summary": "Service summary", "security": [], "operationId": "health",
        "responses": { "200": { "description": "Always 200 while the process is up",
          "content": { "application/json": { "schema": { "type": "object", "additionalProperties": true } } } } }
      }
    },
    "/health/live": {
      "get": {
        "tags": ["Operations"],
        "summary": "Liveness probe",
        "description": "Dependency-free by design: a liveness probe that consulted the database would turn a database outage into a restart loop.",
        "security": [], "operationId": "healthLive",
        "responses": { "200": { "description": "Process is responsive" } }
      }
    },
    "/health/ready": {
      "get": {
        "tags": ["Operations"],
        "summary": "Readiness probe",
        "description": "Checks PostgreSQL, the cache, the worker pool and the maintenance scheduler. 503 removes the instance from the load balancer without restarting it.",
        "security": [], "operationId": "healthReady",
        "responses": {
          "200": { "description": "Ready", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/HealthStatus" } } } },
          "503": { "description": "Not ready", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/HealthStatus" } } } }
        }
      }
    },
    "/health/startup": {
      "get": {
        "tags": ["Operations"],
        "summary": "Startup probe",
        "description": "503 until bootstrap (including migrations) completes. Point a Kubernetes startupProbe here so a slow start is not mistaken for a hung process.",
        "security": [], "operationId": "healthStartup",
        "responses": {
          "200": { "description": "Bootstrap complete" },
          "503": { "description": "Still starting" }
        }
      }
    },
    "/metrics": {
      "get": {
        "tags": ["Operations"],
        "summary": "Prometheus metrics",
        "description": "Text exposition format. Restrict access at the reverse proxy — see nginx/conf.d/realtime-chat.conf.",
        "security": [], "operationId": "metrics",
        "responses": { "200": { "description": "Metrics",
          "content": { "text/plain": { "schema": { "type": "string" } } } } }
      }
    }
  },
  "externalDocs": {
    "description": "Architecture, WebSocket protocol and deployment documentation",
    "url": "https://github.com/devtejasx/realtime-chat-server/tree/main/docs"
  }
})OPENAPI";

}  // namespace

std::string_view openapi_json() noexcept {
    return kOpenApiJson;
}

std::string openapi_json_for(std::string_view base_url) {
    if (base_url.empty()) {
        return std::string(kOpenApiJson);
    }
    // Parse-and-patch rather than string substitution: it guarantees the emitted
    // document is still valid JSON, and a malformed base_url cannot corrupt the
    // spec. On a parse failure the pristine document is returned unchanged.
    auto document = nlohmann::json::parse(kOpenApiJson, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded()) {
        return std::string(kOpenApiJson);
    }
    document["servers"] = nlohmann::json::array(
        {{{"url", std::string(base_url)}, {"description", "This deployment"}}});
    return document.dump();
}

std::string_view swagger_ui_csp() noexcept {
    // The global policy is `default-src 'none'`, which would block the viewer
    // entirely. This relaxation is scoped to the /docs response alone and is
    // still restrictive: assets from one pinned CDN, and no framing.
    return "default-src 'none'; "
           "script-src 'self' https://cdn.jsdelivr.net 'unsafe-inline'; "
           "style-src 'self' https://cdn.jsdelivr.net 'unsafe-inline'; "
           "img-src 'self' data: https://cdn.jsdelivr.net; "
           "font-src 'self' data: https://cdn.jsdelivr.net; "
           "connect-src 'self'; "
           "frame-ancestors 'none'; "
           "base-uri 'none'";
}

std::string swagger_ui_html(std::string_view spec_url) {
    // Assets are pinned to an exact version. A floating "latest" tag would mean
    // the documentation page silently changes behaviour whenever the CDN updates.
    static constexpr std::string_view kSwaggerVersion = "5.17.14";
    return std::string(R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Realtime Chat API</title>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@)HTML") +
           std::string(kSwaggerVersion) + R"HTML(/swagger-ui.css">
  <style>
    body { margin: 0; background: #fafafa; }
    .topbar { display: none; }
  </style>
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@)HTML" +
           std::string(kSwaggerVersion) + R"HTML(/swagger-ui-bundle.js" crossorigin></script>
  <script>
    window.addEventListener('load', function () {
      window.ui = SwaggerUIBundle({
        url: ')HTML" +
           std::string(spec_url) +
           R"HTML(',
        dom_id: '#swagger-ui',
        deepLinking: true,
        displayRequestDuration: true,
        docExpansion: 'none',
        filter: true,
        tryItOutEnabled: true,
        persistAuthorization: true
      });
    });
  </script>
</body>
</html>
)HTML";
}

}  // namespace rtc::docs
