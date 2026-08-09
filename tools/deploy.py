#!/usr/bin/env python3
"""
deploy.py — PluginBased 核心打包模块

职责：
  1. 构建 staging 目录（主程序 / 业务插件 / QML 模块）
  2. 从 Qt 安装目录补全运行时插件（白名单驱动，解决 macdeployqt 静态分析遗漏问题）
  3. 修正所有二进制的 rpath（macOS: install_name_tool / Linux: patchelf）
  4. 调用平台部署工具（macdeployqt / windeployqt）处理 Qt 框架本身
  5. 生成最终压缩包（DMG / tar.gz / zip）

被 package.sh 调用，也可单独运行：
  python3 tools/deploy.py --help
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Callable, NamedTuple, Optional

# ── 日志（CI 友好，带颜色，可通过 NO_COLOR 关闭）─────────────────────────────
_USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") != "1"

def _c(code: str, text: str) -> str:
    return f"\033[{code}m{text}\033[0m" if _USE_COLOR else text

def info(msg: str)  -> None: print(_c("36", f"[INFO]  {msg}"))
def ok(msg: str)    -> None: print(_c("32", f"[ OK ]  {msg}"))
def warn(msg: str)  -> None: print(_c("33", f"[WARN]  {msg}"), file=sys.stderr)
def err(msg: str)   -> None: print(_c("31", f"[ERR ]  {msg}"), file=sys.stderr)
def step(msg: str)  -> None: print(_c("1",  f"\n── {msg} ──"))
def die(msg: str, code: int = 1) -> None:
    err(msg); sys.exit(code)

def run(cmd: list[str], *, check: bool = True, capture: bool = False, **kw) -> subprocess.CompletedProcess:
    """运行子进程，失败时打印命令并退出。"""
    info(f"$ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, capture_output=capture, text=True, **kw)
    if check and result.returncode != 0:
        err(f"命令失败 (exit {result.returncode})")
        if capture and result.stderr:
            print(result.stderr, file=sys.stderr)
        sys.exit(result.returncode)
    return result

# ── 平台检测 ─────────────────────────────────────────────────────────────────
SYSTEM = platform.system()
IS_MACOS   = SYSTEM == "Darwin"
IS_LINUX   = SYSTEM == "Linux"
IS_WINDOWS = SYSTEM == "Windows"

def current_platform() -> str:
    if IS_MACOS:   return "macos"
    if IS_LINUX:   return "linux"
    if IS_WINDOWS: return "windows"
    die(f"不支持的平台: {SYSTEM}")


# =============================================================================
# Qt 工具查找
# =============================================================================
def find_qt_tool(
    name: str,
    qt_dir: Optional[Path] = None,
    *,
    allow_path_fallback: bool = True,
) -> Optional[Path]:
    """优先从指定 Qt kit 查找工具；仅显式允许时才查找 PATH。"""
    candidates: list[Path] = []

    if qt_dir:
        candidates.append(qt_dir / "bin" / name)

    if allow_path_fallback:
        found = shutil.which(name)
        if found:
            candidates.append(Path(found))

    # 常见安装位置
    if allow_path_fallback:
        home = Path.home()
        for pattern in [
            "/usr/local/opt/qt/bin",
            "/usr/lib/qt6/bin",
            str(home / "Qt" / "6.*" / "macos" / "bin"),
            str(home / "Qt" / "6.*" / "gcc_64" / "bin"),
        ]:
            import glob
            for p in sorted(glob.glob(pattern)):
                candidates.append(Path(p) / name)

    for c in candidates:
        if c.exists() and os.access(c, os.X_OK):
            return c
    return None


def cmake_cache_value(build_dir: Path, key: str) -> Optional[str]:
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        return None
    for line in cache.read_text().splitlines():
        if line.startswith(f"{key}:"):
            return line.split("=", 1)[1].strip()
    return None


def detect_qt_dir(build_dir: Path) -> Optional[Path]:
    """从 CMakeCache.txt 读取构建时的 Qt kit 根目录。"""
    configured_root = cmake_cache_value(build_dir, "PLUGINBASED_QT_ROOT")
    if configured_root:
        return Path(configured_root)
    configured_qt6_dir = cmake_cache_value(build_dir, "Qt6_DIR")
    if configured_qt6_dir:
        return Path(configured_qt6_dir).parent.parent.parent
    return None


def resolve_qt_root(
    build_dir: Path,
    qt_dir: Optional[Path] = None,
) -> Optional[Path]:
    """解析打包 Qt，并拒绝与构建 Qt 不一致的覆盖。"""
    build_qt = detect_qt_dir(build_dir)
    build_qt = build_qt.expanduser().resolve() if build_qt else None
    requested_qt = qt_dir.expanduser().resolve() if qt_dir else None
    if build_qt and requested_qt and build_qt != requested_qt:
        raise ValueError(
            f"deployment Qt '{requested_qt}' does not match build Qt '{build_qt}'"
        )
    return build_qt or requested_qt


class QtInstallPaths(NamedTuple):
    root: Path
    prefix: Path
    plugins: Path
    qml: Path
    libraries: Path


def _first_existing_path(candidates: list[Path]) -> Path:
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def detect_qt_install_paths(
    build_dir: Path,
    qt_dir: Optional[Path] = None,
) -> Optional[QtInstallPaths]:
    """Resolve Qt runtime locations using qtpaths, with layout fallbacks."""
    root = resolve_qt_root(build_dir, qt_dir)
    query_tool = find_qt_tool(
        "qtpaths", root, allow_path_fallback=root is None)
    queried: dict[str, Path] = {}
    if query_tool:
        result = run([
            str(query_tool),
            "--query",
            "QT_INSTALL_PREFIX",
            "QT_INSTALL_PLUGINS",
            "QT_INSTALL_QML",
            "QT_INSTALL_LIBS",
        ], capture=True, check=False)
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                key, separator, value = line.partition(":")
                if separator and value:
                    queried[key] = Path(value).expanduser().resolve()

    prefix = queried.get("QT_INSTALL_PREFIX") or root
    if not prefix:
        return None
    if not root:
        root = prefix

    plugins = queried.get("QT_INSTALL_PLUGINS") or _first_existing_path([
        root / "plugins",
        root / "share" / "qt" / "plugins",
        prefix / "plugins",
        prefix / "share" / "qt" / "plugins",
    ])
    qml = queried.get("QT_INSTALL_QML") or _first_existing_path([
        root / "qml",
        root / "share" / "qt" / "qml",
        prefix / "qml",
        prefix / "share" / "qt" / "qml",
    ])
    libraries = queried.get("QT_INSTALL_LIBS") or _first_existing_path([
        root / "lib",
        root / "frameworks",
        prefix / "lib",
    ])
    return QtInstallPaths(root, prefix, plugins, qml, libraries)


# =============================================================================
# 依赖分析
# =============================================================================
def get_dependencies(binary: Path) -> list[Path]:
    """返回二进制的直接动态库依赖列表。"""
    if IS_MACOS:
        return _deps_macos(binary)
    if IS_LINUX:
        return _deps_linux(binary)
    if IS_WINDOWS:
        return _deps_windows(binary)
    return []

def _deps_macos(binary: Path) -> list[Path]:
    result = run(["otool", "-L", str(binary)], capture=True, check=False)
    deps = []
    for dependency in parse_otool_dependencies(result.stdout):
        if dependency.startswith("@"):
            continue
        path = Path(dependency)
        if path.exists():
            deps.append(path)
    return deps


def parse_otool_dependencies(output: str) -> list[str]:
    """Parse fat or thin Mach-O dependencies without treating arch headers as deps."""
    dependencies: list[str] = []
    for raw_line in output.splitlines():
        if not raw_line.startswith(("\t", " ")):
            continue
        dependency = raw_line.strip().split("(", 1)[0].strip()
        if dependency:
            dependencies.append(dependency)
    return dependencies

def _deps_linux(binary: Path) -> list[Path]:
    result = run(["ldd", str(binary)], capture=True, check=False)
    return [Path(dependency) for dependency in parse_ldd_dependencies(result.stdout)
            if Path(dependency).is_absolute() and Path(dependency).exists()]

def _deps_windows(binary: Path) -> list[Path]:
    result = run(["dumpbin", "/dependents", str(binary)], capture=True, check=False)
    return [Path(dependency) for dependency in parse_dumpbin_dependencies(result.stdout)]


def parse_ldd_dependencies(output: str) -> list[str]:
    """Parse both resolved and unresolved dependencies from ldd output."""
    dependencies: list[str] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or "linux-vdso" in line:
            continue
        unresolved = re.match(r"(\S+)\s+=>\s+not found$", line)
        if unresolved:
            dependencies.append(unresolved.group(1))
            continue
        resolved = re.match(r"\S+\s+=>\s+(\S+)\s+\(", line)
        if resolved:
            dependencies.append(resolved.group(1))
            continue
        direct = re.match(r"(/\S+)\s+\(", line)
        if direct:
            dependencies.append(direct.group(1))
    return dependencies


def parse_dumpbin_dependencies(output: str) -> list[str]:
    """Parse dependency names from dumpbin /dependents output."""
    dependencies: list[str] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if re.fullmatch(r"[^\s]+\.dll", line, re.IGNORECASE):
            dependencies.append(line)
    return dependencies


def read_linux_runtime_dependencies(binary: Path) -> list[str]:
    result = run(["ldd", str(binary)], capture=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"ldd failed for runtime binary: {binary}")
    return parse_ldd_dependencies(result.stdout)


def read_windows_runtime_dependencies(binary: Path) -> list[str]:
    result = run(["dumpbin", "/dependents", str(binary)], capture=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"dumpbin failed for runtime binary: {binary}")
    return parse_dumpbin_dependencies(result.stdout)


def is_linux_system_dependency(dependency: str) -> bool:
    return (
        "linux-vdso" in dependency
        or "ld-linux" in dependency
        or dependency.startswith(("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/"))
    )


WINDOWS_SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "comdlg32.dll",
    "crypt32.dll", "d3d11.dll", "dwmapi.dll", "dxgi.dll", "gdi32.dll",
    "imm32.dll", "iphlpapi.dll", "kernel32.dll", "mpr.dll", "netapi32.dll",
    "ntdll.dll", "ole32.dll", "oleaut32.dll", "opengl32.dll", "powrprof.dll",
    "propsys.dll", "psapi.dll", "rpcrt4.dll", "secur32.dll", "setupapi.dll",
    "shell32.dll", "shlwapi.dll", "user32.dll", "userenv.dll", "uxtheme.dll",
    "version.dll", "winhttp.dll", "winmm.dll", "winspool.drv", "ws2_32.dll",
}


def is_windows_system_dependency(dependency: str) -> bool:
    name = Path(dependency.replace("\\", "/")).name.casefold()
    return (
        name in WINDOWS_SYSTEM_DLLS
        or name.startswith("api-ms-win-")
        or name.startswith("ext-ms-win-")
    )


def collect_qt_libs(binary: Path, qt_lib_dir: Path, visited: Optional[set] = None) -> set[Path]:
    """
    递归收集 binary 及其所有 Qt 依赖库。
    返回需要打包的 Qt 库路径集合。
    """
    if visited is None:
        visited = set()

    for dep in get_dependencies(binary):
        dep_name = dep.name
        if dep_name in visited:
            continue
        # 只收集属于 Qt 的库（名称含 Qt6 或 Qt）
        if not (dep_name.startswith("Qt") or "Qt6" in dep_name):
            continue
        visited.add(dep_name)
        # 在 qt_lib_dir 下找实际文件
        candidates = list(qt_lib_dir.glob(f"**/{dep_name}"))
        if candidates:
            lib = candidates[0]
            collect_qt_libs(lib, qt_lib_dir, visited)

    # 返回实际路径
    result = set()
    for name in visited:
        found = list(qt_lib_dir.glob(f"**/{name}"))
        if found:
            result.add(found[0])
    return result


def copy_runtime_dependency_closure(
    initial_binaries: list[Path],
    destination_dir: Path,
    dependency_reader: Callable[[Path], list[str]],
    search_roots: list[Path],
    is_system_dependency: Callable[[str], bool],
) -> list[Path]:
    """递归复制所有非系统运行时依赖，无法解析时立即失败。"""
    destination_dir.mkdir(parents=True, exist_ok=True)
    normalized_roots = [root.expanduser().resolve() for root in search_roots
                        if root.exists()]
    queue = [binary.expanduser().resolve() for binary in initial_binaries]
    inspected: set[Path] = set()
    copied: list[Path] = []

    def locate(dependency: str) -> Optional[Path]:
        candidate = Path(dependency)
        if candidate.is_absolute() and candidate.is_file():
            return candidate

        destination_candidate = destination_dir / candidate.name
        if destination_candidate.is_file():
            return destination_candidate

        for root in normalized_roots:
            for relative in (candidate, Path(candidate.name)):
                direct = root / relative
                if direct.is_file():
                    return direct
            matches = sorted(root.rglob(candidate.name))
            if matches:
                return matches[0]
        return None

    while queue:
        binary = queue.pop(0)
        if binary in inspected:
            continue
        if not binary.is_file():
            raise RuntimeError(f"runtime binary does not exist: {binary}")
        inspected.add(binary)

        for dependency in dependency_reader(binary):
            if is_system_dependency(dependency):
                continue
            source = locate(dependency)
            if source is None:
                raise RuntimeError(
                    f"cannot resolve runtime dependency '{dependency}' required by '{binary}'"
                )

            target_name = Path(dependency).name or source.name
            try:
                source.resolve().relative_to(destination_dir.resolve())
                queue.append(source.resolve())
                continue
            except ValueError:
                pass

            target = destination_dir / target_name
            if target.exists():
                queue.append(target.resolve())
                continue
            shutil.copy2(source, target)
            copied.append(target)
            queue.append(target.resolve())

    return copied


def runtime_dependency_search_roots(
    build_dir: Path,
    qt_paths: QtInstallPaths,
) -> list[Path]:
    """Return project, Qt, and vcpkg runtime roots used by the current build."""
    roots = [
        build_dir / "app",
        build_dir / "plugins",
        build_dir,
        qt_paths.root / "bin",
        qt_paths.libraries,
        qt_paths.plugins,
        qt_paths.qml,
    ]

    installed_value = cmake_cache_value(build_dir, "VCPKG_INSTALLED_DIR")
    triplet = cmake_cache_value(build_dir, "VCPKG_TARGET_TRIPLET")
    if installed_value:
        installed = Path(installed_value)
        if not installed.is_absolute():
            installed = build_dir / installed
        if triplet:
            roots.extend([
                installed / triplet / "bin",
                installed / triplet / "lib",
            ])

    unique_roots: list[Path] = []
    seen: set[Path] = set()
    for root in roots:
        resolved = root.expanduser().resolve()
        if resolved not in seen and resolved.exists():
            seen.add(resolved)
            unique_roots.append(resolved)
    return unique_roots


def linux_runtime_binaries(stage: Path, app_binary: str) -> list[Path]:
    """Collect ELF candidates without treating launcher scripts as binaries."""
    candidates: list[Path] = []
    for path in stage.rglob("*"):
        if not path.is_file():
            continue
        if path.name == app_binary or ".so" in path.name:
            candidates.append(path)
    return candidates


def windows_runtime_binaries(stage: Path) -> list[Path]:
    return [path for path in stage.rglob("*")
            if path.is_file() and path.suffix.casefold() in {".exe", ".dll"}]


def verify_staging(stage: Path, build_dir: Path, *, enabled: bool) -> None:
    """Run the release verifier before any archive is created."""
    if not enabled:
        warn("已显式跳过发布包完整性验证")
        return
    step("完整性验证")
    run([
        sys.executable,
        str(Path(__file__).parent / "verify.py"),
        "--stage-dir",
        str(stage),
        "--expected-version",
        _read_version(build_dir),
    ], check=True)
    ok("发布包完整性验证通过")


# =============================================================================
# rpath 修正
# =============================================================================
def fix_rpath_macos(binary: Path, frameworks_rpath: str = "@executable_path/../Frameworks") -> None:
    """
    修正 macOS 二进制的 rpath：
    - 删除指向构建目录的绝对 rpath
    - 添加发布期标准 rpath（@executable_path/../Frameworks）
    """
    result = run(["otool", "-l", str(binary)], capture=True, check=False)
    # 提取所有 LC_RPATH 条目
    rpaths = re.findall(r"cmd LC_RPATH.*?path (.+?) \(", result.stdout, re.DOTALL)
    for rp in rpaths:
        rp = rp.strip()
        if rp.startswith("@"):
            continue  # 已是相对路径，保留
        # 删除绝对路径（构建目录）
        run(["install_name_tool", "-delete_rpath", rp, str(binary)], check=True)
        info(f"  删除 rpath: {rp}")

    if frameworks_rpath not in rpaths:
        run(["install_name_tool", "-add_rpath", frameworks_rpath, str(binary)])
        info(f"  添加 rpath: {frameworks_rpath}")

def collect_framework_binaries(root: Path) -> list[Path]:
    """收集 bundle 内所有 framework 的真实二进制"""
    bins = []
    for fw in root.rglob("*.framework"):
        name = fw.stem

        # 标准路径
        direct = fw / name
        if direct.exists():
            bins.append(direct)
            continue

        # Versions/A 结构
        ver = fw / "Versions" / "A" / name
        if ver.exists():
            bins.append(ver)

    return bins


def fix_macos_dep_paths(binary: Path) -> None:
    result = run(["otool", "-L", str(binary)], capture=True, check=True)

    for dep in parse_otool_dependencies(result.stdout):

        if dep.startswith("/System") or dep.startswith("/usr/lib"):
            continue

        if dep.startswith("@"):
            continue

        dep_path = Path(dep)
        new_path = None  # 初始化

        # 1️⃣ framework
        if ".framework" in dep:
            parts = dep_path.parts

            fw_name = None
            for p in parts:
                if p.endswith(".framework"):
                    fw_name = p
                    break

            if not fw_name:
                print(f"[WARN] 找不到 framework 名: {dep}")
                continue

            short = fw_name.split(".")[0]
            new_path = f"@rpath/{fw_name}/{short}"

        # 2️⃣ 普通 dylib
        elif dep.endswith(".dylib"):
            # 跳过系统库
            if dep.startswith("/usr/lib") or dep.startswith("/System/Library"):
                continue
            new_path = f"@rpath/{dep_path.name}"

        # 3️⃣ 如果都没匹配到就跳过
        if not new_path:
            print(f"[SKIP] 未处理依赖: {dep}")
            continue

        print(f"[DEBUG] {binary}")
        print(f"        OLD: {dep}")
        print(f"        NEW: {new_path}")

        run([
            "install_name_tool",
            "-change",
            dep,
            new_path,
            str(binary)
        ], capture=True, check=True)
        print("[OK] 修改成功")

def fix_framework_id(binary: Path):
    if ".framework" not in str(binary):
        return

    fw_name = binary.parent.name   # QtDBus.framework
    short = fw_name.split(".")[0]

    new_id = f"@rpath/{fw_name}/{short}"

    run([
        "install_name_tool",
        "-id",
        new_id,
        str(binary)
    ], check=True)

    print(f"[ID] {binary} -> {new_id}")

def fix_dylib_id(binary: Path):
    if not binary.name.endswith(".dylib"):
        return

    new_id = f"@rpath/{binary.name}"

    run([
        "install_name_tool",
        "-id",
        new_id,
        str(binary)
    ], check=True)

    print(f"[ID] {binary} -> {new_id}")

def make_writable(p: Path):
    p.chmod(0o755)


def fix_rpath_linux(binary: Path, lib_rpath: str = "$ORIGIN/../lib") -> None:
    """修正 Linux 二进制的 rpath（需要 patchelf）。"""
    patchelf = shutil.which("patchelf")
    if not patchelf:
        raise RuntimeError("缺少 Linux 发布必需工具 patchelf")
    run([patchelf, "--set-rpath", lib_rpath, str(binary)], check=True)
    info(f"  设置 rpath: {lib_rpath}")


# =============================================================================
# Qt 运行时插件补全
# 目前只测试了mac版本，发现mac下确实framework相关文件，所以暂时添加了frameworks这个目录，因为
# frameworks目录下除了有xxx.framework文件夹，还有dylib动态库，所以不能把dylib都拷贝到plugins下，
# undo：后续优化区分pluigins目录下还是framworks下，根据yml文件去区分或和文件前缀等
# 问题：拷贝包后，mac注册签名会报错bundle format is ambiguous (could be app or framework)，incomponentent,
# 原因：拷贝时copy函数破坏了链接，可以使用ls -al查看符号链接，下面是正常结构，mac应用framework要严格遵循这个结构
# QtDBus.framework/
###├── QtDBus          ❌ 不是 symlink
###├── Versions/
###    └── A/
###        └── QtDBus
### 手动方式：ln -sfn Versions/A/QtDBus QtDBus
###          ln -sfn A Versions/Current
# windows和linux暂时没发现有frameworks相关的文件，所以先不处理frameworks目录
# =============================================================================
def copy_qt_runtime_plugins(
    qt_paths: QtInstallPaths,
    stage_dir: Path,
    layout: dict,
    plugin_list: list[str],
    platform: str,
) -> int:
    """
    按白名单将 Qt 运行时插件从 Qt 安装目录复制到 staging 目录。
    返回成功拷贝的插件数量。
    """
    # staging 内的目标目录
    if platform == "macos":
        dst_frameworks = stage_dir / layout.get("frameworks", "Contents/Frameworks")
        dst_plugins = stage_dir / layout.get("qt_plugins", "Contents/PlugIns")
        dst_qml     = stage_dir / layout.get("qml", "Contents/Resources/qml")
    elif platform == "linux":
        dst_plugins = stage_dir / layout.get("plugins", "bin/plugins")
        dst_qml     = stage_dir / layout.get("qml", "bin/qml")
    else:
        dst_plugins = stage_dir / layout.get("plugins", "plugins")
        dst_qml     = stage_dir / layout.get("qml", "qml")

    copied = 0
    for rel_path in plugin_list:
        relative = Path(rel_path)
        if rel_path.startswith("plugins/"):
            source_relative = Path(*relative.parts[1:])
            src = qt_paths.plugins / source_relative
            dst = dst_plugins / source_relative
        elif rel_path.startswith("qml/"):
            source_relative = Path(*relative.parts[1:])
            src = qt_paths.qml / source_relative
            dst = dst_qml / source_relative
        elif rel_path.startswith("frameworks/"):
            source_relative = Path(*relative.parts[1:])
            src = qt_paths.libraries / source_relative
            dst = dst_frameworks / source_relative
        elif rel_path.startswith("../"):
            src = qt_paths.root / relative
            dst = dst_frameworks / relative.name
        else:
            src = qt_paths.root / relative
            dst = dst_plugins / relative

        src = Path(os.path.abspath(src))
        info(f" 路径为: {src}")

        if not src.exists():
            warn(f"  Qt 插件不存在，跳过: {rel_path}，src为 {src}")
            continue

 
        dst.parent.mkdir(parents=True, exist_ok=True)

        # 👇 统一处理已存在目标（关键！）
        if dst.exists() or dst.is_symlink():
            if dst.is_dir() and not dst.is_symlink():
                shutil.rmtree(dst)
            else:
                dst.unlink()

        if src.is_dir():
            #使用shutil.copytree时，默认会将符号链接当作普通文件复制，
            # 导致mac下framework目录结构被破坏，
            # 所以这里改为先创建符号链接，再复制文件
            shutil.copytree(src, dst, symlinks=True)
            info(f"  Qt 插件目录: {rel_path}/")
        else:
            # 👇 处理符号链接（Qt 很关键）
            if src.is_symlink():
                target = os.readlink(src)
                os.symlink(target, dst)
            else:
                shutil.copy2(src, dst)

            info(f"  Qt 插件: {rel_path}")
        copied += 1

    return copied


def copy_runtime_resources(
    build_dir: Path,
    stage_dir: Path,
    layout: dict,
    platform: str,
) -> int:
    """Copy runtime manifests, build identity, plugin metadata, and themes."""
    plugin_dir = stage_dir / layout.get("plugins", "plugins")
    runtime_root = plugin_dir.parent
    theme_dir = (stage_dir / "Contents" / "Resources" / "themes"
                 if platform == "macos" else runtime_root / "themes")

    manifest = build_dir / "plugins.json"
    metadata_files = sorted((build_dir / "plugins").glob("*.json"))
    theme_files = sorted((build_dir / "themes").glob("*.json"))
    build_info = build_dir / "build-info.json"
    if not manifest.is_file():
        raise FileNotFoundError(f"runtime plugin manifest not found: {manifest}")
    if not metadata_files:
        raise FileNotFoundError(f"runtime plugin metadata not found: {build_dir / 'plugins'}")
    if not theme_files:
        raise FileNotFoundError(f"runtime themes not found: {build_dir / 'themes'}")
    if not build_info.is_file():
        raise FileNotFoundError(f"build identity not found: {build_info}")

    build_info_destination = (
        stage_dir / "Contents" / "Resources" / "build-info.json"
        if platform == "macos" else stage_dir / "build-info.json"
    )

    runtime_root.mkdir(parents=True, exist_ok=True)
    plugin_dir.mkdir(parents=True, exist_ok=True)
    theme_dir.mkdir(parents=True, exist_ok=True)
    build_info_destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(manifest, runtime_root / manifest.name)
    for metadata in metadata_files:
        shutil.copy2(metadata, plugin_dir / metadata.name)
    for theme in theme_files:
        shutil.copy2(theme, theme_dir / theme.name)
    shutil.copy2(build_info, build_info_destination)
    return 2 + len(metadata_files) + len(theme_files)


def clean_macos_deployment(bundle: Path, layout: dict) -> None:
    """Remove stale deployment output while preserving the CMake-built app."""
    directories = [
        bundle / layout["frameworks"],
        bundle / layout["plugins"],
        bundle / layout["qml"],
        bundle / "Contents" / "_CodeSignature",
    ]
    for directory in directories:
        if directory.is_symlink() or directory.is_file():
            directory.unlink()
        elif directory.is_dir():
            shutil.rmtree(directory)

    qt_conf = bundle / "Contents" / "Resources" / "qt.conf"
    if qt_conf.exists() or qt_conf.is_symlink():
        qt_conf.unlink()


def prepare_macos_bundle(
    build_dir: Path,
    app_binary: str,
    layout: dict,
) -> Path:
    """Copy the CMake app into an isolated, clean deployment directory."""
    source_bundle = build_dir / "app" / f"{app_binary}.app"
    if not source_bundle.is_dir():
        raise FileNotFoundError(f".app bundle not found: {source_bundle}")

    staging_root = build_dir / "_package_macos"
    if staging_root.is_dir():
        shutil.rmtree(staging_root)
    elif staging_root.exists() or staging_root.is_symlink():
        staging_root.unlink()
    staging_root.mkdir(parents=True)

    staged_bundle = staging_root / source_bundle.name
    shutil.copytree(source_bundle, staged_bundle)
    clean_macos_deployment(staged_bundle, layout)
    return staged_bundle


def prepare_platform_staging(
    build_dir: Path,
    platform_name: str,
    package_name: str,
) -> Path:
    """Create a deterministic, inspectable staging directory for packaging."""
    staging_root = build_dir / f"_package_{platform_name}"
    if staging_root.is_dir() and not staging_root.is_symlink():
        shutil.rmtree(staging_root)
    elif staging_root.exists() or staging_root.is_symlink():
        staging_root.unlink()

    stage = staging_root / package_name
    stage.mkdir(parents=True)
    return stage


def macdeployqt_command(
    macdeployqt: Path,
    bundle: Path,
    build_dir: Path,
    qml_scan_dirs: list[Path],
) -> list[str]:
    """Build a bounded macdeployqt invocation for project-owned QML only."""
    command = [
        str(macdeployqt),
        str(bundle),
        f"-qmlimport={build_dir}",
        "-always-overwrite",
        "-no-plugins",
        "-verbose=0",
    ]
    command.extend(f"-qmldir={path}" for path in qml_scan_dirs)
    return command


def _macos_binary_candidates(bundle: Path) -> list[Path]:
    candidates: list[Path] = []
    for path in (bundle / "Contents").rglob("*"):
        if not path.is_file():
            continue
        if path.suffix in (".dylib", ".so") or "MacOS" in path.parts:
            candidates.append(path)
            continue
        framework = next(
            (part for part in path.parts if part.endswith(".framework")), None)
        if framework and path.name == Path(framework).stem:
            candidates.append(path)
    return candidates


def copy_missing_macos_runtime_dependencies(
    bundle: Path,
    qt_library_dir: Path,
) -> int:
    """Complete the @rpath dependency closure omitted by macdeployqt."""
    frameworks_dir = bundle / "Contents" / "Frameworks"
    frameworks_dir.mkdir(parents=True, exist_ok=True)
    queue = _macos_binary_candidates(bundle)
    inspected: set[Path] = set()
    copied = 0

    while queue:
        binary = queue.pop()
        if binary in inspected or not binary.is_file():
            continue
        inspected.add(binary)
        result = run(["otool", "-L", str(binary)], capture=True, check=False)
        if result.returncode != 0:
            continue

        for dependency in parse_otool_dependencies(result.stdout):
            if dependency.startswith("@rpath/"):
                relative = Path(dependency.removeprefix("@rpath/"))
                absolute_source: Optional[Path] = None
            else:
                absolute_source = Path(dependency)
                if (not absolute_source.is_absolute()
                        or dependency.startswith("/System/")
                        or dependency.startswith("/usr/lib/")):
                    continue
                framework_index = next(
                    (index for index, part in enumerate(absolute_source.parts)
                     if part.endswith(".framework")),
                    None,
                )
                if framework_index is not None:
                    relative = Path(*absolute_source.parts[framework_index:])
                elif absolute_source.name.endswith(".dylib"):
                    relative = Path(absolute_source.name)
                else:
                    continue

            framework_part = next(
                (part for part in relative.parts if part.endswith(".framework")),
                None,
            )
            if framework_part:
                destination = frameworks_dir / framework_part
                if not destination.exists():
                    source = qt_library_dir / framework_part
                    if not source.is_dir():
                        continue
                    shutil.copytree(source, destination, symlinks=True)
                    copied += 1
                    info(f"  补齐 Qt framework: {framework_part}")
                framework_binary = frameworks_dir / relative
                if framework_binary.is_file():
                    queue.append(framework_binary)
                continue

            if relative.name.endswith(".dylib"):
                destination = frameworks_dir / relative.name
                if not destination.exists():
                    source = absolute_source or (qt_library_dir / relative.name)
                    if not source.is_file():
                        continue
                    shutil.copy2(source, destination)
                    copied += 1
                    info(f"  补齐 Qt dylib: {relative.name}")
                queue.append(destination)

    return copied


# =============================================================================
# macOS 打包
# =============================================================================
def package_macos(
    build_dir: Path,
    dist_dir: Path,
    cfg: dict,
    qt_dir: Optional[Path],
    *,
    verify: bool = True,
) -> Path:
    step("macOS 打包")

    app_name   = cfg["app"]["name"]
    app_binary = cfg["app"]["binary"]
    layout     = cfg["layout"]["macos"]

    source_bundle = build_dir / "app" / f"{app_binary}.app"
    if not source_bundle.exists():
        die(f".app bundle 不存在: {source_bundle}")

    resolved_qt = resolve_qt_root(build_dir, qt_dir)
    mdqt = find_qt_tool(
        "macdeployqt", resolved_qt, allow_path_fallback=resolved_qt is None)
    if not mdqt:
        die("找不到 macdeployqt。请设置 QT_DIR 环境变量或使用 --qt-dir 参数。")

    qt_paths = detect_qt_install_paths(build_dir, resolved_qt)
    if not qt_paths:
        die("无法确定 Qt 安装根目录，请通过 --qt-dir 指定")
    info(f"Qt 安装目录: {qt_paths.root}")

    bundle = prepare_macos_bundle(build_dir, app_binary, layout)

    # ── 1. 拷贝业务插件 .so → Contents/PlugIns/ ──────────────────────────────
    step("拷贝业务插件")
    plug_dst = bundle / layout["plugins"]
    plug_dst.mkdir(parents=True, exist_ok=True)
    plugins_src = build_dir / "plugins"
    if plugins_src.exists():
        for f in plugins_src.iterdir():
            if f.suffix in (".so", ".dylib"):
                shutil.copy2(f, plug_dst / f.name)
                info(f"  业务插件: {f.name}")
    resource_count = copy_runtime_resources(build_dir, bundle, layout, "macos")
    info(f"  运行时清单/元数据/主题: {resource_count} 个文件")

    # ── 2. 拷贝 QML 模块目录 → Contents/Resources/qml/ ──────────────────────
    step("拷贝 QML 模块")
    qml_dst = bundle / layout["qml"]
    qml_dst.mkdir(parents=True, exist_ok=True)
    for mod in cfg.get("qml_modules", []):
        src = build_dir / mod
        dst = qml_dst / mod
        if src.is_dir():
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
            info(f"  QML: {mod}/")
        else:
            warn(f"  QML 模块不存在，跳过: {mod}")

    # ── 3. macdeployqt 处理 Qt 框架（主干）──────────────────────────────────
    step("macdeployqt 处理 Qt 框架")
    source_qml = build_dir.parent / "app" / "qml"
    if not source_qml.exists():
        source_qml = build_dir / ".." / "app" / "qml"
    qml_scan_dirs = [source_qml]
    qml_scan_dirs.extend(
        module_dir
        for module in cfg.get("qml_modules", [])
        if (module_dir := build_dir / module).is_dir()
    )
    run(macdeployqt_command(mdqt, bundle, build_dir, qml_scan_dirs))
    ok("macdeployqt 完成")

    # ── 4. 补全 macdeployqt 遗漏的 Qt 运行时插件 ────────────────────────────
    step("补全 Qt 运行时插件")
    rt_plugins = []
    rt_cfg = cfg.get("qt_runtime_plugins", {}).get("macos", {})
    for category, paths in rt_cfg.items():
        rt_plugins.extend(paths)

    n = copy_qt_runtime_plugins(qt_paths, bundle, layout, rt_plugins, "macos")
    ok(f"补全 {n} 个 Qt 运行时插件")

    dependency_count = copy_missing_macos_runtime_dependencies(
        bundle, qt_paths.libraries)
    ok(f"补全 {dependency_count} 个 QML/插件传递运行时依赖")

    # ── 5. 修正 bundle 内所有 .so/.dylib 的 rpath ────────────────────────────
    # step("修正 rpath")
    # all_bins = list((bundle / "Contents").rglob("*.so")) + \
    #            list((bundle / "Contents").rglob("*.dylib"))
    # for b in all_bins:
    #     fix_rpath_macos(b)
    # info(f"  共处理 {len(all_bins)} 个二进制")
    all_bins = list((bundle / "Contents").rglob("*.so")) + \
            list((bundle / "Contents").rglob("*.dylib"))

    # 👇 新增：framework 二进制
    fw_bins = collect_framework_binaries(bundle / "Contents")
    all_bins.extend(fw_bins)

    for b in all_bins:
        make_writable(b)

        fix_dylib_id(b) # 👈 新增：修正 .dylib 的 id（Qt 运行时插件 ） 
        fix_framework_id(b)   # 👈 新增
        fix_rpath_macos(b)
        fix_macos_dep_paths(b)
    
    #--5.5 重新签名（macdeployqt 可能修改了二进制，导致签名失效）────────────────────────────────────────
    step("重新签名 App")

    run([
        "codesign",
        "--force",
        "--deep",
        "--sign", "-",
        str(bundle)
    ])

    ok("签名完成")
    verify_staging(bundle, build_dir, enabled=verify)

    # ── 6. 生成 DMG ───────────────────────────────────────────────────────────
    step("生成 DMG")
    version  = _read_version(build_dir)
    dmg_name = f"{app_name}-{version}-macOS.dmg"
    dmg_path = dist_dir / dmg_name
    dist_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        shutil.copytree(bundle, tmp_path / bundle.name)
        (tmp_path / "Applications").symlink_to("/Applications")
        run([
            "hdiutil", "create",
            "-volname", f"{app_name} {version}",
            "-srcfolder", str(tmp_path),
            "-ov", "-format", "UDZO",
            str(dmg_path),
        ])

    ok(f"DMG: {dmg_path}")
    return dmg_path


# =============================================================================
# Linux 打包
# =============================================================================
def package_linux(
    build_dir: Path,
    dist_dir: Path,
    cfg: dict,
    qt_dir: Optional[Path],
    *,
    verify: bool = True,
) -> Path:
    step("Linux 打包")

    app_name = cfg["app"]["name"]
    app_binary = cfg["app"]["binary"]
    layout = cfg["layout"]["linux"]
    binary = build_dir / "app" / app_binary
    if not binary.is_file():
        raise FileNotFoundError(f"可执行文件不存在: {binary}")

    version = _read_version(build_dir)
    pkg_name = f"{app_name}-{version}-linux-x86_64"
    stage = prepare_platform_staging(build_dir, "linux", pkg_name)
    bin_dir = stage / layout["bin"]
    plug_dir = stage / layout["plugins"]
    qml_dir = stage / layout["qml"]
    lib_dir = stage / layout["lib"]
    for directory in (bin_dir, plug_dir, qml_dir, lib_dir):
        directory.mkdir(parents=True, exist_ok=True)

    shutil.copy2(binary, bin_dir / app_binary)
    for plugin in (build_dir / "plugins").glob("*.so"):
        shutil.copy2(plugin, plug_dir / plugin.name)
        info(f"  业务插件: {plugin.name}")
    resource_count = copy_runtime_resources(build_dir, stage, layout, "linux")
    info(f"  运行时清单/元数据/主题: {resource_count} 个文件")

    for module in cfg.get("qml_modules", []):
        source = build_dir / module
        if source.is_dir():
            shutil.copytree(source, qml_dir / module)
            info(f"  QML: {module}/")

    resolved_qt = resolve_qt_root(build_dir, qt_dir)
    qt_paths = detect_qt_install_paths(build_dir, resolved_qt)
    if not qt_paths:
        raise RuntimeError("无法确定构建使用的 Qt 安装根目录")
    runtime_plugins: list[str] = []
    for paths in cfg.get("qt_runtime_plugins", {}).get("linux", {}).values():
        runtime_plugins.extend(paths)
    count = copy_qt_runtime_plugins(
        qt_paths, stage, layout, runtime_plugins, "linux")
    ok(f"补全 {count} 个 Qt 运行时插件")

    initial_binaries = linux_runtime_binaries(stage, app_binary)
    copied = copy_runtime_dependency_closure(
        initial_binaries,
        lib_dir,
        read_linux_runtime_dependencies,
        runtime_dependency_search_roots(build_dir, qt_paths),
        is_linux_system_dependency,
    )
    ok(f"补全 {len(copied)} 个传递运行时依赖")

    step("修正 rpath")
    for runtime_binary in linux_runtime_binaries(stage, app_binary):
        relative_lib = os.path.relpath(lib_dir, runtime_binary.parent)
        rpath = "$ORIGIN" if relative_lib == "." else f"$ORIGIN/{relative_lib}"
        fix_rpath_linux(runtime_binary, rpath)

    run_sh = stage / "run.sh"
    run_sh.write_text(
        "#!/usr/bin/env bash\n"
        'DIR="$(cd "$(dirname "$0")" && pwd)"\n'
        'export LD_LIBRARY_PATH="${DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"\n'
        'export QT_PLUGIN_PATH="${DIR}/bin/plugins"\n'
        f'exec "${{DIR}}/bin/{app_binary}" "$@"\n',
        encoding="utf-8",
    )
    run_sh.chmod(0o755)

    verify_staging(stage, build_dir, enabled=verify)
    out = dist_dir / f"{pkg_name}.tar.gz"
    dist_dir.mkdir(parents=True, exist_ok=True)
    run(["tar", "-czf", str(out), "-C", str(stage.parent), stage.name])
    ok(f"tar.gz: {out}")
    return out


# =============================================================================
# Windows 打包
# =============================================================================
def package_windows(
    build_dir: Path,
    dist_dir: Path,
    cfg: dict,
    qt_dir: Optional[Path],
    *,
    verify: bool = True,
) -> Path:
    step("Windows 打包")

    app_name   = cfg["app"]["name"]
    app_binary = cfg["app"]["binary"]
    layout     = cfg["layout"]["windows"]
    version    = _read_version(build_dir)
    pkg_name   = f"{app_name}-{version}-win64"

    # 找可执行文件
    binary: Optional[Path] = None
    for candidate in [
        build_dir / "app" / "Release" / f"{app_binary}.exe",
        build_dir / "app" / f"{app_binary}.exe",
    ]:
        if candidate.exists():
            binary = candidate
            break
    if not binary:
        die(f"找不到 {app_binary}.exe，请确认构建成功")

    stage = prepare_platform_staging(build_dir, "windows", pkg_name)
    plug_dir = stage / layout["plugins"]
    qml_dir = stage / layout["qml"]
    plug_dir.mkdir(parents=True)
    qml_dir.mkdir(parents=True)

    shutil.copy2(binary, stage / f"{app_binary}.exe")
    for plugin in (build_dir / "plugins").glob("*.dll"):
        shutil.copy2(plugin, plug_dir / plugin.name)
    resource_count = copy_runtime_resources(build_dir, stage, layout, "windows")
    info(f"  运行时清单/元数据/主题: {resource_count} 个文件")

    for module in cfg.get("qml_modules", []):
        source = build_dir / module
        if source.is_dir():
            shutil.copytree(source, qml_dir / module)

    resolved_qt = resolve_qt_root(build_dir, qt_dir)
    qt_paths = detect_qt_install_paths(build_dir, resolved_qt)
    if not qt_paths:
        raise RuntimeError("无法确定构建使用的 Qt 安装根目录")
    wdqt = find_qt_tool(
        "windeployqt", resolved_qt, allow_path_fallback=resolved_qt is None)
    if not wdqt:
        raise RuntimeError("构建使用的 Qt kit 中缺少 windeployqt")
    step("windeployqt")
    run([
        str(wdqt),
        "--qmldir", str(build_dir.parent / "app" / "qml"),
        "--qmldir", str(qml_dir),
        "--release", "--compiler-runtime",
        str(stage / f"{app_binary}.exe"),
    ])

    runtime_plugins: list[str] = []
    for paths in cfg.get("qt_runtime_plugins", {}).get("windows", {}).values():
        runtime_plugins.extend(paths)
    count = copy_qt_runtime_plugins(
        qt_paths, stage, layout, runtime_plugins, "windows")
    ok(f"补全 {count} 个 Qt 运行时插件")

    copied = copy_runtime_dependency_closure(
        windows_runtime_binaries(stage),
        stage,
        read_windows_runtime_dependencies,
        [stage, *runtime_dependency_search_roots(build_dir, qt_paths)],
        is_windows_system_dependency,
    )
    ok(f"补全 {len(copied)} 个传递运行时依赖")

    verify_staging(stage, build_dir, enabled=verify)
    out = dist_dir / f"{pkg_name}.zip"
    dist_dir.mkdir(parents=True, exist_ok=True)
    shutil.make_archive(
        str(out.with_suffix("")), "zip", str(stage.parent), stage.name)

    ok(f"ZIP: {out}")
    return out


# =============================================================================
# 工具函数
# =============================================================================
def _read_version(build_dir: Path) -> str:
    """从 CMakeCache.txt 或上级 CMakeLists.txt 读取版本号。"""
    cache = build_dir / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text().splitlines():
            if line.startswith("CMAKE_PROJECT_VERSION:"):
                return line.split("=", 1)[1].strip()

    # 回退：向上找 CMakeLists.txt
    for parent in [build_dir, build_dir.parent, build_dir.parent.parent]:
        cmake = parent / "CMakeLists.txt"
        if cmake.exists():
            m = re.search(r'project\(\w+\s+VERSION\s+([\d.]+)', cmake.read_text())
            if m:
                return m.group(1)
    return "0.0.0"


def load_config(config_path: Path) -> dict:
    """加载仅使用 Python 标准库的 package.json 配置。"""
    if not config_path.exists():
        die(f"配置文件不存在: {config_path}")
    try:
        document = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        die(f"打包配置无效: {config_path} ({error})")
    if not isinstance(document, dict):
        die(f"打包配置根节点必须是 JSON object: {config_path}")
    return document


# =============================================================================
# 入口
# =============================================================================
def main() -> None:
    parser = argparse.ArgumentParser(
        description="PluginBased 打包工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--build-dir",  type=Path, required=True,  help="CMake 构建目录")
    parser.add_argument("--dist-dir",   type=Path, required=True,  help="输出目录")
    parser.add_argument("--config",     type=Path, default=None,   help="配置文件路径（默认 tools/package.json）")
    parser.add_argument("--qt-dir",     type=Path, default=None,   help="Qt 安装根目录")
    parser.add_argument("--skip-verify", action="store_true",      help="跳过归档前完整性验证")
    parser.add_argument("--platform",   choices=["macos", "linux", "windows"], default=None)
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    dist_dir  = args.dist_dir.resolve()

    if args.config:
        cfg_path = args.config.resolve()
    else:
        cfg_path = Path(__file__).parent / "package.json"

    cfg = load_config(cfg_path)

    qt_dir = args.qt_dir
    if not qt_dir and os.environ.get("QT_DIR"):
        qt_dir = Path(os.environ["QT_DIR"])

    plat = args.platform or current_platform()

    try:
        if plat == "macos":
            out = package_macos(
                build_dir, dist_dir, cfg, qt_dir, verify=not args.skip_verify)
        elif plat == "linux":
            out = package_linux(
                build_dir, dist_dir, cfg, qt_dir, verify=not args.skip_verify)
        elif plat == "windows":
            out = package_windows(
                build_dir, dist_dir, cfg, qt_dir, verify=not args.skip_verify)
        else:
            die(f"不支持的平台: {plat}")
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        die(str(error))

    print()
    ok(f"打包完成: {out}")


if __name__ == "__main__":
    main()
