#!/usr/bin/env python3
"""Prepare pinned build dependencies and optionally configure a CMake preset."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import venv
from pathlib import Path


VCPKG_BASELINE = "ea1a7396b05637a53bf23c078647ecc0edee4b80"
VCPKG_REPOSITORY = "https://github.com/microsoft/vcpkg.git"
QT_VERSION = "6.8.3"
AQTINSTALL_VERSION = "3.3.0"
QT_MODULES = ("qtmultimedia", "qtshadertools")
QT_REQUIRED_CONFIGS = (
    ("Qt6", "Qt6Config.cmake"),
    ("Qt6Multimedia", "Qt6MultimediaConfig.cmake"),
    ("Qt6ShaderTools", "Qt6ShaderToolsConfig.cmake"),
)


class SetupError(RuntimeError):
    pass


def run(
    command: list[str],
    *,
    cwd: Path | None = None,
    environment: dict[str, str] | None = None,
) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def require_command(name: str) -> None:
    if shutil.which(name) is None:
        raise SetupError(f"缺少 {name}，请先安装后重试")


def qt_platform_settings() -> tuple[str, str, str]:
    system = platform.system()
    if system == "Darwin":
        return "mac", "clang_64", "macos"
    if system == "Linux":
        return "linux", "gcc_64", "gcc_64"
    if system == "Windows":
        return "windows", "win64_msvc2022_64", "msvc2022_64"
    raise SetupError(f"暂不支持的平台: {system}")


def default_roots() -> tuple[Path, Path]:
    _, _, qt_kit_directory = qt_platform_settings()
    home = Path.home()
    vcpkg_root = Path(os.environ.get("VCPKG_ROOT", home / "vcpkg" / "vcpkg-master"))
    qt_root = Path(os.environ.get("QT_ROOT", home / "Qt" / QT_VERSION / qt_kit_directory))
    return vcpkg_root.expanduser().resolve(), qt_root.expanduser().resolve()


def vcpkg_is_ready(vcpkg_root: Path) -> bool:
    executable = "vcpkg.exe" if platform.system() == "Windows" else "vcpkg"
    return (
        (vcpkg_root / executable).is_file()
        and (vcpkg_root / "scripts" / "buildsystems" / "vcpkg.cmake").is_file()
    )


def ensure_vcpkg(vcpkg_root: Path, *, install: bool) -> None:
    if vcpkg_is_ready(vcpkg_root):
        if install:
            if not (vcpkg_root / ".git").is_dir():
                raise SetupError(
                    f"现有 VCPKG_ROOT 不是 Git checkout，无法解析 builtin-baseline: {vcpkg_root}"
                )
            head = subprocess.run(
                ["git", "-C", str(vcpkg_root), "rev-parse", "HEAD"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            if head != VCPKG_BASELINE:
                raise SetupError(
                    f"vcpkg checkout 为 {head}，项目要求 {VCPKG_BASELINE}: {vcpkg_root}"
                )
        return

    if not install:
        raise SetupError(f"vcpkg 未准备完成: {vcpkg_root}")
    if vcpkg_root.exists():
        raise SetupError(f"vcpkg 目标目录已存在但不完整，请先处理: {vcpkg_root}")

    require_command("git")
    vcpkg_root.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", VCPKG_REPOSITORY, str(vcpkg_root)])
    run(["git", "checkout", VCPKG_BASELINE], cwd=vcpkg_root)
    bootstrap = "bootstrap-vcpkg.bat" if platform.system() == "Windows" else "bootstrap-vcpkg.sh"
    run([str(vcpkg_root / bootstrap)], cwd=vcpkg_root)


def qt_is_ready(qt_root: Path) -> bool:
    cmake_root = qt_root / "lib" / "cmake"
    return all((cmake_root / package / config).is_file()
               for package, config in QT_REQUIRED_CONFIGS)


def ensure_qt(qt_root: Path, *, install: bool) -> None:
    if qt_is_ready(qt_root):
        return
    if not install:
        raise SetupError(f"Qt {QT_VERSION} 未准备完成: {qt_root}")

    require_command("python3")
    host, architecture, expected_kit_directory = qt_platform_settings()
    if qt_root.name != expected_kit_directory or qt_root.parent.name != QT_VERSION:
        raise SetupError(
            "自动安装 Qt 时，QT_ROOT 必须采用 <output>/6.8.3/<kit> 目录结构；"
            f"当前为 {qt_root}"
        )

    environment_root = Path.home() / ".cache" / "pluginbased" / f"aqtinstall-{AQTINSTALL_VERSION}"
    python = environment_root / ("Scripts/python.exe" if platform.system() == "Windows" else "bin/python")
    if not python.is_file():
        environment_root.parent.mkdir(parents=True, exist_ok=True)
        venv.EnvBuilder(with_pip=True).create(environment_root)
        run([str(python), "-m", "pip", "install", f"aqtinstall=={AQTINSTALL_VERSION}"])

    output_directory = qt_root.parent.parent
    run([
        str(python), "-m", "aqt", "install-qt",
        "--outputdir", str(output_directory),
        host, "desktop", QT_VERSION, architecture,
        "-m", *QT_MODULES,
    ])
    if not qt_is_ready(qt_root):
        raise SetupError(f"Qt 安装完成后仍未找到 Qt6Config.cmake: {qt_root}")


def resolve_downloads_root(vcpkg_root: Path) -> Path:
    configured = os.environ.get("VCPKG_DOWNLOADS")
    if configured:
        return Path(configured).expanduser().resolve()

    return (vcpkg_root / "downloads").resolve()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--vcpkg-root", type=Path)
    parser.add_argument("--qt-root", type=Path)
    parser.add_argument("--no-install", action="store_true", help="只验证现有依赖，不下载或安装")
    parser.add_argument("--check-only", action="store_true", help="只验证环境，不创建目录或配置工程")
    parser.add_argument(
        "--configure",
        choices=("debug", "release"),
        help="依赖就绪后配置指定的仓库 Preset",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.check_only and args.configure:
            raise SetupError("--check-only 不能与 --configure 同时使用")
        require_command("cmake")
        require_command("ninja")
        default_vcpkg_root, default_qt_root = default_roots()
        vcpkg_root = (args.vcpkg_root or default_vcpkg_root).expanduser().resolve()
        qt_root = (args.qt_root or default_qt_root).expanduser().resolve()
        install = not args.no_install
        ensure_vcpkg(vcpkg_root, install=install)
        ensure_qt(qt_root, install=install)
        downloads_root = resolve_downloads_root(vcpkg_root)
        if not args.check_only:
            downloads_root.mkdir(parents=True, exist_ok=True)
        if args.configure:
            environment = os.environ.copy()
            environment.update({
                "VCPKG_ROOT": str(vcpkg_root),
                "QT_ROOT": str(qt_root),
                "VCPKG_DOWNLOADS": str(downloads_root),
            })
            run(
                ["cmake", "--preset", args.configure],
                cwd=args.project_root.expanduser().resolve(),
                environment=environment,
            )
        print(f"VCPKG_ROOT={vcpkg_root}")
        print(f"QT_ROOT={qt_root}")
        print(f"VCPKG_DOWNLOADS={downloads_root}")
        print("构建环境已就绪（Ninja）")
        return 0
    except (OSError, SetupError, subprocess.CalledProcessError) as error:
        print(f"错误: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
