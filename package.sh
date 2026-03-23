#!/usr/bin/env bash
# =============================================================================
#  VideoPlayer — 打包脚本
#  用法：
#    ./package.sh [BUILD_DIR] [OPTION]
#
#  参数：
#    BUILD_DIR   CMake 构建目录，默认 ./build
#
#  选项：
#    --qt-dir    Qt 安装根目录（含 bin/macdeployqt 等工具），
#                优先级低于环境变量 QT_DIR
#    --version   覆盖版本号（默认从 CMakeLists.txt 读取）
#    --skip-build  跳过 cmake --build，只做打包（已编译好时使用）
#    -h/--help   显示帮助
#
#  输出：
#    macOS   →  dist/VideoPlayer-<ver>-macOS.dmg
#    Linux   →  dist/VideoPlayer-<ver>-linux-x86_64.tar.gz
#    Windows →  dist/VideoPlayer-<ver>-win64.zip
#               dist/VideoPlayer-<ver>-win64-installer.exe  （需 NSIS）
# =============================================================================
set -euo pipefail

# ── 颜色输出 ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
die()     { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }
step()    { echo -e "\n${BOLD}▶ $*${NC}"; }

# ── 默认值 ────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist"
SKIP_BUILD=false
QT_DIR="${QT_DIR:-}"
APP_NAME="VideoPlayer"
APP_BINARY="VideoPlayerApp"

# ── 从 CMakeLists.txt 读取版本 ────────────────────────────────────────────────
read_version() {
    grep -m1 'project(VideoPlayer VERSION' "${SCRIPT_DIR}/CMakeLists.txt" \
        | sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/'
}
VERSION="$(read_version)"

# ── 参数解析 ──────────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        --qt-dir)    QT_DIR="$2";    shift 2 ;;
        --version)   VERSION="$2";   shift 2 ;;
        --skip-build) SKIP_BUILD=true; shift ;;
        -*)          die "未知选项: $1" ;;
        *)           BUILD_DIR="$1"; shift ;;
    esac
done

# ── 平台检测 ──────────────────────────────────────────────────────────────────
case "$(uname -s)" in
    Darwin)  PLATFORM="macos"   ;;
    Linux)   PLATFORM="linux"   ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    *)       die "不支持的平台: $(uname -s)" ;;
esac

info "平台: ${PLATFORM}  版本: ${VERSION}"
info "构建目录: ${BUILD_DIR}"
info "输出目录: ${DIST_DIR}"

# ── 工具检查辅助 ──────────────────────────────────────────────────────────────
require_tool() {
    command -v "$1" &>/dev/null || die "缺少必要工具: $1  请先安装或检查 PATH"
}

find_qt_tool() {
    local tool="$1"
    # 1. 优先用环境变量 QT_DIR
    if [[ -n "${QT_DIR}" ]]; then
        local candidate="${QT_DIR}/bin/${tool}"
        [[ -x "${candidate}" ]] && { echo "${candidate}"; return; }
    fi
    # 2. PATH 里直接找
    if command -v "${tool}" &>/dev/null; then
        command -v "${tool}"
        return
    fi
    # 3. 常见安装位置兜底（macOS Homebrew / Linux）
    for prefix in /usr/local/opt/qt/bin /usr/lib/qt6/bin /opt/Qt/*/gcc_64/bin \
                  "${HOME}/Qt/*/macos/bin" "${HOME}/Qt/*/gcc_64/bin"; do
        # shellcheck disable=SC2086
        local found; found=$(ls ${prefix}/${tool} 2>/dev/null | head -1)
        [[ -x "${found}" ]] && { echo "${found}"; return; }
    done
    echo ""   # 未找到
}

# ── 可选：编译 ────────────────────────────────────────────────────────────────
if [[ "${SKIP_BUILD}" == false ]]; then
    step "编译项目"
    require_tool cmake
    [[ -f "${BUILD_DIR}/CMakeCache.txt" ]] || \
        die "构建目录未初始化，请先运行:\n  cmake -B ${BUILD_DIR} -DCMAKE_BUILD_TYPE=Release ..."
    cmake --build "${BUILD_DIR}" --config Release --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
    success "编译完成"
fi

# ── 清理并创建 dist ───────────────────────────────────────────────────────────
mkdir -p "${DIST_DIR}"

# =============================================================================
#  macOS — .app bundle + DMG
# =============================================================================
package_macos() {
    step "打包 macOS (.app + DMG)"

    local app_bundle="${BUILD_DIR}/app/${APP_BINARY}.app"
    [[ -d "${app_bundle}" ]] || \
        die ".app bundle 不存在: ${app_bundle}\n请确认构建成功且 MACOSX_BUNDLE=TRUE"

    # ── 查找 macdeployqt ──────────────────────────────────────────────────────
    local macdeployqt; macdeployqt="$(find_qt_tool macdeployqt)"
    [[ -x "${macdeployqt}" ]] || \
        die "找不到 macdeployqt，请设置 QT_DIR 环境变量或 --qt-dir 参数"

    # ── macdeployqt：拷贝 Qt 框架、QML 模块 ──────────────────────────────────
    info "运行 macdeployqt..."
    "${macdeployqt}" "${app_bundle}" \
        -qmldir="${SCRIPT_DIR}/app/qml" \
        -verbose=1

    success "macdeployqt 完成"

    # ── 拷贝插件 .dylib ───────────────────────────────────────────────────────
    local plugins_src="${BUILD_DIR}/plugins"
    local plugins_dst="${app_bundle}/Contents/PlugIns/videoplayer"
    if [[ -d "${plugins_src}" ]]; then
        mkdir -p "${plugins_dst}"
        find "${plugins_src}" -name "*.dylib" -exec cp {} "${plugins_dst}/" \;
        info "插件已拷贝到 Contents/PlugIns/videoplayer/"
    fi

    # ── 生成 DMG ─────────────────────────────────────────────────────────────
    local dmg_name="${APP_NAME}-${VERSION}-macOS.dmg"
    local dmg_path="${DIST_DIR}/${dmg_name}"
    local staging="${BUILD_DIR}/_dmg_staging"

    rm -rf "${staging}"
    mkdir -p "${staging}"
    cp -R "${app_bundle}" "${staging}/"

    # 创建 Applications 快捷方式
    ln -s /Applications "${staging}/Applications"

    info "生成 DMG: ${dmg_name}"
    hdiutil create \
        -volname "${APP_NAME} ${VERSION}" \
        -srcfolder "${staging}" \
        -ov -format UDZO \
        "${dmg_path}"

    rm -rf "${staging}"
    success "输出: ${dmg_path}"
}

# =============================================================================
#  Linux — AppDir + tar.gz（可选 AppImage）
# =============================================================================
package_linux() {
    step "打包 Linux (tar.gz)"

    local bin_path="${BUILD_DIR}/app/${APP_BINARY}"
    [[ -x "${bin_path}" ]] || die "可执行文件不存在: ${bin_path}"

    local pkg_name="${APP_NAME}-${VERSION}-linux-x86_64"
    local staging="${BUILD_DIR}/_linux_staging/${pkg_name}"
    rm -rf "${staging}"
    mkdir -p "${staging}/bin" "${staging}/plugins" "${staging}/lib"

    # ── 拷贝主程序 ────────────────────────────────────────────────────────────
    cp "${bin_path}" "${staging}/bin/${APP_BINARY}"

    # ── 拷贝插件 ─────────────────────────────────────────────────────────────
    find "${BUILD_DIR}/plugins" -name "*.so" -exec cp {} "${staging}/plugins/" \; 2>/dev/null || true

    # ── 用 linuxdeployqt 或手动收集 Qt .so ───────────────────────────────────
    local linuxdeployqt; linuxdeployqt="$(find_qt_tool linuxdeployqt)"
    if [[ -x "${linuxdeployqt}" ]]; then
        info "运行 linuxdeployqt..."
        "${linuxdeployqt}" "${staging}/bin/${APP_BINARY}" \
            -qmldir="${SCRIPT_DIR}/app/qml" \
            -bundle-non-qt-libs \
            -no-translations \
            -verbose=1
    else
        warn "未找到 linuxdeployqt，尝试手动收集 Qt 依赖库..."
        collect_qt_libs_linux "${bin_path}" "${staging}/lib"
        write_run_script_linux "${staging}"
    fi

    # ── 写启动脚本 ────────────────────────────────────────────────────────────
    write_run_script_linux "${staging}"

    # ── 打包 tar.gz ───────────────────────────────────────────────────────────
    local tarball="${DIST_DIR}/${pkg_name}.tar.gz"
    tar -czf "${tarball}" -C "${BUILD_DIR}/_linux_staging" "${pkg_name}"
    rm -rf "${BUILD_DIR}/_linux_staging"
    success "输出: ${tarball}"

    # ── 可选 AppImage ─────────────────────────────────────────────────────────
    if command -v appimagetool &>/dev/null; then
        step "生成 AppImage"
        # appimagetool 需要 AppDir 格式，此处做最小化适配
        local appdir="${BUILD_DIR}/_appimage/${pkg_name}.AppDir"
        mkdir -p "${appdir}/usr/bin" "${appdir}/usr/lib"
        cp "${bin_path}" "${appdir}/usr/bin/${APP_BINARY}"
        write_desktop_file "${appdir}" "${APP_BINARY}"
        ARCH=x86_64 appimagetool "${appdir}" \
            "${DIST_DIR}/${pkg_name}.AppImage" 2>&1 | tail -5
        rm -rf "${BUILD_DIR}/_appimage"
        success "AppImage 输出: ${DIST_DIR}/${pkg_name}.AppImage"
    else
        info "提示：安装 appimagetool 后可额外生成 AppImage"
    fi
}

# 手动收集 ldd 依赖中属于 Qt 的 .so
collect_qt_libs_linux() {
    local binary="$1"
    local lib_dst="$2"
    require_tool ldd
    ldd "${binary}" 2>/dev/null \
        | grep -iE 'Qt6|libspdlog|libfmt' \
        | awk '{print $3}' \
        | grep '^/' \
        | while read -r so; do
            [[ -f "${so}" ]] && cp -n "${so}" "${lib_dst}/"
          done
    info "已收集依赖库到 lib/"
}

write_run_script_linux() {
    local dir="$1"
    cat > "${dir}/run.sh" <<'EOF'
#!/usr/bin/env bash
SELF="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="${SELF}/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="${SELF}/plugins"
exec "${SELF}/bin/VideoPlayerApp" "$@"
EOF
    chmod +x "${dir}/run.sh"
}

write_desktop_file() {
    local dir="$1"
    local binary="$2"
    cat > "${dir}/${binary}.desktop" <<EOF
[Desktop Entry]
Name=VideoPlayer
Exec=${binary}
Icon=${binary}
Type=Application
Categories=Video;Player;
EOF
}

# =============================================================================
#  Windows — zip + 可选 NSIS installer
# =============================================================================
package_windows() {
    step "打包 Windows (ZIP)"

    # Windows 路径：CMake 在 build/app/Release/ 或 build/app/
    local bin_path=""
    for candidate in \
        "${BUILD_DIR}/app/Release/${APP_BINARY}.exe" \
        "${BUILD_DIR}/app/${APP_BINARY}.exe"; do
        [[ -f "${candidate}" ]] && { bin_path="${candidate}"; break; }
    done
    [[ -n "${bin_path}" ]] || die "找不到 ${APP_BINARY}.exe，请确认构建成功"

    local pkg_name="${APP_NAME}-${VERSION}-win64"
    local staging="${BUILD_DIR}/_win_staging/${pkg_name}"
    rm -rf "${staging}"
    mkdir -p "${staging}/plugins"

    # ── 拷贝主程序 ────────────────────────────────────────────────────────────
    cp "${bin_path}" "${staging}/${APP_BINARY}.exe"

    # ── 拷贝插件 ─────────────────────────────────────────────────────────────
    find "${BUILD_DIR}/plugins" \( -name "*.dll" -o -name "*.pdb" \) \
        -exec cp {} "${staging}/plugins/" \; 2>/dev/null || true

    # ── windeployqt ───────────────────────────────────────────────────────────
    local windeployqt; windeployqt="$(find_qt_tool windeployqt)"
    if [[ -x "${windeployqt}" ]]; then
        info "运行 windeployqt..."
        "${windeployqt}" \
            --qmldir "${SCRIPT_DIR}/app/qml" \
            --release \
            --compiler-runtime \
            "${staging}/${APP_BINARY}.exe"
        success "windeployqt 完成"
    else
        warn "未找到 windeployqt，Qt DLL 需手动拷贝！"
    fi

    # ── ZIP ───────────────────────────────────────────────────────────────────
    local zip_path="${DIST_DIR}/${pkg_name}.zip"
    require_tool zip
    (cd "${BUILD_DIR}/_win_staging" && zip -qr "${zip_path}" "${pkg_name}/")
    rm -rf "${BUILD_DIR}/_win_staging"
    success "输出: ${zip_path}"

    # ── 可选 NSIS installer ───────────────────────────────────────────────────
    if command -v makensis &>/dev/null; then
        step "生成 NSIS 安装包"
        generate_nsi_script "${pkg_name}" "${DIST_DIR}"
        makensis "${DIST_DIR}/${pkg_name}.nsi"
        rm -f "${DIST_DIR}/${pkg_name}.nsi"
        success "Installer: ${DIST_DIR}/${pkg_name}-installer.exe"
    else
        info "提示：安装 NSIS (makensis) 后可额外生成安装向导 .exe"
    fi
}

generate_nsi_script() {
    local pkg_name="$1"
    local out_dir="$2"
    cat > "${out_dir}/${pkg_name}.nsi" <<EOF
!define APP_NAME    "VideoPlayer"
!define APP_VERSION "${VERSION}"
!define EXE_NAME    "${APP_BINARY}.exe"
!define INST_DIR    "\${PROGRAMFILES64}\\\${APP_NAME}"

Name "\${APP_NAME} \${APP_VERSION}"
OutFile "${out_dir}/${pkg_name}-installer.exe"
InstallDir "\${INST_DIR}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "MainSection" SEC01
    SetOutPath "\$INSTDIR"
    File /r "${out_dir}/${pkg_name}\\*.*"
    CreateShortcut "\$DESKTOP\\\${APP_NAME}.lnk" "\$INSTDIR\\\${EXE_NAME}"
    WriteUninstaller "\$INSTDIR\\Uninstall.exe"
    WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\\${APP_NAME}" \\
        "DisplayName" "\${APP_NAME} \${APP_VERSION}"
    WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\\${APP_NAME}" \\
        "UninstallString" "\$INSTDIR\\Uninstall.exe"
SectionEnd

Section "Uninstall"
    RMDir /r "\$INSTDIR"
    Delete "\$DESKTOP\\\${APP_NAME}.lnk"
    DeleteRegKey HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\\${APP_NAME}"
SectionEnd
EOF
}

# =============================================================================
#  入口：按平台分发
# =============================================================================
step "开始打包  ${APP_NAME} v${VERSION}  [${PLATFORM}]"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

case "${PLATFORM}" in
    macos)   package_macos   ;;
    linux)   package_linux   ;;
    windows) package_windows ;;
esac

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
success "打包完成！产物位于: ${DIST_DIR}/"
ls -lh "${DIST_DIR}/"
