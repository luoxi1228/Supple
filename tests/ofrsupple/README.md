# OFRSupple regression test

Run from the repository root:

```sh
bash tests/ofrsupple/run_tests.sh
```

The host test compiles the production SWO, OFR, and OFRSupple sources. It
checks sample identities for Compact-only, OFR-only, and mixed recursion;
wide membership words; two independent control streams and nonzero offsets;
and the encrypted ECALL adapter with deterministic host replacements for
randomness, encryption, and leaf shuffle.

For performance comparisons, `run_benchmark.sh` measures the production
algorithms on the host with real Compact and OFork operations. After building
and signing a SIM enclave, `benchmark_sgx_sim.py --application <path> --output
<path>` measures application modes 6 and 8 with alternating order. The SGX SIM
measurements and setup are recorded in `RESULTS/ofrsupple_vs_swo_sgx_sim.md`.

After signing with a heap large enough for the selected case, add `--profile`
to `benchmark_sgx_sim.py` to write the online breakdown for both modes. The
columns `route_ms`, `reorder_ms`, `copy_ms`, `shuffle_ms`, and `other_ms` sum to
`apply_ms`. Routing includes control unpacking and Compact for Supple, and
the prepared OFR network for OFRSupple. Copy includes reusable workspace
growth and record copies. `reorder_ms` remains in the CSV schema and is zero
because OFR now returns contiguous left and right views directly.
`other_ms` includes profiling overhead and recursive bookkeeping. Both modes
currently skip leaf Shuffle. Without `--profile`, the application retains its
original five-line output.

Add `--offline-profile` to record offline mark, count, SWO write, OFR tag,
normalize, control write, membership replay, projection, preparation, and
unattributed timings. It also reports the SGX runtime's process-lifetime heap
growth high-water mark after the first round's offline stage and the maximum
after online across all rounds, in bytes. These are not live allocation or EPC
measurements.
Both profiling flags can be used together.
