#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "rtc/storage/file_storage.hpp"

namespace rtc::storage {

// Local-filesystem IFileStorage. The default backend: objects are written under
// a configured root directory. Keys are validated to prevent path traversal
// (no absolute paths, no "..") so a malicious key can never escape the root.
// Cloud backends (S3/Azure/GCS) implement the same interface and slot in via
// the composition root with no service-layer change.
class LocalFileStorage final : public IFileStorage {
  public:
    explicit LocalFileStorage(std::filesystem::path root);

    StoredObject put(std::string_view key,
                     std::string_view content_type,
                     const std::string& bytes) override;
    [[nodiscard]] std::optional<std::string> get(std::string_view key) override;
    bool remove(std::string_view key) override;
    [[nodiscard]] bool exists(std::string_view key) override;
    [[nodiscard]] std::string_view backend_name() const override { return "local"; }

  private:
    // Resolves a validated key to an absolute path under root_, throwing on an
    // unsafe key.
    [[nodiscard]] std::filesystem::path resolve(std::string_view key) const;

    std::filesystem::path root_;
};

}  // namespace rtc::storage
