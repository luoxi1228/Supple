#!/usr/bin/env python3
import argparse
import csv
import subprocess
import sys
from pathlib import Path
from typing import List


# Edit these defaults for no-argument runs; command-line options override them.
DEFAULT_BINARY = "Application/ofork_vs_oswap"
DEFAULT_BLOCK_SIZES = [4, 8, 12, 16, 24, 32, 64, 128, 256, 512, 1024, 4096]
DEFAULT_PAIRS = [1048576]  # 0 derives pairs from DEFAULT_TARGET_BYTES.
DEFAULT_TARGET_BYTES = 64 * 1024 * 1024
DEFAULT_REPEAT = 15
DEFAULT_WARMUP = 5
DEFAULT_OUTPUT = "RESULTS/ofork_vs_oswap.csv"

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
            try:
                values.append(int(item))
            except ValueError as error:
                raise argparse.ArgumentTypeError(
                    f"invalid integer value: '{item}'"
                ) from error
    if not values:
        raise argparse.ArgumentTypeError("at least one value is required")
    return values


def supported_block_size(block_size: int) -> bool:
    return (
        block_size in (4, 8, 12)
        or (block_size >= 16 and block_size % 16 == 0)
        or (block_size >= 24 and block_size % 16 == 8)
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare OSwap and OFork inside the Supple SGX enclave"
    )
    parser.add_argument(
        "--binary",
        default=DEFAULT_BINARY,
        help="benchmark executable relative to the project root",
    )
    parser.add_argument(
        "--block-sizes",
        type=parse_sizes,
        default=",".join(map(str, DEFAULT_BLOCK_SIZES)),
        help="comma-separated supported block sizes",
    )
    parser.add_argument(
        "--target-bytes",
        type=int,
        default=DEFAULT_TARGET_BYTES,
        help="approximately bytes processed by each timed round",
    )
    parser.add_argument(
        "--pairs",
        type=parse_sizes,
        default=",".join(map(str, DEFAULT_PAIRS)),
        help="comma-separated pair counts; 0 derives it from --target-bytes",
    )
    parser.add_argument(
        "--repeat", type=int, default=DEFAULT_REPEAT,
        help="number of measured rounds averaged after warm-up",
    )
    parser.add_argument(
        "--warmup", type=int, default=DEFAULT_WARMUP,
        help="number of excluded warm-up rounds",
    )
    parser.add_argument(
        "--output",
        default=DEFAULT_OUTPUT,
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

    if args.repeat <= 0 or args.warmup < 0 or args.target_bytes <= 0:
        print("--repeat and --target-bytes must be positive; --warmup must be non-negative", file=sys.stderr)
        return 2
    if any(pairs < 0 for pairs in args.pairs):
        print("every --pairs value must be non-negative", file=sys.stderr)
        return 2
    if any(not supported_block_size(size) for size in args.block_sizes):
        print("supported block sizes are 4, 8, 12, 16*n, and 8+16*n (n>=1)", file=sys.stderr)
        return 2

    if args.build:
        # The root Makefile's configuration target removes generated objects.
        # Run it serially so cleanup cannot race with compilation or linking.
        result = subprocess.run(
            ["make", "-j1"],
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
        for requested_pairs in args.pairs:
            pairs = requested_pairs
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
