#!/usr/bin/env python3
"""Compare medians of raw paired rounds; keep native and ECALL results distinct."""
import argparse
import csv
from pathlib import Path
import statistics


def read(path, backend):
    with path.open() as source:
        rows = list(csv.DictReader(source))
    groups = {}
    for row in rows:
        if backend == "native" and row.get("kind") != "round":
            continue
        key = tuple(row[field] for field in ("case", "n", "m", "k", "width", "method"))
        groups.setdefault(key, []).append(row)
    metrics = (["control_ms", "apply_ms", "total_ms", "control_bytes",
                "control_apply_heap_peak_bytes"]
               if backend == "native" else
               ["offline_ms", "online_ms", "total_ms", "ecall_ms",
                "algorithm_heap_peak_bytes"])
    online = "apply_ms" if backend == "native" else "online_ms"
    result = {}
    for key, rounds in groups.items():
        values = {name: statistics.median(float(row[name]) for row in rounds)
                  for name in metrics}
        values["rounds"] = len(rounds)
        values["online_by_round"] = {int(row["round"]): float(row[online])
                                     for row in rounds}
        if len(values["online_by_round"]) != len(rounds):
            raise ValueError(f"Duplicate round IDs in {path}: {key}")
        result[key] = values
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    output = args.root / "comparison.csv"
    fields = ["backend", "stage", "suite", "case", "n", "m", "k", "width", "rounds",
              "supple_offline_ms", "ofr_offline_ms", "supple_online_ms", "ofr_online_ms",
              "supple_total_ms", "ofr_total_ms", "ofr_total_over_supple",
              "ofr_online_over_byte_baseline", "control_bytes", "supple_ecall_ms", "ofr_ecall_ms",
              "heap_rounds", "supple_heap_peak_bytes", "ofr_heap_peak_bytes",
              "online_baseline_stage", "online_speedup_over_supple",
              "online_speedup_min_paired", "online_speedup_median_paired",
              "online_speedup_max_paired", "online_2x_all_rounds"]
    result = []
    for backend in ("native", "sim", "hw"):
        for suite, suffix in (("matrix", ""), ("shapes", "_shapes")):
            baseline_stage = "baseline_recheck"
            baseline_path = args.root / baseline_stage / (backend + suffix + ".csv")
            if not baseline_path.exists():
                baseline_stage = "stage_00_baseline"
                baseline_path = args.root / baseline_stage / (backend + suffix + ".csv")
            baseline = read(baseline_path, backend) if baseline_path.exists() else {}
            for stage in sorted(args.root.glob("stage_*")):
                path = stage / (backend + suffix + ".csv")
                if not path.exists():
                    continue
                data = read(path, backend)
                heap_path = stage / (backend + suffix + "_heap.csv")
                heaps = read(heap_path, backend) if heap_path.exists() else {}
                heap_metric = ("control_apply_heap_peak_bytes" if backend == "native"
                               else "algorithm_heap_peak_bytes")
                offline, online = (("control_ms", "apply_ms") if backend == "native"
                                   else ("offline_ms", "online_ms"))
                for key, ofr in data.items():
                    if key[-1] != "OFRSupple":
                        continue
                    supple_key = key[:-1] + ("Supple",)
                    supple = data[supple_key]
                    if supple["online_by_round"].keys() != ofr["online_by_round"].keys():
                        raise ValueError(f"Unpaired rounds in {path}: {key}")
                    speedups = [supple["online_by_round"][index] / elapsed
                                for index, elapsed in ofr["online_by_round"].items()]
                    old = baseline.get(key)
                    ofr_heap = heaps.get(key, {})
                    supple_heap = heaps.get(supple_key, {})
                    row = dict(zip(fields[:8], (backend, stage.name, suite, *key[:-1])))
                    row.update(rounds=ofr["rounds"],
                           supple_offline_ms=supple[offline], ofr_offline_ms=ofr[offline],
                           supple_online_ms=supple[online], ofr_online_ms=ofr[online],
                           supple_total_ms=supple["total_ms"], ofr_total_ms=ofr["total_ms"],
                           ofr_total_over_supple=ofr["total_ms"] / supple["total_ms"],
                           ofr_online_over_byte_baseline=ofr[online] / old[online] if old else "",
                           control_bytes=ofr.get("control_bytes", ""),
                           supple_ecall_ms=supple.get("ecall_ms", ""),
                           ofr_ecall_ms=ofr.get("ecall_ms", ""),
                           heap_rounds=ofr_heap.get("rounds", ""),
                           supple_heap_peak_bytes=supple_heap.get(heap_metric, ""),
                           ofr_heap_peak_bytes=ofr_heap.get(heap_metric, ""),
                           online_baseline_stage=baseline_stage if old else "",
                           online_speedup_over_supple=supple[online] / ofr[online],
                           online_speedup_min_paired=min(speedups),
                           online_speedup_median_paired=statistics.median(speedups),
                           online_speedup_max_paired=max(speedups),
                           online_2x_all_rounds=int(min(speedups) >= 2))
                    result.append(row)
    with output.open("w") as target:
        writer = csv.DictWriter(target, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows({key: f"{value:.6f}" if isinstance(value, float) else value
                         for key, value in row.items()} for row in result)
    print(output)


if __name__ == "__main__":
    main()
