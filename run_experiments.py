#!/usr/bin/python3

import argparse
import math
import os
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent
APP_DIR = PROJECT_DIR / "Application"
ENCLAVE_CONFIG = PROJECT_DIR / "Enclave" / "Enclave.config.xml"
DEFAULT_RESULTS_FOLDER = "RESULTS"


def available_cpu_count():
  affinity = None
  try:
    affinity = sorted(os.sched_getaffinity(0))
  except (AttributeError, OSError):
    affinity = list(range(max(1, int(os.cpu_count() or 1))))

  physical_cores = set()
  for cpu in affinity:
    topology = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology")
    try:
      package_id = (topology / "physical_package_id").read_text().strip()
      core_id = (topology / "core_id").read_text().strip()
      physical_cores.add((package_id, core_id))
    except OSError:
      return max(1, len(affinity))
  return max(1, len(physical_cores))


DEFAULT_MODE = [6]
DEFAULT_P = [0.015625]
DEFAULT_N = [1048576]
DEFAULT_K = [16, 64, 256]
DEFAULT_K_SELECT = 2   # 1 => k = 1/p, 2 => use --k list
DEFAULT_BLOCK_SIZE = [16]
DEFAULT_REPEAT = 2
DEFAULT_THREADS = 1 #available_cpu_count()

BASE_HEAP = 1000000
SIZE_T_BYTES = int(os.getenv("SIZE_T_BYTES", "8"))
WORD_BITS = SIZE_T_BYTES * 8

MODE_INFO = {
  1: {"name": "PSQF_single", "needs_k": False, "detailed": False, "fixed_k": True},
  2: {"name": "PSQF_SWO", "needs_k": False, "detailed": False, "fixed_k": False},
  3: {"name": "SubSample", "needs_k": False, "detailed": False, "fixed_k": True},
  4: {"name": "SubSampleMultiSlice", "needs_k": True, "detailed": True, "fixed_k": False},
  5: {"name": "SubSampleMulti_opt", "needs_k": True, "detailed": True, "fixed_k": False},
  6: {"name": "Supple", "needs_k": True, "detailed": True, "fixed_k": False},
  7: {"name": "Supple_parallel", "needs_k": True, "detailed": True, "fixed_k": False},
}


def parse_csv(raw, arg_name, cast):
  values = []
  for part in str(raw).split(","):
    token = part.strip()
    if token == "":
      continue
    try:
      values.append(cast(token))
    except ValueError:
      print(f"Invalid value in --{arg_name}: '{token}'")
      sys.exit(1)
  if not values:
    print(f"--{arg_name} must contain at least one value")
    sys.exit(1)
  return values


def normalize_modes(modes):
  invalid = [mode for mode in modes if mode not in MODE_INFO]
  if invalid:
    print("Each mode must be one of: " + ",".join(str(mode) for mode in MODE_INFO))
    sys.exit(1)
  return modes


def parse_args():
  parser = argparse.ArgumentParser(description="Run SubSample experiments and log metrics.")
  parser.add_argument("--modes", default=",".join(map(str, DEFAULT_MODE)), help="Comma-separated modes, e.g. 3,4")
  parser.add_argument("--n", default=",".join(map(str, DEFAULT_N)), help="Comma-separated N values")
  parser.add_argument("--p", default=",".join(map(str, DEFAULT_P)), help="Comma-separated P values (0 < P <= 1)")
  parser.add_argument("--k", default=",".join(map(str, DEFAULT_K)), help="Comma-separated K values (used by modes 4/5/6/7)")
  parser.add_argument("--k-select", type=int, choices=[1, 2], default=DEFAULT_K_SELECT,
                      help="Modes 4/5/6/7 K selection: 1 => k=1/p, 2 => use --k list")
  parser.add_argument("--block-sizes", default=",".join(map(str, DEFAULT_BLOCK_SIZE)), help="Comma-separated block sizes")
  parser.add_argument(
    "--repeat", type=int, default=DEFAULT_REPEAT,
    help="Number of measured rounds averaged after one warm-up round",
  )
  parser.add_argument("--threads", type=int, default=DEFAULT_THREADS, help="Thread count for mode 7")
  parser.add_argument("--results-folder", default=DEFAULT_RESULTS_FOLDER, help="Results folder path")
  parser.add_argument("--overwrite", action="store_true", help="Overwrite each mode CSV on first write")
  return parser.parse_args()


def sample_size(n, sample_prob):
  m = int(float(n) * float(sample_prob))
  return min(max(m, 1), n)


def derived_k(n, m):
  return max(1, int(n / m)) if m > 0 else 1


def swo_effective_threads(n, m, k, requested_threads):
  effective = min(max(1, int(requested_threads)), max(1, int(k)))
  if effective <= 1 or k <= 1 or n < 4096:
    return 1
  return effective


def node_num(n, m, k):
  if k <= 1 or n == 0:
    return 0
  k_left = k // 2
  k_right = k - k_left
  s_left = min(n, m * k_left)
  s_right = min(n, m * k_right)
  return (2 * n) + node_num(s_left, m, k_left) + node_num(s_right, m, k_right)


def route_state_sizes(n, m, k):
  mask_words = (k + WORD_BITS - 1) // WORD_BITS
  k_left = k // 2
  k_right = k - k_left
  left_mask_words = (k_left + WORD_BITS - 1) // WORD_BITS
  right_mask_words = (k_right + WORD_BITS - 1) // WORD_BITS
  return mask_words, left_mask_words, right_mask_words


def mode5_workspace_bytes(n, m, k, block_size):
  mark_words_total = 0
  data_items_total = 0
  level = [(n, k)]

  while True:
    internal = [(items, count) for items, count in level if items > 0 and count > 1]
    if not internal:
      break

    data_items_total += 2 * max(items for items, _ in internal)
    left_words_cap = 0
    right_words_cap = 0
    next_level = []

    for items, count in internal:
      k_left = count // 2
      k_right = count - k_left
      left_words = (k_left + WORD_BITS - 1) // WORD_BITS
      right_words = (k_right + WORD_BITS - 1) // WORD_BITS
      left_words_cap = max(left_words_cap, items * left_words)
      right_words_cap = max(right_words_cap, items * right_words)
      next_level.append((min(items, m * k_left), k_left))
      next_level.append((min(items, m * k_right), k_right))

    mark_words_total += left_words_cap + right_words_cap
    level = next_level

  return mark_words_total * SIZE_T_BYTES, data_items_total * block_size


def align_page(value):
  return int(math.ceil(float(value) / 4096.0) * 4096)


def write_heap_config(heap_memory, tcs_num=1):
  with open(ENCLAVE_CONFIG, "r") as config_file:
    lines = config_file.readlines()
  lines[4] = "  <HeapMaxSize>" + hex(int(heap_memory)) + "</HeapMaxSize>\n"
  lines[5] = "  <TCSNum>" + str(max(1, int(tcs_num))) + "</TCSNum>\n"
  with open(ENCLAVE_CONFIG, "w") as config_file:
    config_file.writelines(lines)


def estimate_heap(mode, n, block_size, sample_prob, k_value=None, threads=1):
  if sample_prob <= 0 or sample_prob > 1:
    print("Invalid sampling probability P (must satisfy 0 < P <= 1)")
    sys.exit(1)

  m = sample_size(n, sample_prob)
  heap_memory = (n * block_size) + (2 * n * 8) + BASE_HEAP
  tcs_num = 1

  if mode == 2:
    heap_memory += n * (block_size + 64)
    heap_memory += derived_k(n, m) * SIZE_T_BYTES
    heap_memory = int(math.ceil(heap_memory * 1.15))

  elif mode == 4:
    k = max(1, int(k_value if k_value is not None else int(1.0 / sample_prob)))
    mask_words, left_words, right_words = route_state_sizes(n, m, k)
    heap_memory += (
      n * mask_words * SIZE_T_BYTES +
      (m * k) * block_size +
      2 * n +
      2 * mask_words * SIZE_T_BYTES +
      2 * n * block_size +
      n * (left_words + right_words) * SIZE_T_BYTES +
      64 * 1024
    )
    heap_memory = int(math.ceil(heap_memory * 1.25))

  elif mode in (5, 6, 7):
    k = max(1, int(k_value if k_value is not None else int(1.0 / sample_prob)))
    mask_words, left_words, right_words = route_state_sizes(n, m, k)
    route_bytes = (node_num(n, m, k) + 7) // 8
    mark_bytes = n * mask_words * SIZE_T_BYTES
    workspace_mark_bytes = n * (left_words + right_words) * SIZE_T_BYTES
    selected_bytes = 2 * n
    mark_range_bytes = 2 * mask_words * SIZE_T_BYTES
    plain_result_bytes = (m * k) * block_size
    workspace_data_bytes = 3 * n * block_size

    if mode == 5:
      opt_mark_workspace_bytes, opt_data_workspace_bytes = mode5_workspace_bytes(n, m, k, block_size)
      phase1_peak = mark_bytes + route_bytes + opt_mark_workspace_bytes + selected_bytes + mark_range_bytes
      phase2_peak = route_bytes + plain_result_bytes + opt_data_workspace_bytes + selected_bytes
      heap_memory += max(phase1_peak, phase2_peak) + 64 * 1024
    elif mode == 6:
      heap_memory += (
        route_bytes +
        mark_bytes +
        selected_bytes +
        workspace_mark_bytes +
        workspace_data_bytes +
        plain_result_bytes +
        64 * 1024
      )
    else:
      parallel_threads = swo_effective_threads(n, m, k, threads)
      tcs_num = parallel_threads
      parallel_workspace_mark_bytes = workspace_mark_bytes
      parallel_workspace_data_bytes = (2 * n * block_size) * parallel_threads
      heap_memory += (
        route_bytes +
        mark_bytes +
        selected_bytes +
        parallel_workspace_mark_bytes +
        parallel_workspace_data_bytes +
        plain_result_bytes +
        64 * 1024
      )

    heap_memory = int(math.ceil(heap_memory * 1.25))
    rho = float(m * k) / float(n) if n > 0 else 1.0
    if rho > 1.0:
      heap_memory += int(math.ceil(((m * k) - n) * block_size * 1.8))

  else:
    heap_memory = int(math.ceil(heap_memory * 1.05))

  heap_memory = min(heap_memory, 28 * 1024 * 1024 * 1024)
  heap_memory = align_page(heap_memory)
  write_heap_config(heap_memory, tcs_num)
  return int(heap_memory)


def k_candidates_for(mode, sample_prob, k_values, k_select):
  if MODE_INFO[mode]["fixed_k"]:
    return [1]
  if MODE_INFO[mode]["needs_k"] and k_select == 2:
    return k_values
  return [max(1, int(1.0 / sample_prob))]


def build_command(mode, n, block_size, sample_prob, k_value, repeat, threads):
  cmd = ["./application", str(mode), str(n), str(block_size), str(sample_prob)]
  if MODE_INFO[mode]["needs_k"]:
    cmd.append(str(k_value))
  if mode == 7:
    cmd.append(str(threads))
  cmd.append(str(repeat))
  return cmd


def resolve_results_folder(raw_path):
  path = Path(raw_path)
  if path.is_absolute():
    return path
  return PROJECT_DIR / path


def parse_output(mode, output):
  lines = [line.strip() for line in output.splitlines() if line.strip()]
  expected = 5 if MODE_INFO[mode]["detailed"] else 3
  if len(lines) < expected:
    return None

  lines = lines[-expected:]
  try:
    if MODE_INFO[mode]["detailed"]:
      return (lines[0], lines[1], lines[2], lines[3], int(lines[4]))
    return (lines[0], lines[1], int(lines[2]))
  except ValueError:
    print("Line with value error is:")
    print(lines)
    return None


def make_result(mode, k_value, parsed, heap_mb):
  if MODE_INFO[mode]["detailed"]:
    ecall_time, ptime, gen_perm, apply_perm, oswaps = parsed
    return (k_value, ecall_time, ptime, gen_perm, apply_perm, oswaps, heap_mb)
  ecall_time, ptime, oswaps = parsed
  return (k_value, ecall_time, ptime, oswaps, heap_mb)


def format_csv_line(mode, block_size, sample_prob, n, result):
  if MODE_INFO[mode]["detailed"]:
    k_out, ecall_time, ptime, gen_perm, apply_perm, oswaps, heap_mb = result
    values = [block_size, sample_prob, n, k_out, ecall_time, ptime, gen_perm, apply_perm, oswaps, heap_mb]
  else:
    k_out, ecall_time, ptime, oswaps, heap_mb = result
    values = [block_size, sample_prob, n, k_out, ecall_time, ptime, oswaps, heap_mb]
  return ",".join(str(value) for value in values) + "\n"


def main():
  if SIZE_T_BYTES <= 0 or WORD_BITS <= 0:
    print("SIZE_T_BYTES must be > 0")
    return 1

  args = parse_args()
  modes = normalize_modes(parse_csv(args.modes, "modes", int))
  n_values = parse_csv(args.n, "n", int)
  p_values = parse_csv(args.p, "p", float)
  k_values = parse_csv(args.k, "k", int)
  block_sizes = parse_csv(args.block_sizes, "block-sizes", int)
  repeat = int(args.repeat)
  threads = int(args.threads)
  initialized_csv_files = set()

  if repeat <= 0:
    print("REPEAT must be > 0")
    return 1
  if threads <= 0:
    print("THREADS must be > 0")
    return 1
  for p_rate in p_values:
    if p_rate <= 0.0 or p_rate > 1.0:
      print("Each P must satisfy 0 < P <= 1")
      return 1

  results_folder = resolve_results_folder(args.results_folder)
  results_folder.mkdir(parents=True, exist_ok=True)

  for mode in modes:
    mode_name = MODE_INFO[mode]["name"]
    csv_file_name = results_folder / (mode_name + ".csv")

    for block_size in block_sizes:
      for p_rate in p_values:
        results = {}
        for k_value in k_candidates_for(mode, p_rate, k_values, int(args.k_select)):
          for n in n_values:
            m_value = sample_size(n, p_rate)
            heap_bytes = estimate_heap(
              mode,
              n,
              block_size,
              p_rate,
              k_value if MODE_INFO[mode]["needs_k"] else None,
              threads,
            )
            heap_mb = float(heap_bytes) / (1024.0 * 1024.0)

            subprocess.run(["make", "-C", str(PROJECT_DIR)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            cmd = build_command(mode, n, block_size, p_rate, k_value, repeat, threads)

            if mode == 7:
              effective_threads = swo_effective_threads(n, m_value, k_value, threads)
              print(
                "Running experiment: mode = %d (%s), b = %d, p = %.4f, n = %d, m = %d, k = %d, threads = %d, active_threads = %d, heap_est = %.2f MB"
                % (mode, mode_name, block_size, p_rate, n, m_value, k_value, threads, effective_threads, heap_mb)
              )
            else:
              print(
                "Running experiment: mode = %d (%s), b = %d, p = %.4f, n = %d, m = %d, k = %d, heap_est = %.2f MB"
                % (mode, mode_name, block_size, p_rate, n, m_value, k_value, heap_mb)
              )

            proc = subprocess.run(cmd, cwd=str(APP_DIR), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if proc.returncode != 0:
              print("Program exited with non-zero status:", proc.returncode)
              if proc.stderr:
                print("stderr:\n", proc.stderr.decode("utf-8", errors="ignore"))
              continue

            parsed = parse_output(mode, proc.stdout.decode("utf-8", errors="ignore"))
            if parsed is None:
              print("Receieved unexpected output, this experiment run has failed. ONE MUST DEBUG!")
              continue

            print("Out_lines: %s\n" % (parsed,))
            results[(n, k_value)] = make_result(mode, k_value, parsed, heap_mb)

        file_mode = "w" if args.overwrite and mode not in initialized_csv_files else "a"
        initialized_csv_files.add(mode)
        with open(csv_file_name, file_mode) as csv_file:
          for n, k_used in sorted(results.keys(), key=lambda item: (item[0], item[1])):
            csv_file.write(format_csv_line(mode, block_size, p_rate, n, results[(n, k_used)]))

  return 0


if __name__ == "__main__":
  raise SystemExit(main())
