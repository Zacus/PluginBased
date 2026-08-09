#!/usr/bin/env python3

import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "cmake" / "GenerateBuildInfo.cmake"


def run(command, cwd):
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        capture_output=True,
        check=True,
    )


def initialize_repository(source):
    run(["git", "init", "-q"], source)
    run(["git", "config", "user.email", "build-info@example.invalid"], source)
    run(["git", "config", "user.name", "Build Info Test"], source)
    (source / "tracked.txt").write_text("clean\n", encoding="utf-8")
    run(["git", "add", "tracked.txt"], source)
    run(["git", "commit", "-qm", "fixture"], source)
    return run(["git", "rev-parse", "HEAD"], source).stdout.strip()


def generate(source, output, *, official=False, expected_tag=""):
    return subprocess.run(
        [
            "cmake",
            f"-DPLUGINBASED_SOURCE_DIR={source}",
            f"-DPLUGINBASED_OUTPUT_HEADER={output / 'generated/BuildInfoData.h'}",
            f"-DPLUGINBASED_OUTPUT_JSON={output / 'build-info.json'}",
            "-DPLUGINBASED_PRODUCT_NAME=PluginBased",
            "-DPLUGINBASED_PRODUCT_VERSION=1.0.0",
            "-DPLUGINBASED_BUILD_TYPE=Release",
            "-DPLUGINBASED_PLATFORM=Linux",
            "-DPLUGINBASED_ARCHITECTURE=x86_64",
            "-DPLUGINBASED_COMPILER=GNU 13.2.0",
            "-DPLUGINBASED_QT_VERSION=6.8.3",
            f"-DPLUGINBASED_OFFICIAL_BUILD={'ON' if official else 'OFF'}",
            f"-DPLUGINBASED_EXPECTED_TAG={expected_tag}",
            "-P",
            str(GENERATOR),
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def read_document(output):
    return json.loads((output / "build-info.json").read_text(encoding="utf-8"))


def test_clean_repository_records_exact_commit_and_no_private_context():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        commit = initialize_repository(source)

        result = generate(source, output)

        assert result.returncode == 0, result.stderr
        document = read_document(output)
        assert document["schemaVersion"] == 1
        assert document["productVersion"] == "1.0.0"
        assert document["gitCommit"] == commit
        assert document["gitShortCommit"] == commit[:8]
        assert document["gitTreeState"] == "clean"
        assert document["displayVersion"] == f"1.0.0+g{commit[:8]}"
        for forbidden in (
            "gitRef",
            "branch",
            "hostname",
            "sourceDir",
            "remoteUrl",
        ):
            assert forbidden not in document


def test_dirty_repository_has_unofficial_display_version():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        commit = initialize_repository(source)
        (source / "tracked.txt").write_text("dirty\n", encoding="utf-8")

        result = generate(source, output)

        assert result.returncode == 0, result.stderr
        document = read_document(output)
        assert document["gitTreeState"] == "dirty"
        assert document["displayVersion"] == f"1.0.0+g{commit[:8]}.dirty"


def test_source_export_without_git_has_explicit_unknown_identity():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()

        result = generate(source, output)

        assert result.returncode == 0, result.stderr
        document = read_document(output)
        assert document["gitCommit"] == ""
        assert document["gitShortCommit"] == "unknown"
        assert document["gitTag"] == ""
        assert document["gitTreeState"] == "unknown"
        assert document["displayVersion"] == "1.0.0+unknown"


def test_matching_official_tag_uses_stable_product_version():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        initialize_repository(source)
        run(["git", "tag", "v1.0.0"], source)

        result = generate(source, output, official=True, expected_tag="v1.0.0")

        assert result.returncode == 0, result.stderr
        document = read_document(output)
        assert document["gitTag"] == "v1.0.0"
        assert document["gitTreeState"] == "clean"
        assert document["displayVersion"] == "1.0.0"


def test_official_build_rejects_mismatched_version_tag():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        initialize_repository(source)
        run(["git", "tag", "v1.0.0"], source)

        result = generate(source, output, official=True, expected_tag="v2.0.0")

        assert result.returncode != 0
        assert "v2.0.0" in result.stderr
        assert "1.0.0" in result.stderr


def test_official_build_rejects_dirty_and_unknown_sources():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        initialize_repository(source)
        run(["git", "tag", "v1.0.0"], source)
        (source / "tracked.txt").write_text("dirty\n", encoding="utf-8")

        dirty = generate(source, output, official=True, expected_tag="v1.0.0")

        assert dirty.returncode != 0
        assert "dirty" in dirty.stderr.casefold()

        exported, exported_output = root / "exported", root / "exported-out"
        exported.mkdir()
        unknown = generate(
            exported,
            exported_output,
            official=True,
            expected_tag="v1.0.0",
        )
        assert unknown.returncode != 0
        assert "commit" in unknown.stderr.casefold()


def test_official_build_rejects_malformed_expected_tag():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        initialize_repository(source)

        result = generate(source, output, official=True, expected_tag="release/latest")

        assert result.returncode != 0
        assert "vmajor.minor.patch" in result.stderr.casefold()


def test_generation_does_not_rewrite_unchanged_outputs():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        initialize_repository(source)
        first = generate(source, output)
        assert first.returncode == 0, first.stderr
        paths = [
            output / "generated" / "BuildInfoData.h",
            output / "build-info.json",
        ]
        before = [(path.stat().st_ino, path.stat().st_mtime_ns) for path in paths]

        second = generate(source, output)

        assert second.returncode == 0, second.stderr
        after = [(path.stat().st_ino, path.stat().st_mtime_ns) for path in paths]
        assert after == before


def main():
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    for test in tests:
        test()
    print(f"build info generation checks passed ({len(tests)} tests)")


if __name__ == "__main__":
    main()
