#ifndef __UTILS_HPP__
#define __UTILS_HPP__

  #ifndef BEFTS_MODE
    #include <string.h>
    #include <vector>

    #include <stdarg.h>
    #include <stdio.h>      /* vsnprintf */
    #include "Enclave_t.h"  /* print_string */
    #include <stdlib.h>
    #include <stdio.h>
    #include <stdint.h>
    #include <math.h>
    #include "sgx_thread.h"
    #include <sgx_tcrypto.h>
    #include "sgx_trts.h"
    #include <assert.h>

    #include <openssl/ec.h>
    #include <openssl/bn.h>
    #include <openssl/rsa.h>
    #include <openssl/evp.h>
    #include <openssl/err.h>
    #include <openssl/rand.h>

    #include "../CONFIG.h"
    #include "Enclave_globals.h"
    #include "oasm_lib.h" 
  #else
    #include<pthread.h>
  #endif

  #define CTR_INC_BITS 128   

  inline size_t min(size_t a, size_t b){
    return (a<b)? a: b;
  }

  int compare(const void *buf1, const void *buf2);

  // High-level oswap_buffer function that handles all buffer sizes internally
  void oswap_buffer(unsigned char *dest, unsigned char *source, uint32_t buffer_size, uint8_t flag);


  // Encrypt/Decrypt Buffers
  size_t decryptBuffer(unsigned char *buffer, uint64_t N, size_t block_size,
      unsigned char **decrypted_buffer);
  size_t encryptBuffer(unsigned char *buffer, uint64_t N, size_t block_size,
      unsigned char *encrypted_buffer);

  size_t decryptBuffer_attachRTags(unsigned char *encrypted_buffer, uint64_t N, 
        size_t encrypted_block_size, unsigned char *random_bytes, unsigned char **decrypted_buffer);
  size_t encryptBuffer_removeRTags(unsigned char *decrypted_buffer, uint64_t N, 
        size_t decrypted_block_size, unsigned char *encrypted_buffer);

  size_t decryptBuffer_attachRTags_addDummies(unsigned char *encrypted_buffer, uint64_t N, 
          uint64_t N_prime, uint64_t B, uint64_t Z, size_t encrypted_block_size, unsigned char *random_bytes,
          unsigned char **decrypted_buffer);

  // Display/Debug functions
  
  #ifndef BEFTS_MODE
    void printf(const char *fmt, ...);
  #endif

  template <typename t>
  void print_array(t array, size_t N) {
    for(size_t i = 0; i < N; i++)
      printf("%d, ", array[i]);
    printf("\n");
  }

  unsigned long printf_with_rtclock(const char *fmt, ...);
  unsigned long printf_with_rtclock_diff(unsigned long before, const char *fmt, ...);
  void displayPacket(unsigned char* packet_in);
  void displayZeroEncryptedPacket(unsigned char* packet_in);
  void displayEncryptedPacket(unsigned char* packet_in);
  void displayORPPacket(unsigned char* packet_in, size_t block_size);
  void displayKeysInBuffer(unsigned char *buffer, size_t N, size_t block_size);

  // Packet processing functions
  bool isDummy(unsigned char *ptr_to_serialized_packet);
  void setDummy(unsigned char *ptr_to_serialized_packet);

  // Test Packet Dummy
  bool isORPDummy(unsigned char *ptr_to_serialized_packet);
  void setORPDummy(unsigned char *ptr_to_serialized_packet);

  // BORP utility
  size_t packetsConsumedUptoMSN(signed long msn_no, size_t msns_with_extra_packets, size_t packets_per_entry_msn);

  // Other utility functions
  int calculatelog2(uint64_t value);
  int calculatelog2_floor(uint64_t value);
  uint64_t pow2_lt(uint64_t N);
  uint64_t pow2_gt(uint64_t N);

  void merge(unsigned char *data, size_t l, size_t m, size_t r, unsigned char* (*comparator)(unsigned char*, unsigned char*));
  void mergeSort(unsigned char *data, size_t data_size, size_t start_index, size_t end_index, unsigned char* (*comparator)(unsigned char*, unsigned char*));
  unsigned char* compare_keys(unsigned char *packet_1, unsigned char *packet_2);

  // For TightCompaction & Expansion:
  uint8_t isBlockReal_16(unsigned char *block_ptr);
  uint8_t isBlockReal_32(unsigned char *block_ptr);
  uint8_t isBlockReal_64(unsigned char *block_ptr);

  // Correctness test for new inline oswap functions
  uint8_t isCorrect16x(uint32_t block_size);
  uint8_t isCorrect8_16x(uint32_t block_size);

  // For BOS_TC:
  void swapBuckets(unsigned char *bkt1, unsigned char *bkt2, unsigned char *temp_bucket, size_t bucket_size);


  /*** Thread pool implementation ***/

  /* Implements a restricted-model thread pool.  The restriction is that
   * every thread is the "parent" of a number of other threads (and no
   * thread has more than one parent).  Each thread can be dispatched and
   * joined only by its parent, so there's no contention on the dispatch
   * and join inter-thread communication.  A parent thread has to specify
   * the exact thread id of the child thread it dispatches work to. */

  typedef size_t threadid_t;
  extern thread_local threadid_t g_thread_id;

  /* Create the threadpool, with numthreads-1 additional threads (numbered
   * 1 through numthreads-1) in addition to the current "main" thread
   * (numbered 0). Returns 0 on success, -1 on failure. It is allowed, but
   * not very useful, to pass 1 here. */
  int threadpool_init(threadid_t numthreads);

  /* Ask all the threads to terminate, wait for that to happen, and clean
   * up. */
  void threadpool_shutdown();

  /* Dispatch some work to a particular thread in the thread pool. */
  void threadpool_dispatch(threadid_t threadid, void *(*func)(void*), void *data);

  /* Join a thread */
  void threadpool_join(threadid_t threadid, void **resp);

  // PRB = PseudoRandomBytes
  #ifdef USE_PRB
    class PRB_buffer{ 
      private:
        sgx_aes_ctr_128bit_key_t random_seed[SGX_AESCTR_KEY_SIZE];
        unsigned char counter[SGX_AESCTR_KEY_SIZE];
        unsigned char random_bytes[PRB_BUFFER_SIZE];
        unsigned char *random_bytes_ptr;
        int64_t random_bytes_left;
        uint64_t req_ctr; 
        bool initialized = false;

      public:
        PRB_buffer();
        ~PRB_buffer();
        sgx_status_t init_PRB_buffer(uint32_t buffer_size);
        /*  Intended for getting random bytes of size << PRB_BUFFER_SIZE at a time.
         Draws random bytes from the (typically) pre-filled random_bytes[PRB_BUFFER_SIZE] 
         buffer, refilling random_bytes[PRB_BUFFER_SIZE] when the call uses up all the
         PRB stored in the buffer. */
        sgx_status_t getRandomBytes(unsigned char *random_bytes, size_t size);
        /* Intended for getting random bytes of sizes > PRB_BUFFER_SIZE at a time.
         Populates the random_bytes buffer directly with output of SGX_AES_CTR_ENCRYPT, without
         touching the pre-filled random_bytes[PRB_BUFFER_SIZE].
        */
        sgx_status_t getBulkRandomBytes(unsigned char *random_bytes, size_t size);
    };
    extern PRB_buffer* PRB_pool;

    // Spawn a PRB pool for each thread
    void PRB_pool_init(int nthreads);
    // Cleanup PRBPool
    void PRB_pool_shutdown();
    
    inline sgx_status_t getRandomBytes(unsigned char *random_bytes, size_t size) {
      FOAV_SAFE_CNTXT(PRB, size)
      FOAV_SAFE_CNTXT(PRB, g_thread_id)
      return((PRB_pool[g_thread_id]).getRandomBytes(random_bytes, size));
    }

    // Return a random bit
    extern thread_local uint64_t PRB_rand_bits;
    extern thread_local uint32_t PRB_rand_bits_remaining;
    inline bool getRandomBit() {
        FOAV_SAFE_CNTXT(getRandomBit, PRB_rand_bits_remaining)
        if (PRB_rand_bits_remaining == 0) {
            getRandomBytes((unsigned char *)&PRB_rand_bits,
                sizeof(PRB_rand_bits));
            PRB_rand_bits_remaining = 64;
        }
        bool ret = PRB_rand_bits & 1;
        PRB_rand_bits >>= 1;
        PRB_rand_bits_remaining -= 1;
        return ret;
    }


    sgx_status_t initialize_BRB();
    sgx_status_t getBulkRandomBytes(unsigned char *buffer, size_t size);
  #else
    sgx_status_t getRandomBytes(unsigned char *random_bytes, size_t size);
  #endif

  // SortingNetwork has been removed from the minimal SubSample build.
  /*
    Generate a random permutation of range(N), and return it in
  random_permutation
      
    - random_permutation: a pointer to a uint64_t array. The function expects
      this array to have been initialized already and populates it with the
      random permutation.
    - N : the number of elements (and correspondingly the MAX+1 value) of the
      returned array.
  */

  template <typename T>
  void generateRandomPermutation(size_t N, T *random_permutation){
    //Initialize random permutation as 1,...,N
    FOAV_SAFE_CNTXT(GRP, N)
    for(T i=0; i<N; i++) {
      FOAV_SAFE_CNTXT(i, N)
      random_permutation[i]=i;
    }

    //Convert it to a random permutation of [1,N] 
    RecursiveShuffle_M2((unsigned char*) random_permutation, N, sizeof(T));
    /*
    random_permutation[0] = 8;
    random_permutation[1] = 12;
    random_permutation[2] = 6;
    random_permutation[3] = 29;
    random_permutation[4] = 22;
    random_permutation[5] = 0;
    random_permutation[6] = 15;
    random_permutation[7] = 24;
    random_permutation[8] = 30;
    random_permutation[9] = 19;
    random_permutation[10] = 13;
    random_permutation[11] = 28;
    random_permutation[12] = 7;
    random_permutation[13] = 17;
    random_permutation[14] = 14;
    random_permutation[15] = 27;
    random_permutation[16] = 18;
    random_permutation[17] = 25;
    random_permutation[18] = 5;
    random_permutation[19] = 10;
    random_permutation[20] = 23;
    random_permutation[21] = 2;
    random_permutation[22] = 4;
    random_permutation[23] = 9;
    random_permutation[24] = 26;
    random_permutation[25] = 1;
    random_permutation[26] = 21;
    random_permutation[27] = 3;
    random_permutation[28] = 20;
    random_permutation[29] = 16;
    random_permutation[30] = 11;
    random_permutation[31] = 31;
    */
    /*
    printf("\nPermutation output\n");
    for(T i=0; i<N; i++)
      printf("%ld, ", random_permutation[i]);
    printf("\n");
    */
  }

  #define __OSORT_UTILS__
#endif
