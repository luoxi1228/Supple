
#ifndef BEFTS_MODE
  #include <array>
  #include <sgx_tcrypto.h>
  #include "../oasm_lib.h"
  #include "../utils.hpp"
  #include "../ObliviousPrimitives.hpp"
  #include "RecursiveShuffle.hpp"
#endif

size_t RS_RB_BUFFER_SIZE;
bool *selected_list;
unsigned char *random_bytes_buffer = NULL;
uint32_t *random_bytes_buffer_ptr;
uint32_t *random_bytes_buffer_ptr_end;

/*
  MarkHalf: Marks half of the elements of an N sized array randomly.
  Pass in a bool array of size N, which will be populated with 1's at indexes which 
r get marked by MarkHalf
  NOTE: MarkHalf assumes selected_list is initialized to all 0's before passed to MarkHalf
*/

void MarkHalf(uint64_t N, bool *selected_list) {
  
  uint64_t left_to_mark = N/2;
  uint64_t total_left = N;
  PRB_buffer *randpool = PRB_pool + g_thread_id;
  uint32_t coins[RS_MARKHALF_MAX_COINS];
  size_t coinsleft=0;
  
  FOAV_SAFE_CNTXT(MarkHalf_marking_half, N)
  for(uint64_t i=0; i<N; i++){
  FOAV_SAFE2_CNTXT(MarkHalf_marking_half, i, coinsleft)
    if (coinsleft == 0) {
        size_t numcoins = (N-i);
        FOAV_SAFE_CNTXT(MarkHalf_marking_half, numcoins)
        if (numcoins > RS_MARKHALF_MAX_COINS) {
            numcoins = RS_MARKHALF_MAX_COINS;
        }
        randpool->getRandomBytes((unsigned char *) coins,
            sizeof(coins[0])*numcoins);
        coinsleft = numcoins;
    }
    //Mark with probability left_to_mark/total_left;
    uint32_t random_coin;
    random_coin = (total_left * coins[--coinsleft]) >> 32;
    uint32_t mark_threshold = total_left - left_to_mark;
    uint8_t mark_element = oge_set_flag(random_coin, mark_threshold);

    //If mark_element, obliviously set selected_list[i] to 1
    FOAV_SAFE_CNTXT(MarkHalf_marking_half, i)
    selected_list[i] = mark_element;
    left_to_mark-= mark_element;
    total_left--;
    FOAV_SAFE2_CNTXT(MarkHalf_marking_half, i, N)
  }
  
}

#ifndef BEFTS_MODE
  void RecursiveShuffle_M1(unsigned char *buf, uint64_t N, size_t block_size) {
    FOAV_SAFE2_CNTXT(RS_M1, N, block_size)
    size_t num_random_bytes = calculatelog2(N) * N * sizeof(uint32_t);
    #ifdef RS_M2_MEM_OPT1
      FOAV_SAFE2_CNTXT(RS_M1, num_random_bytes, RS_RB_BUFFER_LIMIT)
      if(num_random_bytes > RS_RB_BUFFER_LIMIT) {
        RS_RB_BUFFER_SIZE = RS_RB_BUFFER_LIMIT;
      }
      else{
        RS_RB_BUFFER_SIZE = num_random_bytes;
      }
      try {
        random_bytes_buffer = new unsigned char[RS_RB_BUFFER_SIZE];
        //FOAV_SAFE_CNTXT(RS_M1_initializing_selected_list, N)
        selected_list = new bool[N]{};
      } catch (std::bad_alloc&){
        printf("Allocating memory failed in RS_M2\n");
      }
      getBulkRandomBytes((unsigned char*)random_bytes_buffer, RS_RB_BUFFER_SIZE);
      random_bytes_buffer_ptr_end = (uint32_t*)(random_bytes_buffer + RS_RB_BUFFER_SIZE);
    #else
      try {
        random_bytes_buffer = new unsigned char[num_random_bytes];
        selected_list = new bool[N]{};
      } catch (std::bad_alloc&){
        printf("Allocating memory failed in RS_M2\n");
      }

      getBulkRandomBytes((unsigned char*)random_bytes_buffer, num_random_bytes);
    #endif

    random_bytes_buffer_ptr = (uint32_t*) random_bytes_buffer;
    FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
    if(block_size==4){
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
      RecursiveShuffle_M1_inner<OSWAP_4>(buf, N, block_size, selected_list);
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
    } else if(block_size==8){
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
      RecursiveShuffle_M1_inner<OSWAP_8>(buf, N, block_size, selected_list);
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
    } else if(block_size==12){
      // 【新增】12 字节专属分支
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
      RecursiveShuffle_M1_inner<OSWAP_12>(buf, N, block_size, selected_list);
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
    } else if(block_size%16==0) {
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
      RecursiveShuffle_M1_inner<OSWAP_16X>(buf, N, block_size, selected_list);
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
    } else {
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
      RecursiveShuffle_M1_inner<OSWAP_8_16X>(buf, N, block_size, selected_list);
      FOAV_SAFE_CNTXT(RS_M1_branching_on_block_size_for_OSwap_Style_templates, block_size)
    }

    FOAV_SAFE_CNTXT(RecursiveShuffle_M1_delete, random_bytes_buffer)
    delete []random_bytes_buffer;
    FOAV_SAFE_CNTXT(RecursiveShuffle_M1_delete, selected_list)
    delete []selected_list;
  }
#endif

void RecursiveShuffle_M2(unsigned char *buf, uint64_t N, size_t block_size){
    RecursiveShuffle_M2_parallel(buf, N, block_size, 1);
}

void RecursiveShuffle_M2_parallel(unsigned char *buf, uint64_t N, size_t block_size, size_t nthreads){
  FOAV_SAFE2_CNTXT(RS_M2, N, block_size)
  try {
    selected_list = new bool[N]{};
  } catch (std::bad_alloc&){
    printf("Allocating memory failed in RS_M2\n");
  }

  threadpool_init(nthreads);

  FOAV_SAFE_CNTXT(RS_M2_branching_on_block_size_for_OSwap_Style_templates, block_size)
  if(block_size==4){
    RecursiveShuffle_M2_inner_parallel<OSWAP_4>(buf, N, block_size, selected_list, nthreads);
  } else if(block_size==8){
    RecursiveShuffle_M2_inner_parallel<OSWAP_8>(buf, N, block_size, selected_list, nthreads);
  } else if(block_size==12){
    // 【新增】12 字节专属分支
    RecursiveShuffle_M2_inner_parallel<OSWAP_12>(buf, N, block_size, selected_list, nthreads);
  } else if(block_size%16==0) {
    RecursiveShuffle_M2_inner_parallel<OSWAP_16X>(buf, N, block_size, selected_list, nthreads);
  } else {
    RecursiveShuffle_M2_inner_parallel<OSWAP_8_16X>(buf, N, block_size, selected_list, nthreads);
  }
  
  threadpool_shutdown();

  FOAV_SAFE_CNTXT(RecursiveShuffle_M2_delete, selected_list)
  delete []selected_list;
}

// We maintain a double type return version of RecusiveShuffle_M2, 
// to time strictly the RS_M2 component when using it without any encryption or decryption
// We need this only for the BOS optimizer!!
double RecursiveShuffle_M2_opt(unsigned char *buf, uint64_t N, size_t block_size){
  FOAV_SAFE2_CNTXT(RS_M2_opt, N, block_size)
  //In a single call allocate all the randomness we need here!
  size_t num_random_bytes = calculatelog2(N) * N * sizeof(uint32_t);
  long t0, t1;
  ocall_clock(&t0);

  #ifdef RS_M2_MEM_OPT1
    if(num_random_bytes > RS_RB_BUFFER_LIMIT) {
      RS_RB_BUFFER_SIZE = RS_RB_BUFFER_LIMIT;
    }
    else{
      RS_RB_BUFFER_SIZE = num_random_bytes;
    }
    try {
      random_bytes_buffer = new unsigned char[RS_RB_BUFFER_SIZE];
      selected_list = new bool[N]{};
    } catch (std::bad_alloc&){
      printf("Allocating memory failed in RS_M2\n");
    }
    getBulkRandomBytes((unsigned char*)random_bytes_buffer, RS_RB_BUFFER_SIZE);
    random_bytes_buffer_ptr_end = (uint32_t*)(random_bytes_buffer + RS_RB_BUFFER_SIZE);
  #else
    try {
      random_bytes_buffer = new unsigned char[num_random_bytes];
      selected_list = new bool[N]{};
    } catch (std::bad_alloc&){
      printf("Allocating memory failed in RS_M2\n");
    }

    getBulkRandomBytes((unsigned char*)random_bytes_buffer, num_random_bytes);
  #endif

  random_bytes_buffer_ptr = (uint32_t*) random_bytes_buffer;
  FOAV_SAFE_CNTXT(RS_M2_opt, num_random_bytes)
  FOAV_SAFE2_CNTXT(RS_M2_opt, N, block_size)

  FOAV_SAFE_CNTXT(RS_M2_opt, block_size)
  if(block_size==4){
    RecursiveShuffle_M2_inner<OSWAP_4>(buf, N, block_size, selected_list);
  } else if(block_size==8){
    RecursiveShuffle_M2_inner<OSWAP_8>(buf, N, block_size, selected_list);
  } else if(block_size==12){
    // 【新增】12 字节专属分支
    RecursiveShuffle_M2_inner<OSWAP_12>(buf, N, block_size, selected_list);
  } else if(block_size%16==0) {
    RecursiveShuffle_M2_inner<OSWAP_16X>(buf, N, block_size, selected_list);
  } else {
    RecursiveShuffle_M2_inner<OSWAP_8_16X>(buf, N, block_size, selected_list);
  }

  delete []random_bytes_buffer;
  delete []selected_list;

  ocall_clock(&t1);
  double ptime = ((double)(t1-t0))/1000.0;
  return ptime;
}

#ifndef BEFTS_MODE
double DecryptAndShuffleM1(unsigned char *encrypted_buffer, size_t N, size_t encrypted_block_size, unsigned char *result_buffer, enc_ret *ret) {
 
  // Decrypt buffer to decrypted_buffer
  unsigned char *decrypted_buffer = NULL;
  size_t decrypted_block_size = decryptBuffer(encrypted_buffer, N, encrypted_block_size, &decrypted_buffer);

  long t0, t1;
  ocall_clock(&t0);

  // ShuffleM1 on decrypted_buffer
  PRB_pool_init(1);
  RecursiveShuffle_M1(decrypted_buffer, N, decrypted_block_size);

  ocall_clock(&t1);
  // Encrypt buffer to result_buffer
  encryptBuffer(decrypted_buffer, N, decrypted_block_size, result_buffer);
  PRB_pool_shutdown();

  free(decrypted_buffer); 
  double ptime = ((double)(t1-t0))/1000.0;
  ret->OSWAP_count = OSWAP_COUNTER;
  ret->ptime = ptime;
  return(ptime);
}
#endif

double DecryptAndShuffleM2(unsigned char *encrypted_buffer, size_t N, size_t encrypted_block_size, size_t nthreads, unsigned char *result_buffer, enc_ret *ret) {
 
  // Decrypt buffer to decrypted_buffer
  unsigned char *decrypted_buffer = NULL;
  size_t decrypted_block_size = decryptBuffer(encrypted_buffer, N, encrypted_block_size, &decrypted_buffer);

  long t0, t1;
  ocall_clock(&t0);

  // ShuffleM2 on decrypted_buffer
  PRB_pool_init(nthreads);
  RecursiveShuffle_M2_parallel(decrypted_buffer, N, decrypted_block_size, nthreads);

  ocall_clock(&t1);
  // Encrypt buffer to result_buffer
  encryptBuffer(decrypted_buffer, N, decrypted_block_size, result_buffer);
  PRB_pool_shutdown();

  #ifdef TIME_MARKHALF
    printf("Time taken in MarkHalf calls = %f\n", MARKHALF_TIME);
  #endif

  free(decrypted_buffer); 
  double ptime = ((double)(t1-t0))/1000.0;
  ret->OSWAP_count = OSWAP_COUNTER;
  ret->ptime = ptime;
  return(ptime);
}
