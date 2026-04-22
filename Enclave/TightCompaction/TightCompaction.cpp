
#include "TightCompaction.h"
#include "TightCompaction_v2.hpp"
#include "../ObliviousPrimitives.hpp"

double testTightCompaction(unsigned char *buffer, size_t N, size_t block_size, 
        size_t nthreads, bool *selected_list, enc_ret *ret) {
  // Test NOP_TightCompaction:

  //threadpool_init(nthreads);

  long start_time, stop_time;
  double ptime;
  ocall_clock(&start_time);

  TightCompact_v2(buffer, N, block_size, selected_list);
  //TightCompact_v2_parallel(buffer, N, block_size, selected_list, nthreads);

  ocall_clock(&stop_time);
  ptime = ((double)(stop_time - start_time))/1000.0;
  /*
  if(TightCompactionTest(buffer2, N, block_size, selected_list)){
    printf("Non-order preserving TightCompact test Failed\n");
  }
  */ 
  ret->OSWAP_count = OSWAP_COUNTER;
  ret->ptime = ptime;
  //threadpool_shutdown();
  return (ptime);
}


double testOPTightCompaction(unsigned char *buffer, size_t N, size_t block_size, 
        bool *selected_list, enc_ret *ret) { 
  // Test OP_TightCompaction_V2:
  long start_time, stop_time;
  double ptime;
  ocall_clock(&start_time);

  OP_TightCompact_v2(buffer, N, block_size, selected_list);

  ocall_clock(&stop_time);
  ptime = ((double)(stop_time - start_time))/1000.0;
  /*
  if(OP_TightCompactionTest(buffer, buffer2, N, block_size)){
    printf("Order preserving TightCompact test Failed\n");
  }
  */
  
  ret->OSWAP_count = OSWAP_COUNTER;
  ret->ptime = ptime;
  return (ptime);
}


  // Test expansion on buffer2 to get back buffer 1
  //
  // printf("After TightCompaction:\n");
  // displayBufferLabels(N, buffer2, block_size); 
  /*  
  printf("\nDestinations:\n");
  for(int i =0 ; i<num_reals; i++){
    printf("%d,", destinations[i]);
  }
  printf("\n");
  //Expand<uint32_t>(N, buffer2, N*block_size, N, destinations, block_size, &(isBlockReal_32));
  // Check if buffer2 = buffer 
  //printf("Expanded Buffer:\n");
  //displayBufferLabels(N, buffer2, block_size); 
  //printf("Original Buffer:\n");
  //displayBufferLabels(N, buffer, block_size); 
   
  if(checkBuffers(buffer, buffer2, N*block_size)){
    printf("Expand Failed!\n");  
  }else{
    printf("Expand Success!\n");  
  }
  */
