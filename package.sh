#!/usr/bin/env bash
# =============================================================================
# VideoPlayer 打包入口脚本
#
# 用法:
#   ./package.sh [选项] [构建目录]
#
# 选项:
#   -q, --qt-dir <路径>   Qt 安装根目录（含 bin/macdeployqt 等）
#                         也可通过环境变量 QT_DIR 设置
#   -v, --version         显示版本号后退出
#   -s, --skip-build      跳过编译，直接打包
#   -b, --build-only      只编译，不打包
#       --no-verify       跳过发布包完整性验证
#       --config <文件>   指定配置文件（默认 tools/package.yml）
#   -h, --help            显示帮助
#
# 依赖:
#   cmake               编译
#   python3 + pyyaml    核心打包逻辑（pip3 install pyyaml）
#   macdeployqt         macOS Qt 框架部署
#   linuxdeployqt       Linux Qt 库部署（可选，降级为 ldd）
#   windeployqt         Windows Qt DLL 部署
#   patchelf            Linux rpath 修正（可选）
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

# ── 默认参数 ──────────────────────────────────────────────────────────────────
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist"
QT_DIR="${QT_DIR:-}"
SKIP_BUILD=false
BUILD_ONLY=false
NO_VERIFY=false
CONFIG_FILE="${TOOLS_DIR}/package.yml"
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
            grep -m1 'project(VideoPlayer VERSION' "${SCRIPT_DIR}/CMakeLists.txt" \
                | sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/'
            exit 0
            ;;
        -*)            die "未知选项: $1" ;;
        *)             BUILD_DIR="$(cd "$1" 2>/dev/null && pwd || echo "$1")"; shift ;;
    esac
done

# ── 前置检查 ──────────────────────────────────────────────────────────────────
log_step "环境检查"

command -v python3 &>/dev/null || die "缺少 python3"
python3 -c "import yaml" 2>/dev/null || {
    log_warn "缺少 PyYAML，正在安装..."
    pip3 install --quiet pyyaml || die "PyYAML 安装失败，请手动执行: pip3 install pyyaml"
}

[[ -f "${CONFIG_FILE}" ]] || die "配置文件不存在: ${CONFIG_FILE}"

log_info "构建目录: ${BUILD_DIR}"
log_info "输出目录: ${DIST_DIR}"
[[ -n "${QT_DIR}" ]] && log_info "Qt 目录  : ${QT_DIR}"

# ── 编译 ──────────────────────────────────────────────────────────────────────
if [[ "${SKIP_BUILD}" == false ]]; then
    log_step "编译"
    command -v cmake &>/dev/null || die "缺少 cmake"

    if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
        die "构建目录未初始化。请先执行:\n  cmake -B ${BUILD_DIR} -DCMAKE_BUILD_TYPE=Release ."
    fi

    cmake --build "${BUILD_DIR}" \
          --config Release \
          --parallel "${JOBS}"
    log_ok "编译完成"
fi

[[ "${BUILD_ONLY}" == true ]] && { log_ok "仅编译模式，结束"; exit 0; }

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

python3 "${TOOLS_DIR}/deploy.py" "${ARGS[@]}"

# ── 完整性验证 ────────────────────────────────────────────────────────────────
if [[ "${NO_VERIFY}" == false ]]; then
    log_step "完整性验证"

    # 找到 staging 目录（DMG 前的 .app 或 staging 文件夹）
    case "$(uname -s)" in
        Darwin)
            STAGE="${BUILD_DIR}/app/VideoPlayerApp.app"
            ;;
        Linux)
            # 找 _staging_linux 临时目录（若已清理则跳过）
            STAGE="$(find "${BUILD_DIR}" -maxdepth 2 -name "VideoPlayer-*-linux*" \
                     -type d 2>/dev/null | head -1)"
            ;;
        *)
            STAGE=""
            ;;
    esac

    if [[ -n "${STAGE}" && -d "${STAGE}" ]]; then
        python3 "${TOOLS_DIR}/verify.py" --stage-dir "${STAGE}" \
            && log_ok "验证通过" \
            || { log_warn "发现依赖问题（见上方报告），建议更新 tools/package.yml 白名单"; }
    else
        log_warn "未找到 staging 目录，跳过验证（staging 在打包时已清理属正常）"
        log_info "可手动验证: python3 tools/verify.py --stage-dir <发布包目录>"
    fi
fi

# ── 完成 ──────────────────────────────────────────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
log_ok "全部完成，产物位于: ${DIST_DIR}/"
ls -lh "${DIST_DIR}/" 2>/dev/null || true
