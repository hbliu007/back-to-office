#!/usr/bin/env bash
# BTO (Back-To-Office) — 一键安装脚本
# 用法: curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
#
# 可选参数:
#   --version v1.2.0    安装指定版本（默认 latest）
#   --dir /path         安装目录（默认 ~/.local/bin）
#   --help              显示帮助

set -euo pipefail

# ─── 配置 ────────────────────────────────────────────────────────

REPO="hbliu007/back-to-office"
BINARY="bto"
DEFAULT_INSTALL_DIR="${HOME}/.local/bin"

# ─── 颜色 ────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

info()  { printf "${CYAN}▸${NC} %s\n" "$*"; }
ok()    { printf "${GREEN}✓${NC} %s\n" "$*"; }
warn()  { printf "${YELLOW}!${NC} %s\n" "$*"; }
err()   { printf "${RED}✗${NC} %s\n" "$*" >&2; }
fatal() { err "$@"; exit 1; }

# ─── 参数解析 ────────────────────────────────────────────────────

VERSION="latest"
INSTALL_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version|-v) VERSION="$2"; shift 2 ;;
        --dir|-d)     INSTALL_DIR="$2"; shift 2 ;;
        --help|-h)
            echo "BTO 安装脚本"
            echo ""
            echo "用法:"
            echo "  curl -fsSL https://raw.githubusercontent.com/${REPO}/main/install.sh | bash"
            echo "  curl -fsSL ... | bash -s -- --version v1.2.0"
            echo "  curl -fsSL ... | bash -s -- --dir /usr/local/bin"
            echo ""
            echo "选项:"
            echo "  --version, -v    指定版本（默认 latest）"
            echo "  --dir, -d        安装目录（默认 ~/.local/bin）"
            echo "  --help, -h       显示帮助"
            exit 0
            ;;
        *) fatal "未知参数: $1" ;;
    esac
done

# ─── 检测系统 ────────────────────────────────────────────────────

detect_platform() {
    local os arch

    case "$(uname -s)" in
        Linux*)  os="linux" ;;
        Darwin*) os="darwin" ;;
        *)       fatal "不支持的操作系统: $(uname -s)" ;;
    esac

    case "$(uname -m)" in
        x86_64|amd64)   arch="x86_64" ;;
        aarch64|arm64)  arch="arm64" ;;
        *)              fatal "不支持的架构: $(uname -m)" ;;
    esac

    echo "${os}-${arch}"
}

# ─── 检测下载工具 ─────────────────────────────────────────────────

detect_downloader() {
    if command -v curl &>/dev/null; then
        echo "curl"
    elif command -v wget &>/dev/null; then
        echo "wget"
    else
        fatal "需要 curl 或 wget，请先安装"
    fi
}

download() {
    local url="$1" dest="$2"
    case "$(detect_downloader)" in
        curl) curl -fsSL -o "$dest" "$url" ;;
        wget) wget -q -O "$dest" "$url" ;;
    esac
}

download_text() {
    local url="$1"
    case "$(detect_downloader)" in
        curl) curl -fsSL "$url" ;;
        wget) wget -q -O- "$url" ;;
    esac
}

# ─── 获取版本 ────────────────────────────────────────────────────

resolve_version() {
    if [[ "$VERSION" == "latest" ]]; then
        info "查询最新版本..."
        VERSION=$(download_text "https://api.github.com/repos/${REPO}/releases/latest" \
            | grep '"tag_name"' | head -1 | sed -E 's/.*"tag_name":\s*"([^"]+)".*/\1/')
        if [[ -z "$VERSION" ]]; then
            fatal "无法获取最新版本。请检查 https://github.com/${REPO}/releases"
        fi
    fi
    # 确保有 v 前缀
    [[ "$VERSION" == v* ]] || VERSION="v${VERSION}"
    info "目标版本: ${BOLD}${VERSION}${NC}"
}

# ─── 安装目录 ────────────────────────────────────────────────────

resolve_install_dir() {
    if [[ -n "$INSTALL_DIR" ]]; then
        return
    fi

    # 优先 ~/.local/bin（无需 sudo）
    INSTALL_DIR="$DEFAULT_INSTALL_DIR"

    # 如果 /usr/local/bin 在 PATH 里但 ~/.local/bin 不在，提示
    if [[ ":$PATH:" != *":${DEFAULT_INSTALL_DIR}:"* ]]; then
        if [[ ":$PATH:" == *":/usr/local/bin:"* ]] && [[ -w /usr/local/bin ]]; then
            INSTALL_DIR="/usr/local/bin"
        fi
    fi

    info "安装目录: ${BOLD}${INSTALL_DIR}${NC}"
}

# ─── 主流程 ──────────────────────────────────────────────────────

main() {
    echo ""
    printf "${BOLD}${CYAN}  BTO (Back-To-Office)${NC} — P2P SSH 隧道客户端\n"
    echo "  One command to SSH into your office from anywhere"
    echo ""

    local platform
    platform=$(detect_platform)
    info "检测到平台: ${BOLD}${platform}${NC}"

    resolve_version
    resolve_install_dir

    # 构造下载 URL
    local filename="bto-${VERSION}-${platform}.tar.gz"
    local url="https://github.com/${REPO}/releases/download/${VERSION}/${filename}"

    # 创建临时目录
    local tmpdir
    tmpdir=$(mktemp -d)
    trap 'rm -rf "$tmpdir"' EXIT

    # 下载
    info "下载 ${filename}..."
    download "$url" "${tmpdir}/${filename}" || {
        err "下载失败: ${url}"
        echo ""
        err "可能原因:"
        err "  1. 版本 ${VERSION} 不存在"
        err "  2. 平台 ${platform} 暂无预编译包"
        err "  3. 网络问题（国内可能需要代理）"
        echo ""
        err "查看所有版本: https://github.com/${REPO}/releases"
        exit 1
    }

    # 下载并验证 SHA256 校验和
    local checksum_url="https://github.com/${REPO}/releases/download/${VERSION}/checksums.sha256"
    if download "${checksum_url}" "${tmpdir}/checksums.sha256" 2>/dev/null; then
        info "验证 SHA256 校验和..."
        local expected
        expected=$(grep "${filename}" "${tmpdir}/checksums.sha256" | awk '{print $1}')
        if [[ -n "$expected" ]]; then
            local actual
            if command -v sha256sum &>/dev/null; then
                actual=$(sha256sum "${tmpdir}/${filename}" | awk '{print $1}')
            elif command -v shasum &>/dev/null; then
                actual=$(shasum -a 256 "${tmpdir}/${filename}" | awk '{print $1}')
            fi
            if [[ -n "$actual" && "$actual" != "$expected" ]]; then
                fatal "SHA256 校验失败！文件可能被篡改。\n  期望: ${expected}\n  实际: ${actual}"
            fi
            ok "SHA256 校验通过"
        else
            warn "checksums.sha256 中未找到 ${filename}，跳过校验"
        fi
    else
        warn "未找到校验和文件，跳过 SHA256 验证"
    fi

    # 解压
    info "解压..."
    tar -xzf "${tmpdir}/${filename}" -C "$tmpdir"

    # 安装
    mkdir -p "$INSTALL_DIR"
    if [[ -w "$INSTALL_DIR" ]]; then
        cp "${tmpdir}/${BINARY}" "${INSTALL_DIR}/${BINARY}"
        chmod +x "${INSTALL_DIR}/${BINARY}"
    else
        info "需要 sudo 权限写入 ${INSTALL_DIR}"
        sudo cp "${tmpdir}/${BINARY}" "${INSTALL_DIR}/${BINARY}"
        sudo chmod +x "${INSTALL_DIR}/${BINARY}"
    fi

    # 验证
    if "${INSTALL_DIR}/${BINARY}" --version &>/dev/null; then
        ok "安装成功！"
    else
        warn "二进制已复制，但运行验证失败（可能缺少动态库）"
    fi

    # PATH 检查
    if [[ ":$PATH:" != *":${INSTALL_DIR}:"* ]]; then
        echo ""
        warn "${INSTALL_DIR} 不在 PATH 中"
        echo ""
        echo "  请添加到 shell 配置文件:"
        echo ""
        if [[ -f "$HOME/.zshrc" ]]; then
            echo "    echo 'export PATH=\"${INSTALL_DIR}:\$PATH\"' >> ~/.zshrc"
            echo "    source ~/.zshrc"
        else
            echo "    echo 'export PATH=\"${INSTALL_DIR}:\$PATH\"' >> ~/.bashrc"
            echo "    source ~/.bashrc"
        fi
    fi

    # 打印使用指南
    echo ""
    printf "${BOLD}${GREEN}  快速开始${NC}\n"
    echo ""
    echo "  1. 添加远端设备:"
    echo "     bto add office-213 --did office-213"
    echo ""
    echo "  2. 连接:"
    echo "     bto connect office-213"
    echo ""
    echo "  3. SSH 登录:"
    echo "     ssh -p 2222 user@127.0.0.1"
    echo ""
    echo "  更多帮助: bto help"
    echo ""
}

main "$@"
