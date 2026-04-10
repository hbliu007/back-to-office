/**
 * @file device_client.cpp
 */

#include "device_client.hpp"

#include "hosted_http.hpp"
#include "hosted_parse.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace bto::cloud {

DeviceClient::DeviceClient(HostedConfig cfg) {
    cfg_.api_base = normalize_api_base(std::move(cfg.api_base));
}

auto DeviceClient::list_devices(const AuthTokens& auth, std::string* error_message) const
    -> std::optional<std::vector<DeviceRecord>> {
    if (cfg_.api_base.empty()) {
        if (error_message) {
            *error_message = "未设置 BTO_API_BASE。契约见 back-to-office/docs/openapi/hosted-api.yaml。";
        }
        return std::nullopt;
    }

    const auto url = cfg_.api_base + "/v1/devices";
    std::string transport_err;
    const std::vector<std::pair<std::string, std::string>> headers{
        {"Authorization", "Bearer " + auth.access_token},
    };
    const auto resp =
        http_json_request("GET", url, std::nullopt, headers, &transport_err);
    if (resp.status == 0) {
        if (error_message) {
            *error_message = transport_err.empty() ? "HTTP 请求失败" : transport_err;
        }
        return std::nullopt;
    }
    if (resp.status == 200) {
        return parse_device_list_response(resp.body, error_message);
    }
    if (error_message) {
        *error_message = "列出设备失败 HTTP " + std::to_string(resp.status) + ": " +
                         parse_hosted_error_body(resp.body);
    }
    return std::nullopt;
}

auto DeviceClient::create_device(const AuthTokens& auth, std::string_view display_name,
                                 std::string* error_message) const -> std::optional<DeviceCreateResult> {
    if (cfg_.api_base.empty()) {
        if (error_message) {
            *error_message = "未设置 BTO_API_BASE。契约见 back-to-office/docs/openapi/hosted-api.yaml。";
        }
        return std::nullopt;
    }
    if (display_name.empty()) {
        if (error_message) {
            *error_message = "设备显示名为空";
        }
        return std::nullopt;
    }

    nlohmann::json req;
    req["display_name"] = std::string(display_name);

    const auto url = cfg_.api_base + "/v1/devices";
    std::string transport_err;
    const std::vector<std::pair<std::string, std::string>> headers{
        {"Authorization", "Bearer " + auth.access_token},
    };
    const auto resp =
        http_json_request("POST", url, req.dump(), headers, &transport_err);
    if (resp.status == 0) {
        if (error_message) {
            *error_message = transport_err.empty() ? "HTTP 请求失败" : transport_err;
        }
        return std::nullopt;
    }
    if (resp.status == 200 || resp.status == 201) {
        return parse_device_create_response(resp.body, error_message);
    }
    if (error_message) {
        *error_message = "创建设备失败 HTTP " + std::to_string(resp.status) + ": " +
                         parse_hosted_error_body(resp.body);
    }
    return std::nullopt;
}

}  // namespace bto::cloud
