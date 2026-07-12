#!/usr/bin/env python3

import argparse
import hashlib
import json
import statistics
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Optional


DIRECT_ALLOCATOR_SYMBOLS = (
    "av_malloc",
    "av_free",
    "av_buffer_alloc",
    "av_buffer_create",
    "av_buffer_unref",
    "buffer_replace",
    "av_frame_get_buffer",
    "thread_get_buffer_internal",
    "video_get_buffer",
)
MEMORY_PRIMITIVES = ("memset", "bzero")


def fail(message: str) -> None:
    raise SystemExit(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def display_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(Path.cwd().resolve()))
    except ValueError:
        return resolved.name


def resolve(element: Optional[ET.Element], elements_by_id: dict[str, ET.Element]) -> Optional[ET.Element]:
    visited: set[str] = set()
    while element is not None and "ref" in element.attrib:
        reference = element.attrib["ref"]
        if reference in visited:
            return None
        visited.add(reference)
        element = elements_by_id.get(reference)
    return element


def parse_time_profile(path: Path) -> dict[str, Any]:
    root = ET.parse(path).getroot()
    elements_by_id = {
        element.attrib["id"]: element
        for element in root.iter()
        if "id" in element.attrib
    }
    total_weight = 0
    direct_weight = 0
    conservative_weight = 0
    direct_symbols: dict[str, int] = {}

    for row in root.iter("row"):
        weight = resolve(row.find("weight"), elements_by_id)
        if weight is None or not weight.text:
            continue
        sample_weight = int(weight.text)
        total_weight += sample_weight

        backtrace = resolve(row.find("backtrace"), elements_by_id)
        if backtrace is None:
            continue
        direct_matches: set[str] = set()
        conservative_match = False
        for frame_reference in backtrace.findall("frame"):
            frame = resolve(frame_reference, elements_by_id)
            if frame is None:
                continue
            symbol = frame.attrib.get("name", "")
            symbol_lower = symbol.lower()
            binary = resolve(frame.find("binary"), elements_by_id)
            binary_name = binary.attrib.get("name", "") if binary is not None else ""
            if any(pattern in symbol_lower for pattern in DIRECT_ALLOCATOR_SYMBOLS):
                direct_matches.add(symbol)
            if (
                direct_matches
                or binary_name == "libsystem_malloc.dylib"
                or any(pattern in symbol_lower for pattern in MEMORY_PRIMITIVES)
            ):
                conservative_match = True

        if direct_matches:
            direct_weight += sample_weight
            for symbol in direct_matches:
                direct_symbols[symbol] = direct_symbols.get(symbol, 0) + sample_weight
        if conservative_match:
            conservative_weight += sample_weight

    if total_weight == 0:
        fail(f"Time Profiler XML has no weighted samples: {path}")

    return {
        "xml": display_path(path),
        "xml_sha256": sha256(path),
        "total_sample_weight_ns": total_weight,
        "direct_allocator_sample_weight_ns": direct_weight,
        "direct_allocator_cpu_percent": direct_weight * 100.0 / total_weight,
        "conservative_upper_bound_sample_weight_ns": conservative_weight,
        "conservative_upper_bound_cpu_percent": conservative_weight * 100.0 / total_weight,
        "direct_symbols": {
            symbol: weight / 1_000_000.0
            for symbol, weight in sorted(direct_symbols.items())
        },
    }


def build_profile(
    xml_paths: list[Path],
    trace_paths: list[Path],
    runner: Optional[Path],
    media: Optional[Path],
    benchmark_results: Optional[list[Path]] = None,
) -> dict[str, Any]:
    runs = [parse_time_profile(path) for path in xml_paths]
    direct = [run["direct_allocator_cpu_percent"] for run in runs]
    upper = [run["conservative_upper_bound_cpu_percent"] for run in runs]
    return {
        "schema_version": 1,
        "tool": "Instruments Time Profiler",
        "decoder_allocator_cpu_percent": max(upper),
        "decoder_allocator_cpu_percent_semantics": "conservative_upper_bound_max",
        "decoder_allocator_allocation_percent": None,
        "decoder_allocator_allocation_percent_semantics": "unavailable",
        "time_profile": {
            "run_count": len(runs),
            "direct_allocator_cpu_percent_median": statistics.median(direct),
            "conservative_upper_bound_cpu_percent_max": max(upper),
            "conservative_upper_bound_includes": [
                "all direct FFmpeg allocator/buffer symbols",
                "all libsystem_malloc.dylib samples",
                "all memset and bzero samples",
            ],
            "runs": runs,
        },
        "evidence": {
            "time_profile_traces": [display_path(path) for path in trace_paths],
            "benchmark_results": [display_path(path) for path in benchmark_results or []],
            "benchmark_result_sha256": {
                display_path(path): sha256(path) for path in benchmark_results or []
            },
            "runner": runner.name if runner else None,
            "runner_sha256": sha256(runner) if runner else None,
            "media": display_path(media) if media else None,
            "media_sha256": sha256(media) if media else None,
        },
    }


def validate_benchmark_result(path: Path, expected_frames: int) -> None:
    benchmark = json.loads(path.read_text(encoding="utf-8"))
    presented_frames = benchmark.get("presenter", {}).get("presented_frames")
    if not benchmark.get("completed") or presented_frames != expected_frames:
        fail(f"Profiling benchmark did not complete {expected_frames} frames: {path}")


def analyze(args: argparse.Namespace) -> None:
    for path in args.time_profile_xml:
        if not path.is_file():
            fail(f"Missing Time Profiler XML: {path}")
    for path in args.benchmark_result or []:
        validate_benchmark_result(path, args.max_presented_frames)
    profile = build_profile(
        args.time_profile_xml,
        args.trace or [],
        args.runner,
        args.input,
        args.benchmark_result or [],
    )
    write_json(args.output, profile)
    print(f"Allocator CPU conservative upper bound: {profile['decoder_allocator_cpu_percent']:.3f}%")
    print(f"Profile: {args.output}")


def record(args: argparse.Namespace) -> None:
    runner = args.runner.resolve()
    media = args.input.resolve()
    if not runner.is_file():
        fail(f"Missing benchmark runner: {runner}")
    if not media.is_file():
        fail(f"Missing benchmark media: {media}")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    xml_paths: list[Path] = []
    trace_paths: list[Path] = []
    benchmark_results: list[Path] = []
    for index in range(1, args.runs + 1):
        stem = f"time-profile-run-{index:02d}"
        trace = (args.output_dir / f"{stem}.trace").resolve()
        xml = (args.output_dir / f"{stem}.xml").resolve()
        runner_output = (args.output_dir / f"{stem}-benchmark.json").resolve()
        for output in (trace, xml, runner_output):
            if output.exists():
                fail(f"Refusing to overwrite profiling artifact: {output}")

        record_command = [
            "xcrun", "xctrace", "record",
            "--template", "Time Profiler",
            "--time-limit", f"{args.trace_timeout_ms}ms",
            "--output", str(trace),
            "--no-prompt",
            "--launch", "--", str(runner),
            "--input", str(media),
            "--output", str(runner_output),
            "--label", args.label,
            "--max-presented-frames", str(args.max_presented_frames),
            "--timeout-ms", str(args.benchmark_timeout_ms),
        ]
        print(f"Time Profiler run {index}/{args.runs}")
        subprocess.run(record_command, check=True)
        validate_benchmark_result(runner_output, args.max_presented_frames)
        subprocess.run([
            "xcrun", "xctrace", "export",
            "--input", str(trace),
            "--xpath", '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]',
            "--output", str(xml),
        ], check=True)
        trace_paths.append(trace)
        xml_paths.append(xml)
        benchmark_results.append(runner_output)

    profile = build_profile(xml_paths, trace_paths, runner, media, benchmark_results)
    write_json(args.output, profile)
    print(f"Allocator CPU conservative upper bound: {profile['decoder_allocator_cpu_percent']:.3f}%")
    print(f"Profile: {args.output}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="Record and analyze decoder allocator CPU attribution")
    subparsers = result.add_subparsers(dest="command", required=True)

    analyze_parser = subparsers.add_parser("analyze-time", help="analyze exported Time Profiler XML")
    analyze_parser.add_argument("--time-profile-xml", type=Path, action="append", required=True)
    analyze_parser.add_argument("--trace", type=Path, action="append")
    analyze_parser.add_argument("--benchmark-result", type=Path, action="append")
    analyze_parser.add_argument("--max-presented-frames", type=int, default=600)
    analyze_parser.add_argument("--runner", type=Path)
    analyze_parser.add_argument("--input", type=Path)
    analyze_parser.add_argument("--output", type=Path, required=True)
    analyze_parser.set_defaults(handler=analyze)

    record_parser = subparsers.add_parser("record-time", help="record and analyze Time Profiler runs")
    record_parser.add_argument("--runner", type=Path, required=True)
    record_parser.add_argument("--input", type=Path, required=True)
    record_parser.add_argument("--output-dir", type=Path, required=True)
    record_parser.add_argument("--output", type=Path, required=True)
    record_parser.add_argument("--label", required=True)
    record_parser.add_argument("--runs", type=int, default=3)
    record_parser.add_argument("--max-presented-frames", type=int, default=600)
    record_parser.add_argument("--benchmark-timeout-ms", type=int, default=30_000)
    record_parser.add_argument("--trace-timeout-ms", type=int, default=60_000)
    record_parser.set_defaults(handler=record)
    return result


def main() -> None:
    args = parser().parse_args()
    if getattr(args, "runs", 1) < 1:
        fail("--runs must be positive")
    args.handler(args)


if __name__ == "__main__":
    main()
