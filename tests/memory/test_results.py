import importlib.util
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("experiments", ROOT / "run_experiments.py")
experiments = importlib.util.module_from_spec(spec)
spec.loader.exec_module(experiments)


class MemoryResultsTest(unittest.TestCase):
    def test_ofrsupple_balanced_frontier_counts(self):
        self.assertEqual(experiments.ofrsupple_frontier(25, 5, 21),
                         ((0, 5), (5, 4), (9, 4), (13, 4), (17, 4)))
        # Independently checked by the matching C++ regression case.
        self.assertEqual(experiments.ofrsupple_control_counts(25, 5, 21), (100, 390))
        self.assertEqual(experiments.ofrsupple_feature_scratch_bytes(25, 5, 21), 12)
        self.assertEqual(experiments.ofrsupple_feature_scratch_bytes(15, 3, 5), 7)
        self.assertEqual(experiments.ofrsupple_feature_scratch_bytes(4096, 64, 64), 0)

    def test_renumbered_modes_and_heap_configuration(self):
        expected = {
            1: ("ShuffleBasedSWO", True, True, False, 1667072, 1),
            2: ("PSQF_SWO", False, False, False, 1679360, 1),
            3: ("CompactionBasedSWO", True, True, False, 1687552, 1),
            4: ("Supple", True, True, False, 2215936, 1),
            5: ("OFRSupple", True, True, False, 2146304, 1),
            10: ("PSQF_single", False, False, True, 1187840, 1),
            11: ("SubSample", False, False, True, 1187840, 1),
            12: ("SubSampleMultiSlice", True, True, False, 1998848, 1),
            13: ("SubSampleMulti_opt", True, True, False, 2293760, 1),
            14: ("Supple_parallel", True, True, False, 2625536, 4),
        }
        self.assertTrue(experiments.DEFAULT_MODE)
        self.assertEqual(len(experiments.DEFAULT_MODE), len(set(experiments.DEFAULT_MODE)))
        self.assertTrue(set(experiments.DEFAULT_MODE).issubset(experiments.MODE_INFO))
        self.assertEqual(set(experiments.MODE_INFO), set(expected))
        for mode, (name, needs_k, detailed, fixed_k, heap, tcs) in expected.items():
            self.assertEqual(experiments.MODE_INFO[mode], dict(
                name=name, needs_k=needs_k, detailed=detailed, fixed_k=fixed_k))
            with mock.patch.object(experiments, "write_heap_config") as config:
                self.assertEqual(experiments.estimate_heap(
                    mode, 4096, 16, .015625, k_value=128, threads=4), heap)
                config.assert_called_once_with(heap, tcs)
            expected_command = ["./application", str(mode), "64", "24", "0.25"]
            if needs_k:
                expected_command.append("2")
            if mode == 14:
                expected_command.append("4")
            expected_command += ["1", "0"]
            self.assertEqual(experiments.build_command(mode, 64, 24, .25, 2, 1, 4, 0), expected_command)
            self.assertEqual(experiments.k_candidates_for(mode, .25, [2, 8], 2),
                             [1] if fixed_k else [2, 8] if needs_k else [4])
        for mode in (0, 6, 7, 8, 9, 15):
            with self.assertRaises(SystemExit):
                experiments.normalize_modes([mode])

    def test_main_uses_measured_peak_and_rejects_missing_peak(self):
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(modes="1,2,3,4,5", n="64", p=".25", k="2",
                                   block_sizes="24", repeat=2, warmup=2, threads=1,
                                   k_select=2, results_folder=directory,
                                   overwrite=False, offline_profile=False)
            def process(command, **kwargs):
                if command[0] == "make":
                    return SimpleNamespace(returncode=0, stdout=b"", stderr=b"")
                mode = int(command[1])
                numbers = "1\n2\n5\n" if mode == 2 else "1\n2\n3\n4\n5\n"
                return SimpleNamespace(returncode=0, stdout=(numbers + "MEMORY,1048576\n").encode(), stderr=b"")
            with mock.patch.object(experiments, "parse_args", return_value=args), \
                 mock.patch.object(experiments, "estimate_heap", return_value=64 * 1048576), \
                 mock.patch.object(experiments.subprocess, "run", side_effect=process):
                self.assertEqual(experiments.main(), 0)
            for mode in (2, 4, 5, 1, 3):
                path = Path(directory) / (experiments.MODE_INFO[mode]["name"] + ".csv")
                self.assertEqual(path.read_text().splitlines()[-1].split(",")[-1], "1.0")
            args.results_folder = str(Path(directory) / "failed")
            failure = SimpleNamespace(returncode=0, stdout=b"1\n2\n3\n4\n5\n", stderr=b"")
            with mock.patch.object(experiments, "parse_args", return_value=args), \
                 mock.patch.object(experiments, "estimate_heap", return_value=64 * 1048576), \
                 mock.patch.object(experiments.subprocess, "run", return_value=failure):
                self.assertEqual(experiments.main(), 1)
            self.assertEqual(list(Path(args.results_folder).glob("*.csv")), [])

    def test_output_protocol(self):
        for mode in (2, 4, 5, 1, 3):
            timings = "1\n2\n3\n4\n5\n" if mode != 2 else "1\n2\n5\n"
            output = timings + "MEMORY,1048576\nPROFILE,1,2,3\nOFFLINE_PROFILE,0\n"
            parsed = experiments.parse_output(mode, output)
            self.assertIsNotNone(parsed)
            self.assertEqual(experiments.parse_memory_peak(output), 1048576)
            result = experiments.make_result(mode, 2, parsed, 1048576 / 1048576)
            self.assertEqual(experiments.format_csv_line(mode, 16, .5, 16, result).split(",")[-1], "1.0\n")
            self.assertTrue(experiments.result_csv_header(mode).endswith("algorithm_heap_peak_mib\n"))
        self.assertTrue(experiments.result_csv_header(14).endswith("heap_est_mb\n"))
        for output in ("", "MEMORY,-1", "MEMORY,1.5", "MEMORY,", "MEMORY,1,2",
                       "MEMORY,1\nMEMORY,2", "MEMORY,١"):
            self.assertIsNone(experiments.parse_memory_peak(output))

    def test_archive_estimates_without_relabeling(self):
        with tempfile.TemporaryDirectory() as directory:
            for mode in (2, 4, 5, 1, 3):
                path = Path(directory) / f"mode{mode}.csv"
                header = experiments.result_csv_header(mode)
                row = "16,.5,16,2," + ("1,2,3,4,5,6\n" if mode != 2 else "1,2,5,6\n")
                old_header = header.replace("algorithm_heap_peak_mib", "heap_est_mb")
                for index, original in enumerate((row, old_header + row)):
                    path.write_text(original)
                    experiments.prepare_result_csv(path, mode, False)
                    suffix = f".{index}" if index else ""
                    backup = path.with_name(f"mode{mode}.heap_est_backup{suffix}.csv")
                    self.assertEqual(backup.read_text(), original)
                    self.assertEqual(path.read_text(), header)
                path.write_text(header + row)
                experiments.prepare_result_csv(path, mode, False)
                self.assertEqual(path.read_text(), header + row + "\n")
                experiments.prepare_result_csv(path, mode, False)
                self.assertEqual(path.read_text(), header + row + "\n")
                experiments.prepare_result_csv(path, mode, True)
                self.assertEqual(path.read_text(), header)

    def test_unknown_header_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.csv"
            path.write_text("unknown,data\n1,2\n")
            with self.assertRaises(ValueError):
                experiments.prepare_result_csv(path, 5, False)
            self.assertEqual(path.read_text(), "unknown,data\n1,2\n")


if __name__ == "__main__":
    unittest.main()
