#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bto::upgrade {

struct ArtifactManifestEntry {
    std::string name;
    uint64_t size = 0;
    std::string sha256;
};

struct ArtifactManifest {
    std::string version;
    std::string git_commit;
    std::string build_time;
    std::vector<ArtifactManifestEntry> artifacts;
};

class ManifestClient {
public:
    ManifestClient(std::string manifest_url, std::string binaries_base_url);

    auto fetch_manifest(std::string* error = nullptr) const -> std::optional<ArtifactManifest>;
    auto download_artifact(const ArtifactManifestEntry& artifact,
                           const std::filesystem::path& output_path,
                           std::string* error = nullptr) const -> bool;

private:
    std::string manifest_url_;
    std::string binaries_base_url_;
};

auto default_manifest_url() -> std::string;
auto default_binaries_base_url() -> std::string;

}  // namespace bto::upgrade
