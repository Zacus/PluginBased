#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import platform
import statistics
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "tools" / "video_benchmark" / "media_manifest.json"
DEFAULT_CASES = (
    "h264_1080p24",
    "hevc_4k60_realtime",
    "prores_4k120_422p10_stress",
)


def fail(message: str) -> None:
    raise SystemExit(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def expected_frames(case: dict[str, Any]) -> int:
    value = case.get("max_video_frames", case.get("max_presented_frames"))
    if value is None:
        fail(f"Case {case['id']} does not define a frame target")
    return int(value)


def run_once(
    runner: Path,
    case: dict[str, Any],
    media: Path,
    output: Path,
    label: str,
    mode: str,
) -> None:
    command = [
        str(runner),
        "--input", str(media),
        "--output", str(output),
        "--label", label,
        "--allocator", mode,
        "--max-video-frames", str(expected_frames(case)),
        "--hold-video-frames", str(case.get("hold_video_frames", 3)),
        "--timeout-ms", str(case.get("timeout_ms", 120_000)),
    ]
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode != 0:
        fail(
            f"{case['id']} {mode} failed ({completed.returncode})\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    result = load_json(output)
    if not result.get("completed") or result.get("frames", {}).get("video") != expected_frames(case):
        fail(f"{case['id']} {mode} did not complete the required frame count")


def load_results(case_dir: Path, mode: str) -> list[dict[str, Any]]:
    return [load_json(path) for path in sorted((case_dir / mode).glob("run-*.json"))]


def summarize(results: list[dict[str, Any]]) -> dict[str, Any]:
    wall = [float(result["timing"]["wall_ms"]) for result in results]
    cpu = [
        float(result["timing"]["user_cpu_ms"]) + float(result["timing"]["system_cpu_ms"])
        for result in results
    ]
    rss = [float(result["timing"]["max_rss_bytes"]) for result in results]
    throughput = [
        float(result["frames"]["video"]) * 1000.0 / float(result["timing"]["wall_ms"])
        for result in results
        if float(result["timing"]["wall_ms"]) > 0.0
    ]
    return {
        "run_count": len(results),
        "completed_count": sum(bool(result.get("completed")) for result in results),
        "frame_counts": sorted({int(result["frames"]["video"]) for result in results}),
        "checksums": sorted({int(result["frames"]["checksum"]) for result in results}),
        "wall_median_ms": statistics.median(wall),
        "cpu_median_ms": statistics.median(cpu),
        "rss_median_bytes": statistics.median(rss),
        "throughput_fps_median": statistics.median(throughput),
        "callback_count_median": statistics.median(
            int(result["allocator"]["callback_count"]) for result in results
        ),
        "prototype_frame_count_median": statistics.median(
            int(result["allocator"]["prototype_frame_count"]) for result in results
        ),
        "fallback_count_max": max(
            (int(result["allocator"]["fallback_count"]) for result in results),
            default=0,
        ),
        "plane_acquire_count_median": statistics.median(
            int(result["allocator"]["plane_acquire_count"]) for result in results
        ),
        "plane_allocation_count_median": statistics.median(
            int(result["allocator"]["plane_allocation_count"]) for result in results
        ),
    }


def improvement(default: float, prototype: float) -> float:
    return (default - prototype) * 100.0 / default if default else 0.0


def rss_delta(default: float, prototype: float) -> float:
    return (prototype - default) * 100.0 / default if default else 0.0


def execute(args: argparse.Namespace) -> None:
    runner = args.runner.resolve()
    if not runner.is_file() or not os.access(runner, os.X_OK):
        fail(f"Runner is not executable: {runner}")
    manifest = load_json(args.manifest)
    available = {case["id"]: case for case in manifest["cases"]}
    case_ids = args.case or list(DEFAULT_CASES)
    missing = set(case_ids) - set(available)
    if missing:
        fail(f"Unknown cases: {', '.join(sorted(missing))}")
    if args.primary_case not in case_ids:
        fail("--primary-case must be one of the selected cases")

    selected = [available[case_id] for case_id in case_ids]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for case in selected:
        media = args.media_dir / case["filename"]
        if not media.is_file() or sha256(media) != case["sha256"]:
            fail(f"Missing or invalid media for {case['id']}: {media}")
        case_dir = args.output_dir / case["id"]
        for mode in ("default", "prototype"):
            (case_dir / mode).mkdir(parents=True, exist_ok=True)

        for warmup in range(1, args.warmups + 1):
            for mode in ("default", "prototype"):
                print(f"[{case['id']}] warmup {warmup}/{args.warmups} {mode}")
                run_once(
                    runner,
                    case,
                    media,
                    case_dir / mode / f"warmup-{warmup:02d}.json",
                    args.label,
                    mode,
                )
        for run_index in range(1, args.runs + 1):
            order = ("default", "prototype") if run_index % 2 else ("prototype", "default")
            for mode in order:
                print(f"[{case['id']}] run {run_index}/{args.runs} {mode}")
                run_once(
                    runner,
                    case,
                    media,
                    case_dir / mode / f"run-{run_index:02d}.json",
                    args.label,
                    mode,
                )

    summaries: dict[str, Any] = {}
    correctness_errors: list[str] = []
    guardrail_errors: list[str] = []
    for case in selected:
        case_dir = args.output_dir / case["id"]
        default = summarize(load_results(case_dir, "default"))
        prototype = summarize(load_results(case_dir, "prototype"))
        target = expected_frames(case)
        for mode, summary in (("default", default), ("prototype", prototype)):
            if summary["completed_count"] != args.runs or summary["frame_counts"] != [target]:
                correctness_errors.append(f"{case['id']} {mode} frame coverage failed")
            if len(summary["checksums"]) != 1:
                correctness_errors.append(f"{case['id']} {mode} checksum is unstable")
        if default["checksums"] != prototype["checksums"]:
            correctness_errors.append(f"{case['id']} default/prototype checksum differs")
        if prototype["prototype_frame_count_median"] <= 0 or prototype["fallback_count_max"] != 0:
            correctness_errors.append(f"{case['id']} prototype did not exclusively use custom buffers")

        wall_improvement = improvement(default["wall_median_ms"], prototype["wall_median_ms"])
        cpu_improvement = improvement(default["cpu_median_ms"], prototype["cpu_median_ms"])
        memory_delta = rss_delta(default["rss_median_bytes"], prototype["rss_median_bytes"])
        if wall_improvement < -args.maximum_regression_percent:
            guardrail_errors.append(f"{case['id']} wall regressed {-wall_improvement:.2f}%")
        if cpu_improvement < -args.maximum_regression_percent:
            guardrail_errors.append(f"{case['id']} CPU regressed {-cpu_improvement:.2f}%")
        if memory_delta > args.maximum_rss_increase_percent:
            guardrail_errors.append(f"{case['id']} RSS increased {memory_delta:.2f}%")
        summaries[case["id"]] = {
            "case": case,
            "default": default,
            "prototype": prototype,
            "wall_improvement_percent": wall_improvement,
            "cpu_improvement_percent": cpu_improvement,
            "rss_delta_percent": memory_delta,
        }

    primary = summaries[args.primary_case]
    primary_gain = max(
        primary["wall_improvement_percent"],
        primary["cpu_improvement_percent"],
    )
    gain_passes = primary_gain >= args.minimum_improvement_percent
    decision = "GO" if not correctness_errors and not guardrail_errors and gain_passes else "NO-GO"
    reasons = correctness_errors + guardrail_errors
    if not gain_passes:
        reasons.append(
            f"{args.primary_case} best improvement {primary_gain:.2f}% is below "
            f"{args.minimum_improvement_percent:.2f}%"
        )

    report = {
        "schema_version": 1,
        "decision": decision,
        "start_f2": decision == "GO",
        "label": args.label,
        "platform": platform.platform(),
        "runner": runner.name,
        "runner_sha256": sha256(runner),
        "manifest_sha256": sha256(args.manifest),
        "runs": args.runs,
        "warmups": args.warmups,
        "primary_case": args.primary_case,
        "thresholds": {
            "minimum_improvement_percent": args.minimum_improvement_percent,
            "maximum_regression_percent": args.maximum_regression_percent,
            "maximum_rss_increase_percent": args.maximum_rss_increase_percent,
        },
        "reasons": reasons,
        "cases": summaries,
    }
    write_json(args.output_json, report)

    lines = [
        "# get_buffer2 F1 实验结果",
        "",
        "## 决策",
        "",
        f"**{decision}：{'启动' if decision == 'GO' else '不启动'} F2 正式实现。**",
        "",
    ]
    if reasons:
        lines.extend(["原因：", ""])
        lines.extend(f"- {reason}" for reason in reasons)
        lines.append("")
    lines.extend([
        "## 对比",
        "",
        "| Case | Runs | Throughput default/prototype | Wall improvement | CPU improvement | RSS delta | Plane allocation/acquire |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ])
    for case_id, summary in summaries.items():
        default = summary["default"]
        prototype = summary["prototype"]
        lines.append(
            f"| {case_id} | {args.runs} | "
            f"{default['throughput_fps_median']:.2f}/{prototype['throughput_fps_median']:.2f} fps | "
            f"{summary['wall_improvement_percent']:+.2f}% | "
            f"{summary['cpu_improvement_percent']:+.2f}% | "
            f"{summary['rss_delta_percent']:+.2f}% | "
            f"{prototype['plane_allocation_count_median']:.0f}/"
            f"{prototype['plane_acquire_count_median']:.0f} |"
        )
    lines.extend([
        "",
        "## 解释",
        "",
        "FFmpeg default `get_buffer2` 已使用内部 `AVBufferPool`。prototype 的 plane",
        "allocation/acquire 比例证明自管池能够复用，但不能证明它比 default 私有池减少了",
        "更多分配；default 没有公开对应计数。只有 wall/CPU/RSS 的成对结果可用于两者性能",
        "决策。",
        "",
        "default 和 prototype 使用同一进程模型、demux/decode 代码和 callback 层级。正式运行",
        "按奇偶轮交替执行顺序；媒体来源、SHA-256 和目标帧数由 manifest 固定。prototype",
        "底层 allocation 只统计成功创建的 plane buffer，FFmpeg default 私有池没有对应公开",
        "计数，因此不对两者的 allocation 次数作伪对比。",
        "",
        "4K120 4:2:2 10-bit case 是确定性 synthetic ProRes throughput stress，不包含实时",
        "PTS 节流、音频时钟、Qt Scene Graph 或 GPU texture upload；它不替代 HEVC 4K60",
        "产品主 case。",
        "",
        "## 环境",
        "",
        f"- Label: `{args.label}`",
        f"- Platform: `{report['platform']}`",
        f"- Runner SHA-256: `{report['runner_sha256']}`",
        f"- Manifest SHA-256: `{report['manifest_sha256']}`",
        f"- Raw results: `{args.output_dir}`",
        f"- Machine report: `{args.output_json}`",
    ])
    args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
    args.output_markdown.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"F1 decision: {decision}")
    print(f"JSON: {args.output_json}")
    print(f"Markdown: {args.output_markdown}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="Run the paired get_buffer2 F1 benchmark")
    result.add_argument("--runner", type=Path, required=True)
    result.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    result.add_argument("--media-dir", type=Path, required=True)
    result.add_argument("--output-dir", type=Path, required=True)
    result.add_argument("--output-json", type=Path, required=True)
    result.add_argument("--output-markdown", type=Path, required=True)
    result.add_argument("--label", required=True)
    result.add_argument("--case", action="append")
    result.add_argument("--primary-case", default="hevc_4k60_realtime")
    result.add_argument("--runs", type=int, default=5)
    result.add_argument("--warmups", type=int, default=1)
    result.add_argument("--minimum-improvement-percent", type=float, default=3.0)
    result.add_argument("--maximum-regression-percent", type=float, default=3.0)
    result.add_argument("--maximum-rss-increase-percent", type=float, default=5.0)
    return result


def main() -> None:
    args = parser().parse_args()
    if args.runs < 1 or args.warmups < 0:
        fail("--runs must be positive and --warmups cannot be negative")
    execute(args)


if __name__ == "__main__":
    main()
