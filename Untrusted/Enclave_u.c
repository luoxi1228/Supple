#include "Enclave_u.h"
#include <errno.h>

typedef struct ms_Enclave_loadTestKeys_t {
	unsigned char* ms_inkey;
	unsigned char* ms_outkey;
} ms_Enclave_loadTestKeys_t;

typedef struct ms_MeasureOSWAPBuffer_t {
	double ms_retval;
	unsigned char* ms_buf;
	size_t ms_N;
	size_t ms_block_size;
} ms_MeasureOSWAPBuffer_t;

typedef struct ms_MeasureObliviousPrimitive_t {
	double ms_retval;
	unsigned char* ms_buf;
	size_t ms_pairs;
	size_t ms_block_size;
	uint8_t ms_primitive;
	uint8_t ms_flag0;
	uint8_t ms_flag1;
} ms_MeasureObliviousPrimitive_t;

typedef struct ms_ecall_type_char_t {
	char ms_val;
} ms_ecall_type_char_t;

typedef struct ms_ecall_type_int_t {
	int ms_val;
} ms_ecall_type_int_t;

typedef struct ms_ecall_type_float_t {
	float ms_val;
} ms_ecall_type_float_t;

typedef struct ms_ecall_type_double_t {
	double ms_val;
} ms_ecall_type_double_t;

typedef struct ms_ecall_type_size_t_t {
	size_t ms_val;
} ms_ecall_type_size_t_t;

typedef struct ms_ecall_type_wchar_t_t {
	wchar_t ms_val;
} ms_ecall_type_wchar_t_t;

typedef struct ms_ecall_type_struct_t {
	struct struct_foo_t ms_val;
} ms_ecall_type_struct_t;

typedef struct ms_ecall_type_enum_union_t {
	enum enum_foo_t ms_val1;
	union union_foo_t* ms_val2;
} ms_ecall_type_enum_union_t;

typedef struct ms_ecall_pointer_user_check_t {
	size_t ms_retval;
	void* ms_val;
	size_t ms_sz;
} ms_ecall_pointer_user_check_t;

typedef struct ms_ecall_pointer_in_t {
	int* ms_val;
} ms_ecall_pointer_in_t;

typedef struct ms_ecall_pointer_out_t {
	int* ms_val;
} ms_ecall_pointer_out_t;

typedef struct ms_ecall_pointer_in_out_t {
	int* ms_val;
} ms_ecall_pointer_in_out_t;

typedef struct ms_ecall_pointer_string_t {
	char* ms_str;
	size_t ms_str_len;
} ms_ecall_pointer_string_t;

typedef struct ms_ecall_pointer_string_const_t {
	const char* ms_str;
	size_t ms_str_len;
} ms_ecall_pointer_string_const_t;

typedef struct ms_ecall_pointer_size_t {
	void* ms_ptr;
	size_t ms_len;
} ms_ecall_pointer_size_t;

typedef struct ms_ecall_pointer_count_t {
	int* ms_arr;
	int ms_cnt;
} ms_ecall_pointer_count_t;

typedef struct ms_ecall_pointer_isptr_readonly_t {
	buffer_t ms_buf;
	size_t ms_len;
} ms_ecall_pointer_isptr_readonly_t;

typedef struct ms_ecall_array_user_check_t {
	int* ms_arr;
} ms_ecall_array_user_check_t;

typedef struct ms_ecall_array_in_t {
	int* ms_arr;
} ms_ecall_array_in_t;

typedef struct ms_ecall_array_out_t {
	int* ms_arr;
} ms_ecall_array_out_t;

typedef struct ms_ecall_array_in_out_t {
	int* ms_arr;
} ms_ecall_array_in_out_t;

typedef struct ms_ecall_array_isary_t {
	array_t*  ms_arr;
} ms_ecall_array_isary_t;

typedef struct ms_ecall_function_private_t {
	int ms_retval;
} ms_ecall_function_private_t;

typedef struct ms_ecall_sgx_cpuid_t {
	int* ms_cpuinfo;
	int ms_leaf;
} ms_ecall_sgx_cpuid_t;

typedef struct ms_ecall_increase_counter_t {
	size_t ms_retval;
} ms_ecall_increase_counter_t;

typedef struct ms_RecursiveShuffle_M1_t {
	unsigned char* ms_buf;
	uint64_t ms_N;
	size_t ms_block_size;
} ms_RecursiveShuffle_M1_t;

typedef struct ms_RecursiveShuffle_M2_t {
	unsigned char* ms_buf;
	uint64_t ms_N;
	size_t ms_block_size;
} ms_RecursiveShuffle_M2_t;

typedef struct ms_RecursiveShuffle_M2_opt_t {
	double ms_retval;
	unsigned char* ms_buf;
	uint64_t ms_N;
	size_t ms_block_size;
} ms_RecursiveShuffle_M2_opt_t;

typedef struct ms_DecryptAndShuffleM1_t {
	double ms_retval;
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_encrypted_block_size;
	unsigned char* ms_result_buffer;
	enc_ret* ms_ret;
} ms_DecryptAndShuffleM1_t;

typedef struct ms_DecryptAndShuffleM2_t {
	double ms_retval;
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_encrypted_block_size;
	size_t ms_nthreads;
	unsigned char* ms_result_buffer;
	enc_ret* ms_ret;
} ms_DecryptAndShuffleM2_t;

typedef struct ms_testTightCompaction_t {
	double ms_retval;
	unsigned char* ms_buffer;
	size_t ms_N;
	size_t ms_block_size;
	size_t ms_nthreads;
	bool* ms_selected_list;
	enc_ret* ms_ret;
} ms_testTightCompaction_t;

typedef struct ms_testOPTightCompaction_t {
	double ms_retval;
	unsigned char* ms_buffer;
	size_t ms_N;
	size_t ms_block_size;
	bool* ms_selected_list;
	enc_ret* ms_ret;
} ms_testOPTightCompaction_t;

typedef struct ms_decryptAndSubSample_t {
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_M;
	size_t ms_encrypted_block_size;
	unsigned char* ms_encryt_result_buffer;
	enc_ret* ms_ret;
} ms_decryptAndSubSample_t;

typedef struct ms_decryptAndSubSampleMulti_t {
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_M;
	size_t ms_K;
	size_t ms_encrypted_block_size;
	unsigned char* ms_encryt_result_buffer;
	enc_ret* ms_ret;
} ms_decryptAndSubSampleMulti_t;

typedef struct ms_decryptAndSubSampleMulti_opt_t {
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_M;
	size_t ms_K;
	size_t ms_encrypted_block_size;
	unsigned char* ms_encryt_result_buffer;
	enc_ret* ms_ret;
} ms_decryptAndSubSampleMulti_opt_t;

typedef struct ms_DecPSQF_single_t {
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_M;
	size_t ms_encrypted_block_size;
	unsigned char* ms_encryt_result_buffer;
	enc_ret* ms_ret;
} ms_DecPSQF_single_t;

typedef struct ms_DecPSQF_SWO_t {
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_M;
	size_t ms_encrypted_block_size;
	unsigned char* ms_encryt_result_buffer;
	enc_ret* ms_ret;
} ms_DecPSQF_SWO_t;

typedef struct ms_DecSuppleSWO_t {
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_M;
	size_t ms_K;
	size_t ms_encrypted_block_size;
	unsigned char* ms_encrypt_result_buffer;
	enc_ret* ms_ret;
} ms_DecSuppleSWO_t;

typedef struct ms_DecSuppleSWO_parallel_t {
	unsigned char* ms_encrypted_buffer;
	size_t ms_N;
	size_t ms_M;
	size_t ms_K;
	size_t ms_encrypted_block_size;
	unsigned char* ms_encrypt_result_buffer;
	enc_ret* ms_ret;
	size_t ms_nthreads;
} ms_DecSuppleSWO_parallel_t;

typedef struct ms_FMSCompactPrepare_t {
	int ms_retval;
	uint8_t* ms_routing_tags;
	size_t ms_n;
	size_t ms_n_left;
	size_t ms_n_right;
	size_t* ms_control_words;
	double* ms_control_us;
} ms_FMSCompactPrepare_t;

typedef struct ms_FMSCompactOnline_t {
	double ms_retval;
	unsigned char* ms_buffer;
	size_t ms_n;
	size_t ms_block_size;
} ms_FMSCompactOnline_t;

typedef struct ms_OCompactOnline_t {
	double ms_retval;
	unsigned char* ms_buffer;
	size_t ms_n;
	size_t ms_block_size;
} ms_OCompactOnline_t;

typedef struct ms_ocall_print_string_t {
	const char* ms_str;
} ms_ocall_print_string_t;

typedef struct ms_ocall_print_string_with_rtclock_t {
	unsigned long int ms_retval;
	const char* ms_str;
} ms_ocall_print_string_with_rtclock_t;

typedef struct ms_ocall_print_string_with_rtclock_diff_t {
	unsigned long int ms_retval;
	const char* ms_str;
	unsigned long int ms_before;
} ms_ocall_print_string_with_rtclock_diff_t;

typedef struct ms_untrustedMemAllocate_t {
	unsigned char* ms_retval;
	uint64_t ms_size;
} ms_untrustedMemAllocate_t;

typedef struct ms_untrustedMemFree_t {
	unsigned char* ms_mem;
} ms_untrustedMemFree_t;

typedef struct ms_ocall_clock_t {
	long int ms_retval;
} ms_ocall_clock_t;

typedef struct ms_ocall_wallclock_t {
	double ms_retval;
	int ms_start_or_stop;
} ms_ocall_wallclock_t;

typedef struct ms_pthread_wait_timeout_ocall_t {
	int ms_retval;
	unsigned long long ms_waiter;
	unsigned long long ms_timeout;
} ms_pthread_wait_timeout_ocall_t;

typedef struct ms_pthread_create_ocall_t {
	int ms_retval;
	unsigned long long ms_self;
} ms_pthread_create_ocall_t;

typedef struct ms_pthread_wakeup_ocall_t {
	int ms_retval;
	unsigned long long ms_waiter;
} ms_pthread_wakeup_ocall_t;

typedef struct ms_ocall_pointer_user_check_t {
	int* ms_val;
} ms_ocall_pointer_user_check_t;

typedef struct ms_ocall_pointer_in_t {
	int* ms_val;
} ms_ocall_pointer_in_t;

typedef struct ms_ocall_pointer_out_t {
	int* ms_val;
} ms_ocall_pointer_out_t;

typedef struct ms_ocall_pointer_in_out_t {
	int* ms_val;
} ms_ocall_pointer_in_out_t;

typedef struct ms_memccpy_t {
	void* ms_retval;
	void* ms_dest;
	const void* ms_src;
	int ms_val;
	size_t ms_len;
} ms_memccpy_t;

typedef struct ms_sgx_oc_cpuidex_t {
	int* ms_cpuinfo;
	int ms_leaf;
	int ms_subleaf;
} ms_sgx_oc_cpuidex_t;

typedef struct ms_sgx_thread_wait_untrusted_event_ocall_t {
	int ms_retval;
	const void* ms_self;
} ms_sgx_thread_wait_untrusted_event_ocall_t;

typedef struct ms_sgx_thread_set_untrusted_event_ocall_t {
	int ms_retval;
	const void* ms_waiter;
} ms_sgx_thread_set_untrusted_event_ocall_t;

typedef struct ms_sgx_thread_setwait_untrusted_events_ocall_t {
	int ms_retval;
	const void* ms_waiter;
	const void* ms_self;
} ms_sgx_thread_setwait_untrusted_events_ocall_t;

typedef struct ms_sgx_thread_set_multiple_untrusted_events_ocall_t {
	int ms_retval;
	const void** ms_waiters;
	size_t ms_total;
} ms_sgx_thread_set_multiple_untrusted_events_ocall_t;

static sgx_status_t SGX_CDECL Enclave_ocall_print_string(void* pms)
{
	ms_ocall_print_string_t* ms = SGX_CAST(ms_ocall_print_string_t*, pms);
	ocall_print_string(ms->ms_str);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_print_string_with_rtclock(void* pms)
{
	ms_ocall_print_string_with_rtclock_t* ms = SGX_CAST(ms_ocall_print_string_with_rtclock_t*, pms);
	ms->ms_retval = ocall_print_string_with_rtclock(ms->ms_str);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_print_string_with_rtclock_diff(void* pms)
{
	ms_ocall_print_string_with_rtclock_diff_t* ms = SGX_CAST(ms_ocall_print_string_with_rtclock_diff_t*, pms);
	ms->ms_retval = ocall_print_string_with_rtclock_diff(ms->ms_str, ms->ms_before);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_untrustedMemAllocate(void* pms)
{
	ms_untrustedMemAllocate_t* ms = SGX_CAST(ms_untrustedMemAllocate_t*, pms);
	ms->ms_retval = untrustedMemAllocate(ms->ms_size);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_untrustedMemFree(void* pms)
{
	ms_untrustedMemFree_t* ms = SGX_CAST(ms_untrustedMemFree_t*, pms);
	untrustedMemFree(ms->ms_mem);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_clock(void* pms)
{
	ms_ocall_clock_t* ms = SGX_CAST(ms_ocall_clock_t*, pms);
	ms->ms_retval = ocall_clock();

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_wallclock(void* pms)
{
	ms_ocall_wallclock_t* ms = SGX_CAST(ms_ocall_wallclock_t*, pms);
	ms->ms_retval = ocall_wallclock(ms->ms_start_or_stop);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_pthread_wait_timeout_ocall(void* pms)
{
	ms_pthread_wait_timeout_ocall_t* ms = SGX_CAST(ms_pthread_wait_timeout_ocall_t*, pms);
	ms->ms_retval = pthread_wait_timeout_ocall(ms->ms_waiter, ms->ms_timeout);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_pthread_create_ocall(void* pms)
{
	ms_pthread_create_ocall_t* ms = SGX_CAST(ms_pthread_create_ocall_t*, pms);
	ms->ms_retval = pthread_create_ocall(ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_pthread_wakeup_ocall(void* pms)
{
	ms_pthread_wakeup_ocall_t* ms = SGX_CAST(ms_pthread_wakeup_ocall_t*, pms);
	ms->ms_retval = pthread_wakeup_ocall(ms->ms_waiter);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_pointer_user_check(void* pms)
{
	ms_ocall_pointer_user_check_t* ms = SGX_CAST(ms_ocall_pointer_user_check_t*, pms);
	ocall_pointer_user_check(ms->ms_val);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_pointer_in(void* pms)
{
	ms_ocall_pointer_in_t* ms = SGX_CAST(ms_ocall_pointer_in_t*, pms);
	ocall_pointer_in(ms->ms_val);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_pointer_out(void* pms)
{
	ms_ocall_pointer_out_t* ms = SGX_CAST(ms_ocall_pointer_out_t*, pms);
	ocall_pointer_out(ms->ms_val);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_pointer_in_out(void* pms)
{
	ms_ocall_pointer_in_out_t* ms = SGX_CAST(ms_ocall_pointer_in_out_t*, pms);
	ocall_pointer_in_out(ms->ms_val);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_memccpy(void* pms)
{
	ms_memccpy_t* ms = SGX_CAST(ms_memccpy_t*, pms);
	ms->ms_retval = memccpy(ms->ms_dest, ms->ms_src, ms->ms_val, ms->ms_len);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_function_allow(void* pms)
{
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ocall_function_allow();
	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_oc_cpuidex(void* pms)
{
	ms_sgx_oc_cpuidex_t* ms = SGX_CAST(ms_sgx_oc_cpuidex_t*, pms);
	sgx_oc_cpuidex(ms->ms_cpuinfo, ms->ms_leaf, ms->ms_subleaf);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_wait_untrusted_event_ocall(void* pms)
{
	ms_sgx_thread_wait_untrusted_event_ocall_t* ms = SGX_CAST(ms_sgx_thread_wait_untrusted_event_ocall_t*, pms);
	ms->ms_retval = sgx_thread_wait_untrusted_event_ocall(ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_set_untrusted_event_ocall(void* pms)
{
	ms_sgx_thread_set_untrusted_event_ocall_t* ms = SGX_CAST(ms_sgx_thread_set_untrusted_event_ocall_t*, pms);
	ms->ms_retval = sgx_thread_set_untrusted_event_ocall(ms->ms_waiter);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_setwait_untrusted_events_ocall(void* pms)
{
	ms_sgx_thread_setwait_untrusted_events_ocall_t* ms = SGX_CAST(ms_sgx_thread_setwait_untrusted_events_ocall_t*, pms);
	ms->ms_retval = sgx_thread_setwait_untrusted_events_ocall(ms->ms_waiter, ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_set_multiple_untrusted_events_ocall(void* pms)
{
	ms_sgx_thread_set_multiple_untrusted_events_ocall_t* ms = SGX_CAST(ms_sgx_thread_set_multiple_untrusted_events_ocall_t*, pms);
	ms->ms_retval = sgx_thread_set_multiple_untrusted_events_ocall(ms->ms_waiters, ms->ms_total);

	return SGX_SUCCESS;
}

static const struct {
	size_t nr_ocall;
	void * table[21];
} ocall_table_Enclave = {
	21,
	{
		(void*)Enclave_ocall_print_string,
		(void*)Enclave_ocall_print_string_with_rtclock,
		(void*)Enclave_ocall_print_string_with_rtclock_diff,
		(void*)Enclave_untrustedMemAllocate,
		(void*)Enclave_untrustedMemFree,
		(void*)Enclave_ocall_clock,
		(void*)Enclave_ocall_wallclock,
		(void*)Enclave_pthread_wait_timeout_ocall,
		(void*)Enclave_pthread_create_ocall,
		(void*)Enclave_pthread_wakeup_ocall,
		(void*)Enclave_ocall_pointer_user_check,
		(void*)Enclave_ocall_pointer_in,
		(void*)Enclave_ocall_pointer_out,
		(void*)Enclave_ocall_pointer_in_out,
		(void*)Enclave_memccpy,
		(void*)Enclave_ocall_function_allow,
		(void*)Enclave_sgx_oc_cpuidex,
		(void*)Enclave_sgx_thread_wait_untrusted_event_ocall,
		(void*)Enclave_sgx_thread_set_untrusted_event_ocall,
		(void*)Enclave_sgx_thread_setwait_untrusted_events_ocall,
		(void*)Enclave_sgx_thread_set_multiple_untrusted_events_ocall,
	}
};
sgx_status_t Enclave_loadTestKeys(sgx_enclave_id_t eid, unsigned char inkey[16], unsigned char outkey[16])
{
	sgx_status_t status;
	ms_Enclave_loadTestKeys_t ms;
	ms.ms_inkey = (unsigned char*)inkey;
	ms.ms_outkey = (unsigned char*)outkey;
	status = sgx_ecall(eid, 0, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t MeasureOSWAPBuffer(sgx_enclave_id_t eid, double* retval, unsigned char* buf, size_t N, size_t block_size)
{
	sgx_status_t status;
	ms_MeasureOSWAPBuffer_t ms;
	ms.ms_buf = buf;
	ms.ms_N = N;
	ms.ms_block_size = block_size;
	status = sgx_ecall(eid, 1, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t MeasureObliviousPrimitive(sgx_enclave_id_t eid, double* retval, unsigned char* buf, size_t pairs, size_t block_size, uint8_t primitive, uint8_t flag0, uint8_t flag1)
{
	sgx_status_t status;
	ms_MeasureObliviousPrimitive_t ms;
	ms.ms_buf = buf;
	ms.ms_pairs = pairs;
	ms.ms_block_size = block_size;
	ms.ms_primitive = primitive;
	ms.ms_flag0 = flag0;
	ms.ms_flag1 = flag1;
	status = sgx_ecall(eid, 2, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t ecall_type_char(sgx_enclave_id_t eid, char val)
{
	sgx_status_t status;
	ms_ecall_type_char_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 3, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_type_int(sgx_enclave_id_t eid, int val)
{
	sgx_status_t status;
	ms_ecall_type_int_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 4, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_type_float(sgx_enclave_id_t eid, float val)
{
	sgx_status_t status;
	ms_ecall_type_float_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 5, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_type_double(sgx_enclave_id_t eid, double val)
{
	sgx_status_t status;
	ms_ecall_type_double_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 6, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_type_size_t(sgx_enclave_id_t eid, size_t val)
{
	sgx_status_t status;
	ms_ecall_type_size_t_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 7, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_type_wchar_t(sgx_enclave_id_t eid, wchar_t val)
{
	sgx_status_t status;
	ms_ecall_type_wchar_t_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 8, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_type_struct(sgx_enclave_id_t eid, struct struct_foo_t val)
{
	sgx_status_t status;
	ms_ecall_type_struct_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 9, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_type_enum_union(sgx_enclave_id_t eid, enum enum_foo_t val1, union union_foo_t* val2)
{
	sgx_status_t status;
	ms_ecall_type_enum_union_t ms;
	ms.ms_val1 = val1;
	ms.ms_val2 = val2;
	status = sgx_ecall(eid, 10, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_pointer_user_check(sgx_enclave_id_t eid, size_t* retval, void* val, size_t sz)
{
	sgx_status_t status;
	ms_ecall_pointer_user_check_t ms;
	ms.ms_val = val;
	ms.ms_sz = sz;
	status = sgx_ecall(eid, 11, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t ecall_pointer_in(sgx_enclave_id_t eid, int* val)
{
	sgx_status_t status;
	ms_ecall_pointer_in_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 12, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_pointer_out(sgx_enclave_id_t eid, int* val)
{
	sgx_status_t status;
	ms_ecall_pointer_out_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 13, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_pointer_in_out(sgx_enclave_id_t eid, int* val)
{
	sgx_status_t status;
	ms_ecall_pointer_in_out_t ms;
	ms.ms_val = val;
	status = sgx_ecall(eid, 14, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_pointer_string(sgx_enclave_id_t eid, char* str)
{
	sgx_status_t status;
	ms_ecall_pointer_string_t ms;
	ms.ms_str = str;
	ms.ms_str_len = str ? strlen(str) + 1 : 0;
	status = sgx_ecall(eid, 15, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_pointer_string_const(sgx_enclave_id_t eid, const char* str)
{
	sgx_status_t status;
	ms_ecall_pointer_string_const_t ms;
	ms.ms_str = str;
	ms.ms_str_len = str ? strlen(str) + 1 : 0;
	status = sgx_ecall(eid, 16, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_pointer_size(sgx_enclave_id_t eid, void* ptr, size_t len)
{
	sgx_status_t status;
	ms_ecall_pointer_size_t ms;
	ms.ms_ptr = ptr;
	ms.ms_len = len;
	status = sgx_ecall(eid, 17, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_pointer_count(sgx_enclave_id_t eid, int* arr, int cnt)
{
	sgx_status_t status;
	ms_ecall_pointer_count_t ms;
	ms.ms_arr = arr;
	ms.ms_cnt = cnt;
	status = sgx_ecall(eid, 18, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_pointer_isptr_readonly(sgx_enclave_id_t eid, buffer_t buf, size_t len)
{
	sgx_status_t status;
	ms_ecall_pointer_isptr_readonly_t ms;
	ms.ms_buf = buf;
	ms.ms_len = len;
	status = sgx_ecall(eid, 19, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ocall_pointer_attr(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 20, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t ecall_array_user_check(sgx_enclave_id_t eid, int arr[4])
{
	sgx_status_t status;
	ms_ecall_array_user_check_t ms;
	ms.ms_arr = (int*)arr;
	status = sgx_ecall(eid, 21, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_array_in(sgx_enclave_id_t eid, int arr[4])
{
	sgx_status_t status;
	ms_ecall_array_in_t ms;
	ms.ms_arr = (int*)arr;
	status = sgx_ecall(eid, 22, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_array_out(sgx_enclave_id_t eid, int arr[4])
{
	sgx_status_t status;
	ms_ecall_array_out_t ms;
	ms.ms_arr = (int*)arr;
	status = sgx_ecall(eid, 23, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_array_in_out(sgx_enclave_id_t eid, int arr[4])
{
	sgx_status_t status;
	ms_ecall_array_in_out_t ms;
	ms.ms_arr = (int*)arr;
	status = sgx_ecall(eid, 24, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_array_isary(sgx_enclave_id_t eid, array_t arr)
{
	sgx_status_t status;
	ms_ecall_array_isary_t ms;
	ms.ms_arr = (array_t *)&arr[0];
	status = sgx_ecall(eid, 25, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_function_calling_convs(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 26, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t ecall_function_public(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 27, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t ecall_function_private(sgx_enclave_id_t eid, int* retval)
{
	sgx_status_t status;
	ms_ecall_function_private_t ms;
	status = sgx_ecall(eid, 28, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t ecall_malloc_free(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 29, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t ecall_sgx_cpuid(sgx_enclave_id_t eid, int cpuinfo[4], int leaf)
{
	sgx_status_t status;
	ms_ecall_sgx_cpuid_t ms;
	ms.ms_cpuinfo = (int*)cpuinfo;
	ms.ms_leaf = leaf;
	status = sgx_ecall(eid, 30, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_exception(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 31, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t ecall_map(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 32, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t ecall_increase_counter(sgx_enclave_id_t eid, size_t* retval)
{
	sgx_status_t status;
	ms_ecall_increase_counter_t ms;
	status = sgx_ecall(eid, 33, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t ecall_producer(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 34, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t ecall_consumer(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 35, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t RecursiveShuffle_M1(sgx_enclave_id_t eid, unsigned char* buf, uint64_t N, size_t block_size)
{
	sgx_status_t status;
	ms_RecursiveShuffle_M1_t ms;
	ms.ms_buf = buf;
	ms.ms_N = N;
	ms.ms_block_size = block_size;
	status = sgx_ecall(eid, 36, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t RecursiveShuffle_M2(sgx_enclave_id_t eid, unsigned char* buf, uint64_t N, size_t block_size)
{
	sgx_status_t status;
	ms_RecursiveShuffle_M2_t ms;
	ms.ms_buf = buf;
	ms.ms_N = N;
	ms.ms_block_size = block_size;
	status = sgx_ecall(eid, 37, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t RecursiveShuffle_M2_opt(sgx_enclave_id_t eid, double* retval, unsigned char* buf, uint64_t N, size_t block_size)
{
	sgx_status_t status;
	ms_RecursiveShuffle_M2_opt_t ms;
	ms.ms_buf = buf;
	ms.ms_N = N;
	ms.ms_block_size = block_size;
	status = sgx_ecall(eid, 38, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t DecryptAndShuffleM1(sgx_enclave_id_t eid, double* retval, unsigned char* encrypted_buffer, size_t N, size_t encrypted_block_size, unsigned char* result_buffer, enc_ret* ret)
{
	sgx_status_t status;
	ms_DecryptAndShuffleM1_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_result_buffer = result_buffer;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 39, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t DecryptAndShuffleM2(sgx_enclave_id_t eid, double* retval, unsigned char* encrypted_buffer, size_t N, size_t encrypted_block_size, size_t nthreads, unsigned char* result_buffer, enc_ret* ret)
{
	sgx_status_t status;
	ms_DecryptAndShuffleM2_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_nthreads = nthreads;
	ms.ms_result_buffer = result_buffer;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 40, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t testTightCompaction(sgx_enclave_id_t eid, double* retval, unsigned char* buffer, size_t N, size_t block_size, size_t nthreads, bool* selected_list, enc_ret* ret)
{
	sgx_status_t status;
	ms_testTightCompaction_t ms;
	ms.ms_buffer = buffer;
	ms.ms_N = N;
	ms.ms_block_size = block_size;
	ms.ms_nthreads = nthreads;
	ms.ms_selected_list = selected_list;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 41, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t testOPTightCompaction(sgx_enclave_id_t eid, double* retval, unsigned char* buffer, size_t N, size_t block_size, bool* selected_list, enc_ret* ret)
{
	sgx_status_t status;
	ms_testOPTightCompaction_t ms;
	ms.ms_buffer = buffer;
	ms.ms_N = N;
	ms.ms_block_size = block_size;
	ms.ms_selected_list = selected_list;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 42, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t decryptAndSubSample(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret)
{
	sgx_status_t status;
	ms_decryptAndSubSample_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_M = M;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_encryt_result_buffer = encryt_result_buffer;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 43, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t decryptAndSubSampleMulti(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret)
{
	sgx_status_t status;
	ms_decryptAndSubSampleMulti_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_M = M;
	ms.ms_K = K;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_encryt_result_buffer = encryt_result_buffer;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 44, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t decryptAndSubSampleMulti_opt(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret)
{
	sgx_status_t status;
	ms_decryptAndSubSampleMulti_opt_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_M = M;
	ms.ms_K = K;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_encryt_result_buffer = encryt_result_buffer;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 45, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t DecPSQF_single(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret)
{
	sgx_status_t status;
	ms_DecPSQF_single_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_M = M;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_encryt_result_buffer = encryt_result_buffer;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 46, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t DecPSQF_SWO(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char* encryt_result_buffer, enc_ret* ret)
{
	sgx_status_t status;
	ms_DecPSQF_SWO_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_M = M;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_encryt_result_buffer = encryt_result_buffer;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 47, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t DecSuppleSWO(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encrypt_result_buffer, enc_ret* ret)
{
	sgx_status_t status;
	ms_DecSuppleSWO_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_M = M;
	ms.ms_K = K;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_encrypt_result_buffer = encrypt_result_buffer;
	ms.ms_ret = ret;
	status = sgx_ecall(eid, 48, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t DecSuppleSWO_parallel(sgx_enclave_id_t eid, unsigned char* encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char* encrypt_result_buffer, enc_ret* ret, size_t nthreads)
{
	sgx_status_t status;
	ms_DecSuppleSWO_parallel_t ms;
	ms.ms_encrypted_buffer = encrypted_buffer;
	ms.ms_N = N;
	ms.ms_M = M;
	ms.ms_K = K;
	ms.ms_encrypted_block_size = encrypted_block_size;
	ms.ms_encrypt_result_buffer = encrypt_result_buffer;
	ms.ms_ret = ret;
	ms.ms_nthreads = nthreads;
	status = sgx_ecall(eid, 49, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t FMSCompactPrepare(sgx_enclave_id_t eid, int* retval, uint8_t* routing_tags, size_t n, size_t n_left, size_t n_right, size_t* control_words, double* control_us)
{
	sgx_status_t status;
	ms_FMSCompactPrepare_t ms;
	ms.ms_routing_tags = routing_tags;
	ms.ms_n = n;
	ms.ms_n_left = n_left;
	ms.ms_n_right = n_right;
	ms.ms_control_words = control_words;
	ms.ms_control_us = control_us;
	status = sgx_ecall(eid, 50, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t FMSCompactOnline(sgx_enclave_id_t eid, double* retval, unsigned char* buffer, size_t n, size_t block_size)
{
	sgx_status_t status;
	ms_FMSCompactOnline_t ms;
	ms.ms_buffer = buffer;
	ms.ms_n = n;
	ms.ms_block_size = block_size;
	status = sgx_ecall(eid, 51, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t OCompactOnline(sgx_enclave_id_t eid, double* retval, unsigned char* buffer, size_t n, size_t block_size)
{
	sgx_status_t status;
	ms_OCompactOnline_t ms;
	ms.ms_buffer = buffer;
	ms.ms_n = n;
	ms.ms_block_size = block_size;
	status = sgx_ecall(eid, 52, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t FMSCompactRelease(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 53, &ocall_table_Enclave, NULL);
	return status;
}

