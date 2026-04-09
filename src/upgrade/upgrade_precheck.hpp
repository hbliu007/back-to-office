#pragma once

#include "upgrade/manifest_client.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace bto::upgrade {

// Returns true if file exists and SHA256 (hex) matches manifest entry (case-insensitive).
auto local_artifact_matches_manifest(const std::filesystem::path& path,
                                       const ArtifactManifestEntry& artifact) -> bool;

// Run: ssh -batch ... user@host sha256sum <remote_path>, parse first field. Empty key skips -i.
auto remote_live_binary_sha256(const std::string& host,
                               uint16_t ssh_port,
                               const std::string& user,
                               const std::string& key_path,
                               const std::string& remote_path,
                               std::string* error) -> std::optional<std::string>;

auto normalize_sha256_hex(std::string value) -> std::string;

}  // namespace bto::upgrade
