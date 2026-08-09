#!/usr/bin/env python3

import json
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "video_benchmark.py"
PROFILE_TOOL = ROOT / "tools" / "video_allocator_profile.py"
GET_BUFFER2_F1_TOOL = ROOT / "tools" / "get_buffer2_f1_benchmark.py"
PLAYBACK_RATE_TOOL = ROOT / "tools" / "playback_rate_benchmark.py"


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


def realtime_result(case_id: str, wall: float, checksum: int) -> dict:
    return {
        "case_id": case_id,
        "benchmark_kind": "realtime_pipeline",
        "completed": True,
        "presenter": {
            "presented_frames": 600,
            "checksum": checksum,
            "lateness_average_us": 1500,
        },
        "timing": {
            "wall_ms": wall,
            "user_cpu_ms": wall * 0.5,
            "system_cpu_ms": wall * 0.1,
            "max_rss_bytes": 200 * 1024 * 1024,
        },
        "runtime": {
            "video_dropped_late": 0,
            "video_queue_high_watermark": 8,
        },
        "pool": {"acquire_count": 0, "in_flight_count": 0},
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
            "cases": [
                {
                    "id": "required",
                    "codec": "h264",
                    "filename": "sample.mp4",
                    "url": "https://example.invalid/sample.mp4",
                    "sha256": "0" * 64,
                    "max_video_frames": 240,
                    "minimum_runs": 2,
                    "f1_required": True,
                },
                {
                    "id": "realtime",
                    "codec": "hevc",
                    "benchmark_kind": "realtime_pipeline",
                    "filename": "realtime.mp4",
                    "url": "https://example.invalid/realtime.mp4",
                    "sha256": "1" * 64,
                    "max_presented_frames": 600,
                    "minimum_runs": 2,
                    "f1_required": True,
                },
            ],
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
            write_json(root / label / "realtime" / "run-01.json", realtime_result("realtime", 10_000, 8))
            write_json(root / label / "realtime" / "run-02.json", realtime_result("realtime", 10_010, 8))

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

        write_json(profile, {
            "tool": "test",
            "decoder_allocator_cpu_percent": 4.0,
            "decoder_allocator_allocation_percent": None,
        })
        below_threshold = run_compare(root, profile)
        assert below_threshold["decision"] == "CLOSED"
        assert any("allocation 不可用" in reason for reason in below_threshold["reasons"])

        for run in ("run-01.json", "run-02.json"):
            path = root / "current" / "realtime" / run
            truncated = json.loads(path.read_text(encoding="utf-8"))
            truncated["presenter"]["presented_frames"] = 599
            write_json(path, truncated)
        incomplete = run_compare(root, profile)
        assert incomplete["decision"] == "CLOSED"
        assert any("要求 600 帧" in reason for reason in incomplete["reasons"])

        time_profile = root / "time-profile.xml"
        time_profile.write_text("""<?xml version="1.0"?>
<trace-query-result>
  <row><weight>100</weight><backtrace><frame name="decode"/></backtrace></row>
  <row><weight>10</weight><backtrace><frame name="av_malloc"/></backtrace></row>
  <row><weight>5</weight><backtrace><frame name="malloc"><binary name="libsystem_malloc.dylib"/></frame></backtrace></row>
  <row><weight>5</weight><backtrace><frame name="_platform_memset"/></backtrace></row>
</trace-query-result>
""", encoding="utf-8")
        generated_profile = root / "generated-profile.json"
        subprocess.run([
            sys.executable,
            str(PROFILE_TOOL),
            "analyze-time",
            "--time-profile-xml", str(time_profile),
            "--output", str(generated_profile),
        ], check=True, capture_output=True, text=True)
        profile_result = json.loads(generated_profile.read_text(encoding="utf-8"))
        assert abs(profile_result["decoder_allocator_cpu_percent"] - 20 / 120 * 100) < 1e-9
        assert profile_result["decoder_allocator_allocation_percent"] is None
        assert profile_result["time_profile"]["runs"][0]["direct_allocator_sample_weight_ns"] == 10

        generated_media = b"generated-media"
        generator = root / "fake-generator.py"
        generator.write_text("""#!/usr/bin/env python3
import argparse
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('output', 'codec', 'encoder', 'pixel-format', 'profile', 'width', 'height', 'fps', 'seconds', 'bitrate'):
    p.add_argument('--' + name)
a=p.parse_args()
Path(a.output).write_bytes(b'generated-media')
""", encoding="utf-8")
        generator.chmod(0o755)
        generated_manifest = root / "generated-manifest.json"
        write_json(generated_manifest, {
            "schema_version": 1,
            "cases": [{
                "id": "generated",
                "codec": "prores",
                "source_kind": "generated",
                "filename": "generated.mov",
                "generator": {
                    "codec": "prores",
                    "encoder": "prores_ks",
                    "pixel_format": "yuv422p10le",
                    "profile": "standard",
                    "width": 3840,
                    "height": 2160,
                    "fps": 120,
                    "seconds": 1,
                    "bitrate": 200_000_000,
                },
                "sha256": hashlib.sha256(generated_media).hexdigest(),
            }],
        })
        generated_media_dir = root / "generated-media"
        subprocess.run([
            sys.executable,
            str(TOOL),
            "fetch",
            "--manifest", str(generated_manifest),
            "--media-dir", str(generated_media_dir),
            "--media-generator", str(generator),
        ], check=True, capture_output=True, text=True)
        assert (generated_media_dir / "generated.mov").read_bytes() == generated_media

        f1_root = root / "get-buffer2-f1"
        media = f1_root / "media.bin"
        media.parent.mkdir(parents=True, exist_ok=True)
        media.write_bytes(b"fixed-media")
        media_hash = hashlib.sha256(media.read_bytes()).hexdigest()
        write_json(f1_root / "manifest.json", {
            "schema_version": 1,
            "cases": [{
                "id": "primary",
                "codec": "test",
                "filename": media.name,
                "sha256": media_hash,
                "max_video_frames": 10,
            }],
        })
        fake_runner = f1_root / "fake-runner.py"
        fake_runner.write_text("""#!/usr/bin/env python3
import argparse, json
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--input'); p.add_argument('--output'); p.add_argument('--label')
p.add_argument('--allocator'); p.add_argument('--max-video-frames', type=int)
p.add_argument('--hold-video-frames'); p.add_argument('--timeout-ms')
a=p.parse_args()
prototype=a.allocator == 'prototype'
result={
  'completed': True,
  'frames': {'video': a.max_video_frames, 'checksum': 7},
  'timing': {
    'wall_ms': 98.0 if prototype else 100.0,
    'user_cpu_ms': 98.0 if prototype else 100.0,
    'system_cpu_ms': 0.0,
    'max_rss_bytes': 1000,
  },
  'allocator': {
    'callback_count': 10,
    'prototype_frame_count': 10 if prototype else 0,
    'fallback_count': 0 if prototype else 10,
    'plane_acquire_count': 30 if prototype else 0,
    'plane_allocation_count': 3 if prototype else 0,
  },
}
Path(a.output).write_text(json.dumps(result), encoding='utf-8')
""", encoding="utf-8")
        fake_runner.chmod(0o755)

        f1_command = [
            sys.executable,
            str(GET_BUFFER2_F1_TOOL),
            "--runner", str(fake_runner),
            "--manifest", str(f1_root / "manifest.json"),
            "--media-dir", str(f1_root),
            "--output-dir", str(f1_root / "results"),
            "--output-json", str(f1_root / "decision.json"),
            "--output-markdown", str(f1_root / "decision.md"),
            "--label", "test",
            "--case", "primary",
            "--primary-case", "primary",
            "--runs", "2",
            "--warmups", "0",
        ]
        subprocess.run(f1_command, check=True, capture_output=True, text=True)
        f1_decision = json.loads((f1_root / "decision.json").read_text(encoding="utf-8"))
        assert f1_decision["decision"] == "NO-GO"
        subprocess.run(
            f1_command + ["--minimum-improvement-percent", "1"],
            check=True,
            capture_output=True,
            text=True,
        )
        assert json.loads((f1_root / "decision.json").read_text(encoding="utf-8"))["decision"] == "GO"

        rate_root = root / "playback-rate"
        write_json(rate_root / "suite.json", {
            "schema_version": 1,
            "label": "test",
            "runs": 3,
            "window_ms": 4000,
            "build_type": "Release",
            "runner": "test-runner",
            "runner_sha256": "test",
            "platform": "test",
            "media": {
                kind: {"path": f"{kind}.media", "sha256": kind}
                for kind in ("av", "audio", "video")
            },
        })

        def write_rate_case(case_id: str, media_kind: str, rate: float = 1.0,
                            legacy: bool = False) -> None:
            case_dir = rate_root / case_id
            write_json(case_dir / "case.json", {
                "id": case_id,
                "media_kind": media_kind,
                "scenario": "steady",
                "rate": rate,
                "legacy": legacy,
            })
            for index in range(1, 4):
                expected_wall = 4000 / rate
                write_json(case_dir / f"run-{index:02d}.json", {
                    "completed": True,
                    "scenario": {"confirmed_rate_changes": 0, "seek_completions": 0},
                    "presenter": {
                        "startup_ms": 20,
                        "av_drift_abs_max_us": 10_000 if media_kind == "av" else 0,
                    },
                    "audio": {
                        "startup_ms": 15 if media_kind != "video" else 0,
                        "expected_tone_hz": 440 if media_kind == "audio" else 0,
                        "estimated_tone_hz": 440 if media_kind == "audio" else 0,
                        "underflow_count": 0,
                    },
                    "timing": {
                        "wall_ms": expected_wall,
                        "expected_wall_ms": 0 if legacy else expected_wall,
                        "user_cpu_ms": 100,
                        "system_cpu_ms": 10,
                        "max_rss_bytes": 100 * 1024 * 1024,
                        "rss_growth_bytes": 1024,
                    },
                    "runtime": {
                        "video_dropped_late": 0,
                        "audio_tempo_failure_count": 0,
                    },
                })

        for media_kind in ("av", "audio", "video"):
            write_rate_case(f"legacy_{media_kind}_1_0", media_kind, legacy=True)
            write_rate_case(f"steady_{media_kind}_1_0", media_kind)
        write_rate_case("steady_audio_1_5", "audio", rate=1.5)

        rate_json = rate_root / "gate.json"
        rate_markdown = rate_root / "gate.md"
        rate_command = [
            sys.executable,
            str(PLAYBACK_RATE_TOOL),
            "report",
            "--input-dir", str(rate_root),
            "--output-json", str(rate_json),
            "--output-markdown", str(rate_markdown),
        ]
        subprocess.run(rate_command, check=True, capture_output=True, text=True)
        assert json.loads(rate_json.read_text(encoding="utf-8"))["decision"] == "PASS"

        failed_run = rate_root / "steady_audio_1_5" / "run-03.json"
        failed_result = json.loads(failed_run.read_text(encoding="utf-8"))
        failed_result["timing"]["wall_ms"] *= 1.2
        write_json(failed_run, failed_result)
        subprocess.run(rate_command, check=True, capture_output=True, text=True)
        failed_gate = json.loads(rate_json.read_text(encoding="utf-8"))
        assert failed_gate["decision"] == "FAIL"
        assert any("wall error" in reason for reason in failed_gate["failures"])


if __name__ == "__main__":
    main()
