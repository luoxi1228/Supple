# FFOS_C_Opt (mode 7)

The implementation lives in `Enclave/SubSample_v2/FFOS_C_Opt`. Original C
and FR source files are unchanged. Compare **mode 7 with serial mode 5**;
mode 6 also changes parallelism and the gate backend.

## Common memory policy

| Policy | Optimized C | Serial FR |
| --- | --- | --- |
| Root input | Immutable; separate final output | Same |
| Membership generation/frontier | Shared FFOS_CMark/FFOS_CFrontier | Same |
| Internal membership | Owned mutable views; const public roots copy | Same |
| Child membership | Explicit reserve; per-depth left/right buffers; omit terminal child projections | Same |
| Selection flags | One call-owned SelectedScratch shared across serial compactions | Same |
| Online exact-fit subtree | Prepare children in final disjoint output slices, then recurse there | Same |
| Offline temporary lifetime | Released before online output allocation | Same |
| Validation | Full public plan/tape preflight before output writes | Same |
| Measurement | Same ECALL Scope, requested allocation bytes, clock boundaries and scalar gate counters | Same |

C continues to perform two independent TightCompact operations at each split;
it does not substitute OFork. To preserve the original parent for the second
compaction, one shared dataset-sized backup is used. It is dead before child
recursion and reused at every depth. FR routes the parent into independent
child slices directly. These different data dependencies and the different
packed control tapes remain visible in the measurements.

The C selection tape is byte-for-byte identical to mode 4. A public-shape plan
locates the right edge after the left DFS subtree, allowing sibling edges to
execute before descending without changing control contents, Compact call
counts, logical gates or output order. Both algorithm and profile timings
include online backup allocation/copying; no online work moves offline.
Plan construction is timed offline and its allocation counts toward the peak.
Scratch sizes and execution order depend only on public dimensions.

Non-exact roots/frontier edges retain filtering followed by a copy into the
final subtree output, as in FR. All later exact-fit descendants reuse output
slices. Input/output aliasing in the low-level reader is rejected before writes;
the ECALL supports the existing encrypted host-buffer aliasing through its
separate decrypted input and plaintext output.

## Running

```bash
python3 run_experiments.py --modes 5,7 --n 1048576 --p 0.015625 \
  --k 64 --block-sizes 16 --repeat 3 --warmup 1 \
  --results-folder RESULTS/c_opt_vs_fr
```

This fully rebuilds/signs through the existing experiment runner and writes
`FFOS_FR.csv` and `FFOS_C_Opt.csv`. Add `--offline-profile` for a separate
diagnostic invocation and `FFOS_C_Opt_offline_profile.csv`. Defaults and
historical result files are unchanged. Compare fresh rows from the same build.

```bash
bash tests/ffos_c_opt/run_tests.sh
bash tests/ffos_c_opt/run_memory_tests.sh
bash tests/memory/run_tests.sh
bash tests/memory/run_sgx_tests.sh
bash tests/ffos_fr/run_benchmark.sh 3 --warmup 1 \
  --case 1048576 16384 64 16 --memory --threads 1
```

The native and paired SGX benchmark harnesses now include C_Opt as a fourth
method and rotate method order. The native control/apply harness excludes
preexisting input and membership and uses immutable control API roots for
both C_Opt and FR; its peak must not be confused with the full ECALL scope.

## Validation

- 3,717 ASan/UBSan cases compare exact controls and outputs, offline/online
  gate counts and Compact calls against the original C. Cases include complete
  sample overlap, odd shapes, k up to 257, record widths 4–64, owned versus
  immutable membership and guarded outputs. Five malformed plan/buffer cases
  per routing case verify rejection before writes. LeakSanitizer is disabled
  in the ptrace-supervised environment; allocation ownership is checked
  independently by the ledger tests.
- Native full-adapter allocation tests compare repeated exact-byte peaks and
  unchanged online gate counts, and exercise 336 throwing allocation points.
  Each failure unwinds with an invalid result and no tracked live allocations;
  a subsequent valid call succeeds.
- The shared memory/CSV regressions cover mode 7 dispatch, heap configuration,
  measured memory parsing, failure rejection and independent result/profile
  filenames.
- Isolated SGX SIM validates real AES-GCM, payloads, sample uniqueness, repeated
  and interleaved calls, invalid authentication, wide membership, overflow,
  allocation failure and the actual experiment CSV/profile pipeline.
- Isolated hardware enclave/bridge/application compilation succeeds. Hardware
  performance measurements require an SGX device; SIM times are not hardware
  speedup evidence.

At N=1,048,576, M=16,384, K=64 and B=16, one measured paired SIM round
after one warmup reports these full algorithm allocation peaks:

| Method | Bytes | MiB |
| --- | ---: | ---: |
| Original C / mode 4 | 73,500,836 | 70.095860 |
| C_Opt / mode 7 | 57,253,772 | 54.601452 |
| Serial FR / mode 5 | 57,350,824 | 54.694008 |

The C payload workspace decreases from 31.5 MiB across depths to one 16 MiB
backup. Remaining differences include C's Compact flags/prefixes, FR's OFR
tape and tags, child membership capacity, and their respective public plans.
This validates the stated common storage policy; it is not a proof that either
implementation attains its theoretical minimum memory.
