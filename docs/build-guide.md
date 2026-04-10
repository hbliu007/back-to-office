# 构建与测试指南

## 前置依赖

| 依赖 | 版本 | 安装 (macOS) |
|------|------|-------------|
| CMake | >= 3.20 | `brew install cmake` |
| Boost | any | `brew install boost` |
| spdlog | any | `brew install spdlog` |
| fmt | any | `brew install fmt` |
| Protobuf | any | `brew install protobuf` |
| GoogleTest | any | `brew install googletest` |
| PeerLink | - | `../p2p-cpp/` 已编译 |

## 编译

### Release 构建

```bash
cd back-to-office
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Debug 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### 构建产物

```
build/
  bto              # 主程序
  bto_tests        # 测试二进制
```

## 测试

### 运行全部测试

```bash
cd build && ctest --output-on-failure
# 或直接运行
./build/bto_tests
```

### 运行特定测试

```bash
./build/bto_tests --gtest_filter="Parser.*"       # CLI 解析测试
./build/bto_tests --gtest_filter="ConfigLoad.*"    # 配置加载测试
./build/bto_tests --gtest_filter="Commands.*"      # 集成测试
./build/bto_tests --gtest_filter="ResolvePeerTest.*"  # 模糊匹配测试
```

### 测试结构

```
test/
  test_main.cpp      — GTest 入口
  test_parser.cpp    — CLI 解析 (47 用例)
  test_config.cpp    — 配置管理 (40 用例)
  test_commands.cpp  — 命令集成 (29 用例)
```

**测试策略**:
- `test_parser` / `test_config`: 直接链接 `bto_testable` 库的单元测试
- `test_commands`: 通过 `popen()` 运行 `bto` 二进制的集成测试，使用隔离 HOME 目录

**不可测部分**: `p2p_bridge_v2.cpp` 依赖真实 P2P 基础设施（Relay 服务器），无法在 CI 环境中测试。connect 命令的 P2P 连接路径通过 parser + config 单元测试间接覆盖参数解析逻辑。

## 覆盖率

### 构建带覆盖率的版本

```bash
# 必须使用系统 clang（miniforge 版本不兼容 --coverage）
cmake -B build-cov \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_CXX_FLAGS="--coverage" \
    -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build-cov -j$(nproc)
```

### 生成覆盖率报告

```bash
# 清理旧数据
find build-cov -name '*.gcda' -delete

# 运行测试
./build-cov/bto_tests

# 生成 lcov 报告
lcov --capture \
     --directory build-cov \
     --output-file build-cov/coverage.info \
     --ignore-errors inconsistent

# 过滤系统头文件
lcov --remove build-cov/coverage.info \
     '/usr/*' '/opt/*' '*/test/*' '*/gtest/*' \
     --output-file build-cov/coverage_filtered.info \
     --ignore-errors unused

# 查看摘要
lcov --summary build-cov/coverage_filtered.info
```

### 覆盖率目标

| 模块 | 行覆盖率 | 说明 |
|------|----------|------|
| `parser.cpp` | ~95% | CLI 解析全路径覆盖 |
| `config.cpp` | ~95% | 加载/保存/匹配全覆盖 |
| `bto.cpp` | ~70% | P2P 相关路径无法测试 |
| `p2p_bridge_v2.cpp` | 0% | 需要真实 P2P 基础设施 |
| **可测代码合计** | **85.5%** | 超过 80% 目标 |

## CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BTO_BUILD_TESTS` | ON | 构建测试 |
| `CMAKE_BUILD_TYPE` | - | Release / Debug |

## 目录布局

```
back-to-office/
├── CMakeLists.txt          # 构建配置
├── src/
│   ├── bto.cpp             # 主入口
│   ├── p2p_bridge_v2.hpp/cpp  # P2P 桥接层
│   ├── cli/
│   │   ├── parser.hpp      # CLI 接口
│   │   └── parser.cpp      # CLI 实现
│   └── config/
│       ├── config.hpp       # 配置接口
│       └── config.cpp       # 配置实现
├── test/
│   ├── test_main.cpp
│   ├── test_parser.cpp
│   ├── test_config.cpp
│   └── test_commands.cpp
└── docs/                   # 研发文档
```
