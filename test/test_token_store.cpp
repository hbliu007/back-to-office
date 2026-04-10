/**
 * @file test_token_store.cpp
 */

#include <gtest/gtest.h>
#include "cloud/token_store.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

TEST(TokenStore, SaveLoadRoundTrip) {
    const auto dir =
        fs::temp_directory_path() / ("bto_token_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const auto path = (dir / "auth.json").string();

    bto::cloud::TokenStore store(path);
    bto::cloud::AuthTokens in;
    in.access_token = "access";
    in.refresh_token = "refresh";
    in.expires_at = "2026-04-09T12:00:00Z";
    in.user_id = "usr_1";
    in.email = "u@example.com";
    in.plan = "personal";

    ASSERT_TRUE(store.save(in));
    const auto out = store.load();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->access_token, "access");
    EXPECT_EQ(out->refresh_token, "refresh");
    EXPECT_EQ(out->expires_at, "2026-04-09T12:00:00Z");
    EXPECT_EQ(out->user_id, "usr_1");
    EXPECT_EQ(out->email, "u@example.com");
    EXPECT_EQ(out->plan, "personal");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(TokenStore, LoadMissingReturnsNullopt) {
    const auto dir =
        fs::temp_directory_path() / ("bto_token_missing_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    bto::cloud::TokenStore store((dir / "nope.json").string());
    EXPECT_FALSE(store.load().has_value());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(TokenStore, LoadCorruptReturnsNullopt) {
    const auto dir =
        fs::temp_directory_path() / ("bto_token_bad_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const auto path = (dir / "auth.json").string();
    {
        std::ofstream f(path);
        f << "not json";
    }
    bto::cloud::TokenStore store(path);
    EXPECT_FALSE(store.load().has_value());
    std::error_code ec;
    fs::remove_all(dir, ec);
}
