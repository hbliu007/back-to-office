/**
 * @file test_auth_client.cpp
 */

#include <gtest/gtest.h>
#include "cloud/auth_client.hpp"

#include <cstdlib>

TEST(AuthClient, LoginWithoutApiBase) {
    bto::cloud::AuthClient client{bto::cloud::HostedConfig{""}};
    std::string err;
    const auto t = client.login_email_password("a@b.c", "x", &err);
    EXPECT_FALSE(t.has_value());
    EXPECT_FALSE(err.empty());
}

#if !defined(_WIN32)
TEST(AuthClient, LoginFailsWhenUnreachable) {
    const char* old = std::getenv("BTO_ALLOW_INSECURE_HTTP_API");
    setenv("BTO_ALLOW_INSECURE_HTTP_API", "1", 1);
    bto::cloud::AuthClient client{bto::cloud::HostedConfig{"http://127.0.0.1:1"}};
    std::string err;
    const auto t = client.login_email_password("a@b.c", "x", &err);
    if (old) {
        setenv("BTO_ALLOW_INSECURE_HTTP_API", old, 1);
    } else {
        unsetenv("BTO_ALLOW_INSECURE_HTTP_API");
    }
    EXPECT_FALSE(t.has_value());
    EXPECT_FALSE(err.empty());
}
#endif
