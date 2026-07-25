#include "rtc/media/mime.hpp"

#include <gtest/gtest.h>

#include "rtc/models/attachment.hpp"

namespace {

using rtc::models::AttachmentKind;

TEST(MimeTest, AllowsKnownTypes) {
    EXPECT_TRUE(rtc::media::is_allowed_upload_type("image/png"));
    EXPECT_TRUE(rtc::media::is_allowed_upload_type("application/pdf"));
    EXPECT_TRUE(rtc::media::is_allowed_upload_type("video/mp4"));
}

TEST(MimeTest, RejectsUnknownAndDangerousTypes) {
    EXPECT_FALSE(rtc::media::is_allowed_upload_type("application/x-msdownload"));
    EXPECT_FALSE(rtc::media::is_allowed_upload_type("text/html"));
    EXPECT_FALSE(rtc::media::is_allowed_upload_type(""));
}

TEST(MimeTest, ClassifiesByType) {
    EXPECT_EQ(rtc::media::classify("image/jpeg"), AttachmentKind::kImage);
    EXPECT_EQ(rtc::media::classify("application/pdf"), AttachmentKind::kPdf);
    EXPECT_EQ(rtc::media::classify("audio/mpeg"), AttachmentKind::kAudio);
    EXPECT_EQ(rtc::media::classify("video/webm"), AttachmentKind::kVideo);
}

TEST(MimeTest, IsCaseInsensitive) {
    EXPECT_TRUE(rtc::media::is_allowed_upload_type("IMAGE/PNG"));
    EXPECT_EQ(rtc::media::classify("Image/Png"), AttachmentKind::kImage);
}

TEST(MimeTest, InfersTypeFromExtension) {
    EXPECT_EQ(rtc::media::content_type_for_extension("photo.JPG"), "image/jpeg");
    EXPECT_EQ(rtc::media::content_type_for_extension("doc.pdf"), "application/pdf");
    EXPECT_EQ(rtc::media::content_type_for_extension("noext"), "");
}

}  // namespace
