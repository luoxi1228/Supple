"""New comparison reports must accept immutable legacy and FFOS result labels."""
import csv
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("compare_optimization_stages.py")
spec = importlib.util.spec_from_file_location("comparison", SCRIPT)
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class LegacyResultsTest(unittest.TestCase):
    def test_old_and_new_labels_compare_without_rewriting_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            originals = {}
            for stage, labels in (("stage_00_baseline", ("Supple", "OFRSupple")),
                                  ("stage_01_ffos", ("FFOS_C", "FFOS_FR")),
                                  ("stage_02_opt", ("FFOS_C", "FFOS_FR", "FFOS_FR_Opt"))):
                (root / stage).mkdir()
                for backend in ("native", "sim"):
                    path = root / stage / (backend + ".csv")
                    fields = ("kind", "case", "n", "m", "k", "width", "method", "round",
                              "control_ms", "apply_ms", "offline_ms", "online_ms", "total_ms",
                              "ecall_ms", "control_bytes", "control_apply_heap_peak_bytes",
                              "algorithm_heap_peak_bytes")
                    with path.open("w") as target:
                        writer = csv.DictWriter(target, fieldnames=fields)
                        writer.writeheader()
                        for index, method in enumerate(labels):
                            for round_id in range(2):
                                row = dict.fromkeys(fields, 1)
                                row.update(kind="round", case="equal", n=64, m=16, k=4,
                                           width=16, method=method, round=round_id,
                                           apply_ms=4 if index == 0 else 2,
                                           online_ms=4 if index == 0 else 2,
                                           total_ms=5 if index == 0 else 3)
                                writer.writerow(row)
                    originals[path] = path.read_bytes()
            for backend in ("native", "sim"):
                self.assertEqual(comparison.read(root / "stage_00_baseline" / (backend + ".csv"), backend),
                                 comparison.read(root / "stage_01_ffos" / (backend + ".csv"), backend))
            subprocess.run([sys.executable, str(SCRIPT), str(root)], check=True,
                           stdout=subprocess.PIPE)
            with (root / "comparison.csv").open() as source:
                rows = list(csv.DictReader(source))
            self.assertEqual(len(rows), 8)
            for row in rows:
                self.assertEqual(float(row["online_speedup_over_ffos_c"]), 2)
                if row["method"] == "FFOS_FR_Opt":
                    self.assertEqual(row["ffos_fr_online_over_byte_baseline"], "")
                else:
                    self.assertEqual(float(row["ffos_fr_online_over_byte_baseline"]), 1)
                self.assertIn("ffos_c_offline_ms", row)
                self.assertIn("ffos_fr_offline_ms", row)
                self.assertFalse(any("supple" in field for field in row))
            for path, content in originals.items():
                self.assertEqual(path.read_bytes(), content)


if __name__ == "__main__":
    unittest.main()
