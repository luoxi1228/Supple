

#include "Untrusted.h"

struct timespec start, stop;
double elapsed;

/* Global EID shared by multiple threads */
sgx_enclave_id_t global_eid = 0;

/* OCall functions */
void ocall_print_string(const char *str) {
    /* Proxy/Bridge will check the length and null-terminate
     * the input string to prevent buffer overflow.
     */
    printf("%s", str);
}

/* Print the given string, prefixed with the current time, and return the
 * current time. */
unsigned long ocall_print_string_with_rtclock(const char *str) {
    /* Proxy/Bridge will check the length and null-terminate
     * the input string to prevent buffer overflow.
     */
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME_COARSE, &tp);
    unsigned long now = tp.tv_sec * 1000000 + tp.tv_nsec/1000;

    printf("%lu.%06lu: %s", tp.tv_sec, tp.tv_nsec/1000, str);

    return now;
}

/* Print the given string, prefixed with the current time, and return the
 * current time. Also print the time difference to the provided time. */
unsigned long ocall_print_string_with_rtclock_diff(const char *str, unsigned long before) {
    /* Proxy/Bridge will check the length and null-terminate
     * the input string to prevent buffer overflow.
     */
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME_COARSE, &tp);
    unsigned long now = tp.tv_sec * 1000000 + tp.tv_nsec/1000;
    unsigned long diff = now - before;

    printf("%lu.%06lu: (%lu.%06lu) %s", tp.tv_sec, tp.tv_nsec/1000,
        diff/1000000, diff%1000000, str);

    return now;
}

long ocall_clock() {
    return (long)clock();
}

// Returns wallclock time taken between a start (0) and stop flag(1)
// invocation of this function (Time returned in ms)
double ocall_wallclock(int start_or_stop) {
  if(start_or_stop==0) {
    clock_gettime(CLOCK_MONOTONIC, &start);
    return 0;
  } else {
    clock_gettime(CLOCK_MONOTONIC, &stop);
    elapsed = (stop.tv_sec - start.tv_sec);
    elapsed += (stop.tv_nsec - start.tv_nsec) / 1000000000.0;
    return (elapsed * 1000);
  }
}

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret) {
    size_t idx = 0;
    size_t ttl = sizeof sgx_errlist/sizeof sgx_errlist[0];

    for (idx = 0; idx < ttl; idx++) {
        if(ret == sgx_errlist[idx].err) {
            if(NULL != sgx_errlist[idx].sug)
                fprintf(stderr, "Info: %s\n", sgx_errlist[idx].sug);
            fprintf(stderr, "Error: %s\n", sgx_errlist[idx].msg);
            break;
        }
    }

    if (idx == ttl)
        fprintf(stderr, "Error: Unexpected error occurred.\n");
}

/*
void thread_func(){
  size_t thread_id = std::hash<std::thread::id>()(std::this_thread::get_id());
  Enclave_testMT(global_eid, thread_id);
}
*/
/*
void Enclave_testMT() {
  printf("Starting Enclave_testMT for %d threads\n", NUM_THREADS);
  std::thread trd[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    trd[i] = std::thread(thread_func);
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    trd[i].join();
  }
}
*/


double testTightCompaction(unsigned char *buffer, size_t N, size_t block_size, 
        size_t nthreads, bool *selected_list, enc_ret *ret){
  double r;
  testTightCompaction(global_eid, &r, buffer, N, block_size, nthreads, selected_list, ret);
  return r;
}

double testOPTightCompaction(unsigned char *buffer, size_t N, size_t block_size, 
        bool *selected_list, enc_ret *ret){
  double r;
  testOPTightCompaction(global_eid, &r, buffer, N, block_size, selected_list, ret);
  return r;
}

unsigned char* untrustedMemAllocate(size_t mem_size){
  unsigned char* mem_ptr = (unsigned char*) malloc (mem_size);
  return mem_ptr;
}

int BucketOSort_computeParams(uint64_t n) {
  //TODO
  return 1;
}

#ifdef MULTITHREADED
  /*
 int routepackets_thread_function(int thread, unsigned char *inbuf_ptr, size_t packet_start, size_t num_packets, size_t block_size) {
    int ret;
    BORP_MT_processPacketsThroughBRN(global_eid, &ret, thread, inbuf_ptr, packet_start, num_packets, block_size);
    return ret;
  }

  void flushbuffers_thread_function(int thread, int depth, int msn_start, int msn_stop, size_t block_size) {
    int ret;
    BORP_MT_flushBuffers(global_eid, &ret, thread, depth, msn_start, msn_stop, block_size);
  }

  void removeFakes_p1_thread_function(int bucket_start, int bucket_stop, size_t block_size) {
    int ret;
    BORP_MT_removeFakes_p1(global_eid, &ret, bucket_start, bucket_stop, block_size);
  }

  void removeFakes_p2_thread_function(int j, int bucket_start, int bucket_stop, unsigned char *result_buf, size_t block_size) {
    int ret;
    BORP_MT_removeFakes_p2(global_eid, &ret, j, bucket_start, bucket_stop, result_buf, block_size);
  }

  void mergesort_thread_function(){
    //TODO:
  }
  */

  double BORP_MT(unsigned char *buf, size_t N, size_t block_size, 
          bos_params *params, size_t nthreads, unsigned char *result_buf, enc_ret *ret) {
    /*
    int ret_code;
    double twtime=0, tptime=0;
    double ptime;
    clock_t process_start, process_stop;
    uint64_t CLOCKS_PER_MS = (CLOCKS_PER_SEC/1000);
    double wtime;
    
    process_start = clock();
    ocall_wallclock(0);
    BORP_MT_setup(global_eid, params, nthreads, block_size);
    wtime = ocall_wallclock(1);
    process_stop = clock();
    ptime = double(process_stop-process_start)/double(CLOCKS_PER_MS);
    printf("(CPU) Time taken for BucketOSort_setup() = %f\n", ptime);
    printf("(Wallclock) Time taken for BucketOSort_setup() = %f\n", wtime);

    std::thread trd[nthreads];
    size_t packet_start = 0;
    size_t n_per_thread = N/nthreads;
    size_t buf_len = N * block_size;
    size_t buff_portion_per_thread = n_per_thread * block_size;
    size_t threads_with_extra_packet = N % nthreads;
    unsigned char *buff_ptr = buf;

    printf("nthreads = %ld, n_per_thread = %ld\n", nthreads, n_per_thread);
    for (int i = 0; i < nthreads; i++) {
      // Partition the input packets across the threads correctly
      if(threads_with_extra_packet>0 && i<threads_with_extra_packet) {
          trd[i] = std::thread(routepackets_thread_function, i, buff_ptr, packet_start, n_per_thread+1, block_size);
          buff_ptr+=(buff_portion_per_thread+block_size);
          packet_start+=(n_per_thread+1);
      }
      else{
          trd[i] = std::thread(routepackets_thread_function, i, buff_ptr, packet_start, n_per_thread, block_size);
          buff_ptr+=(buff_portion_per_thread);
          packet_start+=(n_per_thread);
      }

    }
    for (int i = 0; i < nthreads; i++) {
      trd[i].join();
    }

    wtime = ocall_wallclock(1);
    process_stop = clock();
    ptime = double(process_stop-process_start)/double(CLOCKS_PER_MS);
    printf("(CPU) Time taken for processPacketsThroughBRN_MT = %f\n", ptime);
    printf("(Wallclock) Time taken for processPacketsThroughBRN_MT = %f\n", wtime);
    twtime+=wtime;
    tptime+=ptime;

    // Multithreaded flushBuffers:
    process_start = clock();
    ocall_wallclock(0);
    int num_msns_per_layer = (params->b/params->f);
    int msn_per_thread = num_msns_per_layer/nthreads;
    int threads_with_extra_msn = num_msns_per_layer % nthreads;
    
    for(int i=0; i<params->d; i++) {
      // We want the flushing to have barriers between each layer of the BRN
      // Since SGX doesnt have barriers or countable mutexes we just respawn and join threads for
      // each layer.

      int msn_start = 0;
      int msn_stop = msn_per_thread;
      // There are b/f MSN nodes in each layer, which get distributed over NUM_THREADS threads
      for (int j = 0; j < nthreads; j++) {
        if(j<threads_with_extra_msn)
          msn_stop+=1;
        trd[j] = std::thread(flushbuffers_thread_function, j, i, msn_start, msn_stop, block_size);
        msn_start = msn_stop;
        msn_stop+=msn_per_thread;
      }
      for (int j = 0; j < nthreads; j++) {
        trd[j].join();
      }
    }
    wtime = ocall_wallclock(1);
    process_stop = clock();
    ptime = double(process_stop-process_start)/double(CLOCKS_PER_MS);
    printf("(CPU) Time taken for flushBuffers_MT = %f\n", ptime);
    printf("(Wallclock) Time taken for flushBuffers_MT = %f\n", wtime);
    twtime+=wtime;
    tptime+=ptime;
    

    //reportPNcounts(global_eid);

    // Multi-thread the RemoveFakes
    process_start = clock();
    ocall_wallclock(0);
    int buckets_per_thread = (params->b/nthreads);
    int bucket_start = 0;
    int bucket_stop = buckets_per_thread;
    int threads_with_extra_buckets = params->b % nthreads;
    printf("In untrusted, buckets_per_thread = %d\n", buckets_per_thread);

    for (int j = 0; j < nthreads; j++) {
      // Partition the outbuf buckets across the threads correctly
      if(j<threads_with_extra_buckets)
        bucket_stop+=1;
      trd[j] = std::thread(removeFakes_p1_thread_function, bucket_start, bucket_stop, block_size);
      bucket_start = bucket_stop;
      bucket_stop+= buckets_per_thread;
    }
    for (int j = 0; j < nthreads; j++) {
      trd[j].join();
    }


    wtime = ocall_wallclock(1);
    process_stop = clock();
    ptime = double(process_stop-process_start)/double(CLOCKS_PER_MS);
    printf("(CPU) Time taken for removeFakes_MT_p1 = %f\n", ptime);
    printf("(Wallclock) Time taken for removeFakes_MT_p1 = %f\n", wtime);
    twtime+=wtime;
    tptime+=ptime;

    process_start = clock();
    ocall_wallclock(0);
    bucket_start = 0;
    bucket_stop = buckets_per_thread;
    printf("In untrusted, bucket_stop = %d\n", bucket_stop);

    for (int j = 0; j < nthreads; j++) {
      // Partition the outbuf buckets across the threads correctly
      if(j<threads_with_extra_buckets)
        bucket_stop+=1;
      trd[j] = std::thread(removeFakes_p2_thread_function, j, bucket_start, bucket_stop, result_buf, block_size);
      bucket_start = bucket_stop;
      bucket_stop+= buckets_per_thread;
    }
    for (int j = 0; j < nthreads; j++) {
      trd[j].join();
    }
    printf("In untrusted, done with RF_p2_Mt\n");

    wtime = ocall_wallclock(1);
    process_stop = clock();
    ptime = double(process_stop-process_start)/double(CLOCKS_PER_MS);
    printf("(CPU) Time taken for removeFakes_MT_p2 = %f\n", ptime);
    printf("(Wallclock) Time taken for removeFakes_MT_p2 = %f\n", wtime);
    twtime+=wtime;
    tptime+=ptime;

    BORP_MT_cleanup(global_eid, block_size);
    */

    /*
    // TODO: Multi-thread the MergeSort
    // Do the rest of the single-threaded functions to finish the Bucket Oblivious Sort
    process_start = clock();
    ocall_wallclock(0);
    BucketOSort_remainderOS(global_eid, &ret_code);
    wtime = ocall_wallclock(1);
    process_stop = clock();
    ptime = double(process_stop-process_start)/double(CLOCKS_PER_MS);
    printf("(CPU) Time taken for BOS_remainder = %f\n", ptime);
    printf("(Wallclock) Time taken for BOS_remainder = %f\n", wtime);
    twtime+=wtime;
    tptime+=ptime;

    printf("(CPU) Total Sort time = %f\n", tptime);
    printf("(Wallclock) Total Sort time = %f\n", twtime);
    */
    //return ret_code;
    return 0.0;
  }

#endif

int initialize_enclave(void) {
    char token_path[MAX_PATH] = {'\0'};
    sgx_launch_token_t token = {0};
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    int updated = 0;

    /* Step 1: try to retrieve the launch token saved by last transaction
     *         if there is no token, then create a new one.
     */
    /* try to get the token saved in $HOME */
    const char *home_dir = getpwuid(getuid())->pw_dir;

    if (home_dir != NULL &&
        (strlen(home_dir)+strlen("/")+sizeof(TOKEN_FILENAME)+1) <= MAX_PATH) {
        /* compose the token path */
        strncpy(token_path, home_dir, strlen(home_dir));
        strncat(token_path, "/", strlen("/"));
        strncat(token_path, TOKEN_FILENAME, sizeof(TOKEN_FILENAME)+1);
    } else {
        /* if token path is too long or $HOME is NULL */
        strncpy(token_path, TOKEN_FILENAME, sizeof(TOKEN_FILENAME));
    }

    FILE *fp = fopen(token_path, "rb");
    if (fp == NULL && (fp = fopen(token_path, "wb")) == NULL) {
        fprintf(stderr, "ZT_LSORAM:Warning: Failed to create/open the launch token file \"%s\".\n", token_path);
    }

    if (fp != NULL) {
        /* read the token from saved file */
        size_t read_num = fread(token, 1, sizeof(sgx_launch_token_t), fp);
        if (read_num != 0 && read_num != sizeof(sgx_launch_token_t)) {
            /* if token is invalid, clear the buffer */
            memset(&token, 0x0, sizeof(sgx_launch_token_t));
            fprintf(stderr, "ZT_LSORAM:Warning: Invalid launch token read from \"%s\".\n", token_path);
        }
    }

    /* Step 2: call sgx_create_enclave to initialize an enclave instance */
    /* Debug Support: set 2nd parameter to 1 */
    
    ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        print_error_message(ret);
        if (fp != NULL) fclose(fp);
        return -1;
    }

    /* Step 3: save the launch token if it is updated */
    if (updated == false || fp == NULL) {
        /* if the token is not updated, or file handler is invalid, do not perform saving */
        if (fp != NULL) fclose(fp);
        return 0;
    }
    /* reopen the file with write capablity */
    fp = freopen(token_path, "wb", fp);
    if (fp == NULL) return 0;
    size_t write_num = fwrite(token, 1, sizeof(sgx_launch_token_t), fp);
    if (write_num != sizeof(sgx_launch_token_t))
        fprintf(stderr, "Warning: Failed to save launch token to \"%s\".\n", token_path);
    fclose(fp);

    printf("End of initialize_enclave call \n");
    return 0;
}

int8_t Enclave_Initialize(unsigned char *bin_x, unsigned char* bin_y,
       unsigned char *bin_r, unsigned char* bin_s, uint32_t buff_size) {

  int8_t ret;

  // Initialize the enclave
  if(initialize_enclave() < 0) {
    printf("Enter a character before exit ...\n");
    getchar();
    return -1;
  }

  /*
  // Utilize edger8r attributes
  edger8r_array_attributes();
  edger8r_pointer_attributes();
  edger8r_type_attributes();
  edger8r_function_attributes();

  // Utilize trusted libraries
  ecall_libc_functions();
  ecall_libcxx_functions();
  ecall_thread_functions();
  */

  // Extract Public Key and send it over
  // InitializeKeys(global_eid, &ret, bin_x, bin_y, bin_r, bin_s, buff_size);
  return ret;
}

void OLib_initialize(){
  Enclave_Initialize(NULL, NULL, NULL, NULL, 32);
}

void Enclave_loadTestKeys(unsigned char inkey[16], unsigned char outkey[16]) {
  Enclave_loadTestKeys(global_eid, inkey, outkey);
}

int untrustedMemAllocate(unsigned char** mem_ptr, uint64_t size){
  unsigned char *mem = (unsigned char*) malloc(size);
  *mem_ptr = mem;
  if(mem==NULL)
    return -1;
  else
    return 1;
}

void untrustedMemFree(unsigned char *mem) {
  free(mem);
}


void decryptAndSubSample(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size,
  unsigned char *encryt_result_buffer, enc_ret *ret) {
  decryptAndSubSample(global_eid, encrypted_buffer, N, M, encrypted_block_size, encryt_result_buffer, ret);
}


void decryptAndSubSampleMulti(unsigned char *encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size,
  unsigned char *encryt_result_buffer, enc_ret *ret) {
  decryptAndSubSampleMulti(global_eid, encrypted_buffer, N, M, K, encrypted_block_size, encryt_result_buffer, ret);
}

void decryptAndSubSampleMulti_opt(unsigned char *encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size,
  unsigned char *encryt_result_buffer, enc_ret *ret) {
  decryptAndSubSampleMulti_opt(global_eid, encrypted_buffer, N, M, K, encrypted_block_size, encryt_result_buffer, ret);
}

void decryptAndSubSampleShuffle(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size,
  unsigned char *encryt_result_buffer, enc_ret *ret) {
  decryptAndSubSampleShuffle(global_eid, encrypted_buffer, N, M, encrypted_block_size, encryt_result_buffer, ret);
}

void decryptAndSubSampleSWO(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size,
  unsigned char *encryt_result_buffer, enc_ret *ret) {
  decryptAndSubSampleSWO(global_eid, encrypted_buffer, N, M, encrypted_block_size, encryt_result_buffer, ret);
}

void RecursiveShuffle_M1(unsigned char *buf, uint64_t N, size_t block_size) {
  RecursiveShuffle_M1(global_eid, buf, N, block_size);
}

double RecursiveShuffle_M2(unsigned char *buf, uint64_t N, size_t block_size) {
  double ptime;
  RecursiveShuffle_M2_opt(global_eid, &ptime, buf, N, block_size);
  return ptime;
}

double DecryptAndShuffleM1(unsigned char *buf, size_t N, size_t encrypted_block_size, unsigned char *result_buf, enc_ret *ret) {
  double ptime;
  DecryptAndShuffleM1(global_eid, &ptime, buf, N, encrypted_block_size, result_buf, ret);
  return ptime;
}

double DecryptAndShuffleM2(unsigned char *buf, size_t N, size_t encrypted_block_size, size_t nthreads, unsigned char *result_buf, enc_ret *ret) {
  double ptime;
  DecryptAndShuffleM2(global_eid, &ptime, buf, N, encrypted_block_size, nthreads, result_buf, ret);
  return ptime;
}

double MeasureOSWAPBuffer(unsigned char *buf, size_t N, size_t block_size) {
  double ptime;
  MeasureOSWAPBuffer(global_eid, &ptime, buf, N, block_size);
  return ptime;
}
