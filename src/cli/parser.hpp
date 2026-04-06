/**
 * @file parser.hpp
 * @brief BTO 命令行解析 — 纯客户端
 */

#pragma once

#include <string>
#include <cstdint>

namespace bto::cli {

/// 结构化退出码 (Google Style: kCamelCase)
namespace ExitCode {
    constexpr int kOk            = 0;   // 成功
    constexpr int kUsage         = 1;   // 用法错误（缺参数、未知命令）
    constexpr int kConfig        = 2;   // 配置错误（文件缺失、格式错误）
    constexpr int kNetwork       = 3;   // 网络错误（relay 不可达、连接失败）
    constexpr int kPeerNotFound  = 4;   // 目标设备未找到
    constexpr int kInterrupted   = 10;  // 信号中断 (SIGINT/SIGTERM)

    // 向后兼容别名（逐步迁移后移除）
    constexpr int OK             = kOk;
    constexpr int USAGE          = kUsage;
    constexpr int CONFIG         = kConfig;
    constexpr int NETWORK        = kNetwork;
    constexpr int PEER_NOT_FOUND = kPeerNotFound;
    constexpr int INTERRUPTED    = kInterrupted;
}

struct Command {
    std::string name;           // connect, list, add, remove, status, config, ping
    std::string target;         // peer name or DID
    std::string did;            // --did override
    std::string relay;          // --relay override
    std::string help_topic;     // help <topic> 的主题
    std::string user;           // --user (for add)
    std::string key;            // --key (for add)
    uint16_t listen_port = 2222;
    bool version = false;
    bool help    = false;
};

auto parse_arguments(int argc, char* argv[]) -> Command;
void show_help(const std::string& topic = "");
void show_version();

}  // namespace bto::cli
