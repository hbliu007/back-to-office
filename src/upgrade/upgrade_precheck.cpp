#include "upgrade/upgrade_precheck.hpp"

#include "p2p/core/artifact_transfer.hpp"

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace bto::upgrade {

auto normalize_sha256_hex(std::string value) -> std::string {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

auto local_artifact_matches_manifest(const std::filesystem::path& path,
                                     const ArtifactManifestEntry& artifact) -> bool {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
    const auto digest = p2p::core::compute_artifact_sha256(path);
    if (!digest.has_value()) {
        return false;
    }
    return normalize_sha256_hex(*digest) == normalize_sha256_hex(artifact.sha256);
}

namespace {

auto insecure_ssh_precheck_allowed() -> bool {
    const char* value = std::getenv("BTO_ALLOW_INSECURE_SSH_PRECHECK");
    return value && std::string(value) == "1";
}

auto wait_for_child(pid_t pid, int* status, std::string* error) -> bool {
    for (;;) {
        const auto rc = waitpid(pid, status, 0);
        if (rc >= 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        if (error) {
            *error = "waitpid failed";
        }
        return false;
    }
}

auto is_safe_remote_path(const std::string& p) -> bool {
    if (p.empty() || p[0] != '/') {
        return false;
    }
    if (p.find("..") != std::string::npos) {
        return false;
    }
    for (char ch : p) {
        if (ch == ';' || ch == '|' || ch == '`' || ch == '\n' || ch == '\r' || ch == '$') {
            return false;
        }
    }
    return true;
}

auto is_reasonable_ssh_host(const std::string& h) -> bool {
    if (h.empty() || h.size() > 253) {
        return false;
    }
    for (char ch : h) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' ||
                ch == ':' || ch == '[' || ch == ']')) {
            return false;
        }
    }
    return true;
}

auto is_reasonable_ssh_user(const std::string& user) -> bool {
    if (user.empty() || user.size() > 64) {
        return false;
    }
    for (char ch : user) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    return true;
}

auto shell_single_quote(const std::string& value) -> std::string {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

}  // namespace

auto remote_live_binary_sha256(const std::string& host,
                               uint16_t ssh_port,
                               const std::string& user,
                               const std::string& key_path,
                               const std::string& remote_path,
                               std::string* error) -> std::optional<std::string> {
    if (!is_reasonable_ssh_host(host)) {
        if (error) {
            *error = "invalid ssh host";
        }
        return std::nullopt;
    }
    if (!is_safe_remote_path(remote_path)) {
        if (error) {
            *error = "invalid remote path";
        }
        return std::nullopt;
    }
    if (!is_reasonable_ssh_user(user)) {
        if (error) {
            *error = "invalid ssh user";
        }
        return std::nullopt;
    }

    const std::string remote_cmd = "sha256sum -- " + shell_single_quote(remote_path);

    std::vector<std::string> storage;
    storage.emplace_back("ssh");
    storage.emplace_back("-o");
    storage.emplace_back("BatchMode=yes");
    if (insecure_ssh_precheck_allowed()) {
        storage.emplace_back("-o");
        storage.emplace_back("StrictHostKeyChecking=no");
        storage.emplace_back("-o");
        storage.emplace_back("UserKnownHostsFile=/dev/null");
    }
    storage.emplace_back("-o");
    storage.emplace_back("ConnectTimeout=10");
    storage.emplace_back("-p");
    storage.emplace_back(std::to_string(ssh_port));
    storage.emplace_back("-l");
    storage.emplace_back(user);
    if (!key_path.empty()) {
        storage.emplace_back("-i");
        storage.emplace_back(key_path);
    }
    storage.emplace_back(host);
    storage.emplace_back(remote_cmd);

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& s : storage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (error) {
            *error = "pipe failed";
        }
        return std::nullopt;
    }

    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp("ssh", argv.data());
        _exit(127);
    }
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (error) {
            *error = "fork failed";
        }
        return std::nullopt;
    }

    close(pipefd[1]);
    std::string output;
    std::array<char, 4096> buf{};
    for (;;) {
        const auto n = read(pipefd[0], buf.data(), buf.size());
        if (n <= 0) {
            break;
        }
        output.append(buf.data(), static_cast<size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    if (!wait_for_child(pid, &status, error)) {
        return std::nullopt;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (error) {
            *error = "ssh failed: " + output;
        }
        return std::nullopt;
    }

    // ssh may print host key warnings on stderr (merged into capture); skip non-hex lines.
    auto is_hex64 = [](const std::string& s) -> bool {
        if (s.size() != 64) {
            return false;
        }
        for (unsigned char ch : s) {
            if (!std::isxdigit(ch)) {
                return false;
            }
        }
        return true;
    };

    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream ls(line);
        std::string token;
        if (ls >> token && is_hex64(token)) {
            return normalize_sha256_hex(std::move(token));
        }
    }

    if (error) {
        *error = "could not parse sha256sum output";
    }
    return std::nullopt;
}

}  // namespace bto::upgrade
