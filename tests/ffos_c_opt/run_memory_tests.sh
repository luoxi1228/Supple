#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-ffos-c-opt-memory.XXXXXX)
nasm -f elf64 "$test_dir/../../Enclave/oblivious_functions.asm" -o "$test_build/primitives.o"
"${CXX:-g++}" -include "$test_dir/../../CONFIG.h" -std=c++11 -O3 \
  -DSUPPLE_MEMORY_TRACKING -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
  -ffunction-sections -fdata-sections "$test_dir/memory_test.cpp" \
  "$test_dir/../../Enclave/SubSample_v2/OFR/OFR.cpp" \
  "$test_dir/../../Enclave/SubSample_v2/OFR/helper.cpp" "$test_build/primitives.o" \
  -Wl,--gc-sections -Wl,-z,noexecstack -pthread \
  -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free \
  -Wl,--wrap=memalign,--wrap=posix_memalign,--wrap=aligned_alloc -o "$test_build/memory_test"
"$test_build/memory_test"
