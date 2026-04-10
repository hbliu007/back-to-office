/**
 * @file test_hosted_parse.cpp
 */

#include <gtest/gtest.h>
#include "cloud/hosted_parse.hpp"

TEST(HostedParse, AuthLoginOk) {
    const char* json = R"({
  "access_token": "at",
  "refresh_token": "rt",
  "expires_at": "2026-04-09T12:00:00Z",
  "user": { "user_id": "u1", "email": "e@x.com", "plan": "personal" }
})";
    std::string err;
    const auto t = bto::cloud::parse_auth_login_response(json, &err);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->access_token, "at");
    EXPECT_EQ(t->refresh_token, "rt");
    EXPECT_EQ(t->expires_at, "2026-04-09T12:00:00Z");
    EXPECT_EQ(t->user_id, "u1");
    EXPECT_EQ(t->email, "e@x.com");
    EXPECT_EQ(t->plan, "personal");
}

TEST(HostedParse, AuthLoginMissingField) {
    std::string err;
    EXPECT_FALSE(bto::cloud::parse_auth_login_response("{}", &err).has_value());
    EXPECT_FALSE(err.empty());
}

TEST(HostedParse, DeviceListOk) {
    const char* json = R"({
  "devices": [
    {
      "device_id": "d1",
      "display_name": "box",
      "platform": "linux",
      "status": "online",
      "agent_version": "1.0.0"
    }
  ]
})";
    std::string err;
    const auto list = bto::cloud::parse_device_list_response(json, &err);
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list->size(), 1u);
    EXPECT_EQ((*list)[0].device_id, "d1");
    EXPECT_EQ((*list)[0].display_name, "box");
    EXPECT_EQ((*list)[0].platform, "linux");
    EXPECT_EQ((*list)[0].status, "online");
    EXPECT_EQ((*list)[0].agent_version, "1.0.0");
}

TEST(HostedParse, DeviceCreateOk) {
    const char* json = R"({
  "claim_code": "ABCD-EFGH",
  "device_id": "dev_1",
  "display_name": "office",
  "expires_at": "2026-04-09T12:00:00Z",
  "install_url": "https://x/y.sh"
})";
    std::string err;
    const auto r = bto::cloud::parse_device_create_response(json, &err);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->claim_code, "ABCD-EFGH");
    EXPECT_EQ(r->device_id, "dev_1");
    EXPECT_EQ(r->display_name, "office");
    EXPECT_EQ(r->expires_at, "2026-04-09T12:00:00Z");
    EXPECT_EQ(r->install_url, "https://x/y.sh");
}

TEST(HostedParse, ErrorBodyMessage) {
    EXPECT_NE(bto::cloud::parse_hosted_error_body(R"({"message":"nope"})").find("nope"), std::string::npos);
}
