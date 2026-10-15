import contextlib
import csv
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
spec = importlib.util.spec_from_file_location("experiments", ROOT / "run_experiments.py")
experiments = importlib.util.module_from_spec(spec)
spec.loader.exec_module(experiments)


class MemoryResultsTest(unittest.TestCase):
    def test_ffos_result_and_profile_filenames(self):
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(modes="4,5,6,7", n="64", p=".25", k="2",
                                   block_sizes="24", repeat=1, warmup=0,
                                   k_select=2, results_folder=directory,
                                   overwrite=False, offline_profile=True)
            output = ("1\n2\n3\n4\n5\nMEMORY,1048576\nOFFLINE_MEMORY,524288\nONLINE_MEMORY,786432\nPARALLEL,0,8,1\nOFFLINE_PROFILE," +
                      ",".join(["0"] * 12) + "\n")
            def process(command, **kwargs):
                result = output
                if command[0] != "make" and command[1] == "6":
                    result = result.replace("OFFLINE_PROFILE," + ",".join(["0"] * 12),
                                            "PAR_OFFLINE_PROFILE," + ",".join(["0"] * 11))
                return SimpleNamespace(returncode=0, stdout=result.encode(), stderr=b"")
            with mock.patch.object(experiments, "parse_args", return_value=args), \
                 mock.patch.object(experiments, "estimate_heap", return_value=1048576), \
                 mock.patch.object(experiments.subprocess, "run", side_effect=process):
                self.assertEqual(experiments.main(), 0)
            self.assertEqual({path.name for path in Path(directory).iterdir()}, {
                "FFOS_C.csv", "FFOS_FR.csv", "FFOS_FR_Par.csv",
                "FFOS_C_offline_profile.csv", "FFOS_FR_offline_profile.csv",
                "FFOS_FR_Par_offline_profile.csv", "FFOS_C_Opt.csv",
                "FFOS_C_Opt_offline_profile.csv"})
            for name in ("FFOS_C", "FFOS_FR", "FFOS_FR_Par", "FFOS_C_Opt"):
                rows = (Path(directory) / (name + "_offline_profile.csv")).read_text().splitlines()
                self.assertEqual(len(rows), 2)
                self.assertIn("total_heap_peak_bytes", rows[0])
                self.assertEqual(len(rows[0].split(",")), len(rows[1].split(",")))

    def test_ffos_fr_balanced_frontier_counts(self):
        self.assertEqual(experiments.ffos_fr_frontier(25, 5, 21),
                         ((0, 5), (5, 4), (9, 4), (13, 4), (17, 4)))
        # Independently checked by the matching C++ regression case.
        self.assertEqual(experiments.ffos_fr_control_counts(25, 5, 21), (100, 390))
        self.assertEqual(experiments.ffos_fr_feature_scratch_bytes(25, 5, 21), 12)
        self.assertEqual(experiments.ffos_fr_feature_scratch_bytes(15, 3, 5), 7)
        self.assertEqual(experiments.ffos_fr_feature_scratch_bytes(4096, 64, 64), 0)

    def test_renumbered_modes_and_heap_configuration(self):
        expected = {
            1: ("ShuffleBasedSWO", True, True, False, 1667072, 1),
            2: ("PSQF_SWO", False, False, False, 1679360, 1),
            3: ("CompactionBasedSWO", True, True, False, 1687552, 1),
            4: ("FFOS_C", True, True, False, 2215936, 1),
            5: ("FFOS_FR", True, True, False, 2146304, 1),
            6: ("FFOS_FR_Par", True, True, False, 2240512, 1),
            7: ("FFOS_C_Opt", True, True, False, 0, 1),
            10: ("PSQF_single", False, False, True, 1187840, 1),
            11: ("SubSample", False, False, True, 1187840, 1),
            12: ("SubSampleMultiSlice", True, True, False, 1998848, 1),
            13: ("SubSampleMulti_opt", True, True, False, 2293760, 1),
        }
        self.assertTrue(experiments.DEFAULT_MODE)
        self.assertEqual(len(experiments.DEFAULT_MODE), len(set(experiments.DEFAULT_MODE)))
        self.assertTrue(set(experiments.DEFAULT_MODE).issubset(experiments.MODE_INFO))
        self.assertEqual(set(experiments.MODE_INFO), set(expected))
        for mode, (name, needs_k, detailed, fixed_k, heap, tcs) in expected.items():
            self.assertEqual(experiments.MODE_INFO[mode], dict(
                name=name, needs_k=needs_k, detailed=detailed, fixed_k=fixed_k))
            with mock.patch.object(experiments, "available_cpus", return_value=1), mock.patch.object(experiments, "write_heap_config") as config:
                actual = experiments.estimate_heap(mode, 4096, 16, .015625, k_value=128)
                if mode == 7:
                    self.assertGreater(actual, 4096 * 16 + 64 * 128 * 16)
                    self.assertEqual(actual % 4096, 0)
                else:
                    self.assertEqual(actual, heap)
                config.assert_called_once_with(actual, tcs)
            expected_command = ["./application", str(mode), "64", "24", "0.25"]
            if needs_k:
                expected_command.append("2")
            expected_command += ["1", "0"]
            self.assertEqual(experiments.build_command(mode, 64, 24, .25, 2, 1, 0), expected_command)
            self.assertEqual(experiments.k_candidates_for(mode, .25, [2, 8], 2),
                             [1] if fixed_k else [2, 8] if needs_k or mode == 2 else [4])
        for mode in (0, 8, 9, 14, 15):
            with self.assertRaises(SystemExit):
                experiments.normalize_modes([mode])

    def test_ffos_c_opt_heap_covers_fragmented_output_allocation(self):
        # At N=2^20, M=2^14, B=16, the layout retains 16 MiB of input,
        # a 256/512 MiB freed membership hole below live controls, 48/96 MiB
        # of tape, and needs a contiguous 512/1024 MiB plaintext output.
        # These layout floors exclude allocator overhead and other scratch.
        # The old live-peak estimate (829/1585 MiB) falls below both floors.
        for k, minimum_mib in ((2048, 832), (4096, 1648)):
            with self.subTest(k=k), \
                 mock.patch.object(experiments, "available_cpus", return_value=1), \
                 mock.patch.object(experiments, "write_heap_config") as config:
                heap = experiments.estimate_heap(7, 1048576, 16, .015625, k_value=k)
                self.assertGreater(heap, minimum_mib * 1048576)
                self.assertEqual(heap % 4096, 0)
                config.assert_called_once_with(heap, 1)

    def run_psqf(self, p, k, *, n=1048576, k_select=2, failure=None):
        commands = []
        builds = []

        def process(command, **kwargs):
            if command[0] == "make":
                builds.append(command)
                return SimpleNamespace(returncode=0, stdout=b"", stderr=b"")
            commands.append(command)
            index = len(commands)
            if failure is not None and index == 2:
                return failure
            # Vary timings and place the largest peak first: totals must sum
            # every call, while memory must use the maximum, not sum or last.
            peak = (32 if index == 1 else 1) * 1048576
            output = f"{index}.25\n{index}.125\n{index * 2}\nMEMORY,{peak}\n"
            return SimpleNamespace(returncode=0, stdout=output.encode(), stderr=b"")

        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(modes="2", n=str(n), p=str(p), k=str(k),
                                   block_sizes="16", repeat=3, warmup=1,
                                   k_select=k_select, results_folder=directory,
                                   overwrite=False, offline_profile=False)
            log = io.StringIO()
            with mock.patch.object(experiments, "parse_args", return_value=args), \
                 mock.patch.object(experiments, "estimate_heap", return_value=1048576), \
                 mock.patch.object(experiments.subprocess, "run", side_effect=process), \
                 contextlib.redirect_stdout(log):
                status = experiments.main()
            path = Path(directory) / "PSQF_SWO.csv"
            if path.exists():
                with path.open() as source:
                    rows = list(csv.DictReader(source))
            else:
                rows = []
        for command in commands:
            self.assertEqual(command, ["./application", "2", str(n), "16", str(p), "3", "1"])
        return status, rows, commands, builds, log.getvalue()

    def test_psqf_group_p_batches_and_metrics(self):
        for p, calls, produced in ((.25, 16, 64), (.0625, 4, 64),
                                   (.015625, 1, 64), (.00390625, 1, 256),
                                   (.0009765625, 1, 1024)):
            with self.subTest(p=p):
                status, rows, commands, builds, log = self.run_psqf(p, 64)
                self.assertEqual(status, 0)
                self.assertEqual(len(commands), calls)
                self.assertEqual(len(builds), 1)
                self.assertEqual(len(rows), 1)
                row = rows[0]
                self.assertEqual(int(row["k"]), 64)
                total = calls * (calls + 1) // 2
                self.assertEqual(float(row["ecall_time_ms"]), total + .25 * calls)
                self.assertEqual(float(row["ptime_ms"]), total + .125 * calls)
                self.assertEqual(int(row["oswaps"]), total * 2)
                self.assertEqual(float(row["algorithm_heap_peak_mib"]), 32.0)
                self.assertIn(f"calls_per_round = {calls}", log)
                self.assertIn(f"actual_samples_per_round = {produced}", log)

    def test_psqf_group_k_sweeps_each_target(self):
        status, rows, commands, builds, log = self.run_psqf(.015625, "16,64,256,1024,4096")
        self.assertEqual(status, 0)
        self.assertEqual(len(commands), 86)
        self.assertEqual(len(builds), 5)
        self.assertEqual([int(row["k"]) for row in rows], [16, 64, 256, 1024, 4096])
        first = 1
        for row, calls, produced in zip(rows, (1, 1, 4, 16, 64), (64, 64, 256, 1024, 4096)):
            total = sum(range(first, first + calls))
            self.assertEqual(float(row["ecall_time_ms"]), total + .25 * calls)
            self.assertEqual(float(row["ptime_ms"]), total + .125 * calls)
            self.assertEqual(int(row["oswaps"]), total * 2)
            self.assertIn(f"target_k = {row['k']}, calls_per_round = {calls}, "
                          f"actual_samples_per_round = {produced}", log)
            first += calls

    def test_psqf_rounds_up_without_truncating_or_changing_derived_selection(self):
        status, rows, commands, _, log = self.run_psqf(.25, 5)
        self.assertEqual(status, 0)
        self.assertEqual(len(commands), 2)
        self.assertEqual(rows[0]["k"], "5")
        self.assertIn("actual_samples_per_round = 8", log)
        status, rows, commands, _, _ = self.run_psqf(.015625, 4096, k_select=1)
        self.assertEqual(status, 0)
        self.assertEqual(len(commands), 1)
        self.assertEqual(rows[0]["k"], "64")

    def test_psqf_counts_actual_samples_with_rounding_and_fallback(self):
        for p, k, calls, produced in ((.25, 9, 2, 10), (.3, 3, 3, 3)):
            with self.subTest(p=p):
                status, rows, commands, _, log = self.run_psqf(p, k, n=10)
                self.assertEqual(status, 0)
                self.assertEqual(len(commands), calls)
                self.assertEqual(rows[0]["k"], str(k))
                self.assertIn(f"actual_samples_per_round = {produced}", log)

    def test_psqf_rejects_partial_batches_and_continues_next_target(self):
        failures = (
            SimpleNamespace(returncode=1, stdout=b"", stderr=b"failed"),
            SimpleNamespace(returncode=0, stdout=b"bad output\n", stderr=b""),
            SimpleNamespace(returncode=0, stdout=b"1\n2\n3\n", stderr=b""),
            SimpleNamespace(returncode=0, stdout=b"1\n2\n3\nMEMORY,0\n", stderr=b""),
        )
        for failure in failures:
            with self.subTest(failure=failure):
                status, rows, commands, _, _ = self.run_psqf(.25, 16, failure=failure)
                self.assertEqual(status, 1)
                self.assertEqual(rows, [])
                self.assertEqual(len(commands), 2)
        status, rows, commands, _, _ = self.run_psqf(.25, "8,4", failure=failures[0])
        self.assertEqual(status, 1)
        self.assertEqual([row["k"] for row in rows], ["4"])
        self.assertEqual(len(commands), 3)

    def test_psqf_rejects_nonpositive_targets_before_execution(self):
        for k in (0, -1):
            with self.subTest(k=k):
                status, rows, commands, builds, _ = self.run_psqf(.25, k)
                self.assertEqual(status, 1)
                self.assertEqual(rows, [])
                self.assertEqual(commands, [])
                self.assertEqual(builds, [])

    def test_main_uses_measured_peak_and_rejects_missing_peak(self):
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(modes="1,2,3,4,5,6,7", n="64", p=".25", k="2",
                                   block_sizes="24", repeat=2, warmup=2,
                                   k_select=2, results_folder=directory,
                                   overwrite=False, offline_profile=False)
            def process(command, **kwargs):
                if command[0] == "make":
                    return SimpleNamespace(returncode=0, stdout=b"", stderr=b"")
                mode = int(command[1])
                numbers = "1\n2\n5\n" if mode == 2 else "1\n2\n3\n4\n5\n"
                phases = "OFFLINE_MEMORY,524288\nONLINE_MEMORY,786432\n" if mode in experiments.CONTROL_MEMORY_MODES else ""
                return SimpleNamespace(returncode=0, stdout=(numbers + "MEMORY,1048576\n" + phases + "PARALLEL,0,8,1\n").encode(), stderr=b"")
            with mock.patch.object(experiments, "parse_args", return_value=args), \
                 mock.patch.object(experiments, "estimate_heap", return_value=64 * 1048576), \
                 mock.patch.object(experiments.subprocess, "run", side_effect=process):
                self.assertEqual(experiments.main(), 0)
            for mode in (2, 4, 5, 6, 7, 1, 3):
                path = Path(directory) / (experiments.MODE_INFO[mode]["name"] + ".csv")
                self.assertEqual(list(csv.DictReader(io.StringIO(path.read_text())))[-1]["algorithm_heap_peak_mib"], "1.0")
                row = list(csv.DictReader(io.StringIO(path.read_text())))[-1]
                if mode in experiments.CONTROL_MEMORY_MODES:
                    self.assertEqual(row["offline_heap_peak_mib"], "0.5")
                    self.assertEqual(row["online_heap_peak_mib"], "0.75")
                else:
                    self.assertNotIn("offline_heap_peak_mib", row)
                    self.assertNotIn("online_heap_peak_mib", row)
            args.results_folder = str(Path(directory) / "failed")
            failure = SimpleNamespace(returncode=0, stdout=b"1\n2\n3\n4\n5\n", stderr=b"")
            with mock.patch.object(experiments, "parse_args", return_value=args), \
                 mock.patch.object(experiments, "estimate_heap", return_value=64 * 1048576), \
                 mock.patch.object(experiments.subprocess, "run", return_value=failure):
                self.assertEqual(experiments.main(), 1)
            self.assertEqual(list(Path(args.results_folder).glob("*.csv")), [])

    def test_output_protocol(self):
        for mode in (2, 4, 5, 6, 7, 1, 3):
            timings = "1\n2\n3\n4\n5\n" if mode != 2 else "1\n2\n5\n"
            output = timings + "MEMORY,1048576\nOFFLINE_MEMORY,524288\nONLINE_MEMORY,786432\nPROFILE,1,2,3\nOFFLINE_PROFILE,0\n"
            parsed = experiments.parse_output(mode, output)
            self.assertIsNotNone(parsed)
            self.assertEqual(experiments.parse_memory_peak(output), 1048576)
            self.assertEqual(experiments.parse_offline_memory_peak(output), 524288)
            self.assertEqual(experiments.parse_online_memory_peak(output), 786432)
            result = experiments.make_result(mode, 2, parsed, 1.0, .5, .75)
            if mode == 6:
                result += (0, 8, 1)
            header = experiments.result_csv_header(mode).strip().split(",")
            values = experiments.format_csv_line(mode, 16, .5, 16, result).strip().split(",")
            self.assertEqual(len(values), len(header))
            row = dict(zip(header, values))
            self.assertEqual(row["algorithm_heap_peak_mib"], "1.0")
            if mode in experiments.CONTROL_MEMORY_MODES:
                self.assertEqual(row["offline_heap_peak_mib"], "0.5")
                self.assertEqual(row["online_heap_peak_mib"], "0.75")
            self.assertIn("algorithm_heap_peak_mib", experiments.result_csv_header(mode))
        self.assertTrue(experiments.result_csv_header(13).endswith("heap_est_mb\n"))
        for output in ("", "MEMORY,-1", "MEMORY,1.5", "MEMORY,", "MEMORY,1,2",
                       "MEMORY,1\nMEMORY,2", "MEMORY,١"):
            self.assertIsNone(experiments.parse_memory_peak(output))
            for prefix, parser in (("OFFLINE_MEMORY", experiments.parse_offline_memory_peak),
                                   ("ONLINE_MEMORY", experiments.parse_online_memory_peak)):
                self.assertIsNone(parser(output.replace("MEMORY", prefix)))

    def test_reject_missing_invalid_or_inconsistent_phase_peaks(self):
        for mode in (4, 5, 6, 7):
            for phases in ("", "OFFLINE_MEMORY,1\n", "ONLINE_MEMORY,1\n",
                           "OFFLINE_MEMORY,0\nONLINE_MEMORY,1\n",
                           "OFFLINE_MEMORY,1\nONLINE_MEMORY,0\n",
                           "OFFLINE_MEMORY,101\nONLINE_MEMORY,1\n",
                           "OFFLINE_MEMORY,1\nONLINE_MEMORY,101\n",
                           "OFFLINE_MEMORY,1\nOFFLINE_MEMORY,1\nONLINE_MEMORY,1\n",
                           "OFFLINE_MEMORY,1\nONLINE_MEMORY,1\nONLINE_MEMORY,1\n"):
                with self.subTest(mode=mode, phases=phases), tempfile.TemporaryDirectory() as directory:
                    args = SimpleNamespace(modes=str(mode), n="64", p=".25", k="2",
                                           block_sizes="24", repeat=1, warmup=0, k_select=2,
                                           results_folder=directory, overwrite=False, offline_profile=False)
                    failure = SimpleNamespace(returncode=0, stderr=b"", stdout=(
                        "1\n2\n3\n4\n5\nMEMORY,100\nPARALLEL,0,8,1\n" + phases).encode())
                    with mock.patch.object(experiments, "parse_args", return_value=args), \
                         mock.patch.object(experiments, "estimate_heap", return_value=1048576), \
                         mock.patch.object(experiments.subprocess, "run", return_value=failure), \
                         contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(experiments.main(), 1)
                    self.assertEqual(list(Path(directory).glob("*.csv")), [])

    def test_archive_estimates_without_relabeling(self):
        with tempfile.TemporaryDirectory() as directory:
            for mode in (2, 4, 5, 6, 7, 1, 3):
                path = Path(directory) / f"mode{mode}.csv"
                header = experiments.result_csv_header(mode)
                row = "16,.5,16,2," + ("1,2,3,4,5,6,0,8,1\n" if mode == 6 else "1,2,3,4,5,6\n" if mode != 2 else "1,2,5,6\n")
                if mode in experiments.CONTROL_MEMORY_MODES:
                    row = row.rstrip("\n") + ",3,4\n"
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

    def test_add_phase_columns_preserves_measured_history_and_backups(self):
        with tempfile.TemporaryDirectory() as directory:
            for mode in (4, 5, 6, 7):
                path = Path(directory) / f"mode{mode}.csv"
                header = experiments.result_csv_header(mode)
                old_header = header.replace(",offline_heap_peak_mib,online_heap_peak_mib", "")
                row = "16,.5,16,2,1,2,3,4,5,6" + (",0,8,1" if mode == 6 else "")
                original = (old_header + row + "\n\n" + row + "\n").replace("\n", "\r\n").encode()
                for index in range(2):
                    path.write_bytes(original)
                    experiments.prepare_result_csv(path, mode, False)
                    suffix = f".{index}" if index else ""
                    backup = path.with_name(f"mode{mode}.phase_memory_backup{suffix}.csv")
                    self.assertEqual(backup.read_bytes(), original)
                    rows = list(csv.DictReader(io.StringIO(path.read_text())))
                    self.assertEqual(len(rows), 2)
                    for measured in rows:
                        self.assertEqual(measured["algorithm_heap_peak_mib"], "6")
                        self.assertEqual(measured["offline_heap_peak_mib"], "")
                        self.assertEqual(measured["online_heap_peak_mib"], "")
                        if mode == 6:
                            self.assertEqual(measured["effective_threads"], "1")
                    migrated = path.read_bytes()
                    experiments.prepare_result_csv(path, mode, False)
                    self.assertEqual(path.read_bytes(), migrated)
                # Existing columns retain their positions; phase fields append.
                self.assertEqual(header.split(",")[:-2], old_header.strip().split(","))

    def test_malformed_measured_history_is_not_rewritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.csv"
            old_header = experiments.result_csv_header(7).replace(",offline_heap_peak_mib,online_heap_peak_mib", "")
            original = old_header + "16,.5,16\n"
            path.write_text(original)
            with self.assertRaises(ValueError):
                experiments.prepare_result_csv(path, 7, False)
            self.assertEqual(path.read_text(), original)
            self.assertEqual(list(Path(directory).glob("*backup*")), [])

    def test_phase_migration_preserves_comments_and_whitespace(self):
        for mode in (4, 5, 6, 7):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "results.csv"
                header = experiments.result_csv_header(mode)
                old_header = header.replace(",offline_heap_peak_mib,online_heap_peak_mib", "")
                row = "16,.5,16,2,1,2,3,4,5,6" + (",0,8,1" if mode == 6 else "")
                original = (old_header + "# formal test\n" + row + "\n\n   \n"
                            "  # another group, with a note\n" + row + "\n# end\n")
                path.write_text(original)
                experiments.prepare_result_csv(path, mode, False)
                self.assertEqual((Path(directory) / "results.phase_memory_backup.csv").read_text(), original)
                migrated = list(csv.reader(io.StringIO(path.read_text())))
                self.assertEqual(migrated[0], header.strip().split(","))
                self.assertEqual([r for r in migrated[1:] if not experiments.is_result_data_row(r)],
                                 [["# formal test"], [], ["   "],
                                  ["  # another group", " with a note"], ["# end"], []])
                data = [r for r in migrated[1:] if experiments.is_result_data_row(r)]
                self.assertEqual(data, [row.split(",") + ["", ""]] * 2)
                before = path.read_bytes()
                experiments.prepare_result_csv(path, mode, False)
                self.assertEqual(path.read_bytes(), before)

    def test_unknown_header_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.csv"
            path.write_text("unknown,data\n1,2\n")
            with self.assertRaises(ValueError):
                experiments.prepare_result_csv(path, 5, False)
            self.assertEqual(path.read_text(), "unknown,data\n1,2\n")


if __name__ == "__main__":
    unittest.main()
