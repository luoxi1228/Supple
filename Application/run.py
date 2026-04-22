#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List

# Base template. Edit these defaults as needed.
BASE_CONFIG: Dict[str, Any] = {
    "modes": [31,33,32,34,35],
    "p": [0.0078125],
    "n": [1048576],
    "k": [896],
    "k_select": 1,
    "block_sizes": [4, 8, 16, 24, 48, 72, 128, 168, 256, 344, 512, 680, 768, 1032, 1280, 1544, 2048, 2568, 3072, 3592, 3840, 4096],
    "repeat": 1,
    "results_folder": "../RESULTS",
    "overwrite": False,
}

# Add as many groups as you want.
# Each group overrides BASE_CONFIG fields.
EXPERIMENT_GROUPS: List[Dict[str, Any]] = [
    {
        "name": "group_p",
        "modes": [31,33,32,34,35],
        "p": [0.25, 0.0625, 0.015625, 0.00390625, 0.0009765625], # 1/4 to 1/1024
        "n": [1048576],
        "k": [1],
        "k_select": 1,
        "block_sizes": [16],
        "repeat": 2,
        "results_folder": "../RESULTS",
        "overwrite": False,
    },{
        "name": "group_k",
        "modes": [31,33,32,34,35],
        "p": [0.015625],
        "n": [1048576],
        "k": [4,16,64,256,1024],   # 4 to 1024
        "k_select": 2,
        "block_sizes": [16],
        "repeat": 2,
        "results_folder": "../RESULTS",
        "overwrite": False,
    },{
        "name": "group_b",
        "modes": [31,33,32,34,35],
        "p": [0.015625],
        "n": [1048576],
        "k": [1],
        "k_select": 1,
        "block_sizes": [16,64,256,1024,4096], # 16 to 4096
        "repeat": 2,
        "results_folder": "../RESULTS",
        "overwrite": False,
    },{
        "name": "group_n",
        "modes": [31,33,32,34,35],
        "p": [0.015625],
        "n": [1048576, 4194304, 16777216, 67108864, 268435456], # 1M to 256M
        "k": [1],
        "k_select": 1,
        "block_sizes": [16],
        "repeat": 2,
        "results_folder": "../RESULTS",
        "overwrite": False,
    },

]


def list_to_csv(values: List[Any]) -> str:
    return ",".join(str(v) for v in values)


def merged_config(group: Dict[str, Any]) -> Dict[str, Any]:
    cfg = dict(BASE_CONFIG)
    cfg.update(group)
    return cfg


def build_cmd(run_experiments_py: Path, cfg: Dict[str, Any], force_overwrite: bool) -> List[str]:
    cmd = [
        sys.executable,
        str(run_experiments_py),
        "--modes",
        list_to_csv(cfg["modes"]),
        "--n",
        list_to_csv(cfg["n"]),
        "--p",
        list_to_csv(cfg["p"]),
        "--k",
        list_to_csv(cfg["k"]),
        "--k-select",
        str(cfg["k_select"]),
        "--block-sizes",
        list_to_csv(cfg["block_sizes"]),
        "--repeat",
        str(cfg["repeat"]),
        "--results-folder",
        str(cfg["results_folder"]),
    ]

    if force_overwrite or bool(cfg.get("overwrite", False)):
        cmd.append("--overwrite")

    return cmd


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run multiple run_experiments.py configuration groups in one shot")
    parser.add_argument("--dry-run", action="store_true", help="Print commands only, do not execute")
    parser.add_argument("--stop-on-error", action="store_true", help="Stop immediately if any group fails")
    parser.add_argument(
        "--overwrite-first",
        action="store_true",
        help="Force --overwrite only for the first group (useful for fresh result files)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    app_dir = Path(__file__).resolve().parent
    run_experiments_py = app_dir / "run_experiments.py"
    if not run_experiments_py.exists():
        print(f"Missing script: {run_experiments_py}")
        return 2

    if not EXPERIMENT_GROUPS:
        print("No experiment groups configured in EXPERIMENT_GROUPS")
        return 2

    print(f"Total groups: {len(EXPERIMENT_GROUPS)}")

    for idx, group in enumerate(EXPERIMENT_GROUPS, start=1):
        cfg = merged_config(group)
        name = str(cfg.get("name", f"group_{idx:02d}"))
        force_overwrite = bool(args.overwrite_first and idx == 1)
        cmd = build_cmd(run_experiments_py, cfg, force_overwrite)

        print("=" * 80)
        print(f"[{idx}/{len(EXPERIMENT_GROUPS)}] {name}")
        print("CMD:", " ".join(cmd))

        if args.dry_run:
            continue

        result = subprocess.run(cmd, cwd=str(app_dir))
        if result.returncode != 0:
            print(f"Group '{name}' failed with exit code {result.returncode}")
            if args.stop_on_error:
                return result.returncode

    print("=" * 80)
    print("All groups processed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
