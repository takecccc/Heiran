"""Measure Release startup through the first frame, including DLL loading."""

import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import threading
import time


def measure(executable, inputs, sequential, sequential_inputs=False, sequential_decode=False):
    environment = dict(os.environ, QT_ASSUME_STDERR_HAS_CONSOLE="1")
    environment.pop("HEIRAN_BENCHMARK_SEQUENTIAL_FONTS", None)
    environment.pop("HEIRAN_BENCHMARK_SEQUENTIAL_INPUTS", None)
    environment.pop("HEIRAN_BENCHMARK_SEQUENTIAL_DECODE", None)
    if sequential:
        environment["HEIRAN_BENCHMARK_SEQUENTIAL_FONTS"] = "1"
    if sequential_inputs:
        environment["HEIRAN_BENCHMARK_SEQUENTIAL_INPUTS"] = "1"
    if sequential_decode:
        environment["HEIRAN_BENCHMARK_SEQUENTIAL_DECODE"] = "1"
    started = time.perf_counter()
    process = subprocess.Popen(
        [str(executable), *inputs],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=environment,
    )
    lines = []
    sample = {}

    def collect():
        for line in process.stderr:
            received = time.perf_counter()
            lines.append(line)
            match = re.search(r"startup first-frame:.*total=([\d.]+)", line)
            if match:
                sample["process_ms"] = (received - started) * 1000
                sample["main_ms"] = float(match[1])
            match = re.search(r"startup images-frame:.*total=([\d.]+)", line)
            if match:
                sample["images_ms"] = (received - started) * 1000
                sample["main_images_ms"] = float(match[1])

    reader = threading.Thread(target=collect, daemon=True)
    reader.start()
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise RuntimeError("Startup did not complete within 15 seconds.") from None
    finally:
        reader.join()
        process.stderr.close()
    if process.returncode != 0 or "images_ms" not in sample:
        raise RuntimeError("Startup measurement failed:\n" + "".join(lines))
    return sample


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="*", help="Image files or one .hrn file")
    parser.add_argument("--exe", type=Path, default=Path("build/heiran_startup_benchmark.exe"))
    parser.add_argument("--runs", type=int, default=5)
    comparison = parser.add_mutually_exclusive_group()
    comparison.add_argument("--compare", action="store_true", help="Also measure sequential fonts")
    comparison.add_argument("--compare-inputs", action="store_true", help="Also measure sequential input preparation")
    comparison.add_argument("--compare-decode", action="store_true", help="Also measure decode starting after OpenGL initialization")
    parser.add_argument("--output", type=Path, help="Save individual samples as JSON")
    arguments = parser.parse_args()
    if arguments.runs < 1:
        parser.error("--runs must be positive")
    executable = arguments.exe.resolve(strict=True)
    samples = {"parallel": []}
    if arguments.compare:
        samples["sequential"] = []
    if arguments.compare_inputs:
        samples["sequential-inputs"] = []
    if arguments.compare_decode:
        samples["sequential-decode"] = []
    for iteration in range(arguments.runs + 1):
        modes = list(samples)
        if iteration % 2:
            modes.reverse()
        for mode in modes:
            sample = measure(executable, arguments.inputs, mode == "sequential",
                             mode == "sequential-inputs", mode == "sequential-decode")
            if iteration:
                samples[mode].append(sample)
    for mode, values in samples.items():
        for field in ("process_ms", "main_ms", "images_ms", "main_images_ms"):
            durations = [sample[field] for sample in values]
            print(f"{mode} {field}: min={min(durations):.2f} "
                  f"median={statistics.median(durations):.2f} max={max(durations):.2f}")
    if arguments.output:
        arguments.output.write_text(json.dumps(samples, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
