/**
 * @file token_store.cpp
 */

#include "token_store.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace bto::cloud {

namespace {

auto random_suffix() -> std::string {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << dist(rng);
    return oss.str();
}

void set_owner_only_permissions(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::permissions(
        p,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        ec);
}

}  // namespace

TokenStore::TokenStore(std::string path) : path_(std::move(path)) {}

auto TokenStore::load() const -> std::optional<AuthTokens> {
    std::ifstream in(path_);
    if (!in.is_open()) {
        return std::nullopt;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return std::nullopt;
    }
    if (!j.is_object()) {
        return std::nullopt;
    }
    AuthTokens t;
    try {
        t.access_token = j.at("access_token").get<std::string>();
        t.refresh_token = j.at("refresh_token").get<std::string>();
        t.expires_at = j.at("expires_at").get<std::string>();
        const auto& user = j.at("user");
        t.user_id = user.at("user_id").get<std::string>();
        t.email = user.at("email").get<std::string>();
        if (user.contains("plan") && user["plan"].is_string()) {
            t.plan = user["plan"].get<std::string>();
        }
    } catch (...) {
        return std::nullopt;
    }
    return t;
}

auto TokenStore::save(const AuthTokens& tokens) const -> bool {
    nlohmann::json j;
    j["access_token"] = tokens.access_token;
    j["refresh_token"] = tokens.refresh_token;
    j["expires_at"] = tokens.expires_at;
    j["user"] = nlohmann::json::object();
    j["user"]["user_id"] = tokens.user_id;
    j["user"]["email"] = tokens.email;
    if (!tokens.plan.empty()) {
        j["user"]["plan"] = tokens.plan;
    }

    const auto base = std::filesystem::path(path_);
    std::error_code ec;
    std::filesystem::create_directories(base.parent_path(), ec);

    const auto tmp = base.string() + ".tmp." + random_suffix();
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << j.dump(2) << '\n';
        out.flush();
        if (!out.good()) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    set_owner_only_permissions(tmp);
    std::filesystem::rename(tmp, base, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

auto TokenStore::clear() const -> bool {
    std::error_code ec;
    return std::filesystem::remove(path_, ec);
}

}  // namespace bto::cloud
