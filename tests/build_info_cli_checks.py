#!/usr/bin/env python3

import argparse
import json
import os
import subprocess
from pathlib import Path


def run_app(app, option):
    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    return subprocess.run(
        [str(app), option],
        text=True,
        capture_output=True,
        timeout=5,
        env=environment,
        check=False,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    version = run_app(args.app, "--version")
    assert version.returncode == 0, version.stderr
    version_line = version.stdout.strip()
    assert version_line.startswith(f"PluginBased {args.version} (build ")
    assert version_line.endswith(")")
    assert "compiler" not in version_line.casefold()
    assert "qt " not in version_line.casefold()

    details = run_app(args.app, "--build-info")
    assert details.returncode == 0, details.stderr
    printed = json.loads(details.stdout)
    generated = json.loads(args.json.read_text(encoding="utf-8"))
    assert printed == generated
    for forbidden in (
        "gitRef",
        "branch",
        "hostname",
        "sourceDir",
        "remoteUrl",
    ):
        assert forbidden not in printed

    print("build info CLI checks passed")


if __name__ == "__main__":
    main()
