#!/usr/bin/env bash
set -euo pipefail
case "${1:-serial}" in
  serial|parallel) variants=("${1:-serial}") ;;
  all) variants=(serial parallel) ;;
  *) echo "Usage: $0 [serial|parallel|all]" >&2; exit 2 ;;
esac
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-swo-test.XXXXXX)
echo "Test artifacts: $test_build"
nasm -f elf64 "$test_dir/../../Enclave/oblivious_functions.asm" -o "$test_build/primitives.o"
for variant in "${variants[@]}"; do
  "${CXX:-g++}" -std=c++11 -O1 -g -fsanitize=address,undefined \
    -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
    -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
    "$test_dir/${variant}_test.cpp" "$test_build/primitives.o" \
    -Wl,--gc-sections -Wl,-z,noexecstack -pthread -o "$test_build/${variant}_test"
  "$test_build/${variant}_test"
done
