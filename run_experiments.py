#!/usr/bin/python3

import argparse
from functools import lru_cache
import math
import os
import shutil
import subprocess
import sys
import tempfile
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


DEFAULT_MODE = [9,2,6,8]
DEFAULT_P = [0.015625]
DEFAULT_N = [1048576]
DEFAULT_K = [256]
DEFAULT_K_SELECT = 2   # 1 => k = 1/p, 2 => use --k list
DEFAULT_BLOCK_SIZE = [16]
DEFAULT_REPEAT = 5
DEFAULT_WARMUP = 1
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
  8: {"name": "OFRSupple", "needs_k": True, "detailed": True, "fixed_k": False},
  9: {"name": "ShuffleBasedSWO", "needs_k": True, "detailed": True, "fixed_k": False},
}

CSV_HEADER_MODES = {2, 6, 8, 9}
CSV_COMMON_FIELDS = ("block_size", "p", "n", "k", "ecall_time_ms", "ptime_ms")
CSV_DETAILED_FIELDS = ("gen_perm_offline_ms", "apply_perm_online_ms")
CSV_TRAILING_FIELDS = ("oswaps", "heap_est_mb")


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
  parser.add_argument("--k", default=",".join(map(str, DEFAULT_K)), help="Comma-separated K values (used by modes 4/5/6/7/8/9)")
  parser.add_argument("--k-select", type=int, choices=[1, 2], default=DEFAULT_K_SELECT,
                      help="Modes 4/5/6/7/8/9 K selection: 1 => k=1/p, 2 => use --k list")
  parser.add_argument("--block-sizes", default=",".join(map(str, DEFAULT_BLOCK_SIZE)), help="Comma-separated block sizes")
  parser.add_argument(
    "--repeat", type=int, default=DEFAULT_REPEAT,
    help="Number of measured rounds included in the average",
  )
  parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP,
                      help="Number of warm-up rounds excluded from the average")
  parser.add_argument("--threads", type=int, default=DEFAULT_THREADS, help="Thread count for mode 7")
  parser.add_argument("--results-folder", default=DEFAULT_RESULTS_FOLDER, help="Results folder path")
  parser.add_argument("--overwrite", action="store_true", help="Overwrite each mode CSV on first write")
  offline_group = parser.add_mutually_exclusive_group()
  offline_group.add_argument("--offline-profile", dest="offline_profile",
                             action="store_true",
                             help="Collect offline phase timings and enclave heap high-water marks for modes 6 and 8 (adds timing overhead)")
  offline_group.add_argument("--no-offline-profile", dest="offline_profile",
                             action="store_false",
                             help="Skip offline profiling for modes 6 and 8")
  parser.set_defaults(offline_profile=False)
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


@lru_cache(maxsize=None)
def ofr_control_words(n, n_left, n_right):
  """Mirror OFRControlCount: one byte is stored for each two-bit word."""
  if n_left == 0 or n_right == 0:
    return 0
  if n == 2:
    return 1
  top_n = (n + 1) // 2
  top_left = (n_left + 1) // 2
  top_right = top_n - top_left
  bottom_left = n_left - top_left
  bottom_right = n_right - top_right
  return (n // 2 +
          ofr_control_words(top_n, top_left, top_right) +
          ofr_control_words(n // 2, bottom_left, bottom_right))


def ofrsupple_frontier(n, m, k):
  nodes = [(0, k)]
  v = 0
  while v < len(nodes):
    start, count = nodes[v]
    if count == 1 or m * count <= n:
      v += 1
    else:
      left = count // 2
      nodes[v:v + 1] = [(start, left), (start + left, count - left)]
  return tuple(nodes)


def ofrsupple_control_counts(n, m, k):
  """Mirror the two control counts in OFRSuppleControlCount."""
  frontier = ofrsupple_frontier(n, m, k) if m * k > n else ()

  @lru_cache(maxsize=None)
  def count_node(items, samples, nodes):
    if samples == 1:
      return (items if items > m else 0, 0)
    left_k = samples // 2
    right_k = samples - left_k
    if items == m * samples:
      left_n = m * left_k
      right_n = items - left_n
      left = count_node(left_n, left_k, ())
      right = count_node(right_n, right_k, ())
      return (left[0] + right[0],
              ofr_control_words(items, left_n, right_n) + left[1] + right[1])

    children = nodes or ((0, left_k), (left_k, right_k))
    swo_bits = ofr_words = 0
    for _, child_k in children:
      child_n = min(items, m * child_k)
      child = count_node(child_n, child_k, ())
      swo_bits += (items if child_n < items else 0) + child[0]
      ofr_words += child[1]
    return (swo_bits, ofr_words)

  return count_node(n, k, frontier)


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

  elif mode == 9:
    k = max(1, int(k_value if k_value is not None else int(1.0 / sample_prob)))
    # One decrypted dataset, K plaintext samples, and shuffle selection scratch.
    heap_memory += (m * k * block_size) + n + 64 * 1024
    heap_memory = int(math.ceil(heap_memory * 1.25))

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

  elif mode == 8:
    k = max(1, int(k_value if k_value is not None else int(1.0 / sample_prob)))
    mask_words = (k + WORD_BITS - 1) // WORD_BITS
    swo_bits, ofr_words = ofrsupple_control_counts(n, m, k)
    # Prepared nodes reference the single OFR stream. Conversion needs a
    # temporary output tape and one size_t start offset per row of the
    # largest balanced node; both are freed before online execution.
    control_bytes = (swo_bits + 7) // 8 + ofr_words
    shape_bytes = 64 * k
    mark_bytes = n * mask_words * SIZE_T_BYTES
    result_bytes = m * k * block_size
    prepare_bytes = (n // 2) * (n.bit_length() - 1) + n * SIZE_T_BYTES
    # Offline routing keeps membership workspaces for active OFR ancestors.
    offline_peak = (control_bytes + shape_bytes + mark_bytes +
                    8 * mark_bytes + 2 * n + prepare_bytes)
    # Online routing keeps child data and output arrays across recursion.
    online_peak = control_bytes + shape_bytes + result_bytes + 6 * n * block_size + 2 * n
    heap_memory += max(offline_peak, online_peak) + 64 * 1024
    heap_memory = int(math.ceil(heap_memory * 1.35))

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


def build_command(mode, n, block_size, sample_prob, k_value, repeat, threads, warmup):
  cmd = ["./application", str(mode), str(n), str(block_size), str(sample_prob)]
  if MODE_INFO[mode]["needs_k"]:
    cmd.append(str(k_value))
  if mode == 7:
    cmd.append(str(threads))
  cmd.append(str(repeat))
  cmd.append(str(warmup))
  return cmd


def resolve_results_folder(raw_path):
  path = Path(raw_path)
  if path.is_absolute():
    return path
  return PROJECT_DIR / path


def parse_output(mode, output):
  lines = [line.strip() for line in output.splitlines()
           if line.strip() and not line.startswith(("PROFILE,", "OFFLINE_PROFILE,"))]
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


OFFLINE_PROFILE_FIELDS = (
    "mark_ms", "count_ms", "swo_write_ms", "tags_ms", "normalize_ms",
    "ofr_write_ms", "replay_ms", "project_ms", "prepare_ms", "other_ms",
    "offline_heap_peak_bytes", "total_heap_peak_bytes",
)


def parse_offline_profile(output):
  lines = [line for line in output.splitlines()
           if line.startswith("OFFLINE_PROFILE,")]
  if len(lines) != 1:
    return None
  values = lines[0].split(",")[1:]
  if len(values) != len(OFFLINE_PROFILE_FIELDS):
    return None
  try:
    return tuple(float(x) for x in values[:10]) + tuple(int(x) for x in values[10:])
  except ValueError:
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


def result_csv_header(mode):
  fields = CSV_COMMON_FIELDS
  if MODE_INFO[mode]["detailed"]:
    fields += CSV_DETAILED_FIELDS
  return ",".join(fields + CSV_TRAILING_FIELDS) + "\n"


def prepare_result_csv(path, mode, overwrite):
  """Start one run for a headered CSV, preserving older headerless results."""
  header = result_csv_header(mode).encode("utf-8")
  if overwrite or not path.is_file() or path.stat().st_size == 0:
    path.write_bytes(header)
    return

  with path.open("rb") as existing:
    first_line = existing.readline()
  if first_line.rstrip(b"\r\n") != header.rstrip(b"\n"):
    first_fields = first_line.rstrip(b"\r\n").split(b",")
    if len(first_fields) != len(header.rstrip(b"\n").split(b",")) or not first_fields[0].isdigit():
      raise ValueError(f"CSV header does not match mode {mode}: {path}")
    # Older result files were headerless. Add the header without discarding data.
    temp_path = None
    try:
      with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as updated:
        temp_path = Path(updated.name)
        updated.write(header)
        with path.open("rb") as existing:
          shutil.copyfileobj(existing, updated)
      shutil.copymode(path, temp_path)
      os.replace(temp_path, path)
    finally:
      if temp_path is not None:
        temp_path.unlink(missing_ok=True)

  with path.open("rb") as existing:
    existing.readline()
    has_rows = any(line.strip() for line in existing)
    if not has_rows:
      return
    existing.seek(0, os.SEEK_END)
    existing.seek(max(0, existing.tell() - 4))
    tail = existing.read()
  if tail.endswith((b"\n\n", b"\r\n\r\n")):
    return
  separator = b"\r\n" if tail.endswith(b"\r\n") else b"\n" if tail.endswith(b"\n") else b"\n\n"
  with path.open("ab") as output:
    output.write(separator)


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
  warmup = int(args.warmup)
  threads = int(args.threads)
  initialized_csv_files = set()

  if repeat <= 0:
    print("REPEAT must be > 0")
    return 1
  if warmup < 0:
    print("WARMUP must be >= 0")
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
        offline_results = {}
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

            build = subprocess.run(["make", "-C", str(PROJECT_DIR)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if build.returncode != 0:
              print("Build failed before experiment:", build.stderr.decode("utf-8", errors="ignore"))
              return 1
            cmd = build_command(mode, n, block_size, p_rate, k_value, repeat, threads, warmup)

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

            env = os.environ.copy()
            # Keep normal totals free of per-phase clock OCALLs, even if this
            # shell was used for a profiling run earlier.
            env.pop("ONLINE_PROFILE", None)
            env.pop("OFFLINE_PROFILE", None)
            proc = subprocess.run(cmd, cwd=str(APP_DIR), env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if proc.returncode != 0:
              print("Program exited with non-zero status:", proc.returncode)
              if proc.stderr:
                print("stderr:\n", proc.stderr.decode("utf-8", errors="ignore"))
              continue

            output = proc.stdout.decode("utf-8", errors="ignore")
            parsed = parse_output(mode, output)
            if parsed is None:
              print("Receieved unexpected output, this experiment run has failed. ONE MUST DEBUG!")
              continue

            print("Out_lines: %s\n" % (parsed,))
            results[(n, k_value)] = make_result(mode, k_value, parsed, heap_mb)

            if args.offline_profile and mode in (6, 8):
              # Run diagnostics separately so their clock OCALLs never enter
              # the normal CSV's offline or online totals.
              profile_env = env.copy()
              profile_env["OFFLINE_PROFILE"] = "1"
              profile_proc = subprocess.run(
                cmd, cwd=str(APP_DIR), env=profile_env,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
              if profile_proc.returncode != 0:
                print("Offline profile run exited with non-zero status:",
                      profile_proc.returncode)
                if profile_proc.stderr:
                  print("stderr:\n", profile_proc.stderr.decode("utf-8", errors="ignore"))
              else:
                offline_profile = parse_offline_profile(
                  profile_proc.stdout.decode("utf-8", errors="ignore"))
                if offline_profile is None:
                  print("Offline profile output is missing or invalid")
                else:
                  offline_results[(n, k_value)] = (heap_mb, offline_profile)

        if results:
          if mode in CSV_HEADER_MODES:
            if mode not in initialized_csv_files:
              try:
                prepare_result_csv(csv_file_name, mode, args.overwrite)
              except ValueError as exc:
                print(exc, file=sys.stderr)
                return 1
            file_mode = "a"
          else:
            file_mode = "w" if args.overwrite and mode not in initialized_csv_files else "a"
          initialized_csv_files.add(mode)
          with open(csv_file_name, file_mode) as csv_file:
            for n, k_used in sorted(results.keys(), key=lambda item: (item[0], item[1])):
              csv_file.write(format_csv_line(mode, block_size, p_rate, n, results[(n, k_used)]))
        if args.offline_profile and mode in (6, 8):
          profile_file = results_folder / (mode_name + "_offline_profile.csv")
          profile_mode = "w" if args.overwrite and profile_file not in initialized_csv_files else "a"
          initialized_csv_files.add(profile_file)
          with open(profile_file, profile_mode) as csv_file:
            if profile_mode == "w" or profile_file.stat().st_size == 0:
              csv_file.write(",".join(("block_size", "p", "n", "k", "heap_est_mb") +
                                      OFFLINE_PROFILE_FIELDS) + "\n")
            for n, k_used in sorted(offline_results):
              heap_mb, phases = offline_results[(n, k_used)]
              csv_file.write(",".join(map(str, (block_size, p_rate, n, k_used,
                                                heap_mb) + phases)) + "\n")

  return 0


if __name__ == "__main__":
  raise SystemExit(main())
