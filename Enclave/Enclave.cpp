
#include "Enclave.hpp"

unsigned char enclave_decryption_key[SGX_AESGCM_KEY_SIZE];
unsigned char enclave_encryption_key[SGX_AESGCM_KEY_SIZE];

#ifdef COUNT_OSWAPS
thread_local uint64_t OSWAP_COUNTER=0;
#endif

void Enclave_loadTestKeys(unsigned char inkey[16], unsigned char outkey[16]) {
    memmove(enclave_decryption_key, inkey, 16);
    memmove(enclave_encryption_key, outkey, 16);
}


void displayBufferLabels(size_t N, unsigned char *buffer, size_t block_size){
  unsigned char *bfr_ptr = buffer;
  for(int i =0; i<N; i++){
    uint32_t label = *((uint32_t*)(bfr_ptr));
    if(label==UINT32_MAX){
      printf("D, ");
    }
    else{
      printf("%d, ", label%N);
    }
    bfr_ptr+=block_size;
  }
  printf("\n\n");
}


int checkBuffers(unsigned char *buffer1, unsigned char *buffer2, size_t buffer_size){
  for(size_t i=0; i<buffer_size; i++){
    if(buffer1[i]!=buffer2[i])
      return 1;
  }
  return 0;
}

double MeasureOSWAPBuffer(unsigned char *buf, size_t N, size_t block_size){
  
  unsigned char buffer[block_size];
  unsigned char *bfr_ptr = buf;
  long t0, t1;
  ocall_clock(&t0);

  for(size_t i =0; i<N; i++) {
    if(block_size==8){
      oswap_buffer<OSWAP_8>(buffer, bfr_ptr, block_size, (bfr_ptr[0] & 1));
    } else if(block_size%16==0){
      oswap_buffer<OSWAP_16X>(buffer, bfr_ptr, block_size, (bfr_ptr[0] & 1));
    } else {
      oswap_buffer<OSWAP_8_16X>(buffer, bfr_ptr, block_size, (bfr_ptr[0] & 1));
    }
    bfr_ptr+=block_size; 
  }
  
  ocall_clock(&t1);
  
  double ptime = ((double)(t1-t0))/1000.0;
  return ptime;
}


/*
  Weak test for non-order preserving TightCompaction
  This test just checks that the first r blockse (where r = SUM(selected_list)) are real 
  blocks in compacted_buffer.
*/




/*
bool generateAndSealKeys(unsigned char *bin_x_p, unsigned char *bin_y_p, unsigned char *bin_r_p, unsigned char *bin_s_p){
  EC_KEY *ec_signing = NULL;
  ECDSA_SIG *sig_sgxssl = NULL, *sig_pubkey = NULL;
  const EC_POINT* pub_point = NULL;
  const BIGNUM *sig_r, *sig_s;
  unsigned char *bin_x, *bin_y, *bin_r, *bin_s;
  BIGNUM *x, *y;
  int ret;

  // Setup hardcoded Enclave Signing Key
  BIGNUM *r;
  r = BN_new();
  r = BN_bin2bn(hardcoded_signing_key, SGX_ECP256_KEY_SIZE, NULL);

  ec_signing = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (ec_signing == NULL) {
    printf("Enclave: EC_KEY_new_by_curve_name failure: %ld\n", ERR_get_error());
    return false;
  }

  ret = EC_KEY_set_private_key(ec_signing, r);
  if(ret==0)
    printf("Error with EC_KEY_set_private_key()\n");

  // Sample new (ephemeral) asymmetric key pair
  x = BN_new();
  y = BN_new();
  BN_CTX *bn_ctx = BN_CTX_new();
  EC_GROUP *ec_group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);

  sgx_EC_key_pair = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (sgx_EC_key_pair == NULL) {
    printf("Enclave: EC_KEY_new_by_curve_name failure: %ld\n", ERR_get_error());
    return false;
  }

  //Generate an EC Key pair
  if (!EC_KEY_generate_key(sgx_EC_key_pair))
    printf("Enclave: Sampling keys failed\n");

  //Get x,y from ephemeral_key
  pub_point = EC_KEY_get0_public_key(sgx_EC_key_pair);
  if(pub_point == NULL)
    printf("Enclave: EC_KEY_get0_public_key Failed \n");

  ret = EC_POINT_get_affine_coordinates_GFp(ec_group, pub_point, x, y, bn_ctx);
  if(ret==0)
    printf("Enclave: EC_POINT_get_affine_coordinates_GFp Failed \n");
  // End of sampling keys

  //TODO: Seal Keys
  //Serialize public_key x,y to binary
  uint32_t size_bin_x = BN_num_bytes(x), size_bin_y = BN_num_bytes(y);
  SerializeBNPair(x, y, &bin_x, &bin_y);
  //Sign the ephemeral key pair before publishing
  //Sign (bin_x||bin_y)
  uint32_t serialized_pub_key_size = size_bin_x + size_bin_y;
  unsigned char *serialized_pub_key = (unsigned char*) malloc(serialized_pub_key_size);
  memcpy(serialized_pub_key, bin_x, size_bin_x);
  memcpy(serialized_pub_key + size_bin_x, bin_y, size_bin_y);
  BIGNUM *kinv = NULL, *rp = NULL;
  ECDSA_sign_setup(ec_signing, NULL, &kinv, &rp);
  sig_pubkey = ECDSA_do_sign_ex((const unsigned char*) serialized_pub_key, serialized_pub_key_size, kinv, rp, ec_signing);
  if(sig_pubkey == NULL)
    printf("Enclave: ECDSA_do_sign_ex ERROR\n");

  //Serialize signature
  ECDSA_SIG_get0(sig_pubkey, &sig_r, &sig_s);
  uint32_t size_bin_r = BN_num_bytes(sig_r), size_bin_s = BN_num_bytes(sig_s);
  SerializeBNPair((BIGNUM*) sig_r, (BIGNUM*) sig_s, &bin_r, &bin_s);

  //Publish ephemeral key pair and the signature
  //PublishKey(bin_x, size_bin_x, bin_y, size_bin_y, bin_r, bin_s, size_bin_r, size_bin_s);

  memcpy(bin_x_p, bin_x, size_bin_x);
  memcpy(bin_y_p, bin_y, size_bin_y);
  memcpy(bin_r_p, bin_r, size_bin_r);
  memcpy(bin_s_p, bin_s, size_bin_s);
  free(serialized_pub_key);
  free(bin_x);
  free(bin_y);
  free(bin_r);
  free(bin_s);
  return true;
}
*/

bool extractSealedKeys(){
  /*
  uint32_t sealed_keys_size = ;
  unsigned char *sealed_keys = malloc(sealed_keys_size);
    
  // OCALL to outside to extract sealed key
  bool return_value=false;
  retrieveSealedKeys(&return_value, sealed_keys, sealed_keys_size);

  if(return_value==false){
    return false;
  }
  else{
    //Parse the sealed blob, extract keys
    return true
  }
  */

  return false;
}

/*
int8_t InitializeKeys(unsigned char *bin_x,  unsigned char* bin_y,
       unsigned char* bin_r, unsigned char* bin_s, uint32_t size_bin){

  if(!PK_in_memory){
    //Attempt to extract a previously sealed key-pair
    if(!extractSealedKeys()){
      generateAndSealKeys(bin_x, bin_y, bin_r, bin_s);
    }
  }
  return 1;
}
*/



