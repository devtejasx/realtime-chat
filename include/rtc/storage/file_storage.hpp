#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rtc::storage {

// Result of storing an object.
struct StoredObject {
    std::string key;      // final storage key (may differ from the hint)
    std::string backend;  // backend name, e.g. "local", "s3"
    std::int64_t size = 0;
};

// Pluggable blob storage abstraction. The attachment service depends only on
// this interface, so the physical backend — local filesystem, AWS S3, Azure
// Blob, Google Cloud Storage — can be swapped via configuration without any
// change to business logic. Keys are backend-relative, opaque paths.
class IFileStorage {
public:
    virtual ~IFileStorage() = default;

    // Stores `bytes` under `key`. Returns the stored-object descriptor. Throws
    // rtc::errors::* on failure (e.g. an unsafe key or I/O error).
    virtual StoredObject put(std::string_view key, std::string_view content_type,
                             const std::string& bytes) = 0;

    // Reads the object's bytes, or nullopt if the key does not exist.
    [[nodiscard]] virtual std::optional<std::string> get(std::string_view key) = 0;

    virtual bool remove(std::string_view key) = 0;
    [[nodiscard]] virtual bool exists(std::string_view key) = 0;

    [[nodiscard]] virtual std::string_view backend_name() const = 0;
};

}  // namespace rtc::storage
