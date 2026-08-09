#!/usr/bin/env bash
# =============================================================================
# PluginBased 打包入口脚本
#
# 用法:
#   ./package.sh [选项] [构建目录]
#   默认通过 release Preset 使用 ./build-release
#
# 选项:
#   -q, --qt-dir <路径>   Qt 安装根目录（含 bin/macdeployqt 等）
#                         也可通过环境变量 QT_DIR 设置
#   -v, --version         显示版本号后退出
#   -s, --skip-build      跳过编译，直接打包
#   -b, --build-only      只编译，不打包
#       --no-verify       跳过发布包完整性验证
#       --config <文件>   指定配置文件（默认 tools/package.json）
#   -h, --help            显示帮助
#
# 依赖:
#   cmake               编译
#   python3             核心打包逻辑
#   macdeployqt         macOS Qt 框架部署
#   windeployqt         Windows Qt DLL 部署
#   ldd / patchelf      Linux 依赖闭包与 rpath 修正
#   hdiutil             macOS DMG 生成（系统内置）
#
# 输出: ./dist/
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="${SCRIPT_DIR}/tools"

# ── 颜色日志 ──────────────────────────────────────────────────────────────────
_r='\033[0;31m' _g='\033[0;32m' _y='\033[1;33m' _c='\033[0;36m' _b='\033[1m' _n='\033[0m'
log_info()  { echo -e "${_c}[INFO]${_n}  $*"; }
log_ok()    { echo -e "${_g}[ OK ]${_n}  $*"; }
log_warn()  { echo -e "${_y}[WARN]${_n}  $*"; }
log_step()  { echo -e "\n${_b}── $* ──${_n}"; }
die()       { echo -e "${_r}[ERR ]${_n}  $*" >&2; exit 1; }

cmake_cache_value() {
    local key="$1"
    local cache="$2"
    sed -n "s/^${key}:[^=]*=//p" "${cache}" | head -1
}

normalize_path() {
    local value="${1%/}"
    if [[ -z "${value}" ]]; then
        printf '\n'
    elif command -v cygpath &>/dev/null; then
        cygpath -m "${value}"
    else
        printf '%s\n' "${value}"
    fi
}

validate_release_cache() {
    local cache="${BUILD_DIR}/CMakeCache.txt"
    [[ -f "${cache}" ]] || die "构建目录未初始化: ${BUILD_DIR}"

    local build_type config_types
    build_type="$(cmake_cache_value CMAKE_BUILD_TYPE "${cache}")"
    config_types="$(cmake_cache_value CMAKE_CONFIGURATION_TYPES "${cache}")"

    if [[ "${build_type}" != "Release" && ";${config_types};" != *";Release;"* ]]; then
        die "打包要求 Release 构建目录: ${BUILD_DIR}"
    fi

    if [[ "${CUSTOM_BUILD_DIR}" == false ]]; then
        local generator toolchain expected_toolchain
        generator="$(cmake_cache_value CMAKE_GENERATOR "${cache}")"
        toolchain="$(normalize_path "$(cmake_cache_value CMAKE_TOOLCHAIN_FILE "${cache}")")"
        expected_toolchain="$(normalize_path "${SCRIPT_DIR}/cmake/PluginBasedToolchain.cmake")"

        [[ "${generator}" == "Ninja" ]] ||
            die "Release 构建目录必须使用 Ninja；请不带 --skip-build 重新打包"
        [[ "${toolchain}" == "${expected_toolchain}" ]] ||
            die "Release 构建目录未使用仓库工具链；请不带 --skip-build 重新打包"
    fi
}

resolve_packaging_qt_root() {
    local cache="${BUILD_DIR}/CMakeCache.txt"
    local cached_qt_root cached_qt_dir requested_qt_root
    cached_qt_root="$(normalize_path "$(cmake_cache_value PLUGINBASED_QT_ROOT "${cache}")")"
    cached_qt_dir="$(normalize_path "$(cmake_cache_value Qt6_DIR "${cache}")")"
    requested_qt_root="$(normalize_path "${QT_DIR}")"

    if [[ -z "${cached_qt_root}" && "${cached_qt_dir}" == */lib/cmake/Qt6 ]]; then
        cached_qt_root="${cached_qt_dir%/lib/cmake/Qt6}"
    fi
    [[ -n "${cached_qt_root}" ]] ||
        die "无法从 ${cache} 解析构建使用的 Qt 根目录"
    cached_qt_root="${cached_qt_root%/}"

    if [[ -n "${requested_qt_root}" && "${requested_qt_root}" != "${cached_qt_root}" ]]; then
        die "打包 Qt 与构建 Qt 不一致: ${requested_qt_root} != ${cached_qt_root}"
    fi
    QT_DIR="${cached_qt_root}"
}

# ── 默认参数 ──────────────────────────────────────────────────────────────────
BUILD_PRESET="release"
BUILD_DIR="${SCRIPT_DIR}/build-release"
CUSTOM_BUILD_DIR=false
DIST_DIR="${SCRIPT_DIR}/dist"
QT_DIR="${QT_DIR:-${QT_ROOT:-}}"
SKIP_BUILD=false
BUILD_ONLY=false
NO_VERIFY=false
CONFIG_FILE="${TOOLS_DIR}/package.json"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

# ── 参数解析 ──────────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            sed -n '2,22p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        -q|--qt-dir)   QT_DIR="$2";         shift 2 ;;
        -s|--skip-build) SKIP_BUILD=true;   shift ;;
        -b|--build-only) BUILD_ONLY=true;   shift ;;
        --no-verify)   NO_VERIFY=true;       shift ;;
        --config)      CONFIG_FILE="$2";     shift 2 ;;
        -v|--version)
            grep -m1 'project(PluginBased VERSION' "${SCRIPT_DIR}/CMakeLists.txt" \
                | sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/'
            exit 0
            ;;
        -*)            die "未知选项: $1" ;;
        *)
            BUILD_DIR="$(cd "$1" 2>/dev/null && pwd || echo "$1")"
            CUSTOM_BUILD_DIR=true
            shift
            ;;
    esac
done

# ── 前置检查 ──────────────────────────────────────────────────────────────────
log_step "环境检查"

command -v python3 &>/dev/null || die "缺少 python3"
[[ -f "${CONFIG_FILE}" ]] || die "配置文件不存在: ${CONFIG_FILE}"

log_info "构建目录: ${BUILD_DIR}"
log_info "输出目录: ${DIST_DIR}"
[[ -n "${QT_DIR}" ]] && log_info "Qt 目录  : ${QT_DIR}"

# ── 编译 ──────────────────────────────────────────────────────────────────────
if [[ "${SKIP_BUILD}" == false ]]; then
    log_step "编译"
    command -v cmake &>/dev/null || die "缺少 cmake"

    if [[ "${CUSTOM_BUILD_DIR}" == false ]]; then
        log_info "清理默认 Release 生成目录（包含 FetchContent 子构建）"
        cmake -E remove_directory "${BUILD_DIR}"
        (
            cd "${SCRIPT_DIR}"
            cmake --preset "${BUILD_PRESET}" --fresh
        )
    elif [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
        die "自定义构建目录未初始化: ${BUILD_DIR}"
    fi

    validate_release_cache

    if [[ "${CUSTOM_BUILD_DIR}" == false ]]; then
        (
            cd "${SCRIPT_DIR}"
            cmake --build --preset "${BUILD_PRESET}" --parallel "${JOBS}"
        )
    else
        cmake --build "${BUILD_DIR}" \
              --config Release \
              --parallel "${JOBS}"
    fi
    log_ok "编译完成"
else
    validate_release_cache
fi

[[ "${BUILD_ONLY}" == true ]] && { log_ok "仅编译模式，结束"; exit 0; }

resolve_packaging_qt_root

# ── 打包（委托给 Python）──────────────────────────────────────────────────────
log_step "打包"

ARGS=(
  --build-dir "${BUILD_DIR}"
  --dist-dir  "${DIST_DIR}"
  --config    "${CONFIG_FILE}"
)

if [[ -n "${QT_DIR:-}" ]]; then
  ARGS+=(--qt-dir "${QT_DIR}")
fi
if [[ "${NO_VERIFY}" == true ]]; then
  ARGS+=(--skip-verify)
fi

python3 "${TOOLS_DIR}/deploy.py" "${ARGS[@]}"

# ── 完整性验证 ────────────────────────────────────────────────────────────────
log_info "完整性验证由打包器在归档前执行"

# ── 完成 ──────────────────────────────────────────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
log_ok "全部完成，产物位于: ${DIST_DIR}/"
ls -lh "${DIST_DIR}/" 2>/dev/null || true
