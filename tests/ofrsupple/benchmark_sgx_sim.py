#!/usr/bin/env python3
"""Run paired Supple (6) and OFRSupple (8) application benchmarks.

Build and sign a SIM enclave first; this script only runs the application.
Each application invocation includes one warm-up round and averages --repeat
measured rounds. Trial order alternates to reduce order bias.
"""

import argparse
import csv
import os
import subprocess
from pathlib import Path


CASES = (
    ("pure_compact", 4096, 64, 2, 16),
    ("underfilled", 4096, 16, 16, 16),
    ("equal", 4096, 64, 64, 16),
    ("frontier", 4096, 64, 128, 16),
    ("mixed", 4096, 48, 128, 16),
    ("equal_wide", 4096, 64, 64, 64),
    ("equal_large", 16384, 256, 64, 16),
    ("equal_256", 4096, 64, 64, 256),
    ("equal_1024", 4096, 64, 64, 1024),
    ("equal_1536", 4096, 64, 64, 1536),
    ("equal_2048", 4096, 64, 64, 2048),
    ("equal_4096", 4096, 64, 64, 4096),
    ("equal_8192", 4096, 64, 64, 8192),
    ("equal_65536_64", 65536, 1024, 64, 64),
    ("equal_1048576_64", 1048576, 16384, 64, 64),
)

BASELINE_CASES = {case[0] for case in CASES if case[0] not in
                  {"equal_4096", "equal_8192", "equal_65536_64",
                   "equal_1048576_64"}}

FIELDS = (
    "case", "n", "m", "k", "block_size", "trial", "mode", "ecall_ms",
    "algorithm_ms", "control_ms", "apply_ms", "oswap_count",
)
PROFILE_FIELDS = (
    "route_ms", "reorder_ms", "copy_ms", "shuffle_ms", "other_ms",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", type=Path, required=True,
                        help="built Application/application (with a SIM enclave beside it)")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeat", type=int, default=9)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--profile", action="store_true",
                        help="collect the same online phase breakdown for modes 6 and 8")
    parser.add_argument("--cases", default="baseline",
                        help="comma-separated case names, baseline, or all; wide cases need a larger enclave heap")
    args = parser.parse_args()
    if args.repeat < 1 or args.trials < 1:
        parser.error("--repeat and --trials must be positive")

    application = args.application.resolve()
    if not application.is_file():
        parser.error(f"application not found: {application}")
    if args.cases == "all":
        wanted = {case[0] for case in CASES}
    elif args.cases == "baseline":
        wanted = BASELINE_CASES
    else:
        wanted = set(args.cases.split(","))
    unknown = wanted - {case[0] for case in CASES}
    if unknown:
        parser.error(f"unknown cases: {', '.join(sorted(unknown))}")

    env = os.environ.copy()
    sdk = Path(env.get("SGX_SDK", "/opt/intel/sgxsdk"))
    env["LD_LIBRARY_PATH"] = ":".join(filter(None, (
        str(sdk / "lib64"), str(application.parent),
        env.get("LD_LIBRARY_PATH", ""),
    )))
    if args.profile:
        env["ONLINE_PROFILE"] = "1"
    rows = []
    for name, n, m, k, width in CASES:
        if name not in wanted:
            continue
        for trial in range(args.trials):
            for mode in ((6, 8) if trial % 2 == 0 else (8, 6)):
                command = (str(application), str(mode), str(n), str(width),
                           str(m / n), str(k), str(args.repeat))
                run = subprocess.run(command, cwd=application.parent, env=env,
                                     text=True, capture_output=True, timeout=180,
                                     check=True)
                lines = run.stdout.strip().splitlines()
                if len(lines) != (6 if args.profile else 5):
                    raise RuntimeError(f"unexpected output for {name}, mode {mode}: "
                                       f"{run.stdout!r}; stderr={run.stderr!r}")
                values = [float(value) for value in lines[:5]]
                if values[1] <= 0 or abs(values[1] - values[2] - values[3]) > 0.01:
                    raise RuntimeError(f"invalid timings for {name}, mode {mode}: "
                                       f"{values}")
                phases = []
                if args.profile:
                    tokens = lines[5].split(",")
                    if len(tokens) != 6 or tokens[0] != "PROFILE":
                        raise RuntimeError(f"invalid phase output: {lines[5]!r}")
                    phases = [float(token) for token in tokens[1:]]
                    if abs(sum(phases) - values[3]) > 0.05:
                        raise RuntimeError(f"phase sum differs from online time: {phases}")
                rows.append((name, n, m, k, width, trial, mode, *values, *phases))
                print(name, trial, mode, *(f"{value:.3f}" for value in values[:4]),
                      flush=True)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(FIELDS + PROFILE_FIELDS if args.profile else FIELDS)
        writer.writerows(rows)


if __name__ == "__main__":
    main()
