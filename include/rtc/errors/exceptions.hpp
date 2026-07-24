#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "rtc/errors/error_type.hpp"

namespace rtc::errors {

// Base class for all application-defined exceptions.
//
// Every AppException carries a semantic ErrorType (used to derive the HTTP
// status and machine code), a human-readable message, and an optional
// `details` string for extra context that is safe to return to the client
// (e.g. which field failed validation). Server-internal diagnostics should be
// logged, not placed in `details`.
class AppException : public std::runtime_error {
public:
    AppException(ErrorType type, std::string message, std::string details = {})
        : std::runtime_error(message),
          type_(type),
          message_(std::move(message)),
          details_(std::move(details)) {}

    [[nodiscard]] ErrorType type() const noexcept { return type_; }
    [[nodiscard]] int http_status() const noexcept { return http_status_for(type_); }
    [[nodiscard]] std::string_view code() const noexcept { return code_for(type_); }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::string& details() const noexcept { return details_; }
    [[nodiscard]] bool has_details() const noexcept { return !details_.empty(); }

private:
    ErrorType type_;
    std::string message_;
    std::string details_;
};

// 400 — client supplied invalid or malformed input.
class ValidationException : public AppException {
public:
    explicit ValidationException(std::string message, std::string details = {})
        : AppException(ErrorType::kValidation, std::move(message), std::move(details)) {}
};

// 401 — authentication is required and has failed or not been provided.
class AuthenticationException : public AppException {
public:
    explicit AuthenticationException(std::string message, std::string details = {})
        : AppException(ErrorType::kAuthentication, std::move(message), std::move(details)) {}
};

// 403 — the caller is authenticated but not allowed to perform the action.
class AuthorizationException : public AppException {
public:
    explicit AuthorizationException(std::string message, std::string details = {})
        : AppException(ErrorType::kAuthorization, std::move(message), std::move(details)) {}
};

// 404 — the requested resource does not exist.
class NotFoundException : public AppException {
public:
    explicit NotFoundException(std::string message, std::string details = {})
        : AppException(ErrorType::kNotFound, std::move(message), std::move(details)) {}
};

// 409 — the request conflicts with current state (e.g. duplicate unique key).
class ConflictException : public AppException {
public:
    explicit ConflictException(std::string message, std::string details = {})
        : AppException(ErrorType::kConflict, std::move(message), std::move(details)) {}
};

// 500 — a persistence-layer operation failed.
class DatabaseException : public AppException {
public:
    explicit DatabaseException(std::string message, std::string details = {})
        : AppException(ErrorType::kDatabase, std::move(message), std::move(details)) {}
};

// 500 — configuration is missing or invalid; typically fatal at startup.
class ConfigException : public AppException {
public:
    explicit ConfigException(std::string message, std::string details = {})
        : AppException(ErrorType::kConfiguration, std::move(message), std::move(details)) {}
};

// 500 — an unexpected, uncategorised internal failure.
class InternalException : public AppException {
public:
    explicit InternalException(std::string message, std::string details = {})
        : AppException(ErrorType::kInternal, std::move(message), std::move(details)) {}
};

}  // namespace rtc::errors
