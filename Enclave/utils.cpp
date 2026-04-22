
#include "utils.hpp"

PRB_buffer* PRB_pool;
thread_local uint64_t PRB_rand_bits = 0;
thread_local uint32_t PRB_rand_bits_remaining = 0;

bool bulk_initialized = false; 
sgx_aes_ctr_128bit_key_t bulk_random_seed[SGX_AESCTR_KEY_SIZE];
unsigned char bulk_counter[SGX_AESCTR_KEY_SIZE];


int compare(const void *buf1, const void *buf2) {
  uint64_t label1, label2;
  memcpy(&label1, (unsigned char*) buf1, 8);
  memcpy(&label2, (unsigned char*) buf2, 8);

  return((int)(label1 - label2));
}

int compare_32(const void *buf1, const void *buf2) {
  uint32_t label1, label2;
  memcpy(&label1, (unsigned char*) buf1, 4);
  memcpy(&label2, (unsigned char*) buf2, 4);

  return((int)(label1 - label2));
}

/* Debug function to see keys in a buffer*/
void displayKeysInBuffer(unsigned char *buffer, size_t N, size_t block_size){
  unsigned char *ptr = buffer;
  printf("Keys in displayKeysInBuffer:\n");
  for(size_t i=0; i<N; i++){
    size_t key = *((size_t*) ptr);
    ptr+=block_size;
    printf("%ld\n",key);
  }
  printf("\n\n");
}

#ifndef BEFTS_MODE
/*
  Decrypts buffers passed to the enclave that are encrypted with keys from Enclave_LoadTestKeys
  with AES_GCM. In addition it, gives each decrypted block an 8 byte random tag at the start.
  Intended for using SN as a shuffler, by sorting the blocks based on the attached random tags.

  The function assumes the provided encrypted buffer is initalized to the correct length.
  It returns a buffer of correct size (N * block_size_with_tag ) back to the function that
  invoked decryptBuffer, where block_size_with_tag = decrypted_block_size + 8

  The function returns the block_size_with_tag.
*/

size_t decryptBuffer_attachRTags_addDummies(unsigned char *encrypted_buffer, uint64_t N, 
        uint64_t N_prime, uint64_t B, uint64_t Z, size_t encrypted_block_size, 
        unsigned char *random_bytes, unsigned char **decrypted_buffer) {

  size_t decrypted_block_size = encrypted_block_size - SGX_AESGCM_IV_SIZE - SGX_AESGCM_MAC_SIZE;
  size_t block_size_with_tag = decrypted_block_size + 8;

  // If decrypted_buffer hasn't been allocated yet, allocate required memory to hold the decrypted
  // buffer
  if((*decrypted_buffer)==NULL){
    size_t mem_to_malloc = 2 * N_prime * block_size_with_tag;
    (*decrypted_buffer) = (unsigned char *) malloc(mem_to_malloc);
    if(*decrypted_buffer==NULL) {
      printf("Malloc failed in decryptBuffer_withAttachedRandomTags_interleaveDummies\n");
    }
  }

  unsigned char *dec_buf_ptr = *decrypted_buffer;
  unsigned char *enc_buf_ptr = encrypted_buffer;
  unsigned char *tag_ptr = enc_buf_ptr + SGX_AESGCM_IV_SIZE + decrypted_block_size;

  uint64_t reals_per_bucket = N / B;
  uint32_t num_buckets_with_extra_reals = N % B;
  uint64_t packets_to_extract = reals_per_bucket;
  for(size_t B_curr = 0; B_curr < B; B_curr++) {
    packets_to_extract = (B_curr < num_buckets_with_extra_reals)? reals_per_bucket+1 : reals_per_bucket;
    size_t num_dummies = Z - packets_to_extract;

    for(size_t i=0; i<packets_to_extract; i++) {
      uint64_t destination_bucket = (*((uint64_t*) random_bytes)) % B;
      memcpy(dec_buf_ptr, (unsigned char*) &destination_bucket, 8);
      random_bytes+=8;
      dec_buf_ptr+=8; 

      sgx_status_t aesret = sgx_rijndael128GCM_decrypt(
          &enclave_decryption_key, enc_buf_ptr + SGX_AESGCM_IV_SIZE, decrypted_block_size,
          dec_buf_ptr, enc_buf_ptr, SGX_AESGCM_IV_SIZE, NULL, 0,
          (const sgx_aes_gcm_128bit_tag_t*)(tag_ptr));
      if (aesret != SGX_SUCCESS) {
        printf("sgx_rijndael128GCM_decrypt failure (%x)\n", aesret);
        return -1;
      }

      dec_buf_ptr+=decrypted_block_size;
      enc_buf_ptr+=encrypted_block_size;
      tag_ptr+=encrypted_block_size;
    }

    for(size_t i=0; i<num_dummies; i++) {
      // Set the destination label to UINT64_MAX to indicate it's a dummy.
      // We don't care about the contents of the dummy, so whatever came from malloc is fine.
      *((uint64_t*) dec_buf_ptr) = UINT64_MAX;
      dec_buf_ptr+=block_size_with_tag;  
    }
  } 
  return(block_size_with_tag);
}


/*
  Decrypts buffers passed to the enclave that are encrypted with keys from Enclave_LoadTestKeys
  with AES_GCM. In addition it, gives each decrypted block an 8 byte random tag at the start.
  Intended for using SN as a shuffler, by sorting the blocks based on the attached randome tags.

  The function assumes the provided encrypted buffer is initalized to the correct length.
  It returns a buffer of correct size (N * block_size_with_tag ) back to the function that
  invoked decryptBuffer, where block_size_with_tag = decrypted_block_size + 8

  The function returns the block_size_with_tag.
*/

size_t decryptBuffer_attachRTags(unsigned char *encrypted_buffer, uint64_t N, size_t encrypted_block_size, unsigned char *random_bytes, unsigned char **decrypted_buffer) {

  size_t decrypted_block_size = encrypted_block_size - SGX_AESGCM_IV_SIZE - SGX_AESGCM_MAC_SIZE;
  size_t block_size_with_tag = decrypted_block_size + 8;

  // If decrypted_buffer hasn't been allocated yet, allocate required memory to hold the decrypted
  // buffer
  if((*decrypted_buffer)==NULL){
    (*decrypted_buffer) = (unsigned char *) malloc(N * block_size_with_tag);
    if(*decrypted_buffer==NULL) {
      printf("Malloc failed in decryptBuffer_withAttachedRandomTags\n");
    }
  }

  unsigned char *dec_buf_ptr = *decrypted_buffer;
  unsigned char *enc_buf_ptr = encrypted_buffer;
  unsigned char *tag_ptr = enc_buf_ptr + SGX_AESGCM_IV_SIZE + decrypted_block_size;

  
  for(size_t i =0; i<N; i++){
    memcpy(dec_buf_ptr, random_bytes, 8);
    random_bytes+=8;
    dec_buf_ptr+=8; 

    sgx_status_t aesret = sgx_rijndael128GCM_decrypt(
        &enclave_decryption_key, enc_buf_ptr + SGX_AESGCM_IV_SIZE, decrypted_block_size,
        dec_buf_ptr, enc_buf_ptr, SGX_AESGCM_IV_SIZE, NULL, 0,
        (const sgx_aes_gcm_128bit_tag_t*)(tag_ptr));
    if (aesret != SGX_SUCCESS) {
      printf("sgx_rijndael128GCM_decrypt failure (%x)\n", aesret);
      return -1;
    }
    
    dec_buf_ptr+=decrypted_block_size;
    enc_buf_ptr+=encrypted_block_size;
    tag_ptr+=encrypted_block_size;
  }
  return(block_size_with_tag);
}


/*
  Decrypts buffers passed to the enclave that are encrypted with keys from Enclave_LoadTestKeys
  with AES_GCM.
  The function assumes the provided encrypted buffer is initalized to the correct length.
  It returns a buffer of correct size (N * decrypted_block_size) back to the function that
  invoked decryptBuffer.

  The function returns the decrypted_block_size.
*/

size_t decryptBuffer(unsigned char *encrypted_buffer, uint64_t N, size_t encrypted_block_size,
      unsigned char **decrypted_buffer) {
  
  
  size_t decrypted_block_size = encrypted_block_size - SGX_AESGCM_IV_SIZE - SGX_AESGCM_MAC_SIZE;
  // If decrypted_buffer hasn't been allocated yet, allocate required memory to hold the decrypted
  // buffer
  if((*decrypted_buffer)==NULL){
    (*decrypted_buffer) = (unsigned char *) malloc(N * decrypted_block_size);
    if(*decrypted_buffer==NULL) {
      printf("Malloc failed in decryptBuffer for %ld bytes\n", (N*decrypted_block_size));
    }
  }

  unsigned char *dec_buf_ptr = *decrypted_buffer;
  unsigned char *enc_buf_ptr = encrypted_buffer;
  unsigned char *tag_ptr = enc_buf_ptr + SGX_AESGCM_IV_SIZE + decrypted_block_size;

  
  for(size_t i =0; i<N; i++){
    sgx_status_t aesret = sgx_rijndael128GCM_decrypt(
        &enclave_decryption_key, enc_buf_ptr + SGX_AESGCM_IV_SIZE, decrypted_block_size,
        dec_buf_ptr, enc_buf_ptr, SGX_AESGCM_IV_SIZE, NULL, 0,
        (const sgx_aes_gcm_128bit_tag_t*)(tag_ptr));
    if (aesret != SGX_SUCCESS) {
      printf("sgx_rijndael128GCM_decrypt failure (%x)\n", aesret);
      return -1;
    }
    
    dec_buf_ptr+=decrypted_block_size;
    enc_buf_ptr+=encrypted_block_size;
    tag_ptr+=encrypted_block_size;
  }
  return(decrypted_block_size);
}

/*
  Encrypts buffers going out of the Enclave using AESGCM with keys from Enclave_LoadTestKeys.
  The function assumes the buffers are initalized with the correct length.

  Unlike decryptBuffers, encryptBuffer expects the encrypted_buffer of correct size to be passed to it
  and it populates it with encryptions of blocks from decrypted_buffer.
  (This is done to avoid unnecessary additional copying of the encrypted buffer to a result buffer
    passed by the outside application to the enclave)
*/

size_t encryptBuffer(unsigned char *decrypted_buffer, uint64_t N, size_t decrypted_block_size,
      unsigned char *encrypted_buffer) {

  size_t encrypted_block_size = decrypted_block_size + SGX_AESGCM_IV_SIZE + SGX_AESGCM_MAC_SIZE;

  unsigned char *dec_buf_ptr = decrypted_buffer;
  unsigned char *enc_buf_ptr = encrypted_buffer;
  unsigned char *tag_ptr = enc_buf_ptr + SGX_AESGCM_IV_SIZE + decrypted_block_size;

  for(size_t i =0; i<N; i++){
    getRandomBytes(enc_buf_ptr, SGX_AESGCM_IV_SIZE);
    sgx_status_t aesret = sgx_rijndael128GCM_encrypt(
        &enclave_encryption_key, dec_buf_ptr, decrypted_block_size,
        enc_buf_ptr + SGX_AESGCM_IV_SIZE, enc_buf_ptr, SGX_AESGCM_IV_SIZE, NULL, 0,
        (sgx_aes_gcm_128bit_tag_t*)(tag_ptr));
    if (aesret != SGX_SUCCESS) {
      printf("sgx_rijndael128GCM_encrypt failure (%x)\n", aesret);
      return -1;
    }
    dec_buf_ptr+=decrypted_block_size;
    enc_buf_ptr+=encrypted_block_size;
    tag_ptr+=encrypted_block_size;
  }

  return(encrypted_block_size);
}

/*
  Removes the random tags attached by decryptBuffer_attachRTags before encrypting the buffer.
*/
size_t encryptBuffer_removeRTags(unsigned char *decrypted_buffer, uint64_t N, 
        size_t decrypted_block_size, unsigned char *encrypted_buffer) {

  size_t real_block_size = decrypted_block_size - 8;
  size_t encrypted_block_size = real_block_size + SGX_AESGCM_IV_SIZE + SGX_AESGCM_MAC_SIZE;

  unsigned char *dec_buf_ptr = decrypted_buffer;
  unsigned char *enc_buf_ptr = encrypted_buffer;
  unsigned char *tag_ptr = enc_buf_ptr + SGX_AESGCM_IV_SIZE + real_block_size;

  for(size_t i =0; i<N; i++){
    //Skip the attached random tag
    dec_buf_ptr+=8;
    getRandomBytes(enc_buf_ptr, SGX_AESGCM_IV_SIZE);
    sgx_status_t aesret = sgx_rijndael128GCM_encrypt(
        &enclave_encryption_key, dec_buf_ptr, real_block_size,
        enc_buf_ptr + SGX_AESGCM_IV_SIZE, enc_buf_ptr, SGX_AESGCM_IV_SIZE, NULL, 0,
        (sgx_aes_gcm_128bit_tag_t*)(tag_ptr));
    if (aesret != SGX_SUCCESS) {
      printf("i = %d\n", i);
      printf("sgx_rijndael128GCM_encrypt failure (%x)\n", aesret);
      return -1;
    }
    dec_buf_ptr+=real_block_size;
    enc_buf_ptr+=encrypted_block_size;
    tag_ptr+=encrypted_block_size;
  }
  return(encrypted_block_size);
}

#endif

// Returns log2 rounded up.
int calculatelog2(uint64_t value){
  int log2v = 0;
  uint64_t temp = 1;
  while(temp<value){
    temp=temp<<1;
    log2v+=1;
  }
  return log2v;
}

int calculatelog2_floor(uint64_t value){
  int log2v = 0;
  uint64_t temp = 1;
  while(temp<value){
    temp=temp<<1;
    log2v+=1;
  }
  if(temp==value)
    return log2v;
  else
    return log2v-1;
}

// Returns largest power of two less than N
uint64_t pow2_lt(uint64_t N) {
  uint64_t N1 = 1;
  while (N1 < N) {
    N1 <<= 1;
  }
  N1 >>= 1;
  return N1;
}


// Returns largest power of two greater than N
uint64_t pow2_gt(uint64_t N) {
  uint64_t N1 = 1;
  while (N1 < N) {
    N1 <<= 1;
  }
  return N1;
}

#ifndef BEFTS_MODE
/*
 * printf:
 *   Invokes OCALL to display the enclave buffer to the terminal.
 */
void printf(const char *fmt, ...)
{
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
}

/*
 * printf_with_rtclock:
 *   Invokes OCALL to display the enclave buffer to the terminal with a
 *   timestamp and returns the timestamp.
 */
unsigned long printf_with_rtclock(const char *fmt, ...)
{
    unsigned long ret;
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string_with_rtclock(&ret, buf);
    return ret;
}

/*
 * printf_with_rtclock_diff:
 *   Invokes OCALL to display the enclave buffer to the terminal with a
 *   timestamp and returns the timestamp.  Also prints the difference from
 *   the before timestamp.
 */
unsigned long printf_with_rtclock_diff(unsigned long before, const char *fmt, ...)
{
    unsigned long ret;
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string_with_rtclock_diff(&ret, buf, before);
    return ret;
}
#endif

void displayORPPacket(unsigned char* packet_in, size_t block_size) {
  unsigned char *packet_ptr = packet_in;
  uint64_t evict_stream, ORP_label, key;
  unsigned char data[block_size];

  memcpy(&evict_stream, packet_ptr, sizeof(uint64_t));
  packet_ptr+=sizeof(uint64_t);
  memcpy(&ORP_label, packet_ptr, sizeof(uint64_t));
  packet_ptr+=sizeof(uint64_t);
  memcpy(&key, packet_ptr, sizeof(uint64_t));
  packet_ptr+=sizeof(uint64_t);
  memcpy(data, packet_ptr, block_size);

  data[block_size]='\0';
  printf("(evict_stream = %ld, ORP_label = %ld, Key = %ld)\n",
        evict_stream, ORP_label, key);
  //printf("Hex of data is :");
  //for(int i=0;i<DATA_SIZE;++i) printf("%02x", data[i]); printf("\n");
}


// isDummy and setDummy works on real packets : <Key, Data>
bool isDummy(unsigned char *ptr_to_serialized_packet){
  return(((uint64_t*) ptr_to_serialized_packet)[0] == UINT64_MAX);
}

void setDummy(unsigned char *ptr_to_serialized_packet){
  ((uint64_t*) ptr_to_serialized_packet)[0] = UINT64_MAX;
}


// isORPDummy and setORPDummy works on ORP packets : <Eviction_stream, ORP_label, Key, Data>

bool isORPDummy(unsigned char *ptr_to_serialized_packet){
  return(((uint64_t*) ptr_to_serialized_packet)[1] == UINT64_MAX);
}

void setORPDummy(unsigned char *ptr_to_packet){
  ((uint64_t*) ptr_to_packet)[0] = UINT64_MAX;
  ((uint64_t*) ptr_to_packet)[1] = UINT64_MAX;
  ((uint64_t*) ptr_to_packet)[2] = UINT64_MAX;
}


size_t packetsConsumedUptoMSN(signed long msn_no, size_t msns_with_extra_packets, size_t packets_per_entry_msn) {
  if(msn_no<0)
    return 0;

  if(msn_no<=msns_with_extra_packets){
    return (msn_no * (packets_per_entry_msn+1));
  }
  else{
    size_t reg_msn = msn_no - msns_with_extra_packets;
    return ((reg_msn * packets_per_entry_msn) + (msns_with_extra_packets * packets_per_entry_msn));
  } 
}


#ifdef USE_PRB
  void PRB_pool_init(int nthreads) {
    PRB_pool = new PRB_buffer[nthreads];
  }

  void PRB_pool_shutdown() {
    delete [] PRB_pool;
  }

  PRB_buffer::PRB_buffer() {
  }

  PRB_buffer::~PRB_buffer() {
  }

  sgx_status_t PRB_buffer::init_PRB_buffer(uint32_t buffer_size = PRB_BUFFER_SIZE) {
    sgx_status_t rt = SGX_SUCCESS;
    if(initialized==false) {
      rt = sgx_read_rand((unsigned char*) random_seed, SGX_AESCTR_KEY_SIZE);
      if(rt!=SGX_SUCCESS){
        printf("Failed sgx_read_rand (%x)", rt);
        return rt;
      }
      rt = sgx_read_rand((unsigned char*) counter, SGX_AESCTR_KEY_SIZE);
      if(rt!=SGX_SUCCESS){
        printf("Failed sgx_read_rand (%x)", rt);
        return rt;
      }
      initialized=true;
    }

    char zeroes[buffer_size];
    // We don't bother initializing to zeroes since AES_CTR just adds the PRB_stream to the buffer
    // Use AES CTR to populate random_bytes
    rt = sgx_aes_ctr_encrypt(random_seed, (const uint8_t*) zeroes, buffer_size,
          (uint8_t*) counter, CTR_INC_BITS, random_bytes);
    *(uint64_t*)counter += 1;
    if(rt!=SGX_SUCCESS){
      printf("Failed sgx_aes_ctr_encrypt (%x) in init_getRandomBytes\n", rt);
      return rt;
    }
    random_bytes_left = PRB_BUFFER_SIZE;
    random_bytes_ptr = random_bytes;
    return rt;
  }


  sgx_status_t PRB_buffer::getRandomBytes(unsigned char *buffer, size_t size) {
    sgx_status_t rt = SGX_SUCCESS;
    
    if(initialized==false)
      init_PRB_buffer();

    if(size < random_bytes_left) {
      // Supply buffer with random bytes from random_bytes
      memcpy(buffer, random_bytes_ptr, size);
      random_bytes_ptr+=size;
      random_bytes_left-= size;
      return rt;
    } else {
      // Consume all the random bytes we have left
      unsigned char *ptr = buffer;
      size_t size_left_for_req = size - random_bytes_left;
      memcpy(ptr, random_bytes_ptr, random_bytes_left);
      ptr+= random_bytes_left;

      // Use AES CTR to populate random_bytes
      rt = sgx_aes_ctr_encrypt(random_seed, (const uint8_t*) random_bytes, PRB_BUFFER_SIZE,
            (uint8_t*) counter, CTR_INC_BITS, random_bytes);
      *(uint64_t*)counter += 1;
      if(rt!=SGX_SUCCESS){
        printf("Failed sgx_aes_ctr_encrypt (%x)", rt);
        return rt;
      }
      random_bytes_left = PRB_BUFFER_SIZE;
      random_bytes_ptr = random_bytes;

      // Add size_left_for_req random bytes to the buffer
      memcpy(ptr, random_bytes_ptr, size_left_for_req);
      random_bytes_ptr+=size_left_for_req;
      random_bytes_left-=size_left_for_req;
      return rt;
    }
  }

  sgx_status_t PRB_buffer::getBulkRandomBytes(unsigned char *buffer, size_t size) {
    sgx_status_t rt = SGX_SUCCESS;
    rt = sgx_aes_ctr_encrypt(random_seed, (const uint8_t*) buffer, size,
          (uint8_t*) counter, CTR_INC_BITS, buffer);
    *(uint64_t*)counter += 1;
    
    if(rt!=SGX_SUCCESS){
      printf("Failed sgx_aes_ctr_encrypt (%x) in getBulkRandomBytes [%p %p %lu %p %d %p]\n", rt, random_seed, (const uint8_t*) buffer, size, (uint8_t*) counter, CTR_INC_BITS, buffer);
      return rt;
    }
    return rt;
  }


  sgx_status_t initialize_BRB() {
    sgx_status_t rt = SGX_SUCCESS;
    rt = sgx_read_rand((unsigned char*) bulk_random_seed, SGX_AESCTR_KEY_SIZE);
    if(rt!=SGX_SUCCESS){
      printf("initialize_BRB(): Failed sgx_read_rand (%x)", rt);
      return rt;
    }
    rt = sgx_read_rand((unsigned char*) bulk_counter, SGX_AESCTR_KEY_SIZE);
    if(rt!=SGX_SUCCESS){
      printf("initialize_BRB(): Failed sgx_read_rand (%x)", rt);
      return rt;
    }
    bulk_initialized = true;
    return rt;
  }

  sgx_status_t getBulkRandomBytes(unsigned char *buffer, size_t size) {

    if(bulk_initialized == false){
     initialize_BRB();   
    }
    sgx_status_t rt = SGX_SUCCESS;
    rt = sgx_aes_ctr_encrypt(bulk_random_seed, (const uint8_t*) buffer, size,
          (uint8_t*) bulk_counter, CTR_INC_BITS, buffer);
    
    if(rt!=SGX_SUCCESS){
      printf("getBulkRandomBytes: Failed sgx_aes_ctr_encrypt (%x) in getBulkRandomBytes [%p %p %lu %p %d %p]\n", rt, bulk_random_seed, (const uint8_t*) buffer, size, (uint8_t*) bulk_counter, CTR_INC_BITS, buffer);
      return rt;
    }
    return rt;
  }
#else
  sgx_status_t getRandomBytes(unsigned char *random_bytes, size_t size) {
    sgx_status_t rt = SGX_SUCCESS;
    rt = sgx_read_rand((unsigned char*) random_bytes, size);
    return rt;
  }
#endif

unsigned char* compare_keys(unsigned char *packet_1, unsigned char *packet_2){
  if( *((uint64_t*)(packet_1)) < *((uint64_t*)(packet_2))){
    return packet_1;
  }
  else {
    return packet_2;
  }
}

void merge(unsigned char *data, size_t data_size, size_t l, size_t m, size_t r, unsigned char* (*comparator)(unsigned char*, unsigned char*)){
  uint64_t i=0, j=0, k=0;
  size_t s1, s2;

  s1 = l+(m-l+1);
  s2 = (m+1)+(r-m);

  //unsigned char merged_array[(r-l+1)*data_size];
  unsigned char *merged_array = (unsigned char*) malloc((r-l+1)*data_size);
  i = l;
  j = m+1;
  k = 0;

  while (i < s1 && j < s2) {
    unsigned char *smaller_pkt = comparator(data+(i*data_size), data+(j*data_size));
    if(smaller_pkt == data+(i*data_size)){
      memcpy(merged_array+(k*data_size), smaller_pkt, data_size);
      i++;
    }
    else{
      memcpy(merged_array+(k*data_size), smaller_pkt, data_size);
      j++;
    }
    k++;
  }

  while (i < s1) {
    memcpy(merged_array + (k*data_size), data+(i*data_size), data_size);
    i++;
    k++;
  }

  while (j < s2) {
    memcpy(merged_array + (k*data_size), data+(j*data_size), data_size);
    j++;
    k++;
  }

  memcpy(data+(l*data_size), merged_array, data_size * ((r-l)+1));
  free(merged_array);
}

void mergeSort(unsigned char *data, size_t data_size, size_t start_index, size_t end_index, unsigned char* (*comparator)(unsigned char*, unsigned char*)){
  if(start_index < end_index){

    size_t m = start_index + (end_index-start_index)/2;
    mergeSort(data, data_size, start_index, m, comparator);
    mergeSort(data, data_size, m+1, end_index, comparator);

    merge(data, data_size, start_index, m , end_index, comparator);
  }
}


void mergeSort_OPRM(unsigned char *data, size_t data_size, size_t start_index, size_t end_index, unsigned char* (*comparator)(unsigned char*, unsigned char*)){
  if(start_index < end_index){

    size_t m = start_index + (end_index-start_index)/2;
    mergeSort(data, data_size, start_index, m, comparator);
    mergeSort(data, data_size, m+1, end_index, comparator);

    merge(data, data_size, start_index, m , end_index, comparator);
  }
}

//Tight Compaction and Expansion utility functions for testing if a Block is real/dummy

uint8_t isBlockReal_16(unsigned char *block_ptr) {
  uint16_t label = *((uint16_t *)(block_ptr));
  return (label==UINT16_MAX);
}

uint8_t isBlockReal_32(unsigned char *block_ptr) {
  uint32_t label = *((uint32_t *)(block_ptr));
  return (label==UINT32_MAX);
}

uint8_t isBlockReal_64(unsigned char *block_ptr) {
  uint64_t label = *((uint64_t *)(block_ptr));
  return (label==UINT64_MAX);
}

void oswap_buffer(unsigned char *dest, unsigned char *source, uint32_t buffer_size, uint8_t flag){
  #ifdef COUNT_OSWAPS
    uint64_t *ltvp = &OSWAP_COUNTER;
    FOAV_SAFE2_CNTXT(oswap_buffer, buffer_size, *ltvp)
    OSWAP_COUNTER++;
  #endif
  if(buffer_size%16==0){
    oswap_buffer_16x(dest, source, buffer_size, flag);
  } else if(buffer_size==8){
    oswap_buffer_byte(dest, source, buffer_size, flag);
  }
  else{
    oswap_buffer_byte(dest, source, 8, flag);
    oswap_buffer_16x(dest+8, source+8, buffer_size-8, flag);
  }
}


uint8_t isCorrect16x(uint32_t block_size){
  printf("Entered Correctness Tester!!!\n");
  bool is_correct = true;
  unsigned char *b1 = new unsigned char[block_size];
  unsigned char *b2 = new unsigned char[block_size];
  unsigned char *b3 = new unsigned char[block_size];
  unsigned char *b4 = new unsigned char[block_size];
  
  getBulkRandomBytes(b1, block_size);
  getBulkRandomBytes(b2, block_size);
  memcpy(b3, b1, block_size);
  memcpy(b4, b2, block_size);

  bool swap_flag = false;
 
  oswap_buffer<OSWAP_16X>(b1, b2, block_size, swap_flag);
   
  if(memcmp(b1, b3, block_size)){
    is_correct=false;
    printf("Failed Test 1\n");
  }
    
  if(memcmp(b2, b4, block_size)){
    is_correct=false;
    printf("Failed Test 2\n");
  }

  memcpy(b1, b3, block_size);
  memcpy(b2, b4, block_size);

  swap_flag = true;
  oswap_buffer<OSWAP_16X>(b1, b2, block_size, swap_flag);
  if(memcmp(b1, b4, block_size)){
    is_correct=false;
    printf("Failed Test 3\n");
  }
    
  if(memcmp(b2, b3, block_size)){
    is_correct=false;
    printf("Failed Test 4\n");
  }
  

  delete []b1;
  delete []b2; 
  delete []b3;
  delete []b4; 
  if(is_correct){
    printf("Correctness test SUCCESS! \n");
    return true;
  }
  return false; 
}


uint8_t isCorrect8_16x(uint32_t block_size){
  printf("Entered Correctness Tester!!!\n");
  bool is_correct = true;
  unsigned char *b1 = new unsigned char[block_size];
  unsigned char *b2 = new unsigned char[block_size];
  unsigned char *b3 = new unsigned char[block_size];
  unsigned char *b4 = new unsigned char[block_size];
  
  getBulkRandomBytes(b1, block_size);
  getBulkRandomBytes(b2, block_size);
  memcpy(b3, b1, block_size);
  memcpy(b4, b2, block_size);

  bool swap_flag = false;
 
  oswap_buffer<OSWAP_8_16X>(b1, b2, block_size, swap_flag);
   
  if(memcmp(b1, b3, block_size)){
    is_correct=false;
    printf("Failed Test 1\n");
  }
    
  if(memcmp(b2, b4, block_size)){
    is_correct=false;
    printf("Failed Test 2\n");
  }

  memcpy(b1, b3, block_size);
  memcpy(b2, b4, block_size);

  swap_flag = true;
  oswap_buffer<OSWAP_8_16X>(b1, b2, block_size, swap_flag);
  if(memcmp(b1, b4, block_size)){
    is_correct=false;
    printf("Failed Test 3\n");
  }
    
  if(memcmp(b2, b3, block_size)){
    is_correct=false;
    printf("Failed Test 4\n");
  }
  

  delete []b1;
  delete []b2; 
  delete []b3;
  delete []b4; 
  if(is_correct){
    printf("Correctness test SUCCESS! \n");
    return true;
  }
  return false; 
}


void swapBuckets(unsigned char *bkt1, unsigned char *bkt2, unsigned char *temp_bucket, size_t bucket_size) {
  memcpy(temp_bucket, bkt2, bucket_size);
  memcpy(bkt2, bkt1, bucket_size);
  memcpy(bkt1, temp_bucket, bucket_size);
}

/*** Thread pool implementation ***/

/* Implements a restricted-model thread pool.  The restriction is that
 * every thread is the "parent" of a number of other threads (and no
 * thread has more than one parent).  Each thread can be dispatched and
 * joined only by its parent, so there's no contention on the dispatch
 * and join inter-thread communication.  A parent thread has to specify
 * the exact thread id of the child thread it dispatches work to. */

thread_local threadid_t g_thread_id = 0;

enum threadstate_t {
    THREADSTATE_NONE,
    THREADSTATE_WAITING,
    THREADSTATE_DISPATCHING,
    THREADSTATE_WORKING,
    THREADSTATE_TERMINATE
};

struct threadblock_t {
    threadid_t threadid;
    threadstate_t state;
    pthread_t thread_handle;
    pthread_mutex_t mutex;
    pthread_cond_t dispatch_cond;
    void *(*dispatch_func)(void *data);
    void *dispatch_data;
    pthread_cond_t join_cond;
    void *ret_data;
#ifdef COUNT_OSWAPS
    size_t num_oswaps;
#endif
};

static threadblock_t *threadpool_control_blocks = NULL;
static threadid_t threadpool_numthreads = 0;

/* The main thread loop */
static void* threadloop(void *vdata) {
    threadblock_t *block = (threadblock_t *)vdata;

    /* Initialize any per-thread state */
    g_thread_id = block->threadid;
    PRB_rand_bits = 0;
    PRB_rand_bits_remaining = 0;

    pthread_mutex_lock(&block->mutex);
    while(1) {
        /* Wait for work */
        block->state = THREADSTATE_WAITING;
        pthread_cond_wait(&block->dispatch_cond, &block->mutex);

        if (block->state == THREADSTATE_TERMINATE) {
            break;
        }

        /* Do the work */
        block->state = THREADSTATE_WORKING;
        pthread_mutex_unlock(&block->mutex);
        block->ret_data = (block->dispatch_func)(block->dispatch_data);

#ifdef COUNT_OSWAPS
        /* Account for the oswaps done in this thread */
        block->num_oswaps = OSWAP_COUNTER;
        OSWAP_COUNTER = 0;
#endif

        /* Signal the parent thread that we're done, and loop back to
         * wait for more work. */
        pthread_mutex_lock(&block->mutex);
        pthread_cond_signal(&block->join_cond);
    }
    block->state = THREADSTATE_NONE;
    pthread_mutex_unlock(&block->mutex);

    return NULL;
}

/* Create the threadpool, with numthreads-1 additional threads (numbered
 * 1 through numthreads-1) in addition to the current "main" thread
 * (numbered 0). Returns 0 on success, -1 on failure. It is allowed, but
 * not very useful, to pass 1 here. */
int threadpool_init(threadid_t numthreads) {
    g_thread_id = 0;
    PRB_rand_bits = 0;
    PRB_rand_bits_remaining = 0;

    if (numthreads < 1) {
        return -1;
    } else if (numthreads == 1) {
        threadpool_numthreads = 1;
        return 0;
    }

    /* We don't actually create a thread control block for the main
     * thread 0, so the internal indexing into this array will be that
     * thread i's control block lives at index i-1 in this array. */
    threadpool_control_blocks = new threadblock_t[numthreads-1];
    if (threadpool_control_blocks == NULL) {
        return -1;
    }
    threadpool_numthreads = numthreads;

    /* Init each thread control block */
    bool thread_create_failure = false;
    for (threadid_t i = 0; i < numthreads-1; ++i) {
        threadblock_t *block = threadpool_control_blocks + i;
        block->threadid = i+1;
        block->state = THREADSTATE_NONE;
        pthread_mutex_init(&block->mutex, NULL);
        pthread_cond_init(&block->dispatch_cond, NULL);
        pthread_cond_init(&block->join_cond, NULL);
        block->thread_handle = NULL;
        int create_ret =
                pthread_create(&block->thread_handle, NULL, threadloop, block);
        if (create_ret) {
            thread_create_failure = true;
            printf("Failed to launch thread %lu; ret=%d\n", i+1, create_ret);
        }
    }

    if (thread_create_failure) {
        threadpool_shutdown();
        return -1;
    }

    return 0;
}

/* Ask all the threads to terminate, wait for that to happen, and clean
 * up. */
void threadpool_shutdown() {
    /* Note that this function may be called when some threads failed to
     * launch at all in threadpool_init. In that case, the thread field
     * in the thread's control block will be NULL.  The mutex/cond
     * variables will still have been initialized, however, and need
     * cleaning. */
    if (threadpool_numthreads == 0) {
        /* Nothing to do */
        return;
    }
    if (threadpool_numthreads == 1) {
        /* Almost nothing to do */
        threadpool_numthreads = 0;
        return;
    }
    for (threadid_t i=0;i<threadpool_numthreads-1; ++i) {
        threadblock_t *block = threadpool_control_blocks + i;
        pthread_mutex_lock(&block->mutex);
        if (block->state == THREADSTATE_WORKING) {
            /* There's a thread actively running?  Wait for it to
             * finish. */
            pthread_mutex_unlock(&block->mutex);
            threadpool_join(i+1, NULL);
            pthread_mutex_lock(&block->mutex);
        }
        if (block->state == THREADSTATE_WAITING) {
            /* Tell the thread to exit */
            block->state = THREADSTATE_TERMINATE;
            pthread_mutex_unlock(&block->mutex);
            pthread_cond_signal(&block->dispatch_cond);
            pthread_join(block->thread_handle, NULL);
            block->thread_handle = NULL;
        }
        if (block->state != THREADSTATE_NONE) {
            printf("Unexpected state on thread %lu during shutdown: %u\n", i+1, block->state);
            pthread_cond_destroy(&block->dispatch_cond);
            pthread_cond_destroy(&block->join_cond);
            pthread_mutex_destroy(&block->mutex);
        }
    }
    delete[] threadpool_control_blocks;
    threadpool_control_blocks = NULL;
    threadpool_numthreads = 0;
}

/* Dispatch some work to a particular thread in the thread pool. */
void threadpool_dispatch(threadid_t threadid, void *(*func)(void*),
        void *data) {
    threadblock_t *block = threadpool_control_blocks + (threadid-1);
    pthread_mutex_lock(&block->mutex);
    if (block->state != THREADSTATE_WAITING) {
        printf("Thread %lu not in expected WAITING state: %u\n",
            threadid, block->state);
        pthread_mutex_unlock(&block->mutex);
        return;
    }
    block->dispatch_func = func;
    block->dispatch_data = data;
    block->state = THREADSTATE_DISPATCHING;
    pthread_mutex_unlock(&block->mutex);
    /* Tell the thread there's work to do */
    pthread_cond_signal(&block->dispatch_cond);
}

/* Join a thread */
void threadpool_join(threadid_t threadid, void **resp) {
    threadblock_t *block = threadpool_control_blocks + (threadid-1);

    pthread_mutex_lock(&block->mutex);
    /* Did the thread finish already? */
    if (block->state == THREADSTATE_DISPATCHING ||
            block->state == THREADSTATE_WORKING) {
        /* Wait until the thread completes */
        pthread_cond_wait(&block->join_cond, &block->mutex);
    } else if (block->state != THREADSTATE_WAITING) {
        printf("Thread %lu in unexpected state (not WORKING or WAITING) on join: %u\n",
            threadid, block->state);
    }
    if (resp) {
        *resp = block->ret_data;
    }
#ifdef COUNT_OSWAPS
    uint64_t *ltvp = &OSWAP_COUNTER;
    FOAV_SAFE_CNTXT(oswap_buffer, *ltvp)
    OSWAP_COUNTER += block->num_oswaps;
    block->num_oswaps = 0;
#endif
    pthread_mutex_unlock(&block->mutex);
}
