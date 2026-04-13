#pragma once

#include "upgrade/manifest_client.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace bto::upgrade {

struct UpgradeRequest {
    std::string local_did;
    std::string target_did;
    std::string relay_host;
    uint16_t relay_port = 9700;
    std::string relay_token;
    ArtifactManifestEntry artifact;
    std::filesystem::path artifact_path;
    std::string live_binary;
    std::string activate_command;
    std::string rollback_command;
    std::string health_command;
    uint32_t timeout_seconds = 30;
};

struct UpgradeResult {
    bool success = false;
    bool replaced = false;
    bool rolled_back = false;
    std::string error;
};

auto run_remote_upgrade(const UpgradeRequest& request) -> UpgradeResult;

}  // namespace bto::upgrade
