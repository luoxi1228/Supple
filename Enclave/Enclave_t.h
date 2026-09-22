#ifndef ENCLAVE_T_H__
#define ENCLAVE_T_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include "sgx_edger8r.h" /* for sgx_ocall etc. */

#include "stdbool.h"
#include "user_types.h"
#include "../Globals.hpp"
#include "../CONFIG.h"

#include <stdlib.h> /* for size_t */

#define SGX_CAST(type, item) ((type)(item))

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _struct_foo_t
#define _struct_foo_t
typedef struct struct_foo_t {
	uint32_t struct_foo_0;
	uint64_t struct_foo_1;
} struct_foo_t;
#endif

typedef enum enum_foo_t {
	ENUM_FOO_0 = 0,
	ENUM_FOO_1 = 1,
} enum_foo_t;

#ifndef _union_foo_t
#define _union_foo_t
typedef union union_foo_t {
	uint32_t union_foo_0;
	uint32_t union_foo_1;
	uint64_t union_foo_3;
} union_foo_t;
#endif

void Enclave_loadTestKeys(unsigned char inkey[16], unsigned char outkey[16]);
double MeasureOSWAPBuffer(unsigned char* buf, size_t N, size_t block_size);
double MeasureObliviousPrimitive(unsigned char* buf, size_t pairs, size_t block_size, uint8_t primitive, uint8_t flag0, uint8_t flag1);
void ecall_type_char(char val);
void ecall_type_int(int val);
void ecall_type_float(float val);
void ecall_type_double(double val);
void ecall_type_size_t(size_t val);
void ecall_type_wchar_t(wchar_t val);
void ecall_type_struct(struct struct_foo_t val);
void ecall_type_enum_union(enum enum_foo_t val1, union union_foo_t* val2);
size_t ecall_pointer_user_check(void* val, size_t sz);
void ecall_pointer_in(int* val);
void ecall_pointer_out(int* val);
void ecall_pointer_in_out(int* val);
void ecall_pointer_string(char* str);
void ecall_pointer_string_const(const char* str);
void ecall_pointer_size(void* ptr, size_t len);
void ecall_pointer_count(int* arr, int cnt);
void ecall_pointer_isptr_readonly(buffer_t buf, size_t len);
void ocall_pointer_attr(void);
void ecall_array_user_check(int arr[4]);
void ecall_array_in(int arr[4]);
void ecall_array_out(int arr[4]);
void ecall_array_in_out(int arr[4]);
void ecall_array_isary(array_t arr);
void ecall_function_calling_convs(void);
void ecall_function_public(void);
int ecall_function_private(void);
void ecall_malloc_free(void);
void ecall_sgx_cpuid(int cpuinfo[4], int leaf);
void ecall_exception(void);
void ecall_map(void);
size_t ecall_increase_counter(void);
void ecall_producer(void);
void ecall_consumer(void);
void RecursiveShuffle_M1(unsigned char* buf, uint64_t N, size_t block_size);
void RecursiveShuffle_M2(unsigned char* buf, uint64_t N, size_t block_size);
double RecursiveShuffle_M2_opt(unsigned char* buf, uint64_t N, size_t block_size);
double DecryptAndShuffleM1(unsigned char* encrypted_buffer, size_t N, size_t encrypted_block_size, unsigned char* result_buffer, enc_ret* ret);
double DecryptAndShuffleM2(unsigned char* encrypted_buffer, size_t N, size_t encrypted_block_size, size_t nthreads, unsigned char* result_buffer, enc_ret* ret);
double testTightCompaction(unsigned char* buffer, size_t N, size_t block_size, size_t nthreads, bool* selected_list, enc_ret* ret);
double testOPTightCompaction(unsigned char* buffer, size_t N, size_t block_size, bool* selected_list, enc_ret* ret);
void decryptAndSubSample(unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
void decryptAndSubSampleMulti(unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
void decryptAndSubSampleMulti_opt(unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
void DecPSQF_single(unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
void DecPSQF_SWO(unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
void DecSuppleSWO(unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encrypt_result_buffer, enc_ret* ret);
void DecSuppleSWO_parallel(unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encrypt_result_buffer, enc_ret* ret, size_t nthreads);
int FMSCompactPrepare(uint8_t* routing_tags, size_t n, size_t n_left, size_t n_right, size_t* control_words, double* control_us);
double FMSCompactOnline(unsigned char* buffer, size_t n, size_t block_size);
double OCompactOnline(unsigned char* buffer, size_t n, size_t block_size);
void FMSCompactRelease(void);

sgx_status_t SGX_CDECL ocall_print_string(const char* str);
sgx_status_t SGX_CDECL ocall_print_string_with_rtclock(unsigned long int* retval, const char* str);
sgx_status_t SGX_CDECL ocall_print_string_with_rtclock_diff(unsigned long int* retval, const char* str, unsigned long int before);
sgx_status_t SGX_CDECL untrustedMemAllocate(unsigned char** retval, uint64_t size);
sgx_status_t SGX_CDECL untrustedMemFree(unsigned char* mem);
sgx_status_t SGX_CDECL ocall_clock(long int* retval);
sgx_status_t SGX_CDECL ocall_wallclock(double* retval, int start_or_stop);
sgx_status_t SGX_CDECL pthread_wait_timeout_ocall(int* retval, unsigned long long waiter, unsigned long long timeout);
sgx_status_t SGX_CDECL pthread_create_ocall(int* retval, unsigned long long self);
sgx_status_t SGX_CDECL pthread_wakeup_ocall(int* retval, unsigned long long waiter);
sgx_status_t SGX_CDECL ocall_pointer_user_check(int* val);
sgx_status_t SGX_CDECL ocall_pointer_in(int* val);
sgx_status_t SGX_CDECL ocall_pointer_out(int* val);
sgx_status_t SGX_CDECL ocall_pointer_in_out(int* val);
sgx_status_t SGX_CDECL memccpy(void** retval, void* dest, const void* src, int val, size_t len);
sgx_status_t SGX_CDECL ocall_function_allow(void);
sgx_status_t SGX_CDECL sgx_oc_cpuidex(int cpuinfo[4], int leaf, int subleaf);
sgx_status_t SGX_CDECL sgx_thread_wait_untrusted_event_ocall(int* retval, const void* self);
sgx_status_t SGX_CDECL sgx_thread_set_untrusted_event_ocall(int* retval, const void* waiter);
sgx_status_t SGX_CDECL sgx_thread_setwait_untrusted_events_ocall(int* retval, const void* waiter, const void* self);
sgx_status_t SGX_CDECL sgx_thread_set_multiple_untrusted_events_ocall(int* retval, const void** waiters, size_t total);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
