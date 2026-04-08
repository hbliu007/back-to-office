#include "upgrade/manifest_client.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

namespace bto::upgrade {

using Json = nlohmann::json;

namespace {

auto env_or_default(const char* key, const std::string& fallback) -> std::string {
    const char* value = std::getenv(key);
    return value ? std::string(value) : fallback;
}

constexpr const char* kDefaultManifestUrl = "http://47.99.216.25:8082/api/binaries/manifest";
constexpr const char* kDefaultBinariesBaseUrl = "http://47.99.216.25:8082/api/binaries";

auto is_insecure_http(const std::string& url) -> bool {
    return url.rfind("http://", 0) == 0;
}

auto insecure_http_allowed() -> bool {
    const char* value = std::getenv("BTO_ALLOW_INSECURE_HTTP_UPGRADE");
    return value && std::string(value) == "1";
}

auto validate_url_policy(const std::string& url, std::string* error) -> bool {
    const bool default_http =
        url == kDefaultManifestUrl || url == kDefaultBinariesBaseUrl ||
        url.rfind(std::string(kDefaultBinariesBaseUrl) + "/", 0) == 0;
    if (is_insecure_http(url) && !default_http && !insecure_http_allowed()) {
        if (error) {
            *error = "refusing insecure HTTP download; use HTTPS or set BTO_ALLOW_INSECURE_HTTP_UPGRADE=1";
        }
        return false;
    }
    return true;
}

auto validate_artifact_name(const std::string& name, std::string* error) -> bool {
    if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos || name.find('"') != std::string::npos ||
        name.find('\'') != std::string::npos) {
        if (error) {
            *error = "invalid artifact name";
        }
        return false;
    }
    return true;
}

auto read_file_url(const std::string& url, std::string* error) -> std::optional<std::string> {
    const std::string prefix = "file://";
    if (url.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    std::ifstream input(url.substr(prefix.size()), std::ios::binary);
    if (!input.is_open()) {
        if (error) {
            *error = "failed to open local file URL";
        }
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

auto copy_file_url(const std::string& url, const std::filesystem::path& output_path, std::string* error) -> bool {
    const std::string prefix = "file://";
    if (url.rfind(prefix, 0) != 0) {
        return false;
    }
    std::error_code ec;
    std::filesystem::copy_file(
        url.substr(prefix.size()), output_path, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error) {
            *error = ec.message();
        }
        return false;
    }
    return true;
}

auto curl_to_string(const std::string& url, std::string* error) -> std::optional<std::string> {
    if (!validate_url_policy(url, error)) {
        return std::nullopt;
    }
    if (auto local = read_file_url(url, error); local.has_value()) {
        return local;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (error) {
            *error = "failed to create pipe";
        }
        return std::nullopt;
    }

    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execlp("curl", "curl", "-fsSL", url.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buffer[4096];
    ssize_t bytes = 0;
    while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<std::size_t>(bytes));
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (error) {
            *error = output.empty() ? "curl failed" : output;
        }
        return std::nullopt;
    }
    return output;
}

auto curl_to_file(const std::string& url, const std::filesystem::path& output_path, std::string* error) -> bool {
    if (!validate_url_policy(url, error)) {
        return false;
    }
    if (copy_file_url(url, output_path, error)) {
        return true;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execlp("curl", "curl", "-fsSL", url.c_str(), "-o", output_path.c_str(), nullptr);
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (error) {
            *error = "curl download failed";
        }
        return false;
    }
    return true;
}

}  // namespace

ManifestClient::ManifestClient(std::string manifest_url, std::string binaries_base_url)
    : manifest_url_(std::move(manifest_url))
    , binaries_base_url_(std::move(binaries_base_url)) {}

auto ManifestClient::fetch_manifest(std::string* error) const -> std::optional<ArtifactManifest> {
    auto payload = curl_to_string(manifest_url_, error);
    if (!payload.has_value()) {
        return std::nullopt;
    }

    try {
        const auto parsed = Json::parse(*payload);
        ArtifactManifest manifest;
        manifest.version = parsed.at("version").get<std::string>();
        manifest.git_commit = parsed.value("git_commit", "");
        manifest.build_time = parsed.value("build_time", "");
        for (const auto& item : parsed.at("artifacts")) {
            ArtifactManifestEntry entry;
            entry.name = item.at("name").get<std::string>();
            entry.size = item.at("size").get<uint64_t>();
            entry.sha256 = item.at("sha256").get<std::string>();
            manifest.artifacts.push_back(std::move(entry));
        }
        return manifest;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return std::nullopt;
    }
}

auto ManifestClient::download_artifact(const ArtifactManifestEntry& artifact,
                                       const std::filesystem::path& output_path,
                                       std::string* error) const -> bool {
    if (!validate_artifact_name(artifact.name, error)) {
        return false;
    }
    std::filesystem::create_directories(output_path.parent_path());
    const auto url = binaries_base_url_ + "/" + artifact.name;
    return curl_to_file(url, output_path, error);
}

auto default_manifest_url() -> std::string {
    return env_or_default("BTO_MANIFEST_URL", kDefaultManifestUrl);
}

auto default_binaries_base_url() -> std::string {
    return env_or_default("BTO_BINARIES_URL", kDefaultBinariesBaseUrl);
}

}  // namespace bto::upgrade
