/**
 * @file hosted_http.hpp
 * @brief Minimal HTTPS JSON requests via curl (fork), aligned with manifest_client.
 */

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bto::cloud {

/** Strip trailing slashes from API base. */
[[nodiscard]] auto normalize_api_base(std::string base) -> std::string;

struct HttpResponse {
    int status = 0;
    std::string body;
};

/**
 * GET or POST JSON to full URL (including scheme/host/path).
 * POST sends Content-Type: application/json when body is non-empty.
 */
[[nodiscard]] auto http_json_request(std::string_view method, const std::string& full_url,
                                     const std::optional<std::string>& json_body,
                                     const std::vector<std::pair<std::string, std::string>>& headers,
                                     std::string* error_message) -> HttpResponse;

}  // namespace bto::cloud
