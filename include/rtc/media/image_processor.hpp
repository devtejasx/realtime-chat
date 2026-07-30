#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rtc::media {

struct ImageInfo {
    int width = 0;
    int height = 0;
};

struct Thumbnail {
    std::string bytes;
    std::string content_type;
    int width = 0;
    int height = 0;
};

// Abstraction over image inspection and thumbnailing. Kept behind an interface
// so a real backend (stb_image, libvips, ImageMagick) can be dropped in without
// touching the attachment service. Thumbnail generation runs on the background
// executor, never on the request thread.
class IImageProcessor {
  public:
    virtual ~IImageProcessor() = default;

    [[nodiscard]] virtual bool supports(std::string_view content_type) const = 0;

    // Extracts pixel dimensions, if the backend can decode the format.
    [[nodiscard]] virtual std::optional<ImageInfo> probe(const std::string& bytes) const = 0;

    // Produces a thumbnail no larger than `max_dimension` on its longest side.
    [[nodiscard]] virtual std::optional<Thumbnail> make_thumbnail(const std::string& bytes,
                                                                  std::string_view content_type,
                                                                  int max_dimension) const = 0;
};

// Default processor: validates that a content type is a supported image but does
// no decoding or resizing (returns no dimensions / thumbnails). This keeps the
// upload pipeline fully functional with zero image-library dependencies; swap in
// a real processor to enable dimensions and thumbnails.
class NoopImageProcessor final : public IImageProcessor {
  public:
    [[nodiscard]] bool supports(std::string_view content_type) const override {
        return content_type.rfind("image/", 0) == 0;
    }
    [[nodiscard]] std::optional<ImageInfo> probe(const std::string&) const override {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<Thumbnail> make_thumbnail(const std::string&,
                                                          std::string_view,
                                                          int) const override {
        return std::nullopt;
    }
};

}  // namespace rtc::media
