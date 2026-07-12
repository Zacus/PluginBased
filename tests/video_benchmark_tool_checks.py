#!/usr/bin/env python3

import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "video_benchmark.py"


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def result(case_id: str, wall: float, checksum: int) -> dict:
    return {
        "case_id": case_id,
        "completed": True,
        "frames": {"video": 240, "checksum": checksum},
        "pixel_formats": {"yuv420p": 240},
        "timing": {
            "wall_ms": wall,
            "user_cpu_ms": wall * 2,
            "system_cpu_ms": wall * 0.1,
            "max_rss_bytes": 100 * 1024 * 1024,
        },
        "pool_before_release": {"acquire_count": 0},
        "pool_after_release": {"in_flight_count": 0},
    }


def run_compare(root: Path, profile: Path = None) -> dict:
    command = [
        sys.executable,
        str(TOOL),
        "compare",
        "--manifest", str(root / "manifest.json"),
        "--baseline-dir", str(root / "baseline"),
        "--current-dir", str(root / "current"),
        "--output-json", str(root / "gate.json"),
        "--output-markdown", str(root / "gate.md"),
    ]
    if profile:
        command.extend(["--allocation-profile", str(profile)])
    subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads((root / "gate.json").read_text(encoding="utf-8"))


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        write_json(root / "manifest.json", {
            "schema_version": 1,
            "cases": [{
                "id": "required",
                "codec": "h264",
                "filename": "sample.mp4",
                "url": "https://example.invalid/sample.mp4",
                "sha256": "0" * 64,
                "minimum_runs": 2,
                "f1_required": True,
            }],
        })
        for label in ("baseline", "current"):
            write_json(root / label / "suite.json", {
                "schema_version": 1,
                "label": label,
                "runner_sha256": label,
                "platform": "test",
            })
            write_json(root / label / "required" / "run-01.json", result("required", 10.0, 7))
            write_json(root / label / "required" / "run-02.json", result("required", 11.0, 7))

        closed = run_compare(root)
        assert closed["decision"] == "CLOSED"
        assert not closed["start_f1"]

        profile = root / "profile.json"
        write_json(profile, {
            "tool": "test",
            "decoder_allocator_cpu_percent": 6.0,
            "decoder_allocator_allocation_percent": 0.0,
            "evidence": "test",
        })
        opened = run_compare(root, profile)
        assert opened["decision"] == "OPEN"
        assert opened["start_f1"]


if __name__ == "__main__":
    main()
