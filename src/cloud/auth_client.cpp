/**
 * @file auth_client.cpp
 */

#include "auth_client.hpp"

#include "hosted_http.hpp"
#include "hosted_parse.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <utility>

namespace bto::cloud {

auto hosted_api_base_from_env() -> std::string {
    const char* v = std::getenv("BTO_API_BASE");
    return v ? std::string(v) : std::string{};
}

AuthClient::AuthClient(HostedConfig cfg) {
    cfg_.api_base = normalize_api_base(std::move(cfg.api_base));
}

auto AuthClient::login_email_password(std::string_view email, std::string_view password,
                                      std::string* error_message) const -> std::optional<AuthTokens> {
    if (cfg_.api_base.empty()) {
        if (error_message) {
            *error_message =
                "未设置 BTO_API_BASE（托管 API 根 URL）。"
                "契约见 back-to-office/docs/openapi/hosted-api.yaml。";
        }
        return std::nullopt;
    }
    if (email.empty() || password.empty()) {
        if (error_message) {
            *error_message = "邮箱或密码为空";
        }
        return std::nullopt;
    }

    nlohmann::json req;
    req["email"] = std::string(email);
    req["password"] = std::string(password);

    const auto url = cfg_.api_base + "/v1/auth/login";
    std::string transport_err;
    const auto resp = http_json_request("POST", url, req.dump(), {}, &transport_err);
    if (resp.status == 0) {
        if (error_message) {
            *error_message = transport_err.empty() ? "HTTP 请求失败" : transport_err;
        }
        return std::nullopt;
    }
    if (resp.status == 200) {
        return parse_auth_login_response(resp.body, error_message);
    }
    if (error_message) {
        *error_message = "登录失败 HTTP " + std::to_string(resp.status) + ": " +
                         parse_hosted_error_body(resp.body);
    }
    return std::nullopt;
}

}  // namespace bto::cloud
