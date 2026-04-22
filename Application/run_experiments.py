#!/usr/bin/python3

import subprocess
import os
import math
import sys
import argparse

###############################################################################

# CONFIGS to SET (可被命令行参数覆盖):

DEFAULT_RESULTS_FOLDER = "../RESULTS"

# SubSample modes only
# MODE = [31, 32, 33, 34, 35]
# N = [1000000, 2000000, 4000000, 8000000, 16000000, 32000000, 64000000, 128000000]
# N = [65536, 98304, 131072, 196608, 262144, 393216, 524288, 786432, 1048576, 1572864, 2097152, 3145728, 4194304, 6291456, 8388608, 12582912, 16777216, 25165824, 33554432]
# P = [0.0625, 0.0078125, 0.0009765625]  # 1/16, 1/128, 1/1024
# DEFAULT_K = [1,5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100]  # Only for mode 34
# DEFAULT_BLOCK_SIZE = [16, 24, 48, 72, 128, 168, 256, 344, 512, 680, 768, 1032, 1280, 1544, 2048, 2568, 3072, 3592, 3840, 4096]
#[25165824, 33554432, 50331648, 67108864, 100663296, 134217728, 201326592, 268435456]  


DEFAULT_MODE = [34]
DEFAULT_P = [0.0078125]  # 1/16, 1/128, 1/1024
DEFAULT_N = [1048576]  # 2^24 to 2^38
DEFAULT_K = [1024]  # Only for mode 34
DEFAULT_K_SELECT = 1  # 1: k = 1/p, 2: use --k / DEFAULT_K
DEFAULT_BLOCK_SIZE = [16,64,256,1024,4096]  # 16B to 4KB

DEFAULT_REPEAT = 3

def _parse_int_list(raw, arg_name):
  vals = []
  for part in str(raw).split(","):
    token = part.strip()
    if token == "":
      continue
    try:
      vals.append(int(token))
    except ValueError:
      print(f"Invalid integer in --{arg_name}: '{token}'")
      sys.exit(1)
  if len(vals) == 0:
    print(f"--{arg_name} must contain at least one integer")
    sys.exit(1)
  return vals

def _parse_float_list(raw, arg_name):
  vals = []
  for part in str(raw).split(","):
    token = part.strip()
    if token == "":
      continue
    try:
      vals.append(float(token))
    except ValueError:
      print(f"Invalid float in --{arg_name}: '{token}'")
      sys.exit(1)
  if len(vals) == 0:
    print(f"--{arg_name} must contain at least one float")
    sys.exit(1)
  return vals

def parse_args():
  parser = argparse.ArgumentParser(
    description="Run SubSample experiments and log metrics."
  )
  parser.add_argument("--modes", default=",".join(map(str, DEFAULT_MODE)), help="Comma-separated modes, e.g. 33,34")
  parser.add_argument("--n", default=",".join(map(str, DEFAULT_N)), help="Comma-separated N values")
  parser.add_argument("--p", default=",".join(map(str, DEFAULT_P)), help="Comma-separated P values (0 < P <= 1)")
  parser.add_argument("--k", default=",".join(map(str, DEFAULT_K)), help="Comma-separated K values (used by mode 34)")
  parser.add_argument("--k-select", type=int, choices=[1, 2], default=DEFAULT_K_SELECT,
                      help="Mode 34 K selection: 1 => k=1/p, 2 => use --k list")
  parser.add_argument("--block-sizes", default=",".join(map(str, DEFAULT_BLOCK_SIZE)), help="Comma-separated block sizes")
  parser.add_argument("--repeat", type=int, default=DEFAULT_REPEAT, help="Repeat count")
  parser.add_argument("--results-folder", default=DEFAULT_RESULTS_FOLDER, help="Results folder path")
  parser.add_argument("--overwrite", action="store_true", help="Overwrite target log file for each (mode, block, P)")
  return parser.parse_args()

def nodeNum(n, m, k):
    """
    递归计算二叉树中需要生成的布尔路由信息的总 bit 数
    完全对齐 C++ 中的 nodeNum 逻辑
    """
    if k <= 1 or n == 0:
        return 0
    k_left = k // 2
    k_right = k - k_left
    s_left = min(n, m * k_left)
    s_right = min(n, m * k_right)
    return (2 * n) + nodeNum(s_left, m, k_left) + nodeNum(s_right, m, k_right)

args = parse_args()

MODE = _parse_int_list(args.modes, "modes")
N = _parse_int_list(args.n, "n")
P = _parse_float_list(args.p, "p")
K_VALUES = _parse_int_list(args.k, "k")
K_SELECT = int(args.k_select)
BLOCK_SIZE = _parse_int_list(args.block_sizes, "block-sizes")
REPEAT = int(args.repeat)
RESULTS_FOLDER = args.results_folder
OVERWRITE = bool(args.overwrite)

INITIALIZED_MODE_LOGS = set()

if len(P) == 0:
  print("P must have at least one value")
  sys.exit(1)
for p in P:
  if p <= 0.0 or p > 1.0:
    print("Each P must satisfy 0 < P <= 1")
    sys.exit(1)
if REPEAT <= 0:
  print("REPEAT must be > 0")
  sys.exit(1)

###############################################################################

# SubSample modes:
'''
    31 = SubSampleShuffle
    32 = SubSampleSWO
    33 = SubSample
    34 = SubSampleMultiSlice
    35 = SubSampleMulti_opt
'''

###############################################################################

if os.path.exists(RESULTS_FOLDER) == False:
  os.mkdir(RESULTS_FOLDER)

BASE_HEAP = 1000000
SIZE_T_BYTES = int(os.getenv("SIZE_T_BYTES", "8"))
WORD_BITS = SIZE_T_BYTES * 8

if SIZE_T_BYTES <= 0:
  print("SIZE_T_BYTES must be > 0")
  sys.exit(1)

if WORD_BITS <= 0:
  print("WORD_BITS must be > 0")
  sys.exit(1)

def generateConfigFile(mode, n, block_size, sample_prob, k_value=None):
  if sample_prob <= 0 or sample_prob > 1:
    print("Invalid sampling probability P  (must satisfy 0 < P <= 1)")
    sys.exit(1)

  # Base heap: decrypted buffer + offset arrays + base overhead
  heap_memory = (n * block_size) + (2 * n * 8) + BASE_HEAP

  m = int(float(n) * float(sample_prob))
  if m <= 0:
    m = 1
  if m > n:
    m = n
  
  if mode == 31:
    pass

  elif mode == 32:
      k = int(n / m) if m > 0 else 1
      if k <= 0:
        k = 1
      
      # enc_block_size = block_size + SGX_AESGCM_IV_SIZE(12) + SGX_AESGCM_MAC_SIZE(16) = block_size + 28
      # enc_index_size = sizeof(size_t)(8) + SGX_AESGCM_IV_SIZE(12) + SGX_AESGCM_MAC_SIZE(16) = 36
      # tuple_size = enc_block_size + enc_index_size = block_size + 64
      tuple_size = block_size + 64
      
      heap_memory += (n * tuple_size)   # 算法主流程所需的中间状态数组 S
      heap_memory += (k * 8)            # 分组用的 counts 数组

  elif mode == 32:
    k = int(n / m) if m > 0 else 1
    if k <= 0:
      k = 1
    # Mode 32 致命的内存膨胀：
    # enc_block_size = block_size + IV(12) + MAC(16) = block_size + 28
    # enc_index_size = sizeof(size_t)(8) + IV(12) + MAC(16) = 36
    # tuple_size = block_size + 64
    tuple_size = block_size + 64
    heap_memory += (n * tuple_size)   # 庞大的中间状态数组 S
    heap_memory += (k * 8)            # counts 数组

  elif mode == 34:
    if k_value is None:
      k_value = int(1.0 / sample_prob)

    k_int = int(k_value)
    if k_int <= 0:
      k_int = 1

    mask_words = (k_int + WORD_BITS - 1) // WORD_BITS
    k_left = k_int // 2
    k_right = k_int - k_left
    left_mask_words = (k_left + WORD_BITS - 1) // WORD_BITS
    right_mask_words = (k_right + WORD_BITS - 1) // WORD_BITS

    # subSampleMulti 峰值阶段中，以下对象会同时存在：
    # mark_list + plain_result + left/right PairWorkspace + scratch buffers

    # 1) mark_list: vector<size_t>(N * mark_words)
    mark_list_bytes = n * mask_words * SIZE_T_BYTES

    # 2) plain_result: vector<unsigned char>(M*K*block_size)
    total_blocks = m * k_int
    plain_result_bytes = total_blocks * block_size

    # 3) 两套 selected scratch: AcquireSelectedScratchA/B
    selected_scratch_bytes = 2 * n

    # 4) 两套 mark_range scratch: AcquireMarkScratchA/B
    mark_range_scratch_bytes = 2 * mask_words * SIZE_T_BYTES

    # 5) 两个 PairWorkspace 的 data 缓冲峰值
    # left_ws/right_ws 都可能在顶层按 usable_len=N 扩容
    workspace_data_bytes = 2 * n * block_size

    # 6) 两个 PairWorkspace 的 mark 缓冲峰值
    workspace_mark_bytes = n * (left_mask_words + right_mask_words) * SIZE_T_BYTES

    # 7) 一点容器 / 元数据 / 对齐余量
    aux_bytes = (64 * 1024)

    peak_mode_34_bytes = (
      mark_list_bytes +
      plain_result_bytes +
      selected_scratch_bytes +
      mark_range_scratch_bytes +
      workspace_data_bytes +
      workspace_mark_bytes
    )

    heap_memory += peak_mode_34_bytes + aux_bytes
    
  elif mode == 35:
    if k_value is None:
      k_value = int(1.0 / sample_prob)

    k_int = int(k_value)
    if k_int <= 0:
      k_int = 1

    mask_words = (k_int + WORD_BITS - 1) // WORD_BITS
    k_left = k_int // 2
    k_right = k_int - k_left
    left_mask_words = (k_left + WORD_BITS - 1) // WORD_BITS
    right_mask_words = (k_right + WORD_BITS - 1) // WORD_BITS

    # 1. 计算极致压缩后的路由数组 B 的精准字节大小
    total_bits = nodeNum(n, m, k_int)
    B_bytes = (total_bits + 7) // 8

    # 2. 计算各个组件的内存占用
    mark_list_bytes = n * mask_words * SIZE_T_BYTES
    workspace_mark_bytes = n * (left_mask_words + right_mask_words) * SIZE_T_BYTES
    selected_scratch_bytes = 2 * n  # 临时 bool 数组
    mark_range_scratch_bytes = 2 * mask_words * SIZE_T_BYTES

    total_blocks = m * k_int
    plain_result_bytes = total_blocks * block_size
    
    # DataWorkspace 的峰值占用由顶层的左右子树决定
    # size_L = min(n, k_left * m)
    # size_R = min(n, k_right * m)
    # workspace_data_bytes = (size_L + size_R) * block_size
    workspace_data_bytes = 3 * n * block_size

    # --- Phase 1 峰值估算 ---
    # 此时：明文结果和 DataWorkspace 尚未分配；Mark 表达到最大占用
    phase1_peak = mark_list_bytes + B_bytes + workspace_mark_bytes + selected_scratch_bytes + mark_range_scratch_bytes

    # --- Phase 2 峰值估算 ---
    # 此时：mark_list 已经被 std::vector<size_t>().swap 释放！(-)
    # 此时：DataWorkspace 和 plain_result 被分配 (+)
    # (注：由于 left_mws 等在 C++ 中属于外层局部变量，workspace_mark 尚未析构，依然占位)
    phase2_peak = workspace_mark_bytes + B_bytes + plain_result_bytes + workspace_data_bytes + selected_scratch_bytes

    # 真正的峰值取两个阶段中的最大值
    peak_mode_35_bytes = max(phase1_peak, phase2_peak)

    aux_bytes = (64 * 1024)
    heap_memory += peak_mode_35_bytes + aux_bytes  
    
    
  if mode == 35:
    rho = float(m * k_int) / float(n) if n > 0 else 1.0
    # 基础安全系数
    heap_memory = int(math.ceil(heap_memory * 1.25))
    # 当总输出超过输入时，再额外补
    if rho > 1.0:
      excess_blocks = (m * k_int) - n
      excess_bytes = excess_blocks * block_size
      heap_memory += int(math.ceil(excess_bytes * 1.8)) 
  elif mode == 34:
    # mode 34 同时持有多个 vector/workspace，预留更稳妥的冗余
    heap_memory = int(math.ceil(heap_memory * 1.25))
  elif mode == 32:
    heap_memory = int(math.ceil(heap_memory * 1.15))
  else:
    heap_memory = int(math.ceil(heap_memory * 1.05))

  heap_memory = min(heap_memory, 28 * 1024 * 1024 * 1024)  # cap at 28 GiB

  # Align to 4KiB pages
  num_pages = int(heap_memory / 4096)
  if int(heap_memory) % 4096 != 0:
    heap_memory = (num_pages + 1) * 4096

  heap_memory_hex = hex(int(heap_memory))
  heap_line = "  <HeapMaxSize>" + str(heap_memory_hex) + "</HeapMaxSize>\n"

  config_file = open("../Enclave/Enclave.config.xml", "r")
  lines = config_file.readlines()
  config_file.close()
  lines[4] = heap_line
  config_file = open("../Enclave/Enclave.config.xml", "w")
  config_file.writelines(lines)
  config_file.close()

  return int(heap_memory)
# Configure Experiment Parameters:
for mode in MODE:
  log_file_n = RESULTS_FOLDER + "/" + str(mode) + ".lg"

  for b in BLOCK_SIZE:
    for p_rate in P:
      results = {}

      if mode == 34 or mode == 35:
        if K_SELECT == 1:
          k_candidates = [max(1, int(1.0 / p_rate))]
        else:
          k_candidates = K_VALUES
      elif mode == 31 or mode == 33:
        k_candidates = [1]
      elif mode == 32:
        k_candidates = [max(1, int(1.0 / p_rate))]
      else:
        k_candidates = [max(1, int(1.0 / p_rate))]

      for k_value in k_candidates:
        for n in N:
          m_value = int(float(n) * float(p_rate))
          if m_value <= 0:
            m_value = 1
          if m_value > n:
            m_value = n
          heap_bytes = generateConfigFile(mode, n, b, p_rate, k_value if (mode == 34 or mode == 35) else None)
          heap_mb = float(heap_bytes) / (1024.0 * 1024.0)

          # Build
          subprocess.run(["make", "-C", "../"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

          # Run
          if mode == 34 or mode == 35:
            cmd = ["./application", str(mode), str(n), str(b), str(p_rate), str(k_value), str(REPEAT)]
          else:
            cmd = ["./application", str(mode), str(n), str(b), str(p_rate), str(REPEAT)]

          print(
            "Running experiment: mode = %d, b = %d, p = %.4f, n = %d, m = %d, k = %d, heap_est = %.2f MB"
            % (mode, b, p_rate, n, m_value, k_value, heap_mb)
          )

          proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
          rc, out_b, err_b = proc.returncode, proc.stdout, proc.stderr
          if rc != 0:
            print("Program exited with non-zero status:", rc)
            if err_b:
              print("stderr:\n", err_b.decode("utf-8", errors="ignore"))
            continue

          out_lines = [line.strip() for line in out_b.decode("utf-8", errors="ignore").splitlines() if line.strip()]

          if ((mode == 34 or mode == 35) and len(out_lines) < 5) or ((mode != 34 and mode != 35) and len(out_lines) < 3):
            print("Receieved unexpected output, this experiment run has failed. ONE MUST DEBUG!")
            continue

          out_lines = out_lines[-5:] if (mode == 34 or mode == 35) else out_lines[-3:]
          flag_notFloat = False
          for i, line in enumerate(out_lines):
            try:
              if mode == 34 or mode == 35:
                if i == 4:
                  int(line)
                else:
                  float(line)
              else:
                if i == 2:
                  int(line)
                else:
                  float(line)
            except ValueError:
              print("Line with value error is:")
              print(line)
              flag_notFloat = True
              break

          if flag_notFloat:
            print("Receieved unexpected output, this experiment run has failed. ONE MUST DEBUG!")
            continue

          print(
            "Out_lines: %s\n"
            % (out_lines,)
          )

          res_key = (n, k_value)
          if mode == 34 or mode == 35:
            results[res_key] = (
              k_value,
              out_lines[0],
              out_lines[1],
              out_lines[2],
              out_lines[3],
              int(out_lines[4]),
              heap_mb,
            )
          else:
            results[res_key] = (
              k_value,
              out_lines[0],
              out_lines[1],
              int(out_lines[2]),
              heap_mb,
            )

      # Log results
      if OVERWRITE and mode not in INITIALIZED_MODE_LOGS:
        file_mode = "w"
        INITIALIZED_MODE_LOGS.add(mode)
      else:
        file_mode = "a"
      log_file = open(log_file_n, file_mode)
      sorted_keys = sorted(results.keys(), key=lambda x: (x[0], x[1]))
      for (n, k_used) in sorted_keys:
        if mode == 34 or mode == 35:
          k_out, ecall_time, ptime, gen_perm, apply_perm, oswaps, heap_mb_out = results[(n, k_used)]
          log_line = (
            str(b) + "," +
            str(p_rate) + "," +
            str(n) + "," +
            str(k_out) + "," +
            str(ecall_time) + "," +
            str(ptime) + "," +
            str(gen_perm) + "," +
            str(apply_perm) + "," +
            str(oswaps) + "," +
            str(heap_mb_out) + "\n"                 # heap_est (MB)
          )
        else:
          k_out, ecall_time, ptime, oswaps, heap_mb_out = results[(n, k_used)]
          log_line = (
            str(b) + "," +
            str(p_rate) + "," +
            str(n) + "," +
            str(k_out) + "," +
            str(ecall_time) + "," +
            str(ptime) + "," +
            str(oswaps) + "," +
            str(heap_mb_out) + "\n"                 # heap_est (MB)
          )
        log_file.write(log_line)
      log_file.close()