#include "rtc/controllers/attachment_controller.hpp"

#include <chrono>
#include <cstdint>
#include <string>

#include <crow/multipart.h>

#include "rtc/dto/attachment_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/response.hpp"

namespace rtc::controllers {
namespace {

// Extracts the filename parameter from a multipart part's Content-Disposition.
[[nodiscard]] std::string part_filename(const crow::multipart::part& part) {
    if (const auto* header = part.get_header_object("Content-Disposition")) {
        const auto it = header->params.find("filename");
        if (it != header->params.end()) {
            return it->second;
        }
    }
    return "upload.bin";
}

[[nodiscard]] std::string part_content_type(const crow::multipart::part& part) {
    if (const auto* header = part.get_header_object("Content-Type")) {
        return header->value;
    }
    return {};
}

}  // namespace

void AttachmentController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/api/attachments")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                rate_limiter_.enforce(
                    "upload", std::to_string(claims.user_id), config_.rate_limit_upload_max,
                    std::chrono::seconds(config_.rate_limit_window_seconds));

                crow::multipart::message multipart(req);
                const auto file = multipart.get_part_by_name("file");
                if (file.body.empty()) {
                    throw errors::ValidationException("Missing 'file' part in multipart upload");
                }

                services::AttachmentService::UploadInput input;
                input.owner_id = claims.user_id;
                input.filename = part_filename(file);
                input.declared_content_type = part_content_type(file);
                input.bytes = file.body;

                const auto attachment = attachments_.upload(std::move(input));
                return http::json_response(
                    201,
                    dto::AttachmentResponse::from(attachment, config_.upload_public_base_url)
                        .to_json());
            });
        });

    const auto download_handler = [this](const crow::request& req, std::int64_t id,
                                         bool thumbnail) {
        return http::run_guarded([&] {
            const auto claims = auth_guard_.authenticate(req);
            const auto download = attachments_.download(claims.user_id, id, thumbnail);

            crow::response response(200, download.bytes);
            response.set_header("Content-Type", download.content_type);
            response.set_header("Content-Disposition",
                                "inline; filename=\"" + download.meta.original_filename + "\"");
            response.set_header("Cache-Control", "private, max-age=86400");
            return response;
        });
    };

    CROW_ROUTE(app, "/api/attachments/<int>")
        .methods(crow::HTTPMethod::Get)(
            [download_handler](const crow::request& req, std::int64_t id) {
                return download_handler(req, id, /*thumbnail=*/false);
            });

    CROW_ROUTE(app, "/api/attachments/<int>/thumbnail")
        .methods(crow::HTTPMethod::Get)(
            [download_handler](const crow::request& req, std::int64_t id) {
                return download_handler(req, id, /*thumbnail=*/true);
            });

    CROW_ROUTE(app, "/api/attachments/<int>")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                attachments_.remove(claims.user_id, id);
                return http::json_response(200, nlohmann::json{{"deleted", true}});
            });
        });
}

}  // namespace rtc::controllers
