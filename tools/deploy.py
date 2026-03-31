#!/usr/bin/env python3
"""
deploy.py — VideoPlayer 核心打包模块

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
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional

try:
    import yaml
except ImportError:
    print("[ERROR] 缺少 PyYAML，请执行: pip3 install pyyaml", file=sys.stderr)
    sys.exit(1)

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
def find_qt_tool(name: str, qt_dir: Optional[Path] = None) -> Optional[Path]:
    """按优先级查找 Qt 工具：qt_dir → PATH → 常见安装位置。"""
    candidates: list[Path] = []

    if qt_dir:
        candidates.append(qt_dir / "bin" / name)

    # PATH
    found = shutil.which(name)
    if found:
        candidates.append(Path(found))

    # 常见安装位置
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


def detect_qt_dir(build_dir: Path) -> Optional[Path]:
    """从 CMakeCache.txt 读取 Qt6_DIR，推导出 Qt 安装根目录。"""
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        return None
    for line in cache.read_text().splitlines():
        if line.startswith("Qt6_DIR:"):
            qt6_dir = Path(line.split("=", 1)[1].strip())
            # Qt6_DIR 通常是 .../Qt/6.x.x/macos/lib/cmake/Qt6
            # 安装根目录上溯三级：lib/cmake/Qt6 → lib/cmake → lib → 安装根
            return qt6_dir.parent.parent.parent
    return None


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
    for line in result.stdout.splitlines()[1:]:
        line = line.strip()
        if not line or line.startswith("@"):
            continue
        path = Path(line.split("(")[0].strip())
        if path.exists():
            deps.append(path)
    return deps

def _deps_linux(binary: Path) -> list[Path]:
    result = run(["ldd", str(binary)], capture=True, check=False)
    deps = []
    for line in result.stdout.splitlines():
        m = re.search(r"=> (.+?) \(", line)
        if m:
            p = Path(m.group(1).strip())
            if p.exists() and p.name != "linux-vdso.so.1":
                deps.append(p)
    return deps

def _deps_windows(binary: Path) -> list[Path]:
    result = run(["dumpbin", "/dependents", str(binary)], capture=True, check=False)
    deps = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if line.endswith(".dll") and not line.startswith("Dump"):
            deps.append(Path(line))
    return deps


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
        run(["install_name_tool", "-delete_rpath", rp, str(binary)], check=False)
        info(f"  删除 rpath: {rp}")

    # 添加发布期 rpath（若已存在则忽略错误）
    run(["install_name_tool", "-add_rpath", frameworks_rpath, str(binary)], check=False)
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

    for line in result.stdout.splitlines()[1:]:
        line = line.strip()
        if not line:
            continue

        dep = line.split("(")[0].strip()

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

        r = run([
            "install_name_tool",
            "-change",
            dep,
            new_path,
            str(binary)
        ], capture=True, check=False)

        if r.returncode != 0:
            print(f"[ERROR] 修改失败: {dep}")
            print(r.stderr)
        else:
            print(f"[OK] 修改成功")

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
    ], check=False)

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
    ], check=False)

    print(f"[ID] {binary} -> {new_id}")

def make_writable(p: Path):
    p.chmod(0o755)


def fix_rpath_linux(binary: Path, lib_rpath: str = "$ORIGIN/../lib") -> None:
    """修正 Linux 二进制的 rpath（需要 patchelf）。"""
    if not shutil.which("patchelf"):
        warn("未找到 patchelf，跳过 rpath 修正（建议安装: apt/brew install patchelf）")
        return
    run(["patchelf", "--set-rpath", lib_rpath, str(binary)], check=False)
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
    qt_root: Path,
    stage_dir: Path,
    layout: dict,
    plugin_list: list[str],
    platform: str,
) -> int:
    """
    按白名单将 Qt 运行时插件从 Qt 安装目录复制到 staging 目录。
    返回成功拷贝的插件数量。
    """
    # macOS 上 Qt 插件在 qt_root/lib/  （framework 内）或 qt_root/plugins/
    # 其他平台在 qt_root/plugins/
    plugin_base = qt_root / "plugins"
    qml_base    = qt_root / "qml"
    frameworks_base = qt_root / "frameworks"

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
        srctmp = qt_root / rel_path
        strsrc = os.path.abspath(srctmp)
        src = Path(strsrc)
        info(f" 路径为: {src}")
        if not src.exists():
            # 尝试 plugins/ 或 qml/ 子目录
            if rel_path.startswith("qml/"):
                src = qml_base / rel_path[4:]
            else:
                src = plugin_base / rel_path.lstrip("plugins/")

        if not src.exists():
            warn(f"  Qt 插件不存在，跳过: {rel_path}，src为 {src}")
            continue
        
        
        # 确定目标路径
        if rel_path.startswith("qml/"):
            dst = dst_qml / rel_path[4:]
        elif rel_path.startswith("frameworks/"):
            dst = dst_frameworks / rel_path[11:]
        elif rel_path.endswith(".dylib"):
            filename = os.path.basename(rel_path)
            dst =  dst_frameworks / filename if platform == "macos" else dst_plugins / filename
        else:
            dst = dst_plugins / Path(rel_path).relative_to(Path(rel_path).parts[0])

 
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


# =============================================================================
# macOS 打包
# =============================================================================
def package_macos(build_dir: Path, dist_dir: Path, cfg: dict, qt_dir: Optional[Path]) -> Path:
    step("macOS 打包")

    app_name   = cfg["app"]["name"]
    app_binary = cfg["app"]["binary"]
    layout     = cfg["layout"]["macos"]

    bundle = build_dir / "app" / f"{app_binary}.app"
    if not bundle.exists():
        die(f".app bundle 不存在: {bundle}")

    mdqt = find_qt_tool("macdeployqt", qt_dir)
    if not mdqt:
        die("找不到 macdeployqt。请设置 QT_DIR 环境变量或使用 --qt-dir 参数。")

    qt_root = detect_qt_dir(build_dir) or (qt_dir.parent.parent if qt_dir else None)
    if not qt_root:
        die("无法确定 Qt 安装根目录，请通过 --qt-dir 指定")
    info(f"Qt 安装目录: {qt_root}")

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
    run([
        str(mdqt), str(bundle),
        f"-qmldir={source_qml}",
        f"-qmldir={qml_dst}",
        "-verbose=1",
    ])
    ok("macdeployqt 完成")

    # ── 4. 补全 macdeployqt 遗漏的 Qt 运行时插件 ────────────────────────────
    step("补全 Qt 运行时插件")
    rt_plugins = []
    rt_cfg = cfg.get("qt_runtime_plugins", {}).get("macos", {})
    for category, paths in rt_cfg.items():
        rt_plugins.extend(paths)

    n = copy_qt_runtime_plugins(qt_root, bundle, layout, rt_plugins, "macos")
    ok(f"补全 {n} 个 Qt 运行时插件")

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
def package_linux(build_dir: Path, dist_dir: Path, cfg: dict, qt_dir: Optional[Path]) -> Path:
    step("Linux 打包")

    app_name   = cfg["app"]["name"]
    app_binary = cfg["app"]["binary"]
    layout     = cfg["layout"]["linux"]

    binary = build_dir / "app" / app_binary
    if not binary.exists():
        die(f"可执行文件不存在: {binary}")

    version  = _read_version(build_dir)
    pkg_name = f"{app_name}-{version}-linux-x86_64"

    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / pkg_name
        bin_dir  = stage / layout["bin"]
        plug_dir = stage / layout["plugins"]
        qml_dir  = stage / layout["qml"]
        lib_dir  = stage / layout["lib"]
        for d in [bin_dir, plug_dir, qml_dir, lib_dir]:
            d.mkdir(parents=True, exist_ok=True)

        # 主程序
        shutil.copy2(binary, bin_dir / app_binary)

        # 业务插件
        for f in (build_dir / "plugins").glob("*.so"):
            shutil.copy2(f, plug_dir / f.name)
            info(f"  业务插件: {f.name}")

        # QML 模块
        for mod in cfg.get("qml_modules", []):
            src = build_dir / mod
            if src.is_dir():
                dst = qml_dir / mod
                if dst.exists(): shutil.rmtree(dst)
                shutil.copytree(src, dst)
                info(f"  QML: {mod}/")

        # Qt 运行时插件补全
        qt_root = detect_qt_dir(build_dir) or (qt_dir.parent.parent if qt_dir else None)
        if qt_root:
            rt_plugins = []
            for paths in cfg.get("qt_runtime_plugins", {}).get("linux", {}).values():
                rt_plugins.extend(paths)
            n = copy_qt_runtime_plugins(qt_root, stage, layout, rt_plugins, "linux")
            ok(f"补全 {n} 个 Qt 运行时插件")

        # Qt 依赖库：优先 linuxdeployqt，否则 ldd 收集
        ldqt = find_qt_tool("linuxdeployqt", qt_dir)
        if ldqt:
            step("linuxdeployqt")
            run([str(ldqt), str(bin_dir / app_binary),
                 f"-qmldir={build_dir / '..' / 'app' / 'qml'}",
                 f"-qmldir={qml_dir}",
                 "-bundle-non-qt-libs", "-no-translations",
                 "-verbose=1"])
        else:
            warn("未找到 linuxdeployqt，使用 ldd 收集 Qt 依赖库")
            _collect_libs_ldd(binary, lib_dir)

        # 修正 rpath
        step("修正 rpath")
        for b in list(bin_dir.glob("*")) + list(plug_dir.glob("*.so")):
            if b.is_file() and not b.suffix == ".txt":
                fix_rpath_linux(b)

        # 启动脚本
        run_sh = stage / "run.sh"
        run_sh.write_text(
            "#!/usr/bin/env bash\n"
            'DIR="$(cd "$(dirname "$0")" && pwd)"\n'
            'export LD_LIBRARY_PATH="${DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"\n'
            'export QT_PLUGIN_PATH="${DIR}/bin/plugins"\n'
            'exec "${DIR}/bin/VideoPlayerApp" "$@"\n'
        )
        run_sh.chmod(0o755)

        # 打包 tar.gz
        out = dist_dir / f"{pkg_name}.tar.gz"
        dist_dir.mkdir(parents=True, exist_ok=True)
        run(["tar", "-czf", str(out), "-C", str(Path(tmp)), pkg_name])

    ok(f"tar.gz: {out}")
    return out


def _collect_libs_ldd(binary: Path, lib_dir: Path) -> None:
    """用 ldd 收集 Qt 依赖库（linuxdeployqt 降级方案）。"""
    result = run(["ldd", str(binary)], capture=True, check=False)
    for line in result.stdout.splitlines():
        m = re.search(r"=> (.+?) \(", line)
        if not m:
            continue
        so = Path(m.group(1).strip())
        if not so.exists():
            continue
        name = so.name
        if any(k in name for k in ("Qt6", "libspdlog", "libfmt")):
            dst = lib_dir / name
            if not dst.exists():
                shutil.copy2(so, dst)
                info(f"  lib: {name}")


# =============================================================================
# Windows 打包
# =============================================================================
def package_windows(build_dir: Path, dist_dir: Path, cfg: dict, qt_dir: Optional[Path]) -> Path:
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

    with tempfile.TemporaryDirectory() as tmp:
        stage    = Path(tmp) / pkg_name
        plug_dir = stage / layout["plugins"]
        qml_dir  = stage / layout["qml"]
        stage.mkdir(parents=True)
        plug_dir.mkdir(parents=True)
        qml_dir.mkdir(parents=True)

        shutil.copy2(binary, stage / f"{app_binary}.exe")

        for f in (build_dir / "plugins").glob("*.dll"):
            shutil.copy2(f, plug_dir / f.name)

        for mod in cfg.get("qml_modules", []):
            src = build_dir / mod
            if src.is_dir():
                dst = qml_dir / mod
                if dst.exists(): shutil.rmtree(dst)
                shutil.copytree(src, dst)

        # windeployqt
        wdqt = find_qt_tool("windeployqt", qt_dir)
        if wdqt:
            step("windeployqt")
            run([str(wdqt),
                 "--qmldir", str(build_dir / ".." / "app" / "qml"),
                 "--qmldir", str(qml_dir),
                 "--release", "--compiler-runtime",
                 str(stage / f"{app_binary}.exe")])
        else:
            warn("未找到 windeployqt，Qt DLL 需手动拷贝")

        # 补全运行时插件
        qt_root = detect_qt_dir(build_dir) or (qt_dir.parent.parent if qt_dir else None)
        if qt_root:
            rt_plugins = []
            for paths in cfg.get("qt_runtime_plugins", {}).get("windows", {}).values():
                rt_plugins.extend(paths)
            n = copy_qt_runtime_plugins(qt_root, stage, layout, rt_plugins, "windows")
            ok(f"补全 {n} 个 Qt 运行时插件")

        # ZIP
        out = dist_dir / f"{pkg_name}.zip"
        dist_dir.mkdir(parents=True, exist_ok=True)
        shutil.make_archive(str(out.with_suffix("")), "zip", str(Path(tmp)), pkg_name)

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
    """加载 package.yml 配置文件。"""
    if not config_path.exists():
        die(f"配置文件不存在: {config_path}")
    with open(config_path) as f:
        return yaml.safe_load(f)


# =============================================================================
# 入口
# =============================================================================
def main() -> None:
    parser = argparse.ArgumentParser(
        description="VideoPlayer 打包工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--build-dir",  type=Path, required=True,  help="CMake 构建目录")
    parser.add_argument("--dist-dir",   type=Path, required=True,  help="输出目录")
    parser.add_argument("--config",     type=Path, default=None,   help="配置文件路径（默认 tools/package.yml）")
    parser.add_argument("--qt-dir",     type=Path, default=None,   help="Qt 安装根目录")
    parser.add_argument("--platform",   choices=["macos", "linux", "windows"], default=None)
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    dist_dir  = args.dist_dir.resolve()

    if args.config:
        cfg_path = args.config.resolve()
    else:
        # 默认找 deploy.py 同级的 package.yml
        cfg_path = Path(__file__).parent / "package.yml"

    cfg = load_config(cfg_path)

    qt_dir = args.qt_dir
    if not qt_dir and os.environ.get("QT_DIR"):
        qt_dir = Path(os.environ["QT_DIR"])

    plat = args.platform or current_platform()

    if plat == "macos":
        out = package_macos(build_dir, dist_dir, cfg, qt_dir)
    elif plat == "linux":
        out = package_linux(build_dir, dist_dir, cfg, qt_dir)
    elif plat == "windows":
        out = package_windows(build_dir, dist_dir, cfg, qt_dir)
    else:
        die(f"不支持的平台: {plat}")

    print()
    ok(f"打包完成: {out}")


if __name__ == "__main__":
    main()
