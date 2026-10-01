#!/usr/bin/env bash
set -euo pipefail
bench_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bench_build=$(mktemp -d /tmp/supple-ofrsupple-bench.XXXXXX)
nasm -f elf64 "$bench_dir/../../Enclave/oblivious_functions.asm" -o "$bench_build/primitives.o"
"${CXX:-g++}" -std=c++11 -O3 -DNDEBUG -DSUPPLE_MEMORY_TRACKING \
  -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
  -ffunction-sections -fdata-sections \
  "$bench_dir/benchmark_vs_swo.cpp" \
  "$bench_dir/../../Enclave/SubSample_v2/OFR/OFR.cpp" \
  "$bench_dir/../../Enclave/SubSample_v2/OFR/helper.cpp" \
  "$bench_dir/../../Enclave/MemoryProfile.cpp" \
  "$bench_build/primitives.o" \
  -Wl,--gc-sections -Wl,-z,noexecstack -pthread \
  -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free \
  -Wl,--wrap=memalign,--wrap=posix_memalign,--wrap=aligned_alloc \
  -o "${BENCH_BINARY:-$bench_build/benchmark_vs_swo}"
if [[ ${BENCH_BUILD_ONLY:-0} == 1 ]]; then exit 0; fi
if [[ $# -eq 0 ]]; then set -- 7; fi
"${BENCH_BINARY:-$bench_build/benchmark_vs_swo}" "$@"
