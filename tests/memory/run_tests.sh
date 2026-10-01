#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-memory-test.XXXXXX)
"${CXX:-g++}" -std=c++11 -O3 -Wall -Wextra -Werror -DSUPPLE_MEMORY_TRACKING \
  "$test_dir/tracker_test.cpp" "$test_dir/../../Enclave/MemoryProfile.cpp" \
  -o "$test_build/tracker_test"
for test in core failure realloc_failure overflow counter_overflow capacity alignment_failure; do
  "$test_build/tracker_test" "$test"
done
PYTHONDONTWRITEBYTECODE=1 python3 "$test_dir/test_results.py"
echo "PASS: memory tracker and CSV regressions ($test_build)"
