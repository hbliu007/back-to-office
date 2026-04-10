/**
 * @file hosted_parse.cpp
 */

#include "hosted_parse.hpp"

#include <nlohmann/json.hpp>

#include <sstream>

namespace bto::cloud {

namespace {

using Json = nlohmann::json;

}  // namespace

auto parse_auth_login_response(const std::string& json, std::string* error_message)
    -> std::optional<AuthTokens> {
    try {
        const auto j = Json::parse(json);
        AuthTokens t;
        t.access_token = j.at("access_token").get<std::string>();
        t.refresh_token = j.at("refresh_token").get<std::string>();
        t.expires_at = j.at("expires_at").get<std::string>();
        const auto& user = j.at("user");
        t.user_id = user.at("user_id").get<std::string>();
        t.email = user.at("email").get<std::string>();
        if (user.contains("plan") && user["plan"].is_string()) {
            t.plan = user["plan"].get<std::string>();
        }
        return t;
    } catch (const std::exception& ex) {
        if (error_message) {
            *error_message = std::string{"解析登录响应失败: "} + ex.what();
        }
        return std::nullopt;
    }
}

auto parse_hosted_error_body(const std::string& json) -> std::string {
    try {
        const auto j = Json::parse(json);
        if (j.contains("message") && j["message"].is_string()) {
            return j["message"].get<std::string>();
        }
        if (j.contains("error") && j["error"].is_string()) {
            return j["error"].get<std::string>();
        }
    } catch (...) {
    }
    if (json.size() > 200) {
        return json.substr(0, 200) + "...";
    }
    return json;
}

auto parse_device_list_response(const std::string& json, std::string* error_message)
    -> std::optional<std::vector<DeviceRecord>> {
    try {
        const auto j = Json::parse(json);
        std::vector<DeviceRecord> out;
        for (const auto& item : j.at("devices")) {
            DeviceRecord d;
            d.device_id = item.at("device_id").get<std::string>();
            d.display_name = item.at("display_name").get<std::string>();
            d.platform = item.value("platform", std::string{"unknown"});
            d.status = item.at("status").get<std::string>();
            d.agent_version = item.value("agent_version", std::string{});
            out.push_back(std::move(d));
        }
        return out;
    } catch (const std::exception& ex) {
        if (error_message) {
            *error_message = std::string{"解析设备列表失败: "} + ex.what();
        }
        return std::nullopt;
    }
}

auto parse_device_create_response(const std::string& json, std::string* error_message)
    -> std::optional<DeviceCreateResult> {
    try {
        const auto j = Json::parse(json);
        DeviceCreateResult r;
        r.claim_code = j.at("claim_code").get<std::string>();
        r.device_id = j.at("device_id").get<std::string>();
        r.display_name = j.at("display_name").get<std::string>();
        r.expires_at = j.at("expires_at").get<std::string>();
        if (j.contains("install_url") && j["install_url"].is_string()) {
            r.install_url = j["install_url"].get<std::string>();
        }
        return r;
    } catch (const std::exception& ex) {
        if (error_message) {
            *error_message = std::string{"解析创建设备响应失败: "} + ex.what();
        }
        return std::nullopt;
    }
}

}  // namespace bto::cloud
