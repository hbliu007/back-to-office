/**
 * @file device_client.hpp
 * @brief Hosted device registry API client (HTTP only).
 */

#pragma once

#include "auth_client.hpp"

#include <optional>
#include <string>
#include <vector>

namespace bto::cloud {

struct DeviceRecord {
    std::string device_id;
    std::string display_name;
    std::string platform;
    std::string status;
    std::string agent_version;
};

struct DeviceCreateResult {
    std::string device_id;
    std::string display_name;
    std::string claim_code;
    std::string expires_at;
    std::string install_url;
};

class DeviceClient {
public:
    explicit DeviceClient(HostedConfig cfg);

    [[nodiscard]] auto list_devices(const AuthTokens& auth, std::string* error_message) const
        -> std::optional<std::vector<DeviceRecord>>;

    [[nodiscard]] auto create_device(const AuthTokens& auth, std::string_view display_name,
                                     std::string* error_message) const
        -> std::optional<DeviceCreateResult>;

private:
    HostedConfig cfg_;
};

}  // namespace bto::cloud
