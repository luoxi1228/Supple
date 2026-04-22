
// ************************************************************************** //
// Global parameters:

  // Use Pseudorandom bytes instead of Random bytes
  // (Instead of sgx_read_rand we pull random bytes from a sgx_read_rand seeded AES_CTR
  //  of buffer size PRB_BUFFER_SIZE)
  #define USE_PRB
  #define PRB_BUFFER_SIZE 100000

  // Debugging flag to use inputs [1,N]:
  #define RANDOMIZE_INPUTS 1
  //#define SHOW_INPUT_KEYS 1

// ************************************************************************** //
// Bucket Oblivious Random Permutation (BORP) / Bucket Oblivious Sort (BOS) parameters:

  // Output verbose timings
  //#define VERBOSE_TIMINGS_BORP

  // Store the real packets buffer after completing the ORP phase
  // in memory outside the PRM
  // TODO 1: This is not really useful. Remove BOS_OUTSIDE_PRM_STORAGE completely from the rest of the source
  //       and then take it out of here
  // #define BOS_OUTSIDE_PRM_STORAGE 1

  // Multi-Threading Flags
  // TODO 2: BORP/BOS used to have a multi-threaded implementation. Since we no longer maintain it we should
  //       remove it all out of the rest of the source.
  #define MULTITHREADED

  // When running Single threaded, set NUM_THREADS to 1
  // TODO 3: When handling TODO 2, this NUM_THREADS should go away as well.

  #define COUNT_OSWAPS

  // To print BRN configuration
  // #define PRINT_BRN_CONFIGURATION 
  
  // To time all the individual components of BORP/BOS 
  // Namely ProcessPacketsThroughBRN, FlushBuffers, RemoveFakes_TC, 
  // #define DETAILED_BOS_TIMING 1

  // Useful for debugging reals packets in removeFakes_TC of BORP
  // #define DEBUG_RFTC

  // To print a log line whenever BORP evicts incorrectly
  #define DEBUG_BORP_FAILURE

// ************************************************************************** //

// Sorting network parameters:

//data/packet size in bytes
//#define SN_DATA_SIZE 8
#define SN_KEY_SIZE 8
//#define SN_PACKET_SIZE (SN_DATA_SIZE + SN_KEY_SIZE)


// ************************************************************************** //

// Waksman network options:

  // #define TEST_WN_DJB 1 
  // #define TEST_WN_OA 1 

// ************************************************************************** //

// Recursive shuffle parameters:

  //data/packet size in bytes
  #define RS_PACKET_SIZE 16
  #define RS_INTERNAL 1

  // RS_M2
  #define RS_M2_MEM_OPT1 1
  // #define RS_RB_BUFFER_SIZE 1000000 

// ************************************************************************** //
// Tight compaction parameters

  #define TC_PRECOMPUTE_COUNTS 1
  #define TC_OPT_SWAP_FLAG 1

// ************************************************************************** //
