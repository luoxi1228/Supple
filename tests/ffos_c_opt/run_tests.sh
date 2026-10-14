#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-ffos-c-opt.XXXXXX)
nasm -f elf64 "$test_dir/../../Enclave/oblivious_functions.asm" -o "$test_build/primitives.o"
"${CXX:-g++}" -include "$test_dir/../../CONFIG.h" -std=c++11 -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" -ffunction-sections -fdata-sections \
  "$test_dir/opt_test.cpp" "$test_build/primitives.o" \
  -Wl,--gc-sections -Wl,-z,noexecstack -pthread -o "$test_build/opt_test"
ASAN_OPTIONS=detect_leaks=0 "$test_build/opt_test"
