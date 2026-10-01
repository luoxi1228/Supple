We tested our artifact on Ubuntu 20.04 and 22.04.

Before running experiments, make sure SGX is set up correctly.
See [README.sgx.md](README.sgx.md).

The experiment launcher scripts require Python 3.
Please install:

- numpy
- matplotlib

------------------------------------------------------------------------

# 1. Build Once

From the project root:

```bash
make clean
make
```

Note: `run_experiments.py` also invokes `make -C <project-root>` for each run,
so pre-building is recommended but not strictly required.

------------------------------------------------------------------------

# 2. Run Experiments

Current experiment entrypoints are:

- `run_experiments.py`: single config run (CLI configurable)
- `run.py`: batch runner for multiple experiment groups

## 2.1 Quick Start (Batch Mode, Recommended)

From the project root:

```bash
./run.py --overwrite-first
```

Useful options:

- `--dry-run`: print generated `run_experiments.py` commands without running
- `--stop-on-error`: stop immediately when any group fails
- `--overwrite-first`: pass `--overwrite` only to the first group

Default group layout in `run.py`:

- `group_p`: sweep sampling probability `p`
- `group_k`: sweep `k` (with `k-select=2`)
- `group_b`: sweep block size
- `group_n`: sweep `n`

The groups preserve their original algorithm selection with modes
`[10,2,11,12,13,4]`. The direct script defaults to `[1,2,3,4,5]`.

## 2.2 Direct Run (Single Command)

From the project root:

```bash
./run_experiments.py \
    --modes 1,2,3,4,5 \
    --n 1048576 \
    --p 0.015625 \
    --k 4,16,64,256,1024 \
    --k-select 2 \
    --block-sizes 16,64,256,1024,4096 \
    --repeat 2 \
    --results-folder RESULTS \
    --overwrite
```

To compare the online phases of Supple and OFRSupple at one configuration:

```bash
./run_experiments.py --modes 4,5 --n 65536 --p 0.015625 \
    --k 64 --k-select 2 --block-sizes 64 --repeat 5 --overwrite
```

The `apply_perm(online)` column is the enclave's online timing in milliseconds.
For a route, reorder, copy, and Shuffle breakdown, see
`RESULTS/ofrsupple_online_prepared.md`; the paired SIM benchmark supports
`--profile` and writes those measurements to separate CSV columns.

The default `run_experiments.py` run measures offline and online totals without
offline phase clocks. Pass `--offline-profile` to collect offline phase timings
and the SGX heap high-water mark for modes 4 and 5. This runs each case a
second time for diagnostics; the regular result CSV always uses the run
without offline phase clocks. The diagnostic run writes
`Supple_offline_profile.csv` and `OFRSupple_offline_profile.csv` beside the
regular results. `mark_ms`, `count_ms`, `swo_write_ms`, `tags_ms`,
`normalize_ms`, `ofr_write_ms`, `replay_ms`, `project_ms`, `prepare_ms`, and
`other_ms` sum to the diagnostic run's offline time, which can differ from the
unprofiled `gen_perm(offline)` in the regular CSV. The two peak fields are
bytes recorded by the SGX runtime's `g_peak_heap_used`: the offline mark is
captured in the first round, and the total mark is the maximum
across all rounds.
These are process-lifetime heap growth high-water marks, including enclave
setup; they are neither live allocation counts nor EPC usage.
Profiling adds clock OCALLs to the timed offline phase. Previously generated
offline profile CSVs remain on disk when profiling is disabled; they are not
updated by a normal run.

### CLI Parameters (`run_experiments.py`)

- `--modes`: comma-separated mode list
- `--n`: comma-separated input sizes
- `--p`: comma-separated sampling rate (`0 < p <= 1`)
- `--k`: comma-separated the number of samples (used when `--k-select 2`)
- `--k-select`: `1` => `k = max(1, int(1/p))`; `2` => use `--k`
- `--block-sizes`: comma-separated block sizes
- `--repeat`: measured rounds per configuration (default `5`)
- `--warmup`: rounds excluded from the average before measurement (default `1`;
    use `0` to measure from the first round)
- `--results-folder`: output directory
- `--overwrite`: overwrite each mode CSV on first write in current script process

### Mode List (Current)

| Mode | Algorithm | Previous mode |
| --- | --- | --- |
| 1 | ShuffleBasedSWO | 9 |
| 2 | PSQF_SWO | 2 |
| 3 | CompactionBasedSWO | 10 |
| 4 | Supple | 6 |
| 5 | OFRSupple | 8 |
| 10 | PSQF_single | 1 |
| 11 | SubSample | 3 |
| 12 | SubSampleMultiSlice | 4 |
| 13 | SubSampleMulti_opt | 5 |
| 14 | Supple_parallel | 7 |

Modes 6–9 are unused and rejected. Use the new numbers in both
`run_experiments.py` and `Application/application`; old numbers are not aliases.
Algorithm names, result filenames and existing CSV data remain unchanged.

### `k` Selection Behavior

- Modes `1`, `3`, `4`, `5`, `12`, `13`, and `14`:
    - `k-select=1`: use derived `k = max(1, int(1/p))`
    - `k-select=2`: sweep values from `--k`
- Mode `2`: always uses `k = max(1, int(1/p))`
- Modes `10` and `11`: fixed `k = 1`
- Mode `14` also accepts `--threads` for parallel execution.

------------------------------------------------------------------------

# 3. Output Files and CSV Format

Results are written under `--results-folder` (default `RESULTS`).

Each mode writes to one CSV file:

- `<RESULTS_FOLDER>/PSQF_single.csv`
- `<RESULTS_FOLDER>/PSQF_SWO.csv`
- `<RESULTS_FOLDER>/SubSample.csv`
- `<RESULTS_FOLDER>/SubSampleMultiSlice.csv`
- `<RESULTS_FOLDER>/SubSampleMulti_opt.csv`
- `<RESULTS_FOLDER>/Supple.csv`
- `<RESULTS_FOLDER>/Supple_parallel.csv`
- `<RESULTS_FOLDER>/OFRSupple.csv`
- `<RESULTS_FOLDER>/ShuffleBasedSWO.csv`
- `<RESULTS_FOLDER>/CompactionBasedSWO.csv`

Modes `1`, `2`, `3`, `4`, and `5` write the column names on the first line and
leave one blank line between separate executions of `run_experiments.py`.
Existing headerless files and files with a `heap_est_mb` header are preserved as
`<mode>.heap_est_backup.csv` before a new result file is started; collisions
use `.heap_est_backup.1.csv`, `.2.csv`, etc. Backups are never overwritten.
`--overwrite` keeps its explicit replacement behavior. Other modes retain
their existing headerless format and estimated-memory metric.


## 3.1 Common Prefix Columns

All modes start with:

`block_size, p, n, k, ...`

## 3.2 Modes 2/10/11

Columns:

`block_size, p, n, k, ecall_time, ptime, oswaps, heap_est_mb`

Mode `2` uses `ecall_time_ms` and `ptime_ms` in its CSV header.
Its final column is `algorithm_heap_peak_mib` instead of `heap_est_mb`.

## 3.3 Modes 1/3/4/5/12/13/14

Columns:

`block_size, p, n, k, ecall_time, ptime, gen_perm(offline), apply_perm(online), oswaps, heap_est_mb`

Modes `1`, `3`, `4`, and `5` use `ecall_time_ms`, `ptime_ms`,
`gen_perm_offline_ms`, and `apply_perm_online_ms` in their CSV headers.
Their final column is `algorithm_heap_peak_mib` instead of `heap_est_mb`.

Mode `1` has no offline phase: `gen_perm=0` and `apply_perm=ptime`. Use `k=1`
for a single sample and `k>1` for repeated shuffle-and-truncate sampling.

Mode `3` reuses `markGen` and Supple's serial `TightCompact_v2`. Each round
selects exactly `m` records, compacts all `n` records in place, and copies the
first `m` to that sample's output; there is no additional shuffle. Marks use
one reusable `bool[n]` buffer. Samples use fresh randomness and may overlap,
while each sample contains no duplicate records. Compaction determines the
output order. `gen_perm_offline_ms` sums marking time and
`apply_perm_online_ms` sums compaction and truncation/copy time; these phases
alternate within the `k` iterations rather than running as two batch phases.
`ptime_ms` is their sum; decryption/encryption are included in ECALL time.
Algorithm timing uses `2*k+1` clock OCALLs, separate from the allocation
recorder, which adds no clock calls. The default modes are `[1,2,3,4,5]`.

Example:

```bash
python3 run_experiments.py --modes 3 --n 16384 --p 0.015625 --k 64 --block-sizes 16 --repeat 5 --warmup 1
```

Compaction baseline regression tests: `bash tests/compaction/run_tests.sh`.
SGX SIM tests including mode 3: `bash tests/memory/run_sgx_tests.sh`.

Where:

- `heap_est_mb` is the estimated heap size (MB) used to patch `Enclave.config.xml`
    before each run, retained in the CSV only for modes 10/11/12/13/14.
- `algorithm_heap_peak_mib` (modes 1/2/3/4/5) is the maximum simultaneous live
    requested heap bytes across successful **measured** rounds, divided by
    1,048,576. Each round spans decryption, offline and online execution,
    encryption, and cleanup. Warmup peaks are excluded; persistent scratch
    allocated during warmup remains part of each measured round's baseline.
    The counter includes STL capacity (and expansion overlap), plaintext
    input/output, workspaces, PRB, and dynamic allocations in called SDK code.
    It excludes host buffers, enclave initialization, stacks, allocator
    metadata/fragmentation, and the recorder's fixed BSS address table.
    It describes preheated algorithm heap demand, not a minimum `HeapMaxSize`.

The enclave records allocation/free events in the same execution, with no
sampling or additional clock OCALLs. A 16,384-slot table is kept below 50%
occupancy; ledger overflow, allocation failure, failed execution or missing
`MEMORY,<peak_bytes>` output prevents writing a normal result and makes the
experiment command fail. The heap estimate still configures the enclave and
appears in console diagnostics. The optional offline profile's existing SGX
heap-growth fields retain their separate meaning.

Memory regression tests: `bash tests/memory/run_tests.sh`. See
`tests/memory/README.md` for SGX integration and overhead measurements.

------------------------------------------------------------------------

# 4. Plotting

All plotting scripts are under:

`./Supple/plot`

run:

```bash
python3 plot/plot_p/plot_p.py
python3 plot/plot_k/plot_k.py
python3 plot/plot_n/plot_n.py
python3 plot/plot_b/plot_b.py
python3 plot/legend.py
python3 plot/legend_com.py
```

Generated figures are saved in the same corresponding folders/files, e.g.:

- `plot/plot_p/plot_p.pdf`, `plot/plot_p/plot_p.png`
- `plot/plot_k/plot_k.pdf`, `plot/plot_k/plot_k.png`
- `plot/plot_n/plot_n.pdf`, `plot/plot_n/plot_n.png`
- `plot/plot_b/plot_b.pdf`, `plot/plot_b/plot_b.png`
- `plot/legend.pdf`, `plot/legend.png`
- `plot/legend_com.pdf`, `plot/legend_com.png`

Note:

- The plotting scripts in `plot/plot_p`, `plot/plot_k`, `plot/plot_n`, and `plot/plot_b`
    read local `.lg` files in each subfolder (such as `01_S&T.lg` ... `05_OptBatch.lg`).

------------------------------------------------------------------------
