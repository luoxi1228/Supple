# FFOS_FR and FFOS_FR_Opt regression tests

Run from the repository root:

```sh
bash tests/ffos_fr/run_tests.sh
```

The host test compiles the production FFOS_C, OFR, and FFOS_FR sources in an
isolated `/tmp` directory with AddressSanitizer and UndefinedBehaviorSanitizer,
including the default leak checks. Selection scratch is owned by each
control/apply call and released on return or exception.
It uses deterministic host replacements for randomness, encryption, and leaf
shuffle. Stage 06 reports these actual case counts:

- 1,843 integration cases check complete records and sample identities for
  Compact-only, OFR-only, and mixed recursion, including `M*K<N`, `=N`, and
  `>N`; odd/asymmetric shapes; `N=25, M=5, K=21` balanced frontier grouping;
  K=64 high membership bits; and multi-word K=65/129 membership. Public const
  control writers preserve their membership inputs, and both control streams
  support nonzero logical offsets and neighboring sentinels. The encrypted
  ECALL adapter is checked against the same sample identities.
- 2,304 single-word tag cases compare the SIMD/scalar-tail generator and
  accumulated weights to scalar `HasMembership` for N=0..17, unaligned
  word/tag offsets, K=3/5/63/64, all four tag values, high 32-bit membership
  positions and ignored out-of-range padding. Feasible inputs also compare
  canonical controls.
- 10 invalid whole plans are rejected before any input/output write or
  switching count increment. Cases include malformed later-node offsets,
  shape indices, counts or geometry; missing/extra nodes; truncated OFR/SWO
  tapes; nonbalanced nodes marked postordered; and insufficient output space.

The separate [OFR regression](../ofr/README.md) checks packed typed writers,
in-place normalization, 38,016 fused DFS cases and 1,040 tiled postorder cases
against independent DFS generation/conversion/replay, comparing every control,
final tag and membership word. Its four maximum-rank fixtures reach N=524,288;
three membership APIs reject byte-product overflow before writes. Production
SSE2/assembly checks include 131,072 independent four-gate packets and 4,096
mixed N=16/32 networks, with packed/data misalignment, sentinels and exact
`COUNT_OSWAPS` increments. The OFR `run_tests.sh` builds that production smoke
test at O2; the linked README gives an optional isolated ASan/UBSan command,
already run separately for Stage 06. This host test does not sign an enclave.

For performance comparisons, `run_benchmark.sh` measures the production
algorithms on the host with real Compact and production SSE2/assembly OFork
operations. After building and signing a SIM enclave,
`benchmark_sgx_sim.py --application <path> --output
<path>` measures application modes 4, 5 and 6 with alternating order. The
isolated runner below uses `paired_sgx_benchmark.cpp` to preserve each ECALL
measurement, timing and output verification independently.

After signing with a heap large enough for the selected case, add `--profile`
to `benchmark_sgx_sim.py` to write the online breakdown for all three modes. The
columns `route_ms`, `reorder_ms`, `copy_ms`, `shuffle_ms`, and `other_ms` sum to
`apply_ms`. Routing includes control unpacking and Compact for FFOS_C, and
the prepared OFR network for FFOS_FR. Copy includes reusable workspace
growth and record copies. `reorder_ms` remains in the CSV schema and is zero
because OFR now returns contiguous left and right views directly.
`other_ms` includes profiling overhead and recursive bookkeeping. All three modes
currently skip leaf Shuffle. Without `--profile`, the application prints its
normal timing lines and the `MEMORY` line.

Prepared exact-fit FR subtrees (`n == m*k`) now copy their input directly into
the final output slice once, then route its disjoint child views in place.
They allocate no per-depth record buffers and perform no leaf copy back to
the output. The input remains unchanged when input/output are disjoint.
Frontier filtering and raw DFS controls retain their existing workspace paths;
prepared exact-fit subtrees reached after filtering use the same optimization.
Both FR modes share this storage change; mode 5 still invokes one scalar OFork
and increments the counter once per gate. See
[inplace_validation.md](inplace_validation.md) for before/after measurements.

Add `--offline-profile` to record offline mark, count, SWO write, OFR tag,
normalize, control write, membership replay, projection, preparation, and
unattributed timings. It also reports the SGX runtime's process-lifetime heap
growth high-water mark after the first round's offline stage and the maximum
after online across all rounds, in bytes. These are not live allocation or EPC
measurements.
Both profiling flags can be used together.

`FFOS_FRControls::ofr` and all OFR writers/readers now use
`ofr::PackedControls`: `size()` counts logical gates, `byte_size()` counts
`ceil(gates/4)` storage bytes. Gate `g` starts at bit `2*(g%4)` of byte `g/4`.
Node offsets, writer return positions and `view(offset, count)` bounds count
logical gates. Bounded views preserve adjacent nodes sharing a byte and the
final byte's zero padding; sequential online execution decodes packed bytes
directly. Four independent 8 B/16 B gates use `FourGateSSE2.hpp`; dependent
small networks and other widths retain the production assembly/fallback
paths. The GNU vector/builtin implementation avoids host intrinsic headers
for the SGX SDK's `-nostdinc` build. SIMD dispatch uses public shape, width and
platform checks, including the 64-bit membership and dynamic-assembly width
boundaries.

The optimization runner snapshots sources and builds/signs under `/tmp`:

```sh
python3 tests/ffos_fr/run_optimization_stage.py --stage workspace \
  --backend native --matrix --output RESULTS/ofr_native_new
python3 tests/ffos_fr/run_optimization_stage.py --stage workspace \
  --backend sim --case 1048576 65536 16 8 --output RESULTS/ofr_sim_new
python3 tests/ffos_fr/run_optimization_stage.py --stage workspace \
  --backend hw --matrix --output RESULTS/ofr_hw_new
```

Default measurements use two warmups and seven measured rounds, alternating
FFOS_C/FFOS_FR/FFOS_FR_Opt each round. The native primary matrix covers 8 B, P=1/16,
K=16, N=2^16/18/20/22/24, plus 16 B, P=1/64, K=64, N=2^20/22.
Shape tests include compaction, equal capacity, frontier and mixed nodes with
16/64/256 B records. A process lock serializes benchmark runs and builds.
Existing output CSVs are refused, and the production enclave configuration
and binaries remain outside the runner's build directory.

Native timing includes control construction and application on identical
deterministic membership inputs; mark generation and encryption are outside
that measurement. SGX timing includes mark generation in offline and reports
actual ECALL time including enclave decryption/encryption. SGX uses fresh
sampling randomness per ECALL; the encrypted input is identical for each
pair. Every method's first measured output is fully authenticated and checked
for record identity and uniqueness within each sample.

Allocator instrumentation and detailed phase clocks run separately from the
primary timing matrix. Heap companion runs use the same warmups and measured
round count, recording a peak for every round; profiling uses one separate
round. `*_heap.csv` reports live requested heap bytes,
including any allocations retained across calls; selection scratch is reused
only within a control/apply call. Native peaks cover the
control/apply scope, SGX peaks cover the complete algorithm ECALL. These
figures exclude allocator metadata, runtime reserved heap, RSS and EPC use.
In a native `kind=median` row, control/apply/total times are medians of their
own per-round samples, and the heap field is the maximum peak. The phase
columns (`tags_ms`, `normalize_ms`, `write_ms`, `replay_ms`, `project_ms`,
`prepare_ms`) retain the last measured round's values; they are not phase
medians. Read the separate profiling round when comparing stages.
`*_profile.csv` accounts for all generation and routing work in `write_ms`;
`replay_ms` covers remaining caller-preserving copies. Root membership and
offline workspace destruction are included in actual offline time.

Recorded stages under `RESULTS/ofr_optimization_20261001` can be restored with
`--from-stage RESULTS/ofr_optimization_20261001/stage_03_tiled` (or another
stage directory). The runner applies that stage over its baseline commit and
uses the same current benchmark harness with test-only aliases for the archived API. Archived source and result files retain their original names; current method labels are `FFOS_C`, `FFOS_FR` and `FFOS_FR_Opt`. Restored historical sources retain the two-method comparison. The comparison reader accepts both generations of labels. `compare_optimization_stages.py`
computes medians from raw rounds; median offline plus median online can differ
from the median of each round's total. SIM verifies the enclave build,
encrypted ECALL flow and results in the simulated runtime, and permits timing
comparisons within that runtime. It does not reproduce HW EPC limits,
enclave paging or hardware transition costs; representative HW performance
requires the same cases and measurements on the target machine. Host/SIM
ratios are not HW conclusions.
The comparison CSV also reports FFOS_C/FFOS_FR online speedup and the
minimum, median and maximum of paired round speedups. `online_2x_all_rounds`
requires every measured pair to reach two times; this is separate from the
ratio of independent method medians.
If `baseline_recheck/<backend>.csv` is present, online regression ratios use
that new byte-baseline run; `online_baseline_stage` identifies the reference.
Use `--extras-only` with the same `--from-stage`, case selection and output
directory to collect missing shape, heap and profile companions while keeping
an existing primary CSV. Completed companions are also refused.

## Per-gate and optimized FR modes

Mode 5 (`FFOS_FR`) uses scalar assembly OFork for every online gate. Mode 6
(`FFOS_FR_Opt`) enables four-gate SSE2 for balanced postorder B=8/16 spans.
Control generation, packing and workspaces are shared. `gate_backend_test.cpp`
links the real production OFR translation unit and checks scalar/batch call
counts, all four control values, offsets, guard bytes, identical outputs and
both ECALL adapters. Probe updates are compiled only under
`OFR_TEST_GATE_COUNTS`; production builds have no execution probes.

The native benchmark now explicitly includes CONFIG.h in every translation
unit, so C and OFR use the same COUNT_OSWAPS configuration. The earlier host
adapter's BEFTS_MODE otherwise skipped CONFIG.h for the C translation unit.
Archived source snapshots and results are left unchanged.

Stage reports add a `method` column to distinguish FR from FR_Opt; existing
`ffos_fr_*` metric columns describe the variant named by that row. Legacy
Supple/OFRSupple labels continue to be normalized when reading old inputs.
