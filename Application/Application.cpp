#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <openssl/evp.h>
#include <openssl/err.h>
#include "../Globals.hpp"
#include "../Untrusted/OLib.hpp"
#include "../Untrusted/SS.hpp"
#include "../CONFIG.h"
#include "gcm.h"

#define NUM_ARGUMENTS_REQUIRED 5
#define CLOCKS_PER_MS (CLOCKS_PER_SEC / 1000)

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
  if (argc != (NUM_ARGUMENTS_REQUIRED + 1) && argc != (NUM_ARGUMENTS_REQUIRED + 2)) {
    printf("Did NOT receive the right number of command line arguments.\n"
          "Usage: ./application <31|32|33> <N> <BLOCK_SIZE> <P> <REPEAT>\n"
          "   or: ./application <34|35> <N> <BLOCK_SIZE> <P> <K> <REPEAT>\n\n"
           "Oblivious SubSampling (31/32/33/34/35)\n"
          "  (31) SubSampleShuffle (shuffle all then take first N*P)\n"
           "  (32) SubSampleSWO (Algorithm 1)\n"
          "  (33) SubSample (randomly select N*P items, then compact)\n"
           "  (34) SubSampleMulti\n"
           "  (35) SubSampleMulti_opt\n");
    exit(0);
  }

  MODE = atoi(argv[1]);
  if (MODE != 31 && MODE != 32 && MODE != 33 && MODE != 34 && MODE != 35) {
    printf("MODE must be 31, 32, 33, 34, or 35.\n");
    exit(0);
  }

  if ((MODE == 34 || MODE == 35) && argc != (NUM_ARGUMENTS_REQUIRED + 2)) {
    printf("MODE 34/35 expects K as an extra parameter.\n");
    exit(0);
  }
  if (MODE != 34 && MODE != 35 && argc != (NUM_ARGUMENTS_REQUIRED + 1)) {
    printf("MODE 31/32/33 expects no K parameter.\n");
    exit(0);
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
  if (MODE == 34 || MODE == 35) {
    K = atoi(argv[5]);
    REPEAT = atoi(argv[6]);
  } else {
    K = 0;
    REPEAT = atoi(argv[5]);
  }

  // To ignore the first iteration, we perform the experiment REPEAT + 1 times
  REPEAT = REPEAT + 1;
  if ((MODE == 34 || MODE == 35) && K == 0) {
    printf("MODE 34/35 expects K > 0\n");
    exit(0);
  }
  if (REPEAT < 2) {
    printf("REPEAT must be >= 1\n");
    exit(0);
  }
}

// Return real times in microseconds
uint64_t rtclock() {
  static time_t secstart = 0;
  struct timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  if (secstart == 0) {
    secstart = tp.tv_sec;
  }
  return (tp.tv_sec - secstart) * 1000000 + tp.tv_nsec / 1000;
}

int main(int argc, char *argv[]) {
  clock_t process_start, process_stop;
  double ecall_time;

  bool verbose_phases = !!getenv("VERBOSE_PHASES");
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

  double ecallTime_array[REPEAT] = {};
  double ptime_array[REPEAT] = {};
  double gen_perm_time_array[REPEAT] = {};
  double apply_perm_time_array[REPEAT] = {};
  size_t num_oswaps[REPEAT] = {};

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


  OLib_initialize();   // Initialize enclave

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
  if (MODE == 34 || MODE == 35) {
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


  // 6. Run the selected MODE REPEAT times
  phase_start = phase_end;

  for (size_t r = 0; r < REPEAT; r++) {
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

    process_start = clock();

    enc_ret ret;
    switch (MODE) {
      case 31:
        decryptAndSubSampleShuffle(buf, N, SampleSize, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
#ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
#else
        num_oswaps[r] = 0;
#endif
        break;

      case 32:
        decryptAndSubSampleSWO(buf, N, SampleSize, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
#ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
#else
        num_oswaps[r] = 0;
#endif
        break;

      case 33:
        decryptAndSubSample(buf, N, SampleSize, ENC_BLOCK_SIZE, buf, &ret);
        ptime_array[r] = ret.ptime;
#ifdef COUNT_OSWAPS
        num_oswaps[r] = ret.OSWAP_count;
#else
        num_oswaps[r] = 0;
#endif
        break;

      case 34:
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

      case 35:
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
    process_stop = clock();

    ecall_time = double(process_stop - process_start) / double(CLOCKS_PER_MS);
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
    if (MODE == 31 || MODE == 33) {
      output_blocks = SampleSize;
    } else if (MODE == 34 || MODE == 35) {
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
      exit(0);
    }

    phase_end = rtclock();
    phase_time = (double)(phase_end - phase_start) / 1000.0;
    if (verbose_phases) {
      printf("check phase: %f\n", phase_time);
    }
    phase_start = phase_end;
  }

  // NOTE: The +1 and -1 are to remove the additional timing variance that stems from the
  // first run of execution always taking longer to execute due to instruction page cache warming.
  double ecallTime_average = calculateAve(ecallTime_array + 1, REPEAT - 1);
  double ptime_average = calculateAve(ptime_array + 1, REPEAT - 1);

  printf("%f\n", ecallTime_average);
  printf("%f\n", ptime_average);

  if (MODE == 34 || MODE == 35) {
    double gen_perm_time_average = calculateAve(gen_perm_time_array + 1, REPEAT - 1);
    double apply_perm_time_average = calculateAve(apply_perm_time_array + 1, REPEAT - 1);
    printf("%f\n", gen_perm_time_average);
    printf("%f\n", apply_perm_time_average);
  }
  printf("%ld\n", num_oswaps[0]);

  close(randfd);

  delete[] buf;
  delete[] zeroes;
  return 0;
}
