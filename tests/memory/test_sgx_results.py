"""End-to-end experiment CSVs using an already signed test enclave in /tmp."""
import csv
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("experiments", ROOT / "run_experiments.py")
experiments = importlib.util.module_from_spec(spec)
spec.loader.exec_module(experiments)


def main(build):
    # The tiny cases fit the pre-signed heap. Only replace the build command;
    # allocation estimates, subprocess execution, parsing and CSV I/O are real.
    run = subprocess.run
    outputs = {}

    def process(command, **kwargs):
        if command[0] == "make":
            return subprocess.CompletedProcess(command, 0, stdout=b"", stderr=b"")
        result = run(command, **kwargs)
        if kwargs["env"].get("OFFLINE_PROFILE") != "1":
            outputs[int(command[1])] = result.stdout.decode()
        return result

    with tempfile.TemporaryDirectory(prefix="supple-memory-csv-") as directory:
        directory = Path(directory)
        config = directory / "Enclave.config.xml"
        config.write_bytes((ROOT / "Enclave/Enclave.config.xml").read_bytes())
        results = directory / "results"
        args = ["run_experiments.py", "--modes", "1,2,3,4,5,6", "--n", "64", "--p", ".25",
                "--k", "2", "--block-sizes", "24", "--repeat", "2", "--warmup", "2",
                "--results-folder", str(results), "--offline-profile"]
        with mock.patch.object(experiments, "APP_DIR", build), \
             mock.patch.object(experiments, "ENCLAVE_CONFIG", config), \
             mock.patch.object(experiments.subprocess, "run", side_effect=process), \
             mock.patch.object(sys, "argv", args):
            assert experiments.main() == 0
            assert {path.name for path in results.glob("*.csv")} == {
                "ShuffleBasedSWO.csv", "PSQF_SWO.csv", "CompactionBasedSWO.csv",
                "FFOS_C.csv", "FFOS_FR.csv", "FFOS_FR_Opt.csv", "FFOS_C_offline_profile.csv",
                "FFOS_FR_offline_profile.csv", "FFOS_FR_Opt_offline_profile.csv"}
            for mode in (2, 4, 5, 6, 1, 3):
                path = results / (experiments.MODE_INFO[mode]["name"] + ".csv")
                with path.open() as source:
                    rows = list(csv.DictReader(source))
                assert len(rows) == 1
                peak = experiments.parse_memory_peak(outputs[mode])
                assert float(rows[0]["algorithm_heap_peak_mib"]) == peak / 1048576
                # Old estimated data must be archived, not appended or relabeled.
                old = path.read_text().replace("algorithm_heap_peak_mib", "heap_est_mb")
                path.write_text(old)
            assert experiments.main() == 0
            for mode in (2, 4, 5, 6, 1, 3):
                name = experiments.MODE_INFO[mode]["name"]
                assert (results / f"{name}.heap_est_backup.csv").is_file()
                with (results / f"{name}.csv").open() as source:
                    assert len(list(csv.DictReader(source))) == 1
            assert experiments.main() == 0
            for mode in (2, 4, 5, 6, 1, 3):
                name = experiments.MODE_INFO[mode]["name"]
                with (results / f"{name}.csv").open() as source:
                    assert len(list(csv.DictReader(source))) == 2
        for mode in (4, 5, 6):
            profile = results / (experiments.MODE_INFO[mode]["name"] + "_offline_profile.csv")
            with profile.open() as source:
                rows = list(csv.DictReader(source))
            assert len(rows) == 3
            assert "heap_est_mb" in rows[0] and "total_heap_peak_bytes" in rows[0]
        # Application aggregates a single measured round exactly; no averaging
        # or estimated heap capacity should appear in the memory output.
        env = os.environ.copy()
        env.pop("OFFLINE_PROFILE", None)
        env.pop("ONLINE_PROFILE", None)
        for mode in (1, 3):
            for warmup in (0, 2):
                command = [str(build / "application"), str(mode), "64", "24", ".25", "2", "1", str(warmup)]
                result = run(command, cwd=build, env=env, capture_output=True, text=True, check=True)
                assert experiments.parse_memory_peak(result.stdout) > 64 * 24 + 16 * 2 * 24
        command = [str(build / "application"), "3", "64", "24", ".25", "0", "1", "0"]
        result = run(command, cwd=build, env=env, capture_output=True, text=True)
        assert result.returncode != 0
        # Exercise all new identifiers through the real CLI, including the
        # older single-sample and multi-sample modes numbered from 10 onward.
        for mode in experiments.MODE_INFO:
            command = experiments.build_command(mode, 64, 24, .25, 2, 1, 0)
            command[0] = str(build / "application")
            result = run(command, cwd=build, env=env, capture_output=True, text=True, check=True)
            assert experiments.parse_output(mode, result.stdout) is not None
            if mode in experiments.CSV_HEADER_MODES:
                assert experiments.parse_memory_peak(result.stdout) > 0
            else:
                assert experiments.parse_memory_peak(result.stdout) is None
        for mode in (7, 8, 9, 14):
            command = [str(build / "application"), str(mode), "64", "24", ".25", "1", "0"]
            result = run(command, cwd=build, env=env, capture_output=True, text=True)
            assert result.returncode != 0
            assert "MODE must be one of" in result.stdout
        # The former parallel invocation, including its extra thread argument,
        # must fail too rather than being accepted by the usage-only path.
        command = [str(build / "application"), "14", "64", "24", ".25",
                   "2", "2", "1", "0"]
        result = run(command, cwd=build, env=env, capture_output=True, text=True)
        assert result.returncode != 0
    print("PASS: real application, measured CSVs, legacy backups and offline-profile compatibility")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve())
