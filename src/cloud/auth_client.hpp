/**
 * @file auth_client.hpp
 * @brief Hosted auth API client (HTTP contract only; no SSH/tunnel).
 */

#pragma once

#include "token_store.hpp"

#include <optional>
#include <string>

namespace bto::cloud {

struct HostedConfig {
    std::string api_base;  // e.g. https://api.example.com — no trailing slash
};

class AuthClient {
public:
    explicit AuthClient(HostedConfig cfg);

    /** POST /v1/auth/login via curl (fork). */
    [[nodiscard]] auto login_email_password(std::string_view email, std::string_view password,
                                            std::string* error_message) const
        -> std::optional<AuthTokens>;

    [[nodiscard]] const HostedConfig& config() const { return cfg_; }

private:
    HostedConfig cfg_;
};

/** Resolve API base: env BTO_API_BASE, else empty. */
[[nodiscard]] auto hosted_api_base_from_env() -> std::string;

}  // namespace bto::cloud
