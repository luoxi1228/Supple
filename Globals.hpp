
/*
Global File that is accessible to App, Untrusted and Enclave for commonly used
data structures
Global Flags will sit in CONFIG.h
*/

#ifndef __GLOBALS__

  #include <stdint.h>
  #include <stddef.h>

  typedef struct packet{
    uint64_t in_address;
    uint64_t out_address;
    unsigned char *data;
  }packet;

  typedef struct ORP_packet{
    uint64_t eviction_stream;
    uint64_t ORP_label;
    uint64_t in_address;
    uint64_t out_address;
    unsigned char *data;
  }ORP_packet;

  typedef struct BRN_params{
    int f;
    int d;
    int B;
  }params;

  typedef struct BucketOSort_params{
    uint64_t n;
    uint64_t n_1;
    uint64_t b;
    uint64_t Z;
    unsigned int f;
    unsigned int d;
    unsigned int lambda;
    uint64_t B;
    uint64_t block_size;
    size_t evict_start;

    size_t ORP_packet_size;
    size_t num_packets_in_outbuf_bucket;
    unsigned int log2f;

    unsigned int layer;
    unsigned int node_in_layer;

  }bos_params;

  //TODO: Get rid of Node_params. We don't really need this.
  typedef struct Node_params{
    bos_params sort_params;

    size_t ORP_packet_size;
    unsigned int log2f;
    unsigned int layer;
    unsigned int node_in_layer;
    size_t num_packets_in_outbuf_bucket;
  }node_params;

  typedef struct enclave_returns{
    size_t OSWAP_count;
    double ptime;
    //double enc_dec_time;
    
    // For logging any intermediate timings
    double last_layer_time;

    // For Waksman
    double gen_perm_time; 
    double control_bits_time;
    double apply_perm_time;
    size_t OSWAP_gp;
    size_t OSWAP_cb;
    size_t OSWAP_ap;

    // For qsort
    double qsort_time;
  }enc_ret;

  #define __GLOBALS__
#endif
