  #include <sgx_tcrypto.h>

  extern unsigned char enclave_decryption_key[SGX_AESGCM_KEY_SIZE];
  extern unsigned char enclave_encryption_key[SGX_AESGCM_KEY_SIZE];

  #ifdef COUNT_OSWAPS
    extern thread_local uint64_t OSWAP_COUNTER;
  #endif
