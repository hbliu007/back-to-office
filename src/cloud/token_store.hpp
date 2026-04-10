/**
 * @file token_store.hpp
 * @brief Persist hosted auth tokens in ~/.bto/auth.json (separate from config.toml).
 */

#pragma once

#include <optional>
#include <string>

namespace bto::cloud {

struct AuthTokens {
    std::string access_token;
    std::string refresh_token;
    std::string expires_at;  // RFC 3339 UTC from server
    std::string user_id;
    std::string email;
    std::string plan;
};

class TokenStore {
public:
    explicit TokenStore(std::string path);

    [[nodiscard]] auto load() const -> std::optional<AuthTokens>;
    [[nodiscard]] auto save(const AuthTokens& tokens) const -> bool;
    [[nodiscard]] auto clear() const -> bool;

    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string path_;
};

}  // namespace bto::cloud
