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

Note: `Application/run_experiments.py` also invokes `make -C ../` for each run,
so pre-building is recommended but not strictly required.

------------------------------------------------------------------------

# 2. Run Experiments

Current experiment entrypoints are:

- `Application/run_experiments.py`: single config run (CLI configurable)
- `Application/run.py`: batch runner for multiple experiment groups

## 2.1 Quick Start (Batch Mode, Recommended)

From `Application/`:

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

All groups currently use modes `[31,33,32,34,35]`.

## 2.2 Direct Run (Single Command)

From `Application/`:

```bash
./run_experiments.py \
    --modes 31,32,33,34,35 \
    --n 1048576 \
    --p 0.015625 \
    --k 4,16,64,256,1024 \
    --k-select 2 \
    --block-sizes 16,64,256,1024,4096 \
    --repeat 2 \
    --results-folder ../RESULTS \
    --overwrite
```

### CLI Parameters (`run_experiments.py`)

- `--modes`: comma-separated mode list
- `--n`: comma-separated input sizes
- `--p`: comma-separated sampling rate (`0 < p <= 1`)
- `--k`: comma-separated the number of samples (used when `--k-select 2`)
- `--k-select`: `1` => `k = max(1, int(1/p))`; `2` => use `--k`
- `--block-sizes`: comma-separated block sizes
- `--repeat`: repeat count per config
- `--results-folder`: output directory
- `--overwrite`: overwrite each mode log on first write in current script process

### Mode List (Current)

- `31`: S&T
- `32`: PSQF
- `33`: OSBSubsample(SingleSubsampling)
- `34`: RecSubsample(MultiSubsampling)
- `35`: OMBSubsample(OptMultiSubsampling)

### `k` Selection Behavior

- Modes `34` and `35`:
    - `k-select=1`: use derived `k = max(1, int(1/p))`
    - `k-select=2`: sweep values from `--k`
- Mode `32`: always uses `k = max(1, int(1/p))`
- Modes `31` and `33`: fixed `k = 1`

------------------------------------------------------------------------

# 3. Output Files and Log Format

Results are written under `--results-folder` (default `../RESULTS`).

Each mode is appended to one log file:

- `<RESULTS_FOLDER>/31.lg`
- `<RESULTS_FOLDER>/32.lg`
- `<RESULTS_FOLDER>/33.lg`
- `<RESULTS_FOLDER>/34.lg`
- `<RESULTS_FOLDER>/35.lg`


## 3.1 Common Prefix Columns

All modes start with:

`block_size, p, n, k, ...`

## 3.2 Mode 31/32/33

Columns:

`block_size, p, n, k, ecall_time, ptime, oswaps, heap_est_mb`

## 3.3 Mode 34/35

Columns:

`block_size, p, n, k, ecall_time, ptime, gen_perm(offline), apply_perm(online), oswaps, heap_est_mb`

Where:

- `heap_est_mb` is the estimated heap size (MB) used to patch `Enclave.config.xml`
    before each run.

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


