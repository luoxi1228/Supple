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
growth and record copies. Reorder is the public OFR output permutation; both
modes include the same leaf Shuffle. `other_ms` includes profiling overhead
and recursive bookkeeping. Without `--profile`, the application retains its
original five-line output.
