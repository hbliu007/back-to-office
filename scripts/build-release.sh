#!/usr/bin/env bash
# build-release.sh — 本地构建 + 上传 GitHub Release
#
# 用法:
#   ./scripts/build-release.sh v1.1.0          # 构建当前平台 + 上传
#   ./scripts/build-release.sh v1.1.0 --local  # 仅构建，不上传
#
# 前置条件:
#   - p2p-cpp 已编译（静态库）
#   - gh CLI 已安装并登录（用于上传）
#   - Docker 已安装（用于 Linux 交叉编译，可选）

set -euo pipefail

REPO="hbliu007/back-to-office"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P2P_ROOT="${PROJECT_ROOT}/../p2p-cpp"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

info()  { printf "${CYAN}▸${NC} %s\n" "$*"; }
ok()    { printf "${GREEN}✓${NC} %s\n" "$*"; }
fatal() { printf "${RED}✗${NC} %s\n" "$*" >&2; exit 1; }

# ─── 参数 ────────────────────────────────────────────────────────

VERSION="${1:-}"
LOCAL_ONLY=false

[[ -z "$VERSION" ]] && fatal "用法: $0 <version> [--local]"
[[ "$VERSION" == v* ]] || VERSION="v${VERSION}"
[[ "${2:-}" == "--local" ]] && LOCAL_ONLY=true

DIST_DIR="${PROJECT_ROOT}/dist/${VERSION}"
mkdir -p "$DIST_DIR"

# ─── 检测当前平台 ────────────────────────────────────────────────

detect_platform() {
    local os arch
    case "$(uname -s)" in
        Linux*)  os="linux" ;;
        Darwin*) os="darwin" ;;
        *)       fatal "不支持: $(uname -s)" ;;
    esac
    case "$(uname -m)" in
        x86_64|amd64)  arch="x86_64" ;;
        aarch64|arm64) arch="arm64" ;;
        *)             fatal "不支持: $(uname -m)" ;;
    esac
    echo "${os}-${arch}"
}

# ─── 构建当前平台 ────────────────────────────────────────────────

build_native() {
    local platform
    platform=$(detect_platform)
    info "构建 ${BOLD}${platform}${NC}..."

    cd "$PROJECT_ROOT"
    cmake -B build-release \
        -DCMAKE_BUILD_TYPE=Release \
        -DBTO_BUILD_TESTS=OFF \
        -DCMAKE_C_COMPILER=/usr/bin/cc \
        -DCMAKE_CXX_COMPILER=/usr/bin/c++
    cmake --build build-release -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

    local filename="bto-${VERSION}-${platform}.tar.gz"
    tar -czf "${DIST_DIR}/${filename}" -C build-release bto peerlinkd

    ok "构建完成: ${filename}"
    file build-release/bto
    build-release/bto --version
}

# ─── Docker 交叉编译 Linux ───────────────────────────────────────

build_linux_docker() {
    local arch="${1:-x86_64}"
    local docker_platform="linux/amd64"
    [[ "$arch" == "arm64" ]] && docker_platform="linux/arm64"

    info "Docker 交叉编译 ${BOLD}linux-${arch}${NC}..."

    if ! command -v docker &>/dev/null; then
        fatal "Docker 未安装，跳过 Linux 构建"
    fi

    docker run --rm --platform "$docker_platform" \
        -v "${PROJECT_ROOT}:/bto" \
        -v "${P2P_ROOT}:/p2p-cpp" \
        -w /bto \
        ubuntu:22.04 bash -c "
            set -ex
            apt-get update && apt-get install -y --no-install-recommends \
                build-essential cmake pkg-config \
                libssl-dev libboost-all-dev \
                libprotobuf-dev protobuf-compiler \
                libspdlog-dev libfmt-dev
            cmake -B build-docker \
                -DCMAKE_BUILD_TYPE=Release \
                -DBTO_BUILD_TESTS=OFF \
                -DCMAKE_EXE_LINKER_FLAGS='-static-libgcc -static-libstdc++'
            cmake --build build-docker -j\$(nproc)
            file build-docker/bto
            build-docker/bto --version
        "

    local filename="bto-${VERSION}-linux-${arch}.tar.gz"
    tar -czf "${DIST_DIR}/${filename}" -C "${PROJECT_ROOT}/build-docker" bto peerlinkd

    ok "Docker 构建完成: ${filename}"
}

# ─── 上传到 GitHub Releases ──────────────────────────────────────

upload_release() {
    if ! command -v gh &>/dev/null; then
        fatal "gh CLI 未安装。请先: brew install gh && gh auth login"
    fi

    info "上传到 GitHub Releases..."

    # 生成校验和
    cd "$DIST_DIR"
    shasum -a 256 bto-*.tar.gz > checksums.sha256
    cat checksums.sha256

    # 创建 Release（如果不存在）
    gh release create "$VERSION" \
        --repo "$REPO" \
        --title "BTO ${VERSION}" \
        --generate-notes \
        --draft \
        bto-*.tar.gz checksums.sha256 \
    || {
        # Release 已存在，上传文件
        info "Release 已存在，上传文件..."
        gh release upload "$VERSION" \
            --repo "$REPO" \
            --clobber \
            bto-*.tar.gz checksums.sha256
    }

    ok "上传完成！"
    echo ""
    echo "  查看: https://github.com/${REPO}/releases/tag/${VERSION}"
    echo "  注意: Release 为草稿状态，请到 GitHub 手动发布"
}

# ─── 主流程 ──────────────────────────────────────────────────────

main() {
    echo ""
    info "BTO Release Builder ${BOLD}${VERSION}${NC}"
    echo ""

    # 检查 p2p-cpp 库
    if [[ ! -d "$P2P_ROOT/build/src/core" ]]; then
        fatal "p2p-cpp 未编译。请先: cd ${P2P_ROOT} && cmake -B build ... && cmake --build build"
    fi

    # 构建当前平台
    build_native

    # 可选: Docker 交叉编译（--local 模式跳过交互）
    if [[ "$LOCAL_ONLY" == "false" ]] && [[ "$(uname -s)" == "Darwin" ]] && command -v docker &>/dev/null; then
        echo ""
        info "检测到 Docker，是否交叉编译 Linux 版本？"
        read -rp "  构建 linux-x86_64? [y/N] " yn
        [[ "$yn" =~ ^[Yy]$ ]] && build_linux_docker x86_64
    fi

    # 上传
    if [[ "$LOCAL_ONLY" == "false" ]]; then
        echo ""
        upload_release
    else
        echo ""
        ok "构建产物在: ${DIST_DIR}/"
        ls -lh "$DIST_DIR"/
    fi
}

main
