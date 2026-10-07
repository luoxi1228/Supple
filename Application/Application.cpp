#include <stdexcept>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <limits>
#include <openssl/evp.h>
#include <openssl/err.h>
#include "../Globals.hpp"
#include "../Untrusted/OLib.hpp"
#include "../Untrusted/SS.hpp"
#include "../CONFIG.h"
#include "gcm.h"

#define NUM_ARGUMENTS_REQUIRED 5
// IV = 12 bytes, TAG = 16 bytes for AES-GCM
// So data component needs to be at least >28 bytes large to do 0-encrypted integrity check
// We use 40 as it is the next fit for the block sizes supported by our library.
#define DATA_ENCRYPT_MIN_SIZE 40

// Global parameters that are to be supplied by the script that runs this application:
uint8_t MODE;
size_t N;
double SAMPLE_PROB = 0.0;
size_t M;
size_t K;
size_t BLOCK_SIZE;
size_t REPEAT;
size_t WARMUP = 1;
size_t TOTAL_ROUNDS;

uint64_t NUM_ZERO_BYTES;
unsigned char *zeroes = nullptr;

// average calculation
double calculateAve(const double *input, size_t N) {
  double total = 0;
  for (size_t i = 0; i < N; i++) {
    total += input[i];
  }
  return (total / N);
}

void parseCommandLineArguments(int argc, char *argv[]) {
  if (argc < (NUM_ARGUMENTS_REQUIRED + 1) ||
      argc > (NUM_ARGUMENTS_REQUIRED + 3)) {
    printf("Did NOT receive the right number of command line arguments.\n"
          "Usage: ./application <2|10|11> <N> <BLOCK_SIZE> <P> <REPEAT> [WARMUP]\n"
          "   or: ./application <1|3|4|5|6|12|13> <N> <BLOCK_SIZE> <P> <K> <REPEAT> [WARMUP]\n\n"
          "Oblivious SubSampling modes:\n"
          "  (1) ShuffleBasedSWO (shuffle and truncate K times)\n"
          "  (2) PSQF_SWO (Algorithm 1)\n"
          "  (3) CompactionBasedSWO (mark, compact and truncate K times)\n"
          "  (4) FFOS_C\n"
          "  (5) FFOS-FR (per-gate OFork)\n"
          "  (6) FFOS-FR-Opt (four-gate SSE2 for 8/16-byte records)\n"
          "  (10) PSQF_single (shuffle all then take first N*P)\n"
          "  (11) SubSample (randomly select N*P items, then compact)\n"
          "  (12) SubSampleMulti\n"
          "  (13) SubSampleMulti_opt\n");
    exit(argc > 1 ? 1 : 0);
  }

  MODE = atoi(argv[1]);
  if (!((MODE >= 1 && MODE <= 6) || (MODE >= 10 && MODE <= 13))) {
    printf("MODE must be one of 1,2,3,4,5,6,10,11,12,13.\n");
    exit(1);
  }

  const bool needs_k = MODE == 1 || MODE == 3 || MODE == 4 ||
                       MODE == 5 || MODE == 6 || MODE == 12 || MODE == 13;
  const int expected_argc = NUM_ARGUMENTS_REQUIRED + 1 +
                            (needs_k ? 1 : 0);
  if (argc != expected_argc && argc != expected_argc + 1) {
    printf("Incorrect argument count for MODE %u.\n", MODE);
    exit(1);
  }

  N = atoi(argv[2]);
  BLOCK_SIZE = atoi(argv[3]);
  SAMPLE_PROB = atof(argv[4]);
  if (SAMPLE_PROB <= 0.0 || SAMPLE_PROB > 1.0) {
    printf("P must satisfy 0 < P <= 1\n");
    exit(0);
  }
  M = (size_t)((double)N * SAMPLE_PROB);
  if (M == 0) {
    M = 1;
  }
  int requested_repeat = 0;
  if (needs_k) {
    K = atoi(argv[5]);
    requested_repeat = atoi(argv[6]);
  } else {
    K = 0;
    requested_repeat = atoi(argv[5]);
  }
  if (requested_repeat <= 0) {
    printf("REPEAT must be >= 1\n");
    exit(1);
  }
  REPEAT = static_cast<size_t>(requested_repeat);
  if (argc == expected_argc + 1) {
    char *end = nullptr;
    errno = 0;
    const long requested_warmup = strtol(argv[expected_argc], &end, 10);
    if (errno == ERANGE || end == argv[expected_argc] || *end != '\0' ||
        requested_warmup < 0) {
      printf("WARMUP must be a nonnegative integer\n");
      exit(1);
    }
    WARMUP = static_cast<size_t>(requested_warmup);
  }
  if (WARMUP > std::numeric_limits<size_t>::max() - REPEAT) {
    printf("WARMUP + REPEAT is too large\n");
    exit(1);
  }
  TOTAL_ROUNDS = WARMUP + REPEAT;
  if ((MODE == 1 || MODE == 3 || MODE == 4 || MODE == 5 || MODE == 6 || MODE == 12 || MODE == 13) && K == 0) {
    printf("MODE 1/3/4/5/6/12/13 expects K > 0\n");
    exit(1);
  }
}

// Return real times in microseconds
uint64_t rtclock() {
  static time_t secstart = 0;
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  if (secstart == 0) {
    secstart = tp.tv_sec;
  }
  return (tp.tv_sec - secstart) * 1000000 + tp.tv_nsec / 1000;
}

int main(int argc, char *argv[]) {
  uint64_t process_start, process_stop;
  double ecall_time;

  bool verbose_phases = !!getenv("VERBOSE_PHASES");
  const bool profile_online = !!getenv("ONLINE_PROFILE");
  const bool profile_offline = !!getenv("OFFLINE_PROFILE");
  uint64_t phase_start, phase_end;
  double phase_time;


  // 1. Parse command line arguments
  phase_start = rtclock();

  parseCommandLineArguments(argc, argv);
  size_t SampleSize = M;
  if (SampleSize > N) {
    SampleSize = N;
  }

  phase_end = rtclock();
  phase_time = (double)(phase_end - phase_start) / 1000.0;
  if (verbose_phases) {
    printf("parseCommandLineArguments phase: %f\n", phase_time);
  }


  // 2. Initialize libcrypto
  phase_start = phase_end;

  double ecallTime_array[TOTAL_ROUNDS] = {};
  double ptime_array[TOTAL_ROUNDS] = {};
  double gen_perm_time_array[TOTAL_ROUNDS] = {};
  double apply_perm_time_array[TOTAL_ROUNDS] = {};
  double online_route_array[TOTAL_ROUNDS] = {};
  double online_reorder_array[TOTAL_ROUNDS] = {};
  double online_copy_array[TOTAL_ROUNDS] = {};
  double online_shuffle_array[TOTAL_ROUNDS] = {};
  double offline_phase_array[9][TOTAL_ROUNDS] = {};
  size_t offline_heap_peak_bytes = 0;
  size_t total_heap_peak_bytes = 0;
  size_t algorithm_heap_peak_bytes = 0;
  const bool profile_memory = MODE >= 1 && MODE <= 6;
  size_t num_oswaps[TOTAL_ROUNDS] = {};

  OpenSSL_add_all_algorithms();   // Initialize libcrypto
  ERR_load_crypto_strings();

  phase_end = rtclock();
  phase_time = (double)(phase_end - phase_start) / 1000.0;
  if (verbose_phases) {
    printf("initalize libcrypto phase: %f\n", phase_time);
  }


  // 3. Open /dev/urandom for randomnesså
  phase_start = phase_end;

  int randfd = open("/dev/urandom", O_RDONLY);
  if (randfd < 0) {
    throw std::runtime_error("Cannot open /dev/urandom");
  }


  if (!OLib_initialize()) {
    return 2;
  }

  phase_end = rtclock();
  phase_time = (double)(phase_end - phase_start) / 1000.0;
  if (verbose_phases) {
    printf("OLib_initialize phase: %f\n", phase_time);
  }


  // 4. Load test AES keys into the enclave
  phase_start = phase_end;

  // Load test AES keys into the enclave
  unsigned char inkey[16];
  unsigned char outkey[16];
  unsigned char datakey[16];
  read(randfd, inkey, 16);
  read(randfd, outkey, 16);
  read(randfd, datakey, 16);
  Enclave_loadTestKeys(inkey, outkey);

  const size_t ENC_BLOCK_SIZE = 12 + BLOCK_SIZE + 16;

  phase_end = rtclock();
  phase_time = (double)(phase_end - phase_start) / 1000.0;
  if (verbose_phases) {
    printf("loadtestkeys phase: %f\n", phase_time);
  }


  // 5. Create buffer of items to shuffle
  phase_start = phase_end;

  // Create buffer of items to shuffle
  size_t output_blocks = N;
  if (MODE == 1 || MODE == 3 || MODE == 4 || MODE == 5 || MODE == 6 || MODE == 12 || MODE == 13) {
    output_blocks = SampleSize * K;
  }
  size_t total_blocks = (output_blocks > N) ? output_blocks : N;
  unsigned char *buf;
  size_t buflen = total_blocks * ENC_BLOCK_SIZE;
  buf = new unsigned char[buflen];

  if (buf == NULL) {
    printf("Allocating buffer memories in script Application failed!\n");
  }

  unsigned char *bufend = buf + (N * ENC_BLOCK_SIZE);
  phase_end = rtclock();
  phase_time = (double)(phase_end - phase_start) / 1000.0;
  if (verbose_phases) {
    printf("selected_list phase: %f\n", phase_time);
  }


  // 6. Run WARMUP unmeasured rounds, then REPEAT measured rounds.
  phase_start = phase_end;

  for (size_t r = 0; r < TOTAL_ROUNDS; r++) {
    size_t inc_ctr = 0;
    unsigned char iv[12];
    read(randfd, iv, 12);
    for (unsigned char *enc_block_ptr = buf; enc_block_ptr < bufend;
         enc_block_ptr += ENC_BLOCK_SIZE) {
      unsigned char block[BLOCK_SIZE] = {}; // Initializes to zero

      uint64_t rnd;
#ifdef RANDOMIZE_INPUTS
      read(randfd, (unsigned char *)&rnd, sizeof(rnd));
      // For easier visual debugging:
      rnd = rnd % N;
#else
      rnd = inc_ctr++;
#endif

#ifdef SHOW_INPUT_KEYS
      printf("%ld, ", rnd);
#endif

      memcpy(block, (unsigned char *)&rnd, sizeof(rnd));

      if (BLOCK_SIZE >= DATA_ENCRYPT_MIN_SIZE) {
        // NUM_ZERO_BYTES = the number of zero bytes that get encrypted
        // i.e. BLOCK_SIZE - 8 (key bytes) - 12 (IV bytes) - 16 (TAG bytes)
        NUM_ZERO_BYTES = BLOCK_SIZE - 8 - 12 - 16;
        zeroes = new unsigned char[NUM_ZERO_BYTES]();

        (*((uint64_t *)iv))++;
        memmove(block + 8, iv, 12);

        if ((NUM_ZERO_BYTES) != gcm_encrypt(zeroes, NUM_ZERO_BYTES, NULL, 0, datakey, block + 8,
                12, block + 8 + 12, block + 8 + 12 + NUM_ZERO_BYTES)) {
          printf("Encryption failed\n");
          break;
        }
      }

      // Encrypt the chunk to the enclave
      (*((uint64_t *)iv))++;
      memmove(enc_block_ptr, iv, 12);
      if (BLOCK_SIZE != gcm_encrypt(block, BLOCK_SIZE, NULL, 0, inkey, enc_block_ptr, 12,
              enc_block_ptr + 12, enc_block_ptr + 12 + BLOCK_SIZE)) {
        printf("Encryption failed\n");
        break;
      }
    }
#ifdef SHOW_INPUT_KEYS
    printf("\n");
#endif

    phase_end = rtclock();
    phase_time = (double)(phase_end - phase_start) / 1000.0;
    if (verbose_phases) {
      printf("preparation phase: %f\n", phase_time);
    }
    phase_start = phase_end;

    process_start = rtclock();

    enc_ret ret{};
    ret.collect_online_profile = profile_online && (MODE == 4 || MODE == 5 || MODE == 6);
    ret.collect_offline_profile = profile_offline && (MODE == 4 || MODE == 5 || MODE == 6);
    ret.collect_memory_profile = profile_memory;
    switch (MODE) {
      case 1:
        DecShuffleBasedSWO(buf, N, SampleSize, K, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
        gen_perm_time_array[r] = ret.gen_perm_time;
        apply_perm_time_array[r] = ret.apply_perm_time;
#ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
#else
        num_oswaps[r] = 0;
#endif
        break;
      case 2:
        DecPSQF_SWO(buf, N, SampleSize, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
#ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
#else
        num_oswaps[r] = 0;
#endif
        break;
      case 3:
        DecCompactionBasedSWO(buf, N, SampleSize, K, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
        gen_perm_time_array[r] = ret.gen_perm_time;
        apply_perm_time_array[r] = ret.apply_perm_time;
#ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
#else
        num_oswaps[r] = 0;
#endif
        break;
      case 4:
        DecFFOS_C(buf, N, SampleSize, K, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
        gen_perm_time_array[r] = ret.gen_perm_time;
        apply_perm_time_array[r] = ret.apply_perm_time;
        online_route_array[r] = ret.online_route_ms;
        online_reorder_array[r] = ret.online_reorder_ms;
        online_copy_array[r] = ret.online_copy_ms;
        online_shuffle_array[r] = ret.online_shuffle_ms;
      #ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
      #else
        num_oswaps[r] = 0;
      #endif
        break;
      case 5:
      case 6:
        if (MODE == 5)
          DecFFOS_FR(buf, N, SampleSize, K, ENC_BLOCK_SIZE, buf, &ret);
        else
          DecFFOS_FR_Opt(buf, N, SampleSize, K, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
        gen_perm_time_array[r] = ret.gen_perm_time;
        apply_perm_time_array[r] = ret.apply_perm_time;
        online_route_array[r] = ret.online_route_ms;
        online_reorder_array[r] = ret.online_reorder_ms;
        online_copy_array[r] = ret.online_copy_ms;
        online_shuffle_array[r] = ret.online_shuffle_ms;
      #ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
      #else
        num_oswaps[r] = 0;
      #endif
        break;
      case 10:
        DecPSQF_single(buf, N, SampleSize, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
#ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
#else
        num_oswaps[r] = 0;
#endif
        break;
      case 11:
        decryptAndSubSample(buf, N, SampleSize, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
#ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
#else
        num_oswaps[r] = 0;
#endif
        break;
      case 12:
        decryptAndSubSampleMulti(buf, N, SampleSize, K, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
        gen_perm_time_array[r] = ret.gen_perm_time;
        apply_perm_time_array[r] = ret.apply_perm_time;
      #ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
      #else
        num_oswaps[r] = 0;
      #endif
        break;
      case 13:
        decryptAndSubSampleMulti_opt(buf, N, SampleSize, K, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
        gen_perm_time_array[r] = ret.gen_perm_time;
        apply_perm_time_array[r] = ret.apply_perm_time;
      #ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
      #else
        num_oswaps[r] = 0;
      #endif
        break;
    }
    process_stop = rtclock();

    if (profile_memory) {
      if (ret.memory_profile_status != MEMORY_PROFILE_VALID) {
        fprintf(stderr, "Algorithm heap measurement failed in round %zu (mode %u)\n", r, MODE);
        return 1;
      }
      if (r >= WARMUP)
        algorithm_heap_peak_bytes = std::max(algorithm_heap_peak_bytes,
                                            ret.algorithm_heap_peak_bytes);
    }

    if (ret.collect_offline_profile) {
      const double phases[9] = {
          ret.offline_mark_ms, ret.offline_count_ms,
          ret.offline_swo_write_ms, ret.offline_tags_ms,
          ret.offline_normalize_ms, ret.offline_ofr_write_ms,
          ret.offline_replay_ms, ret.offline_project_ms,
          ret.offline_prepare_ms};
      for (size_t phase = 0; phase < 9; ++phase)
        offline_phase_array[phase][r] = phases[phase];
      // The SGX counter is cumulative. Only the first round's offline
      // checkpoint can separate offline growth from later online growth.
      if (r == 0)
        offline_heap_peak_bytes = ret.offline_heap_peak_bytes;
      total_heap_peak_bytes = std::max(total_heap_peak_bytes,
                                       ret.total_heap_peak_bytes);
    }

    ecall_time = double(process_stop - process_start) / 1000.0;
    ecallTime_array[r] = ecall_time;

    phase_end = rtclock();
    phase_time = (double)(phase_end - phase_start) / 1000.0;
    if (verbose_phases) {
      printf("processing phase: %f\n", phase_time);
    }
    phase_start = phase_end;

    bool dec_fail_flag = false;
    int numfailed = 0;
    int cnum = 0;

    size_t output_blocks = N;
    if (MODE == 10 || MODE == 11) {
      output_blocks = SampleSize;
    } else if (MODE == 1 || MODE == 3 || MODE == 4 || MODE == 5 || MODE == 6 || MODE == 12 || MODE == 13) {
      output_blocks = SampleSize * K;
    }
    unsigned char *decrypted_result_buf_ptr = buf;
    unsigned char *result_bufend = buf + (output_blocks * ENC_BLOCK_SIZE);
    for (unsigned char *enc_block_ptr = buf; enc_block_ptr < result_bufend; enc_block_ptr += ENC_BLOCK_SIZE) {
      unsigned char block[BLOCK_SIZE];
      ++cnum;
      if (BLOCK_SIZE != gcm_decrypt(enc_block_ptr + 12, BLOCK_SIZE, NULL, 0,
              enc_block_ptr + 12 + BLOCK_SIZE, outkey, enc_block_ptr, 12, block)) {
        printf("Outer Decryption failed %d/%d\n", ++numfailed, cnum);
        dec_fail_flag = true;
        break;
      }

      if (BLOCK_SIZE >= DATA_ENCRYPT_MIN_SIZE) {
        // Check correctness of data_payload
        unsigned char should_be_zeroes[NUM_ZERO_BYTES] = {};
        int returned_pt_bytes = gcm_decrypt(block + 8 + 12, NUM_ZERO_BYTES, NULL, 0,
            block + 8 + 12 + NUM_ZERO_BYTES, datakey, block + 8, 12, should_be_zeroes);
        if (NUM_ZERO_BYTES != returned_pt_bytes) {
          printf("Data block decryption failed, returned_pt_bytes = %d\n", returned_pt_bytes);
          dec_fail_flag = true;
          break;
        }
      }

      memcpy(decrypted_result_buf_ptr, block, BLOCK_SIZE);
      decrypted_result_buf_ptr += BLOCK_SIZE;
    }
    if (dec_fail_flag) {
      exit(1);
    }

    phase_end = rtclock();
    phase_time = (double)(phase_end - phase_start) / 1000.0;
    if (verbose_phases) {
      printf("check phase: %f\n", phase_time);
    }
    phase_start = phase_end;
  }

  // Exclude the configured warm-up rounds from reported time averages.
  double ecallTime_average = calculateAve(ecallTime_array + WARMUP, REPEAT);
  double ptime_average = calculateAve(ptime_array + WARMUP, REPEAT);

  printf("%f\n", ecallTime_average);
  printf("%f\n", ptime_average);

  if (MODE == 1 || MODE == 3 || MODE == 4 || MODE == 5 || MODE == 6 || MODE == 12 || MODE == 13) {
    double gen_perm_time_average = calculateAve(gen_perm_time_array + WARMUP, REPEAT);
    double apply_perm_time_average = calculateAve(apply_perm_time_array + WARMUP, REPEAT);
    printf("%f\n", gen_perm_time_average);
    printf("%f\n", apply_perm_time_average);
  }
  // Some legacy modes report a cumulative swap counter, so keep round zero.
  printf("%ld\n", num_oswaps[0]);
  if (profile_memory)
    printf("MEMORY,%zu\n", algorithm_heap_peak_bytes);
  if (profile_online && (MODE == 4 || MODE == 5 || MODE == 6)) {
    const double route = calculateAve(online_route_array + WARMUP, REPEAT);
    const double reorder = calculateAve(online_reorder_array + WARMUP, REPEAT);
    const double copy = calculateAve(online_copy_array + WARMUP, REPEAT);
    const double shuffle = calculateAve(online_shuffle_array + WARMUP, REPEAT);
    const double online = calculateAve(apply_perm_time_array + WARMUP, REPEAT);
    printf("PROFILE,%f,%f,%f,%f,%f\n", route, reorder, copy, shuffle,
           online - route - reorder - copy - shuffle);
  }
  if (profile_offline && (MODE == 4 || MODE == 5 || MODE == 6)) {
    double phases[9] = {};
    double measured = 0.0;
    for (size_t phase = 0; phase < 9; ++phase) {
      phases[phase] = calculateAve(offline_phase_array[phase] + WARMUP, REPEAT);
      measured += phases[phase];
    }
    const double offline = calculateAve(gen_perm_time_array + WARMUP, REPEAT);
    printf("OFFLINE_PROFILE,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%zu,%zu\n",
           phases[0], phases[1], phases[2], phases[3], phases[4],
           phases[5], phases[6], phases[7], phases[8], offline - measured,
           offline_heap_peak_bytes, total_heap_peak_bytes);
  }

  close(randfd);

  delete[] buf;
  delete[] zeroes;
  return 0;
}
