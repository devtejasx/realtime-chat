#include "rtc/storage/local_file_storage.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "rtc/errors/exceptions.hpp"

namespace rtc::storage {

namespace fs = std::filesystem;

LocalFileStorage::LocalFileStorage(fs::path root) : root_(std::move(root)) {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (ec) {
        throw rtc::errors::InternalException("Cannot create upload root directory",
                                             root_.string());
    }
}

fs::path LocalFileStorage::resolve(std::string_view key) const {
    if (key.empty()) {
        throw rtc::errors::ValidationException("Empty storage key");
    }
    const fs::path rel(key);
    // Reject absolute paths and any traversal component so a key can never
    // escape the storage root.
    if (rel.is_absolute()) {
        throw rtc::errors::ValidationException("Storage key must be relative");
    }
    for (const auto& part : rel) {
        if (part == "..") {
            throw rtc::errors::ValidationException("Storage key must not contain '..'");
        }
    }
    return root_ / rel;
}

StoredObject LocalFileStorage::put(std::string_view key, std::string_view /*content_type*/,
                                   const std::string& bytes) {
    const fs::path path = resolve(key);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw rtc::errors::InternalException("Failed to open file for writing", path.string());
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw rtc::errors::InternalException("Failed to write file", path.string());
    }
    return StoredObject{std::string(key), std::string(backend_name()),
                        static_cast<std::int64_t>(bytes.size())};
}

std::optional<std::string> LocalFileStorage::get(std::string_view key) {
    const fs::path path = resolve(key);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool LocalFileStorage::remove(std::string_view key) {
    std::error_code ec;
    return fs::remove(resolve(key), ec);
}

bool LocalFileStorage::exists(std::string_view key) {
    std::error_code ec;
    return fs::exists(resolve(key), ec);
}

}  // namespace rtc::storage
