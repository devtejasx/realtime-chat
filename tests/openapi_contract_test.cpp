#include <crow/common.h>
#include <crow/http_request.h>
#include <crow/http_response.h>
#include <gtest/gtest.h>

#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "rtc/docs/openapi.hpp"
#include "rtc/dto/auth_dto.hpp"
#include "rtc/dto/conversation_dto.hpp"
#include "rtc/dto/message_dto.hpp"
#include "rtc/dto/profile_dto.hpp"
#include "rtc/dto/reaction_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/models/conversation.hpp"
#include "rtc/models/message.hpp"
#include "rtc/models/reaction.hpp"
#include "support/test_api_app.hpp"

// Executable proof that the OpenAPI document describes the code.
//
// openapi_spec_test.cpp already asserts the document is well formed and that
// every registered route appears in it. What it could not catch — and what
// shipped broken — is a path that is documented with the *wrong shape*:
// POST /conversations published `participant_id` and `member_ids` while the DTO
// required `type` and `participant_ids`. Every existing assertion passed,
// because the path existed, had a summary, a tag and resolvable $refs. Only the
// field names were fiction, so a client following the published example got a
// 400.
//
// These tests close that gap from both directions:
//
//   Requests  - a payload synthesised from the schema's own `required` list is
//               fed to the real DTO parser. If the spec under-documents a
//               required field the parser throws and the test fails; if it
//               over-documents one, dropping that field fails to throw and the
//               test fails.
//   Responses - live responses from the fully wired app are checked against the
//               documented schema for missing required keys *and* for keys the
//               schema never mentions, so a renamed or added field is caught.

namespace {

using nlohmann::json;
using rtc::testing::TestApiApp;

const json& spec() {
    static const json document = json::parse(rtc::docs::openapi_json());
    return document;
}

const json& resolve(const json& node) {
    if (node.is_object() && node.contains("$ref")) {
        const auto ref = node.at("$ref").get<std::string>();
        const auto name = ref.substr(ref.rfind('/') + 1);
        return spec().at("components").at("schemas").at(name);
    }
    return node;
}

const json& schema_named(const std::string& name) {
    return spec().at("components").at("schemas").at(name);
}

// Builds a value satisfying a single property schema, preferring a documented
// enum so semantic constraints (conversation type, reaction emoji) are honoured.
json sample_for(const json& property) {
    const json& prop = resolve(property);
    if (prop.contains("enum") && !prop.at("enum").empty()) {
        return prop.at("enum").at(0);
    }
    std::string type = "string";
    if (prop.contains("type")) {
        type = prop.at("type").is_array() ? prop.at("type").at(0).get<std::string>()
                                          : prop.at("type").get<std::string>();
    }
    if (type == "integer")
        return 1;
    if (type == "number")
        return 1.0;
    if (type == "boolean")
        return true;
    if (type == "array") {
        json array = json::array();
        if (prop.contains("items")) {
            array.push_back(sample_for(prop.at("items")));
        }
        return array;
    }
    if (type == "object")
        return json::object();
    return std::string("sample");
}

// A payload containing exactly the properties the schema marks required.
//
// `anyOf` branches count as required too: a schema like LoginRequest, where the
// account may be named by `identifier`, `username` or `email`, lists only
// `password` at the top level and expresses the choice as anyOf. Satisfying the
// first branch keeps the payload minimal while still being a legal document.
json minimal_payload(const json& schema, std::size_t any_of_branch = 0) {
    json payload = json::object();
    const auto add = [&](const json& required_list) {
        for (const auto& field : required_list) {
            const auto name = field.get<std::string>();
            payload[name] = sample_for(schema.at("properties").at(name));
        }
    };
    if (schema.contains("required")) {
        add(schema.at("required"));
    }
    if (schema.contains("anyOf") && any_of_branch < schema.at("anyOf").size()) {
        const auto& branch = schema.at("anyOf").at(any_of_branch);
        if (branch.contains("required")) {
            add(branch.at("required"));
        }
    }
    return payload;
}

// Number of alternative minimal payloads a schema admits (>1 only for anyOf).
std::size_t branch_count(const json& schema) {
    return schema.contains("anyOf") ? schema.at("anyOf").size() : 1U;
}

struct RequestContract {
    const char* schema_name;
    std::function<void(const json&)> parse;
};

// Each documented request schema paired with the parser that actually consumes
// it in production. Adding a request DTO without adding it here leaves a hole,
// which is why DocumentedRequestSchemasAreAllCovered asserts the table is total.
const std::vector<RequestContract>& request_contracts() {
    static const std::vector<RequestContract> contracts = {
        {"RegisterRequest", [](const json& b) { rtc::dto::RegisterRequest::from_json(b); }},
        {"LoginRequest", [](const json& b) { rtc::dto::LoginRequest::from_json(b); }},
        {"CreateConversationRequest",
         [](const json& b) { rtc::dto::CreateConversationRequest::from_json(b); }},
        {"SendMessageRequest", [](const json& b) { rtc::dto::SendMessageRequest::from_json(b); }},
        {"UpdateMessageRequest",
         [](const json& b) { rtc::dto::UpdateMessageRequest::from_json(b); }},
        {"ReactionRequest", [](const json& b) { rtc::dto::ReactionRequest::from_json(b); }},
        {"UpdateProfileRequest",
         [](const json& b) { rtc::dto::UpdateProfileRequest::from_json(b); }},
    };
    return contracts;
}

// ---------------------------------------------------------------------------
// Request contracts
// ---------------------------------------------------------------------------

// The regression guard for the shipped bug. Under the old document,
// CreateConversationRequest declared no required fields at all, so the minimal
// payload was `{}` and the real parser rejected it for a missing `type`.
TEST(OpenApiContract, MinimalDocumentedPayloadIsAcceptedByTheParser) {
    for (const auto& contract : request_contracts()) {
        const json& schema = schema_named(contract.schema_name);
        // Every anyOf alternative must work on its own, or the document is
        // advertising a way in that the implementation does not honour.
        for (std::size_t branch = 0; branch < branch_count(schema); ++branch) {
            const json payload = minimal_payload(schema, branch);
            try {
                contract.parse(payload);
            } catch (const rtc::errors::ValidationException& ex) {
                ADD_FAILURE() << contract.schema_name
                              << ": a payload built from the schema's own `required` list was "
                                 "rejected by the implementation — the spec under-documents a "
                                 "required field.\n  payload: "
                              << payload.dump() << "\n  error  : " << ex.message() << " ("
                              << ex.details() << ")";
            }
        }
    }
}

// The mirror image: anything the spec calls required must genuinely be required.
TEST(OpenApiContract, EveryDocumentedRequiredFieldIsEnforced) {
    for (const auto& contract : request_contracts()) {
        const json& schema = schema_named(contract.schema_name);
        if (!schema.contains("required")) {
            continue;
        }
        for (const auto& field : schema.at("required")) {
            const auto name = field.get<std::string>();
            json payload = minimal_payload(schema);
            payload.erase(name);
            EXPECT_THROW(contract.parse(payload), rtc::errors::ValidationException)
                << contract.schema_name << ": the spec marks `" << name
                << "` required, but the implementation accepts a payload without it.";
        }
    }
}

// A documented example that the parser rejects is worse than no example: it is
// the exact failure a developer hits when they copy it out of Swagger UI.
TEST(OpenApiContract, EveryDocumentedExampleIsAccepted) {
    for (const auto& contract : request_contracts()) {
        const json& schema = schema_named(contract.schema_name);
        if (!schema.contains("examples")) {
            continue;
        }
        for (const auto& example : schema.at("examples")) {
            try {
                contract.parse(example);
            } catch (const rtc::errors::ValidationException& ex) {
                ADD_FAILURE() << contract.schema_name
                              << ": a published example is rejected by the implementation.\n"
                              << "  example: " << example.dump() << "\n  error  : " << ex.message()
                              << " (" << ex.details() << ")";
            }
        }
    }
}

TEST(OpenApiContract, DocumentedRequestSchemasAreAllCovered) {
    // Keeps request_contracts() honest: a new *Request schema must be wired to
    // its parser, or the checks above would silently skip it.
    std::set<std::string> covered;
    for (const auto& contract : request_contracts()) {
        covered.insert(contract.schema_name);
    }
    // Schemas parsed inline by a controller rather than by a named DTO. Listed
    // explicitly so the exemption is a deliberate, visible decision.
    static const std::set<std::string> kParsedInline = {"RefreshRequest"};

    for (const auto& [name, _] : spec().at("components").at("schemas").items()) {
        if (name.size() < 7 || name.substr(name.size() - 7) != "Request") {
            continue;
        }
        EXPECT_TRUE(covered.count(name) != 0U || kParsedInline.count(name) != 0U)
            << name << " is documented but not checked against a parser in request_contracts()";
    }
}

// ---------------------------------------------------------------------------
// Enum contracts
// ---------------------------------------------------------------------------

std::set<std::string> documented_enum(const json& schema, const std::string& property) {
    std::set<std::string> values;
    for (const auto& entry : resolve(schema.at("properties").at(property)).at("enum")) {
        values.insert(entry.get<std::string>());
    }
    return values;
}

TEST(OpenApiContract, ReactionEmojiEnumMatchesTheAllowList) {
    // The published palette is what clients render; the allow-list is what the
    // service accepts. They must be the same set.
    std::set<std::string> allowed;
    for (const auto emoji : rtc::models::kAllowedReactions) {
        allowed.insert(std::string(emoji));
    }
    EXPECT_EQ(documented_enum(schema_named("ReactionRequest"), "emoji"), allowed);
    EXPECT_EQ(documented_enum(schema_named("Reaction"), "emoji"), allowed);
}

TEST(OpenApiContract, ConversationAndMessageTypeEnumsMatchTheModels) {
    const std::set<std::string> conversation_types = {"direct", "group"};
    EXPECT_EQ(documented_enum(schema_named("Conversation"), "type"), conversation_types);
    EXPECT_EQ(documented_enum(schema_named("CreateConversationRequest"), "type"),
              conversation_types);
    for (const auto& value : conversation_types) {
        EXPECT_TRUE(rtc::models::conversation_type_from_string(value).has_value())
            << value << " is documented but not accepted by the model";
    }

    const std::set<std::string> message_types = {"text", "system"};
    EXPECT_EQ(documented_enum(schema_named("Message"), "type"), message_types);
    EXPECT_EQ(documented_enum(schema_named("SendMessageRequest"), "type"), message_types);
}

// ---------------------------------------------------------------------------
// Response contracts
// ---------------------------------------------------------------------------

// Checks a real response object against a documented schema in both directions.
void expect_matches_schema(const json& value,
                           const std::string& schema_name,
                           const std::string& context) {
    const json& schema = schema_named(schema_name);
    ASSERT_TRUE(value.is_object()) << context << ": expected an object";

    if (schema.contains("required")) {
        for (const auto& field : schema.at("required")) {
            EXPECT_TRUE(value.contains(field.get<std::string>()))
                << context << ": documented required field `" << field.get<std::string>()
                << "` is missing from the actual response";
        }
    }

    // The direction that caught the real drift: `Conversation` documented
    // `participant_ids` while the service returned `participants`, and `Message`
    // documented `deleted_at` while the service returned `deleted` / `edited` /
    // `updated_at`.
    const auto& properties = schema.at("properties");
    for (const auto& [key, _] : value.items()) {
        EXPECT_TRUE(properties.contains(key))
            << context << ": response contains `" << key << "`, which the " << schema_name
            << " schema does not document";
    }
}

class ResponseContractTest : public ::testing::Test {
  protected:
    void SetUp() override {
        token_ = app_.register_user("ada", "ada@example.com");
        ASSERT_FALSE(token_.empty());
        const auto peer = app_.request(
            crow::HTTPMethod::Post,
            "/api/v1/auth/register",
            R"({"username":"bob","email":"bob@example.com","password":"correct-horse-battery"})");
        ASSERT_EQ(peer.code, 201) << peer.body;
        peer_id_ = TestApiApp::body_of(peer).at("user").at("id").get<std::int64_t>();
    }

    TestApiApp app_;
    std::string token_;
    std::int64_t peer_id_ = 0;
};

TEST_F(ResponseContractTest, UserResponseMatchesTheDocumentedSchema) {
    const auto res = app_.request(crow::HTTPMethod::Get, "/api/v1/auth/me", "", token_);
    ASSERT_EQ(res.code, 200) << res.body;
    expect_matches_schema(TestApiApp::body_of(res), "User", "GET /auth/me");
}

TEST_F(ResponseContractTest, ConversationResponseMatchesTheDocumentedSchema) {
    const auto res = app_.request(crow::HTTPMethod::Post,
                                  "/api/v1/conversations",
                                  json{{"type", "direct"}, {"participant_ids", {peer_id_}}}.dump(),
                                  token_);
    ASSERT_EQ(res.code, 201) << res.body;
    const auto body = TestApiApp::body_of(res);
    expect_matches_schema(body, "Conversation", "POST /conversations");

    ASSERT_TRUE(body.at("participants").is_array());
    for (const auto& participant : body.at("participants")) {
        expect_matches_schema(participant, "ConversationParticipant", "conversation participant");
    }
}

TEST_F(ResponseContractTest, MessageResponseMatchesTheDocumentedSchema) {
    const auto created =
        app_.request(crow::HTTPMethod::Post,
                     "/api/v1/conversations",
                     json{{"type", "direct"}, {"participant_ids", {peer_id_}}}.dump(),
                     token_);
    ASSERT_EQ(created.code, 201) << created.body;
    const auto conversation_id = TestApiApp::body_of(created).at("id").get<std::int64_t>();

    const auto sent = app_.request(
        crow::HTTPMethod::Post,
        "/api/v1/messages",
        json{{"conversation_id", conversation_id}, {"content", "contract check"}}.dump(),
        token_);
    ASSERT_EQ(sent.code, 201) << sent.body;
    expect_matches_schema(TestApiApp::body_of(sent), "Message", "POST /messages");
}

TEST_F(ResponseContractTest, AuthResponseMatchesTheDocumentedSchema) {
    const auto res = app_.request(crow::HTTPMethod::Post,
                                  "/api/v1/auth/login",
                                  R"({"identifier":"ada","password":"correct-horse-battery"})");
    ASSERT_EQ(res.code, 200) << res.body;
    const auto body = TestApiApp::body_of(res);
    expect_matches_schema(body, "AuthResponse", "POST /auth/login");
    expect_matches_schema(body.at("tokens"), "TokenPair", "POST /auth/login tokens");
}

TEST_F(ResponseContractTest, ErrorEnvelopeMatchesTheDocumentedSchema) {
    const auto res = app_.request(crow::HTTPMethod::Post, "/api/v1/conversations", "{}", token_);
    ASSERT_EQ(res.code, 400) << res.body;
    const auto body = TestApiApp::body_of(res);
    expect_matches_schema(body, "Error", "validation failure");
    EXPECT_EQ(body.at("error").at("code"), "validation_error");
}

}  // namespace
