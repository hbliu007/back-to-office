/**
 * @file hosted_http.cpp
 */

#include "hosted_http.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bto::cloud {

namespace {

auto allow_insecure_api() -> bool {
    const char* v = std::getenv("BTO_ALLOW_INSECURE_HTTP_API");
    return v && std::string(v) == "1";
}

auto validate_api_url(const std::string& url, std::string* error) -> bool {
    if (url.rfind("https://", 0) == 0) {
        return true;
    }
    if (url.rfind("http://", 0) == 0) {
        if (allow_insecure_api()) {
            return true;
        }
        if (error) {
            *error =
                "拒绝使用 http:// 访问托管 API；请使用 https://，"
                "或开发环境设置 BTO_ALLOW_INSECURE_HTTP_API=1";
        }
        return false;
    }
    if (error) {
        *error = "URL 必须以 https:// 或 http:// 开头";
    }
    return false;
}

void append_curl_proto_args(std::vector<std::string>& args) {
    if (allow_insecure_api()) {
        args.insert(args.end(), {"--proto", "=http,https", "--proto-redir", "=http,https"});
    } else {
        args.insert(args.end(), {"--proto", "=https", "--proto-redir", "=https"});
    }
}

auto wait_child(pid_t pid, std::string* error) -> int {
    int status = 0;
    for (;;) {
        const auto rc = waitpid(pid, &status, 0);
        if (rc >= 0) {
            break;
        }
        if (errno != EINTR) {
            if (error) {
                *error = "waitpid failed";
            }
            return -1;
        }
    }
    if (!WIFEXITED(status)) {
        if (error) {
            *error = "curl 异常退出";
        }
        return -1;
    }
    return WEXITSTATUS(status);
}

auto read_fd_string(int fd) -> std::string {
    std::string out;
    std::array<char, 4096> buf{};
    for (;;) {
        const auto n = read(fd, buf.data(), buf.size());
        if (n <= 0) {
            break;
        }
        out.append(buf.data(), static_cast<std::size_t>(n));
    }
    return out;
}

auto trim(std::string s) -> std::string {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(0, 1);
    }
    return s;
}

auto read_file_string(const std::string& path, std::string* error) -> std::string {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        if (error) {
            *error = "读取响应文件失败";
        }
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

auto normalize_api_base(std::string base) -> std::string {
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

auto http_json_request(std::string_view method, const std::string& full_url,
                       const std::optional<std::string>& json_body,
                       const std::vector<std::pair<std::string, std::string>>& headers,
                       std::string* error_message) -> HttpResponse {
    HttpResponse out;
    if (!validate_api_url(full_url, error_message)) {
        return out;
    }

    const bool is_post = method == "POST";
    if (is_post && !json_body.has_value()) {
        if (error_message) {
            *error_message = "internal: POST 缺少 body";
        }
        return out;
    }

    std::string req_path;
    if (is_post) {
        char tmpl[] = "/tmp/bto_reqXXXXXX";
        const int fd = mkstemp(tmpl);
        if (fd < 0) {
            if (error_message) {
                *error_message = "mkstemp 失败";
            }
            return out;
        }
        {
            const std::string& body = *json_body;
            const auto written = write(fd, body.data(), body.size());
            close(fd);
            if (written != static_cast<ssize_t>(body.size())) {
                unlink(tmpl);
                if (error_message) {
                    *error_message = "写入请求体失败";
                }
                return out;
            }
        }
        req_path = tmpl;
    }

    char resp_tmpl[] = "/tmp/bto_respXXXXXX";
    const int rfd = mkstemp(resp_tmpl);
    if (rfd < 0) {
        if (!req_path.empty()) {
            unlink(req_path.c_str());
        }
        if (error_message) {
            *error_message = "mkstemp 失败";
        }
        return out;
    }
    close(rfd);
    const std::string resp_path = resp_tmpl;

    std::array<int, 2> out_pipe{};
    std::array<int, 2> err_pipe{};
    if (pipe(out_pipe.data()) != 0 || pipe(err_pipe.data()) != 0) {
        if (!req_path.empty()) {
            unlink(req_path.c_str());
        }
        unlink(resp_path.c_str());
        if (error_message) {
            *error_message = "pipe 失败";
        }
        return out;
    }

    std::vector<std::string> arg_storage;
    arg_storage.reserve(32);
    arg_storage.push_back("curl");
    arg_storage.push_back("-sS");
    arg_storage.push_back("-g");
    arg_storage.push_back("--connect-timeout");
    arg_storage.push_back("15");
    arg_storage.push_back("--max-time");
    arg_storage.push_back("120");
    append_curl_proto_args(arg_storage);
    arg_storage.push_back("-X");
    arg_storage.push_back(std::string(method));
    for (const auto& [k, v] : headers) {
        arg_storage.push_back("-H");
        arg_storage.push_back(k + ": " + v);
    }
    if (is_post) {
        arg_storage.push_back("-H");
        arg_storage.push_back("Content-Type: application/json");
        arg_storage.push_back("--data-binary");
        arg_storage.push_back("@" + req_path);
    }
    arg_storage.push_back("-o");
    arg_storage.push_back(resp_path);
    arg_storage.push_back("-w");
    arg_storage.push_back("%{http_code}");
    arg_storage.push_back(full_url);

    std::vector<char*> argv;
    argv.reserve(arg_storage.size() + 1);
    for (auto& s : arg_storage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        if (!req_path.empty()) {
            unlink(req_path.c_str());
        }
        unlink(resp_path.c_str());
        if (error_message) {
            *error_message = "fork 失败";
        }
        return out;
    }

    if (pid == 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
        execvp("curl", argv.data());
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    const std::string code_str = read_fd_string(out_pipe[0]);
    const std::string err_str = read_fd_string(err_pipe[0]);
    close(out_pipe[0]);
    close(err_pipe[0]);

    const int exit_code = wait_child(pid, error_message);
    if (!req_path.empty()) {
        unlink(req_path.c_str());
    }

    if (exit_code != 0) {
        unlink(resp_path.c_str());
        if (error_message) {
            *error_message = err_str.empty() ? "curl 执行失败" : err_str;
        }
        return out;
    }

    try {
        out.status = std::stoi(trim(code_str));
    } catch (...) {
        unlink(resp_path.c_str());
        if (error_message) {
            *error_message = "无法解析 HTTP 状态: " + code_str;
        }
        return out;
    }

    out.body = read_file_string(resp_path, error_message);
    unlink(resp_path.c_str());
    return out;
}

}  // namespace bto::cloud
