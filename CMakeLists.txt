# BTO v1 — Back-To-Office P2P SSH 隧道
# 基于 PeerLink P2P 平台
cmake_minimum_required(VERSION 3.20)
project(bto VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# PeerLink P2P 库路径
set(P2P_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../p2p-cpp")
set(P2P_BUILD "${P2P_ROOT}/build")

# 查找依赖
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
set(CMAKE_PREFIX_PATH "/opt/homebrew;${CMAKE_PREFIX_PATH}")
find_package(fmt REQUIRED)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt library" FORCE)
find_package(spdlog REQUIRED)
find_package(Protobuf REQUIRED)

# PeerLink 头文件
include_directories(
    ${P2P_ROOT}/include
    ${P2P_ROOT}/third_party/spdlog/include
    ${P2P_ROOT}/third_party/nlohmann
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# PeerLink 预编译库路径
link_directories(
    ${P2P_BUILD}/src/core
    ${P2P_BUILD}/src/transport
    ${P2P_BUILD}/src/nat
    ${P2P_BUILD}/src/protocol
    ${P2P_BUILD}/src/net
    ${P2P_BUILD}/src/utils
    ${P2P_BUILD}/src/security
    ${P2P_BUILD}/src/crypto
    ${P2P_BUILD}/proto
)

add_library(bto_runtime STATIC
    src/p2p_bridge_v2.cpp
    src/cli/parser.cpp
    src/config/config.cpp
    src/daemon/local_endpoint_manager.cpp
    src/daemon/peerlink_service.cpp
    src/daemon/daemon_client.cpp
    src/upgrade/manifest_client.cpp
    src/upgrade/upgrade_client.cpp
    src/upgrade/upgrade_precheck.cpp
    src/util/io_context_thread.cpp
)

add_executable(bto
    src/bto.cpp
)

add_executable(peerlinkd
    src/peerlinkd_main.cpp
)

# 保留旧版本用于对比测试
add_executable(bto_v1
    src/bto.cpp
)

target_link_libraries(bto_runtime PUBLIC
    p2p_core
    p2p_transport
    p2p_nat
    p2p_protocol
    p2p_net
    p2p_utils
    p2p_security
    p2p-crypto
    p2p-proto
    Boost::headers
    Threads::Threads
    spdlog::spdlog
    fmt::fmt
    protobuf::libprotobuf
    OpenSSL::Crypto
)

target_link_libraries(bto
    bto_runtime
)

target_link_libraries(peerlinkd
    bto_runtime
)

target_link_libraries(bto_v1
    bto_runtime
)

install(TARGETS bto peerlinkd bto_v1 RUNTIME DESTINATION bin)

# ─── 测试 ───────────────────────────────────────────────────────
option(BTO_BUILD_TESTS "构建单元测试" ON)

if(BTO_BUILD_TESTS)
    enable_testing()

    # 可测试的库（不含 P2P 依赖的 bto.cpp / p2p_bridge.cpp）
    add_library(bto_testable STATIC
        src/cli/parser.cpp
        src/config/config.cpp
    )
    target_include_directories(bto_testable PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

    find_package(GTest REQUIRED)

    add_executable(bto_tests
        test/test_main.cpp
        test/test_parser.cpp
        test/test_config.cpp
        test/test_commands.cpp
        test/test_manifest_client.cpp
    )
    target_include_directories(bto_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(bto_tests
        bto_testable
        bto_runtime
        GTest::gtest
    )
    # 让集成测试知道 bto 二进制路径
    target_compile_definitions(bto_tests PRIVATE
        BTO_BINARY="$<TARGET_FILE:bto>"
    )

    include(GoogleTest)
    gtest_discover_tests(bto_tests)
endif()
