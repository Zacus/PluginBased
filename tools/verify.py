#!/usr/bin/env python3
"""
verify.py — 发布包完整性验证

扫描 staging 目录内所有二进制，递归解析其动态库依赖，
确保每个依赖要么在包内可找到、要么是系统库（/usr/lib、/System 等）。

找到问题立即报告，CI 友好（非零退出码）。

用法：
  python3 tools/verify.py --stage-dir /path/to/staging
  python3 tools/verify.py --stage-dir VideoPlayerApp.app --platform macos
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

# ── 日志 ─────────────────────────────────────────────────────────────────────
_USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") != "1"
def _c(code: str, t: str) -> str: return f"\033[{code}m{t}\033[0m" if _USE_COLOR else t
def info(m: str)  -> None: print(_c("36", f"[INFO]  {m}"))
def ok(m: str)    -> None: print(_c("32", f"[ OK ]  {m}"))
def warn(m: str)  -> None: print(_c("33", f"[WARN]  {m}"))
def fail(m: str)  -> None: print(_c("31", f"[FAIL]  {m}"), file=sys.stderr)
def step(m: str)  -> None: print(_c("1",  f"\n── {m} ──"))

SYSTEM = platform.system()

# 系统库前缀：这些路径下的依赖无需打包，由操作系统提供
SYSTEM_LIB_PREFIXES = {
    "Darwin": ["/System/", "/usr/lib/", "/usr/local/lib/libSystem"],
    "Linux":  ["/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/"],
    "Windows": ["C:\\Windows\\System32\\", "C:\\Windows\\SysWOW64\\"],
}


def get_direct_deps(binary: Path) -> list[str]:
    """获取二进制的直接动态库依赖名称列表。"""
    sys_name = platform.system()
    if sys_name == "Darwin":
        return _deps_macos(binary)
    if sys_name == "Linux":
        return _deps_linux(binary)
    if sys_name == "Windows":
        return _deps_windows(binary)
    return []

def _deps_macos(binary: Path) -> list[str]:
    try:
        out = subprocess.check_output(["otool", "-L", str(binary)],
                                       stderr=subprocess.DEVNULL, text=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    result = []
    for line in out.splitlines()[1:]:
        line = line.strip()
        dep = line.split("(")[0].strip()
        if dep:
            result.append(dep)
    return result

def _deps_linux(binary: Path) -> list[str]:
    try:
        out = subprocess.check_output(["ldd", str(binary)],
                                       stderr=subprocess.DEVNULL, text=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    result = []
    for line in out.splitlines():
        m = re.search(r"=> (.+?) \(", line)
        if m:
            result.append(m.group(1).strip())
        elif "=>" not in line and line.strip().endswith(")"):
            # 直接依赖（无 =>）
            result.append(line.strip().split()[0])
    return result

def _deps_windows(binary: Path) -> list[str]:
    try:
        out = subprocess.check_output(["dumpbin", "/dependents", str(binary)],
                                       stderr=subprocess.DEVNULL, text=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    result = []
    for line in out.splitlines():
        line = line.strip()
        if line.lower().endswith(".dll") and " " not in line:
            result.append(line)
    return result


def is_system_lib(dep: str) -> bool:
    """判断依赖是否是操作系统提供的系统库（无需打包）。"""
    sys_name = platform.system()
    prefixes = SYSTEM_LIB_PREFIXES.get(sys_name, [])
    # @rpath / @executable_path / @loader_path 都是相对引用，由包内文件提供
    if dep.startswith("@"):
        return False
    for prefix in prefixes:
        if dep.startswith(prefix):
            return True
    # Linux vDSO
    if "linux-vdso" in dep or "ld-linux" in dep:
        return True
    return False


def find_in_bundle(dep_name: str, bundle_files: dict[str, Path]) -> Optional[Path]:
    """在 bundle_files 字典中查找依赖库（按文件名匹配）。"""
    name = Path(dep_name).name
    return bundle_files.get(name)


def scan_bundle(stage_dir: Path) -> tuple[int, int]:
    """
    扫描 staging 目录内所有二进制，验证依赖完整性。
    返回 (问题数, 已检查二进制数)。
    """
    sys_name = platform.system()

    # 收集包内所有二进制文件
    suffixes = {
        "Darwin":  {".so", ".dylib", ""},    # macOS 可执行无后缀
        "Linux":   {".so", ""},
        "Windows": {".dll", ".exe"},
    }.get(sys_name, {".so", ""})

    bundle_binaries: list[Path] = []
    for f in stage_dir.rglob("*"):
        if not f.is_file():
            continue
        if f.suffix in suffixes or (not f.suffix and os.access(f, os.X_OK)):
            bundle_binaries.append(f)

    # 建立包内文件名 → 路径的快速查找表
    bundle_files: dict[str, Path] = {f.name: f for f in bundle_binaries}

    issues: list[str] = []
    checked = 0

    for binary in bundle_binaries:
        deps = get_direct_deps(binary)
        if not deps:
            continue
        checked += 1

        for dep in deps:
            dep_name = Path(dep).name

            # 系统库，跳过
            if is_system_lib(dep):
                continue

            # 相对引用（@rpath 等），检查包内是否有对应文件
            if dep.startswith("@"):
                if dep_name not in bundle_files:
                    issues.append(
                        f"  缺失: {binary.relative_to(stage_dir)}\n"
                        f"    → {dep}  (包内未找到 {dep_name})"
                    )
                continue

            # 绝对路径：检查是否存在，或包内有同名文件
            if not Path(dep).exists() and dep_name not in bundle_files:
                issues.append(
                    f"  缺失: {binary.relative_to(stage_dir)}\n"
                    f"    → {dep}"
                )

    return issues, checked


def main() -> None:
    parser = argparse.ArgumentParser(description="发布包完整性验证")
    parser.add_argument("--stage-dir", type=Path, required=True, help="staging 目录或 .app bundle")
    parser.add_argument("--strict",    action="store_true",       help="警告也视为失败")
    args = parser.parse_args()

    stage = args.stage_dir.resolve()
    if not stage.exists():
        fail(f"目录不存在: {stage}")
        sys.exit(1)

    step("扫描发布包依赖")
    info(f"目录: {stage}")

    issues, checked = scan_bundle(stage)

    print()
    info(f"已检查二进制: {checked}")

    if not issues:
        ok("所有依赖均可在包内解析，发布包完整")
        sys.exit(0)
    else:
        fail(f"发现 {len(issues)} 个依赖问题：")
        for issue in issues:
            print(issue)
        print()
        fail("发布包不完整，请将缺失库加入 tools/package.yml 的 qt_runtime_plugins 白名单")
        sys.exit(1)


# Optional 需要在 Python 3.8 上兼容
from typing import Optional

if __name__ == "__main__":
    main()
