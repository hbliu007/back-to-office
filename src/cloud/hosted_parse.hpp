/**
 * @file hosted_parse.hpp
 * @brief JSON parsing for hosted API responses (unit-testable).
 */

#pragma once

#include "device_client.hpp"
#include "token_store.hpp"

#include <optional>
#include <string>
#include <vector>

namespace bto::cloud {

[[nodiscard]] auto parse_auth_login_response(const std::string& json, std::string* error_message)
    -> std::optional<AuthTokens>;

/** Best-effort message from API error JSON or raw body snippet. */
[[nodiscard]] auto parse_hosted_error_body(const std::string& json) -> std::string;

[[nodiscard]] auto parse_device_list_response(const std::string& json, std::string* error_message)
    -> std::optional<std::vector<DeviceRecord>>;

[[nodiscard]] auto parse_device_create_response(const std::string& json, std::string* error_message)
    -> std::optional<DeviceCreateResult>;

}  // namespace bto::cloud
