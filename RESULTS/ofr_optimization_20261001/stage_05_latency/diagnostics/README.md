The retained short candidate combines fixed postorder networks for N=2/4/8
with direct SIMD pair-field classification. It was tested in `/tmp` before
being copied into production OFR.cpp. The previous stage04 source/results
remain the comparison checkpoint.

These native diagnostics use two warmups and three measured paired rounds,
8-byte records, K=16, and profiling enabled. Heap tracking is disabled.
They are separate from the formal seven-round matrix and do not establish
SGX SIM or HW performance. Median total is computed from per-round totals,
independently of the sum of the two stage medians.

| Candidate | N | OFR offline ms | OFR online ms | OFR total ms | Supple total ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| leaf8 | 1048576 | 210.329 | 84.080 | 294.409 | 275.186 |
| leaf8 + direct fields | 1048576 | 201.211 | 84.276 | 285.488 | 274.383 |
| leaf8 | 4194304 | 1056.632 | 390.542 | 1447.175 | 1235.353 |
| leaf8 + direct fields | 4194304 | 1014.164 | 392.256 | 1406.419 | 1235.953 |

The leaf8-only 4M output is `leaf8_profile.csv`; the other three filenames
include their node size. Direct fields reduced measured writer time from
193.163 to 183.801 ms at 1M and 956.983 to 911.717 ms at 4M. The online
difference between the two short candidates is small. Both additions were
retained; no attempted candidate was rejected during this short phase.
The earlier rejected loop-order/remaining-counter experiments are preserved
under stage04's `diagnostics/vector_writer` directory.

The retained candidate passed the complete OFR ASan/UBSan suite, including
the 128/32768 terminal-rank cases and membership-byte overflow fixtures,
and the production assembly regression. LeakSanitizer was disabled for the
tool execution environment; AddressSanitizer and UndefinedBehaviorSanitizer
remained enabled. The source archive includes both candidate sources, the
test source, the retained production core, and the OFRSupple/harness sources.
Copied candidate includes refer to the original absolute repository path.

The paired short online ratios are approximately 1.54x at 1M and 1.51x at
4M, below the newly requested 2x goal. Total OFRSupple time also remains
above paired Supple. The root agent will perform the formal paired matrix,
same-machine old-byte calibration and SGX SIM measurements; further online
optimization must preserve the same network and control sequence.
