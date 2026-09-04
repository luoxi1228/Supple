#!/usr/bin/env python3
import argparse
import csv
import os
import subprocess
import sys
from pathlib import Path
from typing import List


CSV_FIELDS = [
    "block_size",
    "pairs",
    "repeats",
    "oswap_0_ns",
    "ofork_01_ns",
    "identity_ratio",
    "oswap_1_ns",
    "ofork_10_ns",
    "swap_ratio",
    "ofork_00_ns",
    "ofork_11_ns",
]


def parse_sizes(text: str) -> List[int]:
    values = []
    for item in text.split(","):
        item = item.strip()
        if item:
            values.append(int(item))
    if not values:
        raise argparse.ArgumentTypeError("at least one block size is required")
    return values


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare OSwap and OFork inside the Supple SGX enclave"
    )
    parser.add_argument(
        "--binary",
        default="Application/primitive_benchmark",
        help="benchmark executable relative to the project root",
    )
    parser.add_argument(
        "--block-sizes",
        type=parse_sizes,
        default=parse_sizes("4,8,12,16,24,32,64,128,256,512,1024,4096"),
        help="comma-separated supported block sizes",
    )
    parser.add_argument(
        "--target-bytes",
        type=int,
        default=64 * 1024 * 1024,
        help="approximately bytes processed by each timed round",
    )
    parser.add_argument(
        "--pairs",
        type=int,
        default=0,
        help="fixed number of pairs; 0 derives it from --target-bytes",
    )
    parser.add_argument("--repeat", type=int, default=11)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument(
        "--output",
        default="RESULTS/oswap_ofork.csv",
        help="output CSV path relative to the project root",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="run make before executing the benchmark",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_root = Path(__file__).resolve().parent
    binary = project_root / args.binary
    output = project_root / args.output

    if args.repeat <= 0 or args.warmup < 0:
        print("--repeat must be positive and --warmup must be non-negative")
        return 2

    if args.build:
        jobs = max(1, os.cpu_count() or 1)
        result = subprocess.run(
            ["make", f"-j{jobs}"],
            cwd=str(project_root),
        )
        if result.returncode != 0:
            return result.returncode

    if not binary.exists():
        print(f"Missing benchmark binary: {binary}")
        print("Build it with: make -j$(nproc)")
        return 2

    rows = []
    for block_size in args.block_sizes:
        pairs = args.pairs
        if pairs == 0:
            pairs = max(1, args.target_bytes // (2 * block_size))

        command = [
            str(binary),
            str(pairs),
            str(block_size),
            str(args.repeat),
            str(args.warmup),
        ]

        print(
            f"block_size={block_size:5d}, pairs={pairs:9d}, "
            f"repeats={args.repeat}"
        )

        completed = subprocess.run(
            command,
            cwd=str(project_root),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        if completed.returncode != 0:
            print(completed.stdout)
            print(completed.stderr, file=sys.stderr)
            return completed.returncode

        result_line = None
        for line in completed.stdout.splitlines():
            if line.startswith("RESULT,"):
                result_line = line
                break

        if result_line is None:
            print("Benchmark output did not contain a RESULT line.")
            print(completed.stdout)
            return 2

        fields = result_line.split(",")[1:]
        if len(fields) != len(CSV_FIELDS):
            print(f"Malformed RESULT line: {result_line}")
            return 2

        row = dict(zip(CSV_FIELDS, fields))
        rows.append(row)

        print(
            "  identity: "
            f"OSwap={float(row['oswap_0_ns']):.3f} ns, "
            f"OFork={float(row['ofork_01_ns']):.3f} ns, "
            f"ratio={float(row['identity_ratio']):.4f}"
        )
        print(
            "  swap:     "
            f"OSwap={float(row['oswap_1_ns']):.3f} ns, "
            f"OFork={float(row['ofork_10_ns']):.3f} ns, "
            f"ratio={float(row['swap_ratio']):.4f}"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Saved results to: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
