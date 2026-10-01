#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-compaction-test.XXXXXX)
# Compile the real markGen body against the host RNG adapter, without linking
# unrelated SubSample algorithms and SGX entry points into this focused test.
python3 - "$test_dir/../../Enclave/SubSample/SubSample.cpp" "$test_build/mark_gen.hpp" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
start = source.index('void markGen(size_t n, size_t m, bool *M)')
end = source.index('void markGenMulti(', start)
Path(sys.argv[2]).write_text(source[start:end].replace('void markGen(', 'void ProductionMarkGen(', 1))
PY
nasm -f elf64 "$test_dir/../../Enclave/oblivious_functions.asm" -o "$test_build/primitives.o"
"${CXX:-g++}" -std=c++11 -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
  -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" -I"$test_build" \
  "$test_dir/host_test.cpp" "$test_build/primitives.o" \
  -Wl,--gc-sections -Wl,-z,noexecstack -pthread -o "$test_build/host_test"
ASAN_OPTIONS=detect_leaks=0 "$test_build/host_test"
