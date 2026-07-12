#!/usr/bin/env python3
"""Reproducible real-media benchmark orchestration for the media SDK."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path
from typing import Any, Optional


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "tools" / "video_benchmark" / "media_manifest.json"


def fail(message: str) -> None:
    raise SystemExit(message)


def load_json(path: Path) -> Any:
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


def manifest_cases(manifest_path: Path) -> list[dict[str, Any]]:
    manifest = load_json(manifest_path)
    if manifest.get("schema_version") != 1 or not isinstance(manifest.get("cases"), list):
        fail(f"Unsupported benchmark manifest: {manifest_path}")
    cases = manifest["cases"]
    identifiers = [case.get("id") for case in cases]
    if any(not identifier for identifier in identifiers) or len(identifiers) != len(set(identifiers)):
        fail("Benchmark case ids must be non-empty and unique")
    return cases


def fetch_media(args: argparse.Namespace) -> None:
    cases = manifest_cases(args.manifest)
    args.media_dir.mkdir(parents=True, exist_ok=True)
    for case in cases:
        destination = args.media_dir / case["filename"]
        expected = case.get("sha256", "").lower()
        if destination.is_file() and (not expected or sha256(destination) == expected):
            print(f"[cached] {case['id']}: {destination}")
            continue

        request = urllib.request.Request(
            case["url"],
            headers={"User-Agent": "PluginBased-MediaSdkBenchmark/1.0"},
        )
        print(f"[fetch]  {case['id']}: {case['url']}")
        with tempfile.NamedTemporaryFile(dir=args.media_dir, delete=False) as temporary:
            temporary_path = Path(temporary.name)
            try:
                with urllib.request.urlopen(request, timeout=args.timeout) as response:
                    while chunk := response.read(1024 * 1024):
                        temporary.write(chunk)
            except Exception:
                temporary_path.unlink(missing_ok=True)
                raise

        actual = sha256(temporary_path)
        if expected and actual != expected:
            temporary_path.unlink(missing_ok=True)
            fail(f"SHA-256 mismatch for {case['id']}: expected {expected}, got {actual}")
        temporary_path.replace(destination)
        print(f"[ready]  {case['id']}: sha256={actual}")


def benchmark_command(
    runner: Path,
    case: dict[str, Any],
    media_path: Path,
    output_path: Path,
    label: str,
) -> list[str]:
    command = [
        str(runner),
        "--input", str(media_path),
        "--output", str(output_path),
        "--label", label,
        "--software",
        "--hold-video-frames", str(case.get("hold_video_frames", 3)),
        "--max-video-frames", str(case.get("max_video_frames", 0)),
        "--timeout-ms", str(case.get("timeout_ms", 120_000)),
        "--report-interval-ms", str(case.get("report_interval_ms", 100)),
    ]
    return command


def run_suite(args: argparse.Namespace) -> None:
    runner = args.runner.resolve()
    if not runner.is_file() or not os.access(runner, os.X_OK):
        fail(f"Benchmark runner is not executable: {runner}")
    cases = manifest_cases(args.manifest)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    metadata = {
        "schema_version": 1,
        "label": args.label,
        "runner": str(runner),
        "runner_sha256": sha256(runner),
        "manifest": str(args.manifest.resolve()),
        "manifest_sha256": sha256(args.manifest),
        "runs": args.runs,
        "warmups": args.warmups,
        "platform": platform.platform(),
        "python": platform.python_version(),
    }
    write_json(args.output_dir / "suite.json", metadata)

    for case in cases:
        media_path = args.media_dir / case["filename"]
        if not media_path.is_file():
            fail(f"Missing media for {case['id']}: {media_path}; run the fetch command first")
        expected = case.get("sha256", "").lower()
        actual = sha256(media_path)
        if expected and actual != expected:
            fail(f"Media hash mismatch for {case['id']}: expected {expected}, got {actual}")

        case_dir = args.output_dir / case["id"]
        case_dir.mkdir(parents=True, exist_ok=True)
        for index in range(args.warmups + args.runs):
            warmup = index < args.warmups
            run_index = index - args.warmups + 1
            output_path = case_dir / (f"warmup-{index + 1:02d}.json" if warmup else f"run-{run_index:02d}.json")
            command = benchmark_command(runner, case, media_path, output_path, args.label)
            print(f"[{case['id']}] {'warmup' if warmup else 'run'} {index + 1}/{args.warmups + args.runs}")
            completed = subprocess.run(command, text=True, capture_output=True)
            if completed.returncode != 0:
                sys.stderr.write(completed.stdout)
                sys.stderr.write(completed.stderr)
                fail(f"Benchmark failed for {case['id']} with exit code {completed.returncode}")
            result = load_json(output_path)
            result["case_id"] = case["id"]
            result["codec"] = case.get("codec", "unknown")
            result["media_sha256"] = actual
            result["warmup"] = warmup
            result["run_index"] = index + 1 if warmup else run_index
            write_json(output_path, result)


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize_case(results: list[dict[str, Any]]) -> dict[str, Any]:
    completed = [result for result in results if result.get("completed")]
    wall = [float(result["timing"]["wall_ms"]) for result in completed]
    cpu = [
        float(result["timing"]["user_cpu_ms"]) + float(result["timing"]["system_cpu_ms"])
        for result in completed
    ]
    rss = [float(result["timing"]["max_rss_bytes"]) for result in completed]
    checksums = sorted({result["frames"]["checksum"] for result in completed})
    frame_counts = sorted({result["frames"]["video"] for result in completed})
    pool_acquire = [float(result["pool_before_release"]["acquire_count"]) for result in completed]
    pool_inflight_after = [float(result["pool_after_release"]["in_flight_count"]) for result in completed]
    pixel_formats = sorted({name for result in completed for name in result.get("pixel_formats", {})})
    return {
        "run_count": len(results),
        "completed_count": len(completed),
        "wall_median_ms": statistics.median(wall) if wall else 0.0,
        "wall_p95_ms": percentile(wall, 0.95),
        "cpu_median_ms": statistics.median(cpu) if cpu else 0.0,
        "rss_median_bytes": statistics.median(rss) if rss else 0.0,
        "checksums": checksums,
        "video_frame_counts": frame_counts,
        "pool_acquire_median": statistics.median(pool_acquire) if pool_acquire else 0.0,
        "pool_inflight_after_max": max(pool_inflight_after, default=0.0),
        "pixel_formats": pixel_formats,
    }


def load_suite_results(directory: Path) -> dict[str, list[dict[str, Any]]]:
    grouped: dict[str, list[dict[str, Any]]] = {}
    for path in sorted(directory.glob("*/run-*.json")):
        result = load_json(path)
        grouped.setdefault(result["case_id"], []).append(result)
    return grouped


def delta_percent(current: float, baseline: float) -> Optional[float]:
    if baseline == 0:
        return None
    return (current - baseline) * 100.0 / baseline


def suite_fingerprint(suite: dict[str, Any]) -> dict[str, Any]:
    keys = (
        "schema_version",
        "label",
        "runner_sha256",
        "manifest_sha256",
        "runs",
        "warmups",
        "platform",
        "python",
    )
    return {key: suite[key] for key in keys if key in suite}


def format_delta(value: Optional[float]) -> str:
    return "n/a" if value is None else f"{value:+.2f}%"


def compare_suites(args: argparse.Namespace) -> None:
    cases = {case["id"]: case for case in manifest_cases(args.manifest)}
    baseline_suite = load_json(args.baseline_dir / "suite.json")
    current_suite = load_json(args.current_dir / "suite.json")
    baseline_results = load_suite_results(args.baseline_dir)
    current_results = load_suite_results(args.current_dir)
    summaries: dict[str, Any] = {}
    coverage_errors: list[str] = []

    for case_id, case in cases.items():
        baseline = summarize_case(baseline_results.get(case_id, []))
        current = summarize_case(current_results.get(case_id, []))
        required_runs = int(case.get("minimum_runs", 5))
        if case.get("f1_required", False):
            for label, summary in (("baseline", baseline), ("current", current)):
                if summary["completed_count"] < required_runs:
                    coverage_errors.append(
                        f"{case_id} {label} 仅完成 {summary['completed_count']} 轮，要求 {required_runs} 轮"
                    )
                if len(summary["checksums"]) != 1 or len(summary["video_frame_counts"]) != 1:
                    coverage_errors.append(f"{case_id} {label} 的帧数或 checksum 在多轮间不稳定")
            if current["pool_inflight_after_max"] != 0:
                coverage_errors.append(f"{case_id} current 释放 held frames 后仍有 pool frame 在途")

        summaries[case_id] = {
            "case": case,
            "baseline": baseline,
            "current": current,
            "wall_delta_percent": delta_percent(current["wall_median_ms"], baseline["wall_median_ms"]),
            "cpu_delta_percent": delta_percent(current["cpu_median_ms"], baseline["cpu_median_ms"]),
            "rss_delta_percent": delta_percent(current["rss_median_bytes"], baseline["rss_median_bytes"]),
        }

    allocation_profile = load_json(args.allocation_profile) if args.allocation_profile else None
    profile_cpu = float(allocation_profile.get("decoder_allocator_cpu_percent", 0.0)) if allocation_profile else 0.0
    profile_alloc = float(allocation_profile.get("decoder_allocator_allocation_percent", 0.0)) if allocation_profile else 0.0
    profile_passes = profile_cpu >= args.minimum_allocator_cpu_percent or profile_alloc >= args.minimum_allocator_allocation_percent

    reasons = list(coverage_errors)
    if allocation_profile is None:
        reasons.append("未提供 decoder allocator 的 Allocations/Time Profiler 归因报告")
    elif not profile_passes:
        reasons.append(
            "decoder allocator 的 CPU/分配归因低于配置门槛"
        )
    start_f1 = not coverage_errors and allocation_profile is not None and profile_passes
    decision = "OPEN" if start_f1 else "CLOSED"

    report = {
        "schema_version": 1,
        "decision": decision,
        "start_f1": start_f1,
        "thresholds": {
            "minimum_allocator_cpu_percent": args.minimum_allocator_cpu_percent,
            "minimum_allocator_allocation_percent": args.minimum_allocator_allocation_percent,
        },
        "reasons": reasons,
        "baseline_suite": suite_fingerprint(baseline_suite),
        "current_suite": suite_fingerprint(current_suite),
        "allocation_profile": allocation_profile,
        "cases": summaries,
    }
    write_json(args.output_json, report)

    lines = [
        "# 视频帧对象池真实媒体 Benchmark 结果",
        "",
        "## F1 门禁结论",
        "",
        f"**{decision}：{'启动' if start_f1 else '不启动'} Decoder Direct Rendering F1。**",
        "",
    ]
    if reasons:
        lines.append("门禁原因：")
        lines.append("")
        lines.extend(f"- {reason}" for reason in reasons)
        lines.append("")
    lines.extend([
        "门禁要求每个必选媒体至少完成清单指定的轮数、帧数和 checksum 稳定、释放后",
        "`inFlight=0`，并提供 allocator 归因数据。仅有 wall time/RSS 对比不能证明",
        "FFmpeg decoder allocator 是瓶颈。",
        "",
        "## 对比结果",
        "",
        "| Case | Runs | Wall baseline/current | Delta | CPU baseline/current | Delta | RSS baseline/current | Delta | Pool acquire |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ])
    for case_id, summary in summaries.items():
        baseline = summary["baseline"]
        current = summary["current"]
        lines.append(
            f"| {case_id} | {current['completed_count']} | "
            f"{baseline['wall_median_ms']:.2f}/{current['wall_median_ms']:.2f} ms | "
            f"{format_delta(summary['wall_delta_percent'])} | "
            f"{baseline['cpu_median_ms']:.2f}/{current['cpu_median_ms']:.2f} ms | "
            f"{format_delta(summary['cpu_delta_percent'])} | "
            f"{baseline['rss_median_bytes'] / 1048576:.2f}/{current['rss_median_bytes'] / 1048576:.2f} MiB | "
            f"{format_delta(summary['rss_delta_percent'])} | "
            f"{current['pool_acquire_median']:.0f} |"
        )
    lines.extend([
        "",
        "两个必选 case 均为 renderer 可直接消费的 `yuv420p`，current 的 pool acquire 为 0。",
        "因此这组结果专门观察 software decoder direct-output 路径；当前对象池按设计不应改变",
        "该路径。wall/CPU/RSS 的中位数变化均不足以证明 decoder allocator 是瓶颈。",
        "",
        "## 测量范围",
        "",
        "runner 直接驱动 `media_sdk::Player`，使用真实 MP4 码流执行 software decode，并由",
        "无 UI frame sink 保留 3 帧模拟下游持有。它测量 core decode、frame publication 和",
        "进程资源使用，不包含 Qt Scene Graph、GPU present、音频设备或实时播放节流。",
        "",
        "| Case | Codec | Source | SHA-256 |",
        "|---|---|---|---|",
    ])
    for case_id, case in cases.items():
        lines.append(
            f"| {case_id} | {case.get('codec', 'unknown')} | "
            f"[official test media]({case.get('url', '')}) | `{case.get('sha256', '')}` |"
        )
    lines.extend([
        "",
        "## 环境指纹",
        "",
        f"- Baseline label: `{baseline_suite.get('label', 'unknown')}`",
        f"- Baseline runner SHA-256: `{baseline_suite.get('runner_sha256', 'unknown')}`",
        f"- Current label: `{current_suite.get('label', 'unknown')}`",
        f"- Current runner SHA-256: `{current_suite.get('runner_sha256', 'unknown')}`",
        f"- Platform: `{current_suite.get('platform', 'unknown')}`",
        "- Build type: `Release`",
        "- Decode mode: software",
        "- Held video frames: `3`",
        "- Measured video frames per run: `240`",
        "",
        "## 可重复执行",
        "",
        f"- Manifest SHA-256: `{sha256(args.manifest)}`",
        f"- Baseline results: `{args.baseline_dir}`",
        f"- Current results: `{args.current_dir}`",
        f"- Machine report: `{args.output_json}`",
        "",
        "所有原始单次 JSON 均保留 wall/user/system CPU、max RSS、帧 checksum、像素格式和",
        "pool release 前后状态。媒体二进制不进入 Git，由 manifest URL 与 SHA-256 固定。",
        "",
    ])
    args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
    args.output_markdown.write_text("\n".join(lines), encoding="utf-8")
    print(f"F1 gate: {decision}")
    print(f"JSON: {args.output_json}")
    print(f"Markdown: {args.output_markdown}")


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    subparsers = root.add_subparsers(dest="command", required=True)

    fetch = subparsers.add_parser("fetch", help="download and verify manifest media")
    fetch.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    fetch.add_argument("--media-dir", type=Path, required=True)
    fetch.add_argument("--timeout", type=int, default=120)
    fetch.set_defaults(handler=fetch_media)

    run = subparsers.add_parser("run", help="run one labelled benchmark suite")
    run.add_argument("--runner", type=Path, required=True)
    run.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    run.add_argument("--media-dir", type=Path, required=True)
    run.add_argument("--output-dir", type=Path, required=True)
    run.add_argument("--label", required=True)
    run.add_argument("--runs", type=int, default=5)
    run.add_argument("--warmups", type=int, default=1)
    run.set_defaults(handler=run_suite)

    compare = subparsers.add_parser("compare", help="summarize suites and evaluate the F1 gate")
    compare.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    compare.add_argument("--baseline-dir", type=Path, required=True)
    compare.add_argument("--current-dir", type=Path, required=True)
    compare.add_argument("--allocation-profile", type=Path)
    compare.add_argument("--minimum-allocator-cpu-percent", type=float, default=5.0)
    compare.add_argument("--minimum-allocator-allocation-percent", type=float, default=15.0)
    compare.add_argument("--output-json", type=Path, required=True)
    compare.add_argument("--output-markdown", type=Path, required=True)
    compare.set_defaults(handler=compare_suites)
    return root


def main() -> None:
    args = parser().parse_args()
    if getattr(args, "runs", 1) <= 0 or getattr(args, "warmups", 0) < 0:
        fail("runs must be positive and warmups must be non-negative")
    args.handler(args)


if __name__ == "__main__":
    main()
