#!/usr/bin/env python3
"""Snapshot sources, build in /tmp, then run serial paired stage benchmarks.

No production build, signature or enclave configuration is overwritten.
Use --backend hw on the target SGX machine for final acceptance.
"""
import argparse
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import tarfile
import xml.etree.ElementTree as ET


def run(command, cwd, output, env=None):
    with output.open("w") as log:
        subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)


def snapshot(source, build):
    paths = subprocess.check_output(
        ["git", "ls-files", "-z"], cwd=source).decode().split("\0")
    paths += subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "-z",
         "Enclave", "tests"], cwd=source).decode().split("\0")
    digest = hashlib.sha256()
    for name in sorted(set(paths)):
        path = source / name
        if (not name or name.startswith("RESULTS/") or
                path.suffix in {".pyc", ".o", ".so", ".a"} or not path.is_file()):
            continue
        target = build / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        digest.update(name.encode() + b"\0" + path.read_bytes())
    return digest.hexdigest()


def restore_stage(source, stage, build, commit):
    """Restore recorded production changes over a clean historical checkout."""
    archive = subprocess.check_output(["git", "archive", commit], cwd=source)
    with tarfile.open(fileobj=io.BytesIO(archive)) as files:
        files.extractall(build, filter="data")
    patch = stage / "source.patch"
    digest = hashlib.sha256(commit.encode())
    recorded_source = stage / "source.tar.gz"
    if recorded_source.exists():
        with tarfile.open(recorded_source) as files:
            files.extractall(build, filter="data")
        digest.update(recorded_source.read_bytes())
    elif patch.exists():
        subprocess.run(["git", "apply", str(patch.resolve())], cwd=build, check=True)
        digest.update(patch.read_bytes())
        header = stage / "PackedControls.hpp"
        if not header.exists():
            header = stage.parent / "stage_01_packed/PackedControls.hpp"
        if not header.exists():
            raise RuntimeError("recorded packed header missing")
        shutil.copy2(header, build / "Enclave/SubSample_v2/OFR/PackedControls.hpp")
        digest.update(header.read_bytes())
    # Identical harness for the old byte APIs and all packed stages.
    harness = ("tests/ffos_fr/benchmark_vs_ffos_c.cpp",
               "tests/ffos_fr/run_benchmark.sh",
               "tests/ffos_fr/paired_sgx_benchmark.cpp",
               "tests/ffos_fr/legacy_source.hpp",
               "tests/ffos_c/host_support.hpp",
               "tests/ffos_c/sgx_smoke.mk")
    for name in harness:
        target = build / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, target)
        digest.update(name.encode() + b"\0" + (source / name).read_bytes())
    return digest.hexdigest()


def legacy_source(build):
    """Only restored pre-rename trees need the test-side API aliases."""
    return (build / "Enclave/SubSample_v2/SWO/SuppleSWO.cpp").is_file()


def build_sgx(build, output, backend, heap):
    sdk = Path(os.environ.get("SGX_SDK", "/opt/intel/sgxsdk"))
    command = ["make", "-B", "-j2"]
    command += (["-f", "tests/ffos_c/sgx_smoke.mk"] if backend == "sim"
                else ["SGX_MODE=HW"])
    command += ["enclave.so"]
    run(command, build, output / "enclave_build.log")
    config = ET.parse(build / "Enclave/Enclave.config.xml")
    config.find("HeapMaxSize").text = hex(heap)
    config.find("TCSNum").text = "1"
    config.write(build / "benchmark.config.xml", encoding="unicode")
    run([sdk / "bin/x64/sgx_sign", "sign", "-key",
         build / "Enclave/Enclave_private.pem", "-enclave", build / "enclave.so",
         "-out", build / "benchmark.signed.so", "-config",
         build / "benchmark.config.xml"], build, output / "sign.log")
    run([sdk / "bin/x64/sgx_edger8r", "--untrusted", "Enclave/Enclave.edl",
         "--search-path", "Enclave", "--search-path", sdk / "include",
         "--untrusted-dir", "Untrusted"], build, output / "bridge_generate.log")
    objects = []
    for source in [build / "Untrusted/Enclave_u.c",
                   build / "Untrusted/Untrusted.cpp",
                   *sorted((build / "Untrusted/Edger8rSyntax").glob("*.cpp")),
                   *sorted((build / "Untrusted/TrustedLibrary").glob("*.cpp"))]:
        obj = build / (source.relative_to(build).as_posix().replace("/", "_") + ".o")
        compiler = "gcc" if source.suffix == ".c" else "g++"
        run([compiler, "-O3", "-fPIC", "-I" + str(sdk / "include"),
             "-IUntrusted", "-c", source, "-o", obj], build,
            output / (obj.stem + ".log"))
        objects.append(obj)
    suffix = "_sim" if backend == "sim" else ""
    run(["g++", "-shared", *objects, "-L" + str(sdk / "lib64"),
         "-lsgx_urts" + suffix, "-lsgx_uae_service" + suffix, "-pthread",
         "-Wl,-rpath," + str(sdk / "lib64"), "-o", build / "libOSort.so"],
        build, output / "bridge_link.log")
    binary = build / "paired_sgx_benchmark"
    legacy_flags = ["-DFFOS_LEGACY_SOURCE"] if legacy_source(build) else []
    run(["g++", "-std=c++11", "-O3", *legacy_flags, "-I" + str(sdk / "include"),
         "tests/ffos_fr/paired_sgx_benchmark.cpp", "Application/gcm.cpp",
         "-L" + str(build), "-lOSort", "-L" + str(sdk / "lib64"),
         "-lsgx_urts" + suffix, "-lcrypto", "-pthread",
         "-Wl,-rpath," + str(build), "-Wl,-rpath," + str(sdk / "lib64"),
         "-o", binary], build, output / "benchmark_build.log")
    return [binary, build / "benchmark.signed.so"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--stage", required=True)
    parser.add_argument("--from-stage", type=Path,
                        help="restore a recorded stage's source.patch and packed header")
    parser.add_argument("--baseline-commit", default="fb2fcac45d906eb91416291ce9705ba3d9d88140")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backend", choices=["native", "sim", "hw"], default="native")
    parser.add_argument("--rounds", type=int, default=7)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--matrix", action="store_true")
    parser.add_argument("--case", nargs=4, type=int, metavar=("N", "M", "K", "WIDTH"))
    parser.add_argument("--heap", type=lambda v: int(v, 0), default=0x100000000)
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--extras-only", action="store_true",
                        help="preserve an existing primary CSV and collect companion runs")
    parser.add_argument("--skip-extra", action="store_true",
                        help="skip separate heap and profiling runs")
    args = parser.parse_args()
    if (args.rounds < 1 or args.warmup < 0 or args.heap < 1 or
            (args.matrix and args.case) or (args.build_only and args.extras_only) or
            (args.skip_extra and args.extras_only)):
        parser.error("invalid dimensions/rounds/options")
    source = args.source_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (not args.build_only and not args.extras_only and
            (output / (args.backend + ".csv")).exists()):
        parser.error("result already exists; choose a fresh output directory")
    if args.extras_only and not (output / (args.backend + ".csv")).exists():
        parser.error("--extras-only requires an existing primary CSV")
    if not args.build_only and not args.skip_extra:
        companions = ["_heap.csv", "_profile.csv"]
        if args.matrix:
            companions += ["_shapes.csv", "_shapes_heap.csv"]
        if any((output / (args.backend + suffix)).exists() for suffix in companions):
            parser.error("companion result already exists; choose a fresh output directory")
    build = Path(tempfile.mkdtemp(prefix="supple-ofr-" + args.stage + ".", dir="/tmp"))
    digest = (restore_stage(source, args.from_stage.resolve(), build, args.baseline_commit)
              if args.from_stage else snapshot(source, build))
    metadata = dict(stage=args.stage, backend=args.backend, source=str(source),
                    build=str(build), source_sha256=digest,
                    commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source,
                                                   text=True).strip(),
                    rounds=args.rounds, warmup=args.warmup,
                    matrix=args.matrix, case=args.case,
                    from_stage=str(args.from_stage.resolve()) if args.from_stage else None,
                    cpu_model=next((line.split(":", 1)[1].strip()
                                    for line in Path("/proc/cpuinfo").read_text().splitlines()
                                    if line.startswith("model name")), "unknown"),
                    cpu_affinity=sorted(os.sched_getaffinity(0)),
                    host=platform.platform(), compiler=subprocess.check_output(
                        ["g++", "--version"], text=True).splitlines()[0])
    artifact_prefix = args.backend + ("_extras" if args.extras_only else "")
    (output / (artifact_prefix + "_metadata.json")).write_text(
        json.dumps(metadata, indent=2) + "\n")
    print(f"Isolated source/build: {build}", flush=True)
    # The lock covers builds too, preventing another local benchmark runner
    # from consuming CPU while a timed experiment is in progress.
    with open("/tmp/supple-ofr-performance.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.backend == "native":
            binary = build / "native_benchmark"
            env = dict(os.environ, BENCH_BINARY=str(binary), BENCH_BUILD_ONLY="1",
                       FFOS_LEGACY_SOURCE=str(int(legacy_source(build))))
            run(["bash", "tests/ffos_fr/run_benchmark.sh"], build,
                output / "native_build.log", env)
            base = [binary, str(args.rounds), "--warmup", str(args.warmup)]
        else:
            base = build_sgx(build, output, args.backend, args.heap)
            base += ["--rounds", str(args.rounds), "--warmup", str(args.warmup)]
        (output / (artifact_prefix + "_command.json")).write_text(json.dumps(
            [str(x) for x in base], indent=2) + "\n")
        if args.build_only:
            return
        cases = ["--matrix"] if args.matrix else (
            ["--case", *[str(v) for v in args.case]] if args.case else [])
        if not args.extras_only:
            run(base + cases, build, output / (args.backend + ".csv"))
        if args.matrix:
            run(base, build, output / (args.backend + "_shapes.csv"))
        if not args.skip_extra:
            # Keep allocator instrumentation and phase clocks separate from
            # timing runs. All native cases use identical deterministic marks.
            run(base + cases + ["--memory"], build,
                output / (args.backend + "_heap.csv"))
            if args.matrix:
                run(base + ["--memory"], build,
                    output / (args.backend + "_shapes_heap.csv"))
            profiled = ([binary, "1", "--warmup", "0"] if args.backend == "native"
                        else base[:2] + ["--rounds", "1", "--warmup", "0"])
            run(profiled + ["--case", "1048576", "65536", "16", "8", "--profile"],
                build, output / (args.backend + "_profile.csv"))


if __name__ == "__main__":
    main()
