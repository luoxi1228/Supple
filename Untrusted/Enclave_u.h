#ifndef ENCLAVE_U_H__
#define ENCLAVE_U_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include <string.h>
#include "sgx_edger8r.h" /* for sgx_status_t etc. */

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

#ifndef OCALL_PRINT_STRING_DEFINED__
#define OCALL_PRINT_STRING_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_print_string, (const char* str));
#endif
#ifndef OCALL_PRINT_STRING_WITH_RTCLOCK_DEFINED__
#define OCALL_PRINT_STRING_WITH_RTCLOCK_DEFINED__
unsigned long int SGX_UBRIDGE(SGX_NOCONVENTION, ocall_print_string_with_rtclock, (const char* str));
#endif
#ifndef OCALL_PRINT_STRING_WITH_RTCLOCK_DIFF_DEFINED__
#define OCALL_PRINT_STRING_WITH_RTCLOCK_DIFF_DEFINED__
unsigned long int SGX_UBRIDGE(SGX_NOCONVENTION, ocall_print_string_with_rtclock_diff, (const char* str, unsigned long int before));
#endif
#ifndef UNTRUSTEDMEMALLOCATE_DEFINED__
#define UNTRUSTEDMEMALLOCATE_DEFINED__
unsigned char* SGX_UBRIDGE(SGX_NOCONVENTION, untrustedMemAllocate, (uint64_t size));
#endif
#ifndef UNTRUSTEDMEMFREE_DEFINED__
#define UNTRUSTEDMEMFREE_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, untrustedMemFree, (unsigned char* mem));
#endif
#ifndef OCALL_CLOCK_DEFINED__
#define OCALL_CLOCK_DEFINED__
long int SGX_UBRIDGE(SGX_NOCONVENTION, ocall_clock, (void));
#endif
#ifndef OCALL_WALLCLOCK_DEFINED__
#define OCALL_WALLCLOCK_DEFINED__
double SGX_UBRIDGE(SGX_NOCONVENTION, ocall_wallclock, (int start_or_stop));
#endif
#ifndef PTHREAD_WAIT_TIMEOUT_OCALL_DEFINED__
#define PTHREAD_WAIT_TIMEOUT_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, pthread_wait_timeout_ocall, (unsigned long long waiter, unsigned long long timeout));
#endif
#ifndef PTHREAD_CREATE_OCALL_DEFINED__
#define PTHREAD_CREATE_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, pthread_create_ocall, (unsigned long long self));
#endif
#ifndef PTHREAD_WAKEUP_OCALL_DEFINED__
#define PTHREAD_WAKEUP_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, pthread_wakeup_ocall, (unsigned long long waiter));
#endif
#ifndef OCALL_POINTER_USER_CHECK_DEFINED__
#define OCALL_POINTER_USER_CHECK_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_pointer_user_check, (int* val));
#endif
#ifndef OCALL_POINTER_IN_DEFINED__
#define OCALL_POINTER_IN_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_pointer_in, (int* val));
#endif
#ifndef OCALL_POINTER_OUT_DEFINED__
#define OCALL_POINTER_OUT_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_pointer_out, (int* val));
#endif
#ifndef OCALL_POINTER_IN_OUT_DEFINED__
#define OCALL_POINTER_IN_OUT_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_pointer_in_out, (int* val));
#endif
#ifndef MEMCCPY_DEFINED__
#define MEMCCPY_DEFINED__
SGX_DLLIMPORT void* SGX_UBRIDGE(SGX_CDECL, memccpy, (void* dest, const void* src, int val, size_t len));
#endif
#ifndef OCALL_FUNCTION_ALLOW_DEFINED__
#define OCALL_FUNCTION_ALLOW_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_function_allow, (void));
#endif
#ifndef SGX_OC_CPUIDEX_DEFINED__
#define SGX_OC_CPUIDEX_DEFINED__
void SGX_UBRIDGE(SGX_CDECL, sgx_oc_cpuidex, (int cpuinfo[4], int leaf, int subleaf));
#endif
#ifndef SGX_THREAD_WAIT_UNTRUSTED_EVENT_OCALL_DEFINED__
#define SGX_THREAD_WAIT_UNTRUSTED_EVENT_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_wait_untrusted_event_ocall, (const void* self));
#endif
#ifndef SGX_THREAD_SET_UNTRUSTED_EVENT_OCALL_DEFINED__
#define SGX_THREAD_SET_UNTRUSTED_EVENT_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_set_untrusted_event_ocall, (const void* waiter));
#endif
#ifndef SGX_THREAD_SETWAIT_UNTRUSTED_EVENTS_OCALL_DEFINED__
#define SGX_THREAD_SETWAIT_UNTRUSTED_EVENTS_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_setwait_untrusted_events_ocall, (const void* waiter, const void* self));
#endif
#ifndef SGX_THREAD_SET_MULTIPLE_UNTRUSTED_EVENTS_OCALL_DEFINED__
#define SGX_THREAD_SET_MULTIPLE_UNTRUSTED_EVENTS_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_set_multiple_untrusted_events_ocall, (const void** waiters, size_t total));
#endif

sgx_status_t Enclave_loadTestKeys(sgx_enclave_id_t eid, unsigned char inkey[16], unsigned char outkey[16]);
sgx_status_t MeasureOSWAPBuffer(sgx_enclave_id_t eid, double* retval, unsigned char* buf, size_t N, size_t block_size);
sgx_status_t MeasureObliviousPrimitive(sgx_enclave_id_t eid, double* retval, unsigned char* buf, size_t pairs, size_t block_size, uint8_t primitive, uint8_t flag0, uint8_t flag1);
sgx_status_t ecall_type_char(sgx_enclave_id_t eid, char val);
sgx_status_t ecall_type_int(sgx_enclave_id_t eid, int val);
sgx_status_t ecall_type_float(sgx_enclave_id_t eid, float val);
sgx_status_t ecall_type_double(sgx_enclave_id_t eid, double val);
sgx_status_t ecall_type_size_t(sgx_enclave_id_t eid, size_t val);
sgx_status_t ecall_type_wchar_t(sgx_enclave_id_t eid, wchar_t val);
sgx_status_t ecall_type_struct(sgx_enclave_id_t eid, struct struct_foo_t val);
sgx_status_t ecall_type_enum_union(sgx_enclave_id_t eid, enum enum_foo_t val1, union union_foo_t* val2);
sgx_status_t ecall_pointer_user_check(sgx_enclave_id_t eid, size_t* retval, void* val, size_t sz);
sgx_status_t ecall_pointer_in(sgx_enclave_id_t eid, int* val);
sgx_status_t ecall_pointer_out(sgx_enclave_id_t eid, int* val);
sgx_status_t ecall_pointer_in_out(sgx_enclave_id_t eid, int* val);
sgx_status_t ecall_pointer_string(sgx_enclave_id_t eid, char* str);
sgx_status_t ecall_pointer_string_const(sgx_enclave_id_t eid, const char* str);
sgx_status_t ecall_pointer_size(sgx_enclave_id_t eid, void* ptr, size_t len);
sgx_status_t ecall_pointer_count(sgx_enclave_id_t eid, int* arr, int cnt);
sgx_status_t ecall_pointer_isptr_readonly(sgx_enclave_id_t eid, buffer_t buf, size_t len);
sgx_status_t ocall_pointer_attr(sgx_enclave_id_t eid);
sgx_status_t ecall_array_user_check(sgx_enclave_id_t eid, int arr[4]);
sgx_status_t ecall_array_in(sgx_enclave_id_t eid, int arr[4]);
sgx_status_t ecall_array_out(sgx_enclave_id_t eid, int arr[4]);
sgx_status_t ecall_array_in_out(sgx_enclave_id_t eid, int arr[4]);
sgx_status_t ecall_array_isary(sgx_enclave_id_t eid, array_t arr);
sgx_status_t ecall_function_calling_convs(sgx_enclave_id_t eid);
sgx_status_t ecall_function_public(sgx_enclave_id_t eid);
sgx_status_t ecall_function_private(sgx_enclave_id_t eid, int* retval);
sgx_status_t ecall_malloc_free(sgx_enclave_id_t eid);
sgx_status_t ecall_sgx_cpuid(sgx_enclave_id_t eid, int cpuinfo[4], int leaf);
sgx_status_t ecall_exception(sgx_enclave_id_t eid);
sgx_status_t ecall_map(sgx_enclave_id_t eid);
sgx_status_t ecall_increase_counter(sgx_enclave_id_t eid, size_t* retval);
sgx_status_t ecall_producer(sgx_enclave_id_t eid);
sgx_status_t ecall_consumer(sgx_enclave_id_t eid);
sgx_status_t RecursiveShuffle_M1(sgx_enclave_id_t eid, unsigned char* buf, uint64_t N, size_t block_size);
sgx_status_t RecursiveShuffle_M2(sgx_enclave_id_t eid, unsigned char* buf, uint64_t N, size_t block_size);
sgx_status_t RecursiveShuffle_M2_opt(sgx_enclave_id_t eid, double* retval, unsigned char* buf, uint64_t N, size_t block_size);
sgx_status_t DecryptAndShuffleM1(sgx_enclave_id_t eid, double* retval, unsigned char* encrypted_buffer, size_t N, size_t encrypted_block_size, unsigned char* result_buffer, enc_ret* ret);
sgx_status_t DecryptAndShuffleM2(sgx_enclave_id_t eid, double* retval, unsigned char* encrypted_buffer, size_t N, size_t encrypted_block_size, size_t nthreads, unsigned char* result_buffer, enc_ret* ret);
sgx_status_t testTightCompaction(sgx_enclave_id_t eid, double* retval, unsigned char* buffer, size_t N, size_t block_size, size_t nthreads, bool* selected_list, enc_ret* ret);
sgx_status_t testOPTightCompaction(sgx_enclave_id_t eid, double* retval, unsigned char* buffer, size_t N, size_t block_size, bool* selected_list, enc_ret* ret);
sgx_status_t decryptAndSubSample(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
sgx_status_t decryptAndSubSampleMulti(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
sgx_status_t decryptAndSubSampleMulti_opt(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
sgx_status_t DecPSQF_single(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
sgx_status_t DecPSQF_SWO(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret);
sgx_status_t DecSuppleSWO(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encrypt_result_buffer, enc_ret* ret);
sgx_status_t DecSuppleSWO_parallel(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encrypt_result_buffer, enc_ret* ret, size_t nthreads);
sgx_status_t FMSCompactPrepare(sgx_enclave_id_t eid, int* retval, uint8_t* routing_tags, size_t n, size_t n_left, size_t n_right, size_t* control_words, double* control_us);
sgx_status_t FMSCompactOnline(sgx_enclave_id_t eid, double* retval, unsigned char* buffer, size_t n, size_t block_size);
sgx_status_t OCompactOnline(sgx_enclave_id_t eid, double* retval, unsigned char* buffer, size_t n, size_t block_size);
sgx_status_t FMSCompactRelease(sgx_enclave_id_t eid);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
