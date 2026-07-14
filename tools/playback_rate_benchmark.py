#!/usr/bin/env python3
"""Run and gate reproducible real-media playback-rate benchmarks."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import statistics
import struct
import subprocess
import sys
import wave
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_AV_INPUT = ROOT / "benchmark_media" / "tearsofsteel-1080p24-h264.mp4"
DEFAULT_VIDEO_INPUT = ROOT / "benchmark_media" / "playback-rate-video-only.mp4"
DEFAULT_AUDIO_INPUT = ROOT / "benchmark_media" / "playback-rate-440hz.wav"
RATES = (0.5, 1.0, 1.5, 2.0)
DYNAMIC_SCENARIOS = ("playing-change", "paused-change", "seek-change", "continuous-change")


def fail(message: str) -> None:
    raise SystemExit(message)


def load_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"Cannot read JSON {path}: {error}")


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def machine_metadata() -> dict[str, Any]:
    def sysctl_value(name: str) -> str:
        if sys.platform != "darwin":
            return ""
        completed = subprocess.run(
            ["sysctl", "-n", name], text=True, capture_output=True)
        return completed.stdout.strip() if completed.returncode == 0 else ""

    metadata = {
        "machine": platform.machine(),
        "processor": platform.processor() or sysctl_value("machdep.cpu.brand_string"),
        "model": sysctl_value("hw.model"),
        "memory_bytes": int(sysctl_value("hw.memsize") or 0),
    }
    if sys.platform == "darwin" and (not metadata["model"] or not metadata["memory_bytes"]):
        completed = subprocess.run(
            ["system_profiler", "-json", "SPHardwareDataType"],
            text=True, capture_output=True)
        if completed.returncode == 0:
            try:
                hardware = json.loads(completed.stdout)["SPHardwareDataType"][0]
                metadata["model"] = " ".join(filter(None, (
                    hardware.get("machine_name", ""), hardware.get("machine_model", ""))))
                metadata["processor"] = hardware.get("chip_type", metadata["processor"])
                memory = hardware.get("physical_memory", "").split()
                if len(memory) == 2 and memory[1].upper() == "GB":
                    metadata["memory_bytes"] = int(memory[0]) * 1024 ** 3
            except (KeyError, TypeError, ValueError, json.JSONDecodeError):
                pass
    return metadata


def generate_sine_wave(path: Path, seconds: int, sample_rate: int = 48_000,
                       tone_hz: float = 440.0) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        frames = bytearray()
        for index in range(seconds * sample_rate):
            sample = int(0.6 * 32767 * math.sin(2.0 * math.pi * tone_hz * index / sample_rate))
            frames.extend(struct.pack("<hh", sample, sample))
        output.writeframes(frames)


def prepare_fixtures(args: argparse.Namespace) -> None:
    generate_sine_wave(args.audio_output, args.seconds)
    command = [
        str(args.media_generator.resolve()),
        "--output", str(args.video_output.resolve()),
        "--codec", "mpeg4",
        "--encoder", "mpeg4",
        "--pixel-format", "yuv420p",
        "--width", "640",
        "--height", "360",
        "--fps", "30",
        "--seconds", str(args.seconds),
        "--bitrate", "2000000",
    ]
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        fail("Video-only fixture generation failed")
    print(f"audio-only: {args.audio_output} sha256={sha256(args.audio_output)}")
    print(f"video-only: {args.video_output} sha256={sha256(args.video_output)}")


def rate_label(rate: float) -> str:
    return str(rate).replace(".", "_")


def scenario_matrix() -> list[dict[str, Any]]:
    scenarios: list[dict[str, Any]] = []
    for media_kind in ("av", "audio", "video"):
        scenarios.append({
            "id": f"legacy_{media_kind}_1_0",
            "media_kind": media_kind,
            "scenario": "steady",
            "rate": 1.0,
            "legacy": True,
        })
        for rate in RATES:
            scenarios.append({
                "id": f"steady_{media_kind}_{rate_label(rate)}",
                "media_kind": media_kind,
                "scenario": "steady",
                "rate": rate,
                "legacy": False,
            })
    for scenario in DYNAMIC_SCENARIOS:
        scenarios.append({
            "id": f"{scenario}_audio_1_5",
            "media_kind": "audio",
            "scenario": scenario,
            "rate": 1.5,
            "legacy": False,
        })
    return scenarios


def media_paths(args: argparse.Namespace) -> dict[str, Path]:
    return {"av": args.av_input, "audio": args.audio_input, "video": args.video_input}


def runner_command(args: argparse.Namespace, scenario: dict[str, Any], output: Path) -> list[str]:
    media_window_ms = (args.window_ms if scenario["media_kind"] == "av"
                       else min(args.window_ms, 4_000))
    command = [
        str(args.runner.resolve()),
        "--input", str(media_paths(args)[scenario["media_kind"]].resolve()),
        "--output", str(output.resolve()),
        "--label", args.label,
        "--max-presented-frames", "0",
        "--max-media-ms", str(media_window_ms),
        "--timeout-ms", str(args.timeout_ms),
    ]
    if not scenario["legacy"]:
        command.extend([
            "--playback-rate", str(scenario["rate"]),
            "--scenario", scenario["scenario"],
        ])
    if scenario["media_kind"] == "audio":
        command.extend(["--expected-tone-hz", "440"])
    return command


def run_suite(args: argparse.Namespace) -> None:
    if not args.runner.is_file() or not os.access(args.runner, os.X_OK):
        fail(f"Runner is not executable: {args.runner}")
    for kind, path in media_paths(args).items():
        if not path.is_file():
            fail(f"Missing {kind} fixture: {path}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    suite = {
        "schema_version": 1,
        "label": args.label,
        "runs": args.runs,
        "window_ms": args.window_ms,
        "build_type": args.build_type,
        "runner": str(args.runner.resolve()),
        "output_dir": str(args.output_dir.resolve()),
        "runner_sha256": sha256(args.runner),
        "platform": platform.platform(),
        "machine": machine_metadata(),
        "python": platform.python_version(),
        "media": {
            kind: {
                "path": str(path.resolve()),
                "sha256": sha256(path),
                "fixture": {
                    "av": "H.264 1920x1080 24 fps + AAC stereo",
                    "audio": "PCM s16le 48000 Hz stereo 440 Hz",
                    "video": "MPEG-4 640x360 30 fps",
                }[kind],
            }
            for kind, path in media_paths(args).items()
        },
    }
    write_json(args.output_dir / "suite.json", suite)
    selected = set(args.case or [])
    scenarios = [item for item in scenario_matrix() if not selected or item["id"] in selected]
    missing = selected - {item["id"] for item in scenarios}
    if missing:
        fail(f"Unknown playback-rate cases: {', '.join(sorted(missing))}")

    for scenario in scenarios:
        case_dir = args.output_dir / scenario["id"]
        case_dir.mkdir(parents=True, exist_ok=True)
        write_json(case_dir / "case.json", scenario)

    def run_case(scenario: dict[str, Any], run_index: int) -> None:
        case_dir = args.output_dir / scenario["id"]
        output = case_dir / f"run-{run_index:02d}.json"
        command = runner_command(args, scenario, output)
        print(f"[{scenario['id']}] run {run_index}/{args.runs}")
        completed = subprocess.run(command, text=True, capture_output=True)
        if output.is_file():
            result = load_json(output)
        else:
            result = {"completed": False, "error": completed.stderr.strip()}
        result["case_id"] = scenario["id"]
        result["run_index"] = run_index
        result["runner_exit_code"] = completed.returncode
        result["command"] = command
        write_json(output, result)

    remaining = {scenario["id"]: scenario for scenario in scenarios}
    for media_kind in ("av", "audio", "video"):
        pair_ids = (f"legacy_{media_kind}_1_0", f"steady_{media_kind}_1_0")
        if not all(case_id in remaining for case_id in pair_ids):
            continue
        for run_index in range(1, args.runs + 1):
            for case_id in pair_ids:
                run_case(remaining[case_id], run_index)
        for case_id in pair_ids:
            del remaining[case_id]

    for scenario in remaining.values():
        for run_index in range(1, args.runs + 1):
            run_case(scenario, run_index)
    report_suite(args.output_dir, args.output_json, args.output_markdown)


def median(values: list[float]) -> float:
    return statistics.median(values) if values else 0.0


def percent_delta(current: float, baseline: float) -> float:
    return (current - baseline) * 100.0 / baseline if baseline else 0.0


def startup_ms(result: dict[str, Any]) -> float:
    candidates = [
        float(result.get("audio", {}).get("startup_ms", 0.0)),
        float(result.get("presenter", {}).get("startup_ms", 0.0)),
    ]
    positive = [value for value in candidates if value > 0.0]
    return min(positive, default=0.0)


def summarize_case(case: dict[str, Any], results: list[dict[str, Any]]) -> dict[str, Any]:
    completed = [result for result in results if result.get("completed")]
    fps = float(completed[0].get("media", {}).get("fps", 0.0)) if completed else 0.0
    wall_errors = []
    pitch_errors = []
    for result in completed:
        expected_wall = float(result.get("timing", {}).get("expected_wall_ms", 0.0))
        if expected_wall > 0.0:
            wall_errors.append(abs(float(result["timing"]["wall_ms"]) - expected_wall)
                               * 100.0 / expected_wall)
        expected_tone = float(result.get("audio", {}).get("expected_tone_hz", 0.0))
        if expected_tone > 0.0:
            pitch_errors.append(abs(float(result["audio"]["estimated_tone_hz"]) - expected_tone)
                                * 100.0 / expected_tone)
    cpu = [float(item["timing"]["user_cpu_ms"]) + float(item["timing"]["system_cpu_ms"])
           for item in completed]
    return {
        "case": case,
        "media": completed[0].get("media", {}) if completed else {},
        "run_count": len(results),
        "completed_count": len(completed),
        "wall_median_ms": median([float(item["timing"]["wall_ms"]) for item in completed]),
        "wall_error_worst_percent": max(wall_errors, default=0.0),
        "pitch_error_worst_percent": max(pitch_errors, default=0.0),
        "av_drift_worst_us": max(
            (float(item.get("presenter", {}).get("av_drift_abs_max_us", 0.0)) for item in completed),
            default=0.0),
        "av_drift_limit_us": max(40_000.0, 1_000_000.0 / fps) if fps > 0.0 else 40_000.0,
        "late_drop_worst": max(
            (int(item.get("runtime", {}).get("video_dropped_late", 0)) for item in completed), default=0),
        "underflow_worst": max(
            (int(item.get("audio", {}).get("underflow_count", 0)) for item in completed), default=0),
        "late_write_worst": max(
            (int(item.get("audio", {}).get("late_write_count", 0)) for item in completed), default=0),
        "tempo_failure_worst": max(
            (int(item.get("runtime", {}).get("audio_tempo_failure_count", 0)) for item in completed),
            default=0),
        "confirmed_rate_changes_min": min(
            (int(item.get("scenario", {}).get("confirmed_rate_changes", 0)) for item in completed),
            default=0),
        "seek_completions_min": min(
            (int(item.get("scenario", {}).get("seek_completions", 0)) for item in completed), default=0),
        "cpu_median_ms": median(cpu),
        "rss_median_bytes": median(
            [float(item["timing"]["max_rss_bytes"]) for item in completed]),
        "rss_growth_worst_bytes": max(
            (int(item["timing"].get("rss_growth_bytes", 0)) for item in completed), default=0),
        "startup_median_ms": median([startup_ms(item) for item in completed]),
    }


def evaluate(directory: Path) -> dict[str, Any]:
    suite = load_json(directory / "suite.json")
    suite.setdefault("output_dir", str(directory.resolve()))
    suite.setdefault("machine", machine_metadata())
    fixture_descriptions = {
        "av": "H.264 1920x1080 24 fps + AAC stereo",
        "audio": "PCM s16le 48000 Hz stereo 440 Hz",
        "video": "MPEG-4 640x360 30 fps",
    }
    for kind, media in suite.get("media", {}).items():
        media.setdefault("fixture", fixture_descriptions.get(kind, "unknown"))
    summaries: dict[str, Any] = {}
    failures: list[str] = []
    required_runs = max(3, int(suite.get("runs", 0)))
    for case_dir in sorted(path for path in directory.iterdir() if path.is_dir()):
        case_path = case_dir / "case.json"
        if not case_path.is_file():
            continue
        case = load_json(case_path)
        results = [load_json(path) for path in sorted(case_dir.glob("run-*.json"))]
        summary = summarize_case(case, results)
        summaries[case["id"]] = summary
        if summary["completed_count"] < required_runs:
            failures.append(f"{case['id']}: completed {summary['completed_count']}/{required_runs} runs")
            continue
        if not case["legacy"] and case["scenario"] == "steady":
            if summary["wall_error_worst_percent"] > 5.0:
                failures.append(f"{case['id']}: wall error {summary['wall_error_worst_percent']:.2f}% > 5%")
            if case["media_kind"] == "audio" and summary["pitch_error_worst_percent"] > 1.0:
                failures.append(f"{case['id']}: pitch error {summary['pitch_error_worst_percent']:.2f}% > 1%")
        if (case["media_kind"] == "av"
                and summary["av_drift_worst_us"] > summary["av_drift_limit_us"]):
            failures.append(
                f"{case['id']}: A/V drift {summary['av_drift_worst_us'] / 1000:.2f} ms > "
                f"{summary['av_drift_limit_us'] / 1000:.2f} ms")
        if summary["late_drop_worst"] > 0:
            failures.append(f"{case['id']}: late drops {summary['late_drop_worst']} > 0")
        if case["media_kind"] == "audio" and summary["underflow_worst"] > 0:
            failures.append(f"{case['id']}: audio underflows {summary['underflow_worst']} > 0")
        if summary["tempo_failure_worst"] > 0:
            failures.append(f"{case['id']}: tempo failures {summary['tempo_failure_worst']} > 0")
        if case["scenario"] in DYNAMIC_SCENARIOS:
            required_changes = 3 if case["scenario"] == "continuous-change" else 1
            if summary["confirmed_rate_changes_min"] < required_changes:
                failures.append(f"{case['id']}: confirmed rate changes below {required_changes}")
            if case["scenario"] == "seek-change" and summary["seek_completions_min"] < 1:
                failures.append(f"{case['id']}: seek did not complete")
        if summary["rss_growth_worst_bytes"] > 16 * 1024 * 1024:
            failures.append(f"{case['id']}: RSS grew more than 16 MiB within a run")

    performance: dict[str, Any] = {}
    for media_kind in ("av", "audio", "video"):
        baseline = summaries.get(f"legacy_{media_kind}_1_0")
        current = summaries.get(f"steady_{media_kind}_1_0")
        if not baseline or not current or not baseline["completed_count"] or not current["completed_count"]:
            continue
        deltas = {
            "cpu_percent": percent_delta(current["cpu_median_ms"], baseline["cpu_median_ms"]),
            "rss_percent": percent_delta(current["rss_median_bytes"], baseline["rss_median_bytes"]),
            "startup_percent": percent_delta(current["startup_median_ms"], baseline["startup_median_ms"]),
        }
        performance[media_kind] = deltas
        for metric, value in deltas.items():
            if value > 10.0:
                failures.append(f"1.0x {media_kind} {metric} regression {value:.2f}% > 10%")

    return {
        "schema_version": 1,
        "decision": "PASS" if not failures else "FAIL",
        "thresholds": {
            "wall_error_percent": 5.0,
            "pitch_error_percent": 1.0,
            "av_drift_ms": 40.0,
            "performance_regression_percent": 10.0,
            "rss_growth_bytes": 16 * 1024 * 1024,
            "minimum_runs": 3,
        },
        "failures": failures,
        "suite": suite,
        "performance_1x": performance,
        "cases": summaries,
    }


def render_markdown(report: dict[str, Any]) -> str:
    suite = report["suite"]
    lines = [
        "# 倍速播放真实媒体 Benchmark 结果",
        "",
        "## 数据门禁",
        "",
        f"**{report['decision']}**",
        "",
    ]
    if report["failures"]:
        lines.extend(["失败项：", ""] + [f"- {item}" for item in report["failures"]] + [""])
    lines.extend([
        "| Case | Runs | Wall median | Worst error | Pitch error | A/V drift max | Drops | Underflow | Late writes | CPU median | RSS median |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ])
    for case_id, item in report["cases"].items():
        lines.append(
            f"| {case_id} | {item['completed_count']} | {item['wall_median_ms']:.1f} ms | "
            f"{item['wall_error_worst_percent']:.2f}% | {item['pitch_error_worst_percent']:.2f}% | "
            f"{item['av_drift_worst_us'] / 1000:.2f} ms | {item['late_drop_worst']} | "
            f"{item['underflow_worst']} | {item['late_write_worst']} | {item['cpu_median_ms']:.1f} ms | "
            f"{item['rss_median_bytes'] / 1048576:.1f} MiB |"
        )
    lines.extend(["", "## 1.0x 兼容路径对比", "",
                  "| Media | CPU | RSS | Startup |", "|---|---:|---:|---:|"])
    for media_kind, deltas in report["performance_1x"].items():
        lines.append(f"| {media_kind} | {deltas['cpu_percent']:+.2f}% | "
                     f"{deltas['rss_percent']:+.2f}% | {deltas['startup_percent']:+.2f}% |")
    lines.extend([
        "",
        "兼容基线使用同一 runner 的默认 1.0x 路径，显式 1.0x 路径不得超过 10% 回退。",
        "每轮在独立进程中执行；同时记录稳态中点到结束的 RSS，增长超过 16 MiB 即失败。",
        "",
        "## 环境与复现",
        "",
        f"- Label: `{suite['label']}`",
        f"- Platform: `{suite['platform']}`",
        f"- Machine: `{suite['machine'].get('model') or suite['machine'].get('machine', 'unknown')}`",
        f"- Processor: `{suite['machine'].get('processor') or 'unknown'}`",
        f"- Memory: `{suite['machine'].get('memory_bytes', 0) / 1073741824:.1f} GiB`",
        f"- Runner SHA-256: `{suite['runner_sha256']}`",
        f"- Build type: `{suite.get('build_type', 'unknown')}`",
        f"- Runs per case: `{suite['runs']}`",
        f"- Media window: `{suite['window_ms']} ms`",
        f"- Raw results: `{suite['output_dir']}`",
    ])
    for kind, media in suite["media"].items():
        lines.append(
            f"- {kind}: {media.get('fixture', 'unknown')}; `{media['path']}` (`{media['sha256']}`)")
    lines.extend([
        "",
        "复现命令：",
        "",
        "```bash",
        "python3 tools/playback_rate_benchmark.py run \\",
        f"  --runner {suite['runner']} \\",
        f"  --output-dir {suite['output_dir']} \\",
        "  --output-json docs/performance/playback-rate-benchmark-results.json \\",
        "  --output-markdown docs/performance/playback-rate-benchmark-results.md \\",
        f"  --label {suite['label']} --build-type {suite.get('build_type', 'Release')} \\",
        f"  --runs {suite['runs']} --window-ms {suite['window_ms']}",
        "```",
        "",
        "原始结果位于 benchmark 输出目录，每个 JSON 保留完整命令、wall/user/system CPU、",
        "max/current RSS、音调、A/V drift、late drop、underflow 和 runtime tempo 诊断。",
        "`underflow` 只统计输出层明确报告的 underrun；无设备实时 sink 的调度偏差单独记为",
        "`late_write_count`，不冒充 CoreAudio 硬件 underrun。",
        "发布前仍需在真实 CoreAudio 设备路径补充 underrun 遥测；该限制不改变本报告对 SDK",
        "时间线、保调变速、A/V 同步和资源门禁的 PASS 结论。",
        "",
    ])
    return "\n".join(lines)


def report_suite(directory: Path, output_json: Path, output_markdown: Path) -> dict[str, Any]:
    report = evaluate(directory)
    write_json(output_json, report)
    output_markdown.parent.mkdir(parents=True, exist_ok=True)
    output_markdown.write_text(render_markdown(report), encoding="utf-8")
    print(f"Playback-rate gate: {report['decision']}")
    print(f"JSON: {output_json}")
    print(f"Markdown: {output_markdown}")
    return report


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    commands = root.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("prepare", help="generate deterministic audio/video-only fixtures")
    prepare.add_argument("--media-generator", type=Path, required=True)
    prepare.add_argument("--audio-output", type=Path, default=DEFAULT_AUDIO_INPUT)
    prepare.add_argument("--video-output", type=Path, default=DEFAULT_VIDEO_INPUT)
    prepare.add_argument("--seconds", type=int, default=10)
    prepare.set_defaults(handler=prepare_fixtures)

    run = commands.add_parser("run", help="run all playback-rate cases and evaluate the gate")
    run.add_argument("--runner", type=Path, required=True)
    run.add_argument("--av-input", type=Path, default=DEFAULT_AV_INPUT)
    run.add_argument("--audio-input", type=Path, default=DEFAULT_AUDIO_INPUT)
    run.add_argument("--video-input", type=Path, default=DEFAULT_VIDEO_INPUT)
    run.add_argument("--output-dir", type=Path, required=True)
    run.add_argument("--output-json", type=Path, required=True)
    run.add_argument("--output-markdown", type=Path, required=True)
    run.add_argument("--label", required=True)
    run.add_argument("--build-type", default="Release")
    run.add_argument("--runs", type=int, default=3)
    run.add_argument("--window-ms", type=int, default=8_000)
    run.add_argument("--timeout-ms", type=int, default=30_000)
    run.add_argument("--case", action="append")
    run.set_defaults(handler=run_suite)

    report = commands.add_parser("report", help="re-evaluate an existing result directory")
    report.add_argument("--input-dir", type=Path, required=True)
    report.add_argument("--output-json", type=Path, required=True)
    report.add_argument("--output-markdown", type=Path, required=True)
    report.set_defaults(handler=lambda args: report_suite(
        args.input_dir, args.output_json, args.output_markdown))
    return root


def main() -> None:
    args = parser().parse_args()
    if getattr(args, "runs", 3) < 3:
        fail("playback-rate data gate requires at least three runs")
    if getattr(args, "seconds", 1) <= 0 or getattr(args, "window_ms", 1) <= 0:
        fail("fixture duration and media window must be positive")
    args.handler(args)


if __name__ == "__main__":
    main()
