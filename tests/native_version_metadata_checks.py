#!/usr/bin/env python3

import argparse
import plistlib
import platform
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate native application version metadata."
    )
    parser.add_argument("--app-bundle", type=Path, required=True)
    parser.add_argument("--version", required=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if platform.system() != "Darwin":
        return

    info_path = args.app_bundle / "Contents" / "Info.plist"
    with info_path.open("rb") as stream:
        info = plistlib.load(stream)

    assert info["CFBundleIdentifier"] == "com.pluginbased.app"
    assert info["CFBundleShortVersionString"] == args.version
    assert info["CFBundleVersion"] == args.version


if __name__ == "__main__":
    main()
