#!/usr/bin/env python3
import argparse
import csv
import subprocess
import sys
from pathlib import Path
from typing import Callable, Dict, List, TypeVar


T = TypeVar("T")

# Edit these defaults for no-argument runs; command-line options override them.
DEFAULT_BINARY = "Application/fmscompact_vs_ocompact"
DEFAULT_N = [1048576]#[1048576, 2097152, 4194304, 8388608, 16777216]
DEFAULT_BLOCK_SIZES = [4096]#[4,8,12,16,24,32,64,128,256,512,1024]
DEFAULT_FORK_RATIOS = [0.25]
DEFAULT_REPEAT = 15
DEFAULT_WARMUP = 5
DEFAULT_SEED = 20260903
DEFAULT_OUTPUT = "RESULTS/fmscompact_vs_ocompact.csv"

# FMS timing fields exclude output reordering, which still runs afterward.
BINARY_FIELDS = [
    "n",
    "block_size",
    "fork_ratio",
    "repeats",
    "control_words",
    "fms_schedule",
    "fms_control_ms",
    "fmscompact_ms",
    "ocompact_ms",
    "speedup",
    "correct",
]

CSV_FIELDS = [
    "n",
    "block_size",
    "requested_fork_ratio",
    "fork_ratio",
    "repeats",
    "warmups",
    "seed",
    "control_words",
    "fms_schedule",
    "fms_control_ms",
    "fmscompact_ms",
    "ocompact_ms",
    "speedup",
    "correct",
]


def parse_csv_values(raw: str, cast: Callable[[str], T], name: str) -> List[T]:
    values: List[T] = []
    for part in raw.split(","):
        token = part.strip()
        if not token:
            continue
        try:
            values.append(cast(token))
        except ValueError as error:
            raise argparse.ArgumentTypeError(
                f"invalid value '{token}' in --{name}"
            ) from error
    if not values:
        raise argparse.ArgumentTypeError(
            f"--{name} must contain at least one value"
        )
    return values


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare FMSCompact apply (excluding output "
            "reordering) with one OCompact operation inside the Supple "
            "SGX enclave"
        )
    )
    parser.add_argument(
        "--binary",
        default=DEFAULT_BINARY,
        help="benchmark executable relative to the project root",
    )
    parser.add_argument(
        "--n",
        default=",".join(map(str, DEFAULT_N)),
        help="comma-separated input sizes (each must be at least 2)",
    )
    parser.add_argument(
        "--block-sizes",
        default=",".join(map(str, DEFAULT_BLOCK_SIZES)),
        help="comma-separated supported record sizes in bytes",
    )
    parser.add_argument(
        "--fork-ratios",
        default=",".join(map(str, DEFAULT_FORK_RATIOS)),
        help=(
            "comma-separated #11/n ratios in [0,0.5]; each generated case "
            "also has #00=#11 and balanced left/right responsibilities"
        ),
    )
    parser.add_argument(
        "--repeat", type=int, default=DEFAULT_REPEAT,
        help="number of measured rounds averaged after warm-up",
    )
    parser.add_argument(
        "--warmup", type=int, default=DEFAULT_WARMUP,
        help="number of excluded warm-up rounds",
    )
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument(
        "--output",
        default=DEFAULT_OUTPUT,
        help="CSV output path relative to the project root",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="run make before executing the benchmark",
    )
    return parser.parse_args()


def supported_block_size(block_size: int) -> bool:
    return (
        block_size <= 0xFFFFFFFF
        and (
            block_size in (4, 8, 12)
            or (block_size >= 16 and block_size % 16 == 0)
            or (block_size >= 24 and block_size % 16 == 8)
        )
    )


def parse_result(stdout: str) -> Dict[str, str]:
    result_line = next(
        (line for line in stdout.splitlines() if line.startswith("RESULT,")),
        None,
    )
    if result_line is None:
        raise ValueError("benchmark output did not contain a RESULT line")

    values = result_line.split(",")[1:]
    if len(values) != len(BINARY_FIELDS):
        raise ValueError(f"malformed RESULT line: {result_line}")
    return dict(zip(BINARY_FIELDS, values))


def main() -> int:
    args = parse_args()
    try:
        n_values = parse_csv_values(args.n, int, "n")
        block_sizes = parse_csv_values(
            args.block_sizes, int, "block-sizes"
        )
        fork_ratios = parse_csv_values(
            args.fork_ratios, float, "fork-ratios"
        )
    except argparse.ArgumentTypeError as error:
        print(error, file=sys.stderr)
        return 2

    if args.repeat <= 0 or args.warmup < 0 or args.seed < 0:
        print(
            "--repeat must be positive; --warmup and --seed must be non-negative",
            file=sys.stderr,
        )
        return 2
    if any(value < 2 for value in n_values):
        print("every --n value must be >= 2", file=sys.stderr)
        return 2
    if any(not supported_block_size(value) for value in block_sizes):
        print(
            "supported block sizes are 4, 8, 12, 16*n, and 8+16*n (n>=1)",
            file=sys.stderr,
        )
        return 2
    if any(value < 0.0 or value > 0.5 for value in fork_ratios):
        print("every --fork-ratios value must be in [0,0.5]", file=sys.stderr)
        return 2

    project_root = Path(__file__).resolve().parent
    binary = (project_root / args.binary).resolve()
    output = (project_root / args.output).resolve()

    if args.build:
        # The root Makefile's configuration target removes generated objects.
        # Run it serially so cleanup cannot race with compilation or linking.
        completed = subprocess.run(
            ["make", "-j1"],
            cwd=str(project_root),
        )
        if completed.returncode != 0:
            return completed.returncode

    if not binary.is_file():
        print(f"missing benchmark binary: {binary}", file=sys.stderr)
        print("build it with: make -j$(nproc)", file=sys.stderr)
        return 2

    rows: List[Dict[str, object]] = []
    case_index = 0
    for block_size in block_sizes:
        for n in n_values:
            for requested_ratio in fork_ratios:
                case_seed = args.seed + case_index
                case_index += 1
                command = [
                    str(binary),
                    str(n),
                    str(block_size),
                    str(requested_ratio),
                    str(args.repeat),
                    str(args.warmup),
                    str(case_seed),
                ]

                print(
                    f"n={n:9d}, block_size={block_size:5d}, "
                    f"fork_ratio={requested_ratio:.6f}, repeats={args.repeat}"
                )
                completed = subprocess.run(
                    command,
                    cwd=str(binary.parent),
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                if completed.returncode != 0:
                    if completed.stdout:
                        print(completed.stdout, file=sys.stderr)
                    if completed.stderr:
                        print(completed.stderr, file=sys.stderr)
                    return completed.returncode

                try:
                    result = parse_result(completed.stdout)
                except ValueError as error:
                    print(error, file=sys.stderr)
                    print(completed.stdout, file=sys.stderr)
                    return 2

                if result["correct"] != "1":
                    print("benchmark reported a correctness failure", file=sys.stderr)
                    return 3

                row: Dict[str, object] = dict(result)
                row["requested_fork_ratio"] = requested_ratio
                row["warmups"] = args.warmup
                row["seed"] = case_seed
                rows.append(row)

                print(
                    "  offline generation: "
                    f"FMSControl="
                    f"{float(result['fms_control_ms']):.3f} ms"
                )
                print(
                    "  online mean: "
                    f"FMS {result['fms_schedule']} "
                    f"(no output reorder)={float(result['fmscompact_ms']):.3f} ms, "
                    f"OCompact={float(result['ocompact_ms']):.3f} ms, "
                    f"speedup={float(result['speedup']):.3f}x"
                )

    output.parent.mkdir(parents=True, exist_ok=True)
    has_results = output.is_file() and output.stat().st_size > 0
    separator = ""
    if has_results:
        with output.open("rb") as existing:
            header_line = existing.readline().decode("utf-8").rstrip("\r\n")
            header = next(csv.reader([header_line]))
            if header != CSV_FIELDS:
                print(
                    f"CSV header does not match the current benchmark: {output}",
                    file=sys.stderr,
                )
                return 2
            existing.seek(0, 2)
            existing.seek(max(0, existing.tell() - 4))
            tail = existing.read()
        if not (tail.endswith(b"\r\n\r\n") or tail.endswith(b"\n\n")):
            separator = "\r\n" if tail.endswith(b"\n") else "\r\n\r\n"

    with output.open("a", newline="") as stream:
        stream.write(separator)
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
        if not has_results:
            writer.writeheader()
        writer.writerows(rows)
        stream.write("\r\n")

    print(f"saved results to: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
