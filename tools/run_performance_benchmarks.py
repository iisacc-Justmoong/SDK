#!/usr/bin/env python3
"""Collect paired native SDK microbenchmarks; reject changed results before timing claims."""
import argparse
import datetime
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess


PROGRAMS = ("runtime", "filter", "authorship", "catalog", "hierarchy", "table")


def parse_output(output):
    rows = {}
    for line in output.splitlines():
        name, elapsed, checksum = line.split()
        elapsed = float(elapsed)
        if name in rows or not math.isfinite(elapsed) or elapsed < 0:
            raise ValueError("Invalid or duplicated benchmark timing")
        rows[name] = {"milliseconds": elapsed, "checksum": int(checksum)}
    if not rows: raise ValueError("Benchmark produced no observations")
    return rows


def summarize(samples):
    cases = {}
    for side, runs in samples.items():
        keys = set(runs[0])
        if any(set(run) != keys for run in runs): raise ValueError("Benchmark case coverage changed")
        for name in keys:
            checksums = {run[name]["checksum"] for run in runs}
            if len(checksums) != 1: raise ValueError("Nondeterministic benchmark result: " + name)
            case = cases.setdefault(name, {})
            case[side] = {"median_ms": statistics.median(run[name]["milliseconds"] for run in runs),
                          "checksum": checksums.pop(), "samples_ms": [run[name]["milliseconds"] for run in runs]}
    for name, case in cases.items():
        if "before" in samples:
            if set(case) != {"before", "after"}: raise ValueError("Before/after case coverage differs: " + name)
            if case["before"]["checksum"] != case["after"]["checksum"]:
                raise ValueError("Before/after result differs: " + name)
            case["speedup"] = case["before"]["median_ms"] / case["after"]["median_ms"] if case["after"]["median_ms"] else None
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--require-baseline", action="store_true")
    args = parser.parse_args()
    if args.samples < 3: parser.error("Use at least three samples")
    env = dict(os.environ)
    for name in list(env):
        if name.startswith("DYLD_") or name in {"CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "LIBRARY_PATH"}:
            env.pop(name)
    report = {"date_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "platform": platform.platform(), "machine": platform.machine(),
              "samples_per_side": args.samples, "programs": {},
              "scope": "Native microbenchmarks with warmup and alternating paired order; not whole-application measurements"}
    suffix = ".exe" if os.name == "nt" else ""
    for program in PROGRAMS:
        executable = args.build_dir / ("sdk_" + program + "_benchmark" + suffix)
        if not executable.exists(): continue
        before = executable.with_name("sdk_" + program + "_benchmark_before" + suffix)
        if args.require_baseline and not before.exists(): raise FileNotFoundError(before)
        paths = {"after": executable}
        if before.exists(): paths["before"] = before
        def run(path):
            result = subprocess.run([str(path.resolve())], env=env, capture_output=True, text=True, timeout=120, check=True)
            return parse_output(result.stdout)
        for path in paths.values(): run(path)  # warm the executable and its loaded libraries
        samples = {side: [] for side in paths}
        for iteration in range(args.samples):
            order = list(paths) if iteration % 2 == 0 else list(reversed(paths))
            for side in order: samples[side].append(run(paths[side]))
        report["programs"][program] = {"executables": {side: str(path.resolve()) for side,path in paths.items()},
                                          "cases": summarize(samples)}
        print(program + ": result parity verified", flush=True)
    if not report["programs"]: raise ValueError("No benchmark programs found")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__": main()
