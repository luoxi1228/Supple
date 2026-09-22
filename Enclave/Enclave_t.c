#include "Enclave_t.h"

#include "sgx_trts.h" /* for sgx_ocalloc, sgx_is_outside_enclave */
#include "sgx_lfence.h" /* for sgx_lfence */

#include <errno.h>
#include <mbusafecrt.h> /* for memcpy_s etc */
#include <stdlib.h> /* for malloc/free etc */

#define CHECK_REF_POINTER(ptr, siz) do {	\
	if (!(ptr) || ! sgx_is_outside_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define CHECK_UNIQUE_POINTER(ptr, siz) do {	\
	if ((ptr) && ! sgx_is_outside_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define CHECK_ENCLAVE_POINTER(ptr, siz) do {	\
	if ((ptr) && ! sgx_is_within_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define ADD_ASSIGN_OVERFLOW(a, b) (	\
	((a) += (b)) < (b)	\
)


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

static sgx_status_t SGX_CDECL sgx_Enclave_loadTestKeys(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_Enclave_loadTestKeys_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_Enclave_loadTestKeys_t* ms = SGX_CAST(ms_Enclave_loadTestKeys_t*, pms);
	ms_Enclave_loadTestKeys_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_Enclave_loadTestKeys_t), ms, sizeof(ms_Enclave_loadTestKeys_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_inkey = __in_ms.ms_inkey;
	size_t _len_inkey = 16 * sizeof(unsigned char);
	unsigned char* _in_inkey = NULL;
	unsigned char* _tmp_outkey = __in_ms.ms_outkey;
	size_t _len_outkey = 16 * sizeof(unsigned char);
	unsigned char* _in_outkey = NULL;

	CHECK_UNIQUE_POINTER(_tmp_inkey, _len_inkey);
	CHECK_UNIQUE_POINTER(_tmp_outkey, _len_outkey);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_inkey != NULL && _len_inkey != 0) {
		if ( _len_inkey % sizeof(*_tmp_inkey) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_inkey = (unsigned char*)malloc(_len_inkey);
		if (_in_inkey == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_inkey, _len_inkey, _tmp_inkey, _len_inkey)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	if (_tmp_outkey != NULL && _len_outkey != 0) {
		if ( _len_outkey % sizeof(*_tmp_outkey) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_outkey = (unsigned char*)malloc(_len_outkey);
		if (_in_outkey == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_outkey, _len_outkey, _tmp_outkey, _len_outkey)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	Enclave_loadTestKeys(_in_inkey, _in_outkey);

err:
	if (_in_inkey) free(_in_inkey);
	if (_in_outkey) free(_in_outkey);
	return status;
}

static sgx_status_t SGX_CDECL sgx_MeasureOSWAPBuffer(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_MeasureOSWAPBuffer_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_MeasureOSWAPBuffer_t* ms = SGX_CAST(ms_MeasureOSWAPBuffer_t*, pms);
	ms_MeasureOSWAPBuffer_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_MeasureOSWAPBuffer_t), ms, sizeof(ms_MeasureOSWAPBuffer_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buf = __in_ms.ms_buf;
	double _in_retval;


	_in_retval = MeasureOSWAPBuffer(_tmp_buf, __in_ms.ms_N, __in_ms.ms_block_size);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_MeasureObliviousPrimitive(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_MeasureObliviousPrimitive_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_MeasureObliviousPrimitive_t* ms = SGX_CAST(ms_MeasureObliviousPrimitive_t*, pms);
	ms_MeasureObliviousPrimitive_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_MeasureObliviousPrimitive_t), ms, sizeof(ms_MeasureObliviousPrimitive_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buf = __in_ms.ms_buf;
	double _in_retval;


	_in_retval = MeasureObliviousPrimitive(_tmp_buf, __in_ms.ms_pairs, __in_ms.ms_block_size, __in_ms.ms_primitive, __in_ms.ms_flag0, __in_ms.ms_flag1);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_type_char(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_type_char_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_type_char_t* ms = SGX_CAST(ms_ecall_type_char_t*, pms);
	ms_ecall_type_char_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_type_char_t), ms, sizeof(ms_ecall_type_char_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;


	ecall_type_char(__in_ms.ms_val);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_type_int(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_type_int_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_type_int_t* ms = SGX_CAST(ms_ecall_type_int_t*, pms);
	ms_ecall_type_int_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_type_int_t), ms, sizeof(ms_ecall_type_int_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;


	ecall_type_int(__in_ms.ms_val);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_type_float(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_type_float_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_type_float_t* ms = SGX_CAST(ms_ecall_type_float_t*, pms);
	ms_ecall_type_float_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_type_float_t), ms, sizeof(ms_ecall_type_float_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;


	ecall_type_float(__in_ms.ms_val);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_type_double(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_type_double_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_type_double_t* ms = SGX_CAST(ms_ecall_type_double_t*, pms);
	ms_ecall_type_double_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_type_double_t), ms, sizeof(ms_ecall_type_double_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;


	ecall_type_double(__in_ms.ms_val);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_type_size_t(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_type_size_t_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_type_size_t_t* ms = SGX_CAST(ms_ecall_type_size_t_t*, pms);
	ms_ecall_type_size_t_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_type_size_t_t), ms, sizeof(ms_ecall_type_size_t_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;


	ecall_type_size_t(__in_ms.ms_val);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_type_wchar_t(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_type_wchar_t_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_type_wchar_t_t* ms = SGX_CAST(ms_ecall_type_wchar_t_t*, pms);
	ms_ecall_type_wchar_t_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_type_wchar_t_t), ms, sizeof(ms_ecall_type_wchar_t_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;


	ecall_type_wchar_t(__in_ms.ms_val);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_type_struct(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_type_struct_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_type_struct_t* ms = SGX_CAST(ms_ecall_type_struct_t*, pms);
	ms_ecall_type_struct_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_type_struct_t), ms, sizeof(ms_ecall_type_struct_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;


	ecall_type_struct(__in_ms.ms_val);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_type_enum_union(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_type_enum_union_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_type_enum_union_t* ms = SGX_CAST(ms_ecall_type_enum_union_t*, pms);
	ms_ecall_type_enum_union_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_type_enum_union_t), ms, sizeof(ms_ecall_type_enum_union_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	union union_foo_t* _tmp_val2 = __in_ms.ms_val2;


	ecall_type_enum_union(__in_ms.ms_val1, _tmp_val2);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_user_check(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_user_check_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_user_check_t* ms = SGX_CAST(ms_ecall_pointer_user_check_t*, pms);
	ms_ecall_pointer_user_check_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_user_check_t), ms, sizeof(ms_ecall_pointer_user_check_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	void* _tmp_val = __in_ms.ms_val;
	size_t _in_retval;


	_in_retval = ecall_pointer_user_check(_tmp_val, __in_ms.ms_sz);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_in(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_in_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_in_t* ms = SGX_CAST(ms_ecall_pointer_in_t*, pms);
	ms_ecall_pointer_in_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_in_t), ms, sizeof(ms_ecall_pointer_in_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_val = __in_ms.ms_val;
	size_t _len_val = sizeof(int);
	int* _in_val = NULL;

	CHECK_UNIQUE_POINTER(_tmp_val, _len_val);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_val != NULL && _len_val != 0) {
		if ( _len_val % sizeof(*_tmp_val) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_val = (int*)malloc(_len_val);
		if (_in_val == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_val, _len_val, _tmp_val, _len_val)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_pointer_in(_in_val);

err:
	if (_in_val) free(_in_val);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_out(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_out_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_out_t* ms = SGX_CAST(ms_ecall_pointer_out_t*, pms);
	ms_ecall_pointer_out_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_out_t), ms, sizeof(ms_ecall_pointer_out_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_val = __in_ms.ms_val;
	size_t _len_val = sizeof(int);
	int* _in_val = NULL;

	CHECK_UNIQUE_POINTER(_tmp_val, _len_val);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_val != NULL && _len_val != 0) {
		if ( _len_val % sizeof(*_tmp_val) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		if ((_in_val = (int*)malloc(_len_val)) == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		memset((void*)_in_val, 0, _len_val);
	}
	ecall_pointer_out(_in_val);
	if (_in_val) {
		if (memcpy_verw_s(_tmp_val, _len_val, _in_val, _len_val)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_val) free(_in_val);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_in_out(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_in_out_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_in_out_t* ms = SGX_CAST(ms_ecall_pointer_in_out_t*, pms);
	ms_ecall_pointer_in_out_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_in_out_t), ms, sizeof(ms_ecall_pointer_in_out_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_val = __in_ms.ms_val;
	size_t _len_val = sizeof(int);
	int* _in_val = NULL;

	CHECK_UNIQUE_POINTER(_tmp_val, _len_val);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_val != NULL && _len_val != 0) {
		if ( _len_val % sizeof(*_tmp_val) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_val = (int*)malloc(_len_val);
		if (_in_val == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_val, _len_val, _tmp_val, _len_val)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_pointer_in_out(_in_val);
	if (_in_val) {
		if (memcpy_verw_s(_tmp_val, _len_val, _in_val, _len_val)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_val) free(_in_val);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_string(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_string_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_string_t* ms = SGX_CAST(ms_ecall_pointer_string_t*, pms);
	ms_ecall_pointer_string_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_string_t), ms, sizeof(ms_ecall_pointer_string_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	char* _tmp_str = __in_ms.ms_str;
	size_t _len_str = __in_ms.ms_str_len ;
	char* _in_str = NULL;

	CHECK_UNIQUE_POINTER(_tmp_str, _len_str);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_str != NULL && _len_str != 0) {
		_in_str = (char*)malloc(_len_str);
		if (_in_str == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_str, _len_str, _tmp_str, _len_str)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

		_in_str[_len_str - 1] = '\0';
		if (_len_str != strlen(_in_str) + 1)
		{
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}
	ecall_pointer_string(_in_str);
	if (_in_str)
	{
		_in_str[_len_str - 1] = '\0';
		_len_str = strlen(_in_str) + 1;
		if (memcpy_verw_s((void*)_tmp_str, _len_str, _in_str, _len_str)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_str) free(_in_str);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_string_const(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_string_const_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_string_const_t* ms = SGX_CAST(ms_ecall_pointer_string_const_t*, pms);
	ms_ecall_pointer_string_const_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_string_const_t), ms, sizeof(ms_ecall_pointer_string_const_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	const char* _tmp_str = __in_ms.ms_str;
	size_t _len_str = __in_ms.ms_str_len ;
	char* _in_str = NULL;

	CHECK_UNIQUE_POINTER(_tmp_str, _len_str);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_str != NULL && _len_str != 0) {
		_in_str = (char*)malloc(_len_str);
		if (_in_str == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_str, _len_str, _tmp_str, _len_str)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

		_in_str[_len_str - 1] = '\0';
		if (_len_str != strlen(_in_str) + 1)
		{
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}
	ecall_pointer_string_const((const char*)_in_str);

err:
	if (_in_str) free(_in_str);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_size(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_size_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_size_t* ms = SGX_CAST(ms_ecall_pointer_size_t*, pms);
	ms_ecall_pointer_size_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_size_t), ms, sizeof(ms_ecall_pointer_size_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	void* _tmp_ptr = __in_ms.ms_ptr;
	size_t _tmp_len = __in_ms.ms_len;
	size_t _len_ptr = _tmp_len;
	void* _in_ptr = NULL;

	CHECK_UNIQUE_POINTER(_tmp_ptr, _len_ptr);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_ptr != NULL && _len_ptr != 0) {
		_in_ptr = (void*)malloc(_len_ptr);
		if (_in_ptr == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_ptr, _len_ptr, _tmp_ptr, _len_ptr)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_pointer_size(_in_ptr, _tmp_len);
	if (_in_ptr) {
		if (memcpy_verw_s(_tmp_ptr, _len_ptr, _in_ptr, _len_ptr)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_ptr) free(_in_ptr);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_count(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_count_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_count_t* ms = SGX_CAST(ms_ecall_pointer_count_t*, pms);
	ms_ecall_pointer_count_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_count_t), ms, sizeof(ms_ecall_pointer_count_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_arr = __in_ms.ms_arr;
	int _tmp_cnt = __in_ms.ms_cnt;
	size_t _len_arr = _tmp_cnt * sizeof(int);
	int* _in_arr = NULL;

	if (sizeof(*_tmp_arr) != 0 &&
		(size_t)_tmp_cnt > (SIZE_MAX / sizeof(*_tmp_arr))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_arr, _len_arr);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_arr != NULL && _len_arr != 0) {
		if ( _len_arr % sizeof(*_tmp_arr) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_arr = (int*)malloc(_len_arr);
		if (_in_arr == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_arr, _len_arr, _tmp_arr, _len_arr)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_pointer_count(_in_arr, _tmp_cnt);
	if (_in_arr) {
		if (memcpy_verw_s(_tmp_arr, _len_arr, _in_arr, _len_arr)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_arr) free(_in_arr);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_pointer_isptr_readonly(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_pointer_isptr_readonly_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_pointer_isptr_readonly_t* ms = SGX_CAST(ms_ecall_pointer_isptr_readonly_t*, pms);
	ms_ecall_pointer_isptr_readonly_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_pointer_isptr_readonly_t), ms, sizeof(ms_ecall_pointer_isptr_readonly_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	buffer_t _tmp_buf = __in_ms.ms_buf;
	size_t _tmp_len = __in_ms.ms_len;
	size_t _len_buf = _tmp_len;
	buffer_t _in_buf = NULL;

	CHECK_UNIQUE_POINTER(_tmp_buf, _len_buf);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_buf != NULL && _len_buf != 0) {
		_in_buf = (buffer_t)malloc(_len_buf);
		if (_in_buf == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s((void*)_in_buf, _len_buf, _tmp_buf, _len_buf)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_pointer_isptr_readonly(_in_buf, _tmp_len);

err:
	if (_in_buf) free((void*)_in_buf);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ocall_pointer_attr(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ocall_pointer_attr();
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_array_user_check(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_array_user_check_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_array_user_check_t* ms = SGX_CAST(ms_ecall_array_user_check_t*, pms);
	ms_ecall_array_user_check_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_array_user_check_t), ms, sizeof(ms_ecall_array_user_check_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_arr = __in_ms.ms_arr;


	ecall_array_user_check(_tmp_arr);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_array_in(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_array_in_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_array_in_t* ms = SGX_CAST(ms_ecall_array_in_t*, pms);
	ms_ecall_array_in_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_array_in_t), ms, sizeof(ms_ecall_array_in_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_arr = __in_ms.ms_arr;
	size_t _len_arr = 4 * sizeof(int);
	int* _in_arr = NULL;

	CHECK_UNIQUE_POINTER(_tmp_arr, _len_arr);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_arr != NULL && _len_arr != 0) {
		if ( _len_arr % sizeof(*_tmp_arr) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_arr = (int*)malloc(_len_arr);
		if (_in_arr == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_arr, _len_arr, _tmp_arr, _len_arr)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_array_in(_in_arr);

err:
	if (_in_arr) free(_in_arr);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_array_out(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_array_out_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_array_out_t* ms = SGX_CAST(ms_ecall_array_out_t*, pms);
	ms_ecall_array_out_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_array_out_t), ms, sizeof(ms_ecall_array_out_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_arr = __in_ms.ms_arr;
	size_t _len_arr = 4 * sizeof(int);
	int* _in_arr = NULL;

	CHECK_UNIQUE_POINTER(_tmp_arr, _len_arr);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_arr != NULL && _len_arr != 0) {
		if ( _len_arr % sizeof(*_tmp_arr) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		if ((_in_arr = (int*)malloc(_len_arr)) == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		memset((void*)_in_arr, 0, _len_arr);
	}
	ecall_array_out(_in_arr);
	if (_in_arr) {
		if (memcpy_verw_s(_tmp_arr, _len_arr, _in_arr, _len_arr)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_arr) free(_in_arr);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_array_in_out(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_array_in_out_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_array_in_out_t* ms = SGX_CAST(ms_ecall_array_in_out_t*, pms);
	ms_ecall_array_in_out_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_array_in_out_t), ms, sizeof(ms_ecall_array_in_out_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_arr = __in_ms.ms_arr;
	size_t _len_arr = 4 * sizeof(int);
	int* _in_arr = NULL;

	CHECK_UNIQUE_POINTER(_tmp_arr, _len_arr);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_arr != NULL && _len_arr != 0) {
		if ( _len_arr % sizeof(*_tmp_arr) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_arr = (int*)malloc(_len_arr);
		if (_in_arr == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_arr, _len_arr, _tmp_arr, _len_arr)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_array_in_out(_in_arr);
	if (_in_arr) {
		if (memcpy_verw_s(_tmp_arr, _len_arr, _in_arr, _len_arr)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_arr) free(_in_arr);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_array_isary(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_array_isary_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_array_isary_t* ms = SGX_CAST(ms_ecall_array_isary_t*, pms);
	ms_ecall_array_isary_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_array_isary_t), ms, sizeof(ms_ecall_array_isary_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;


	ecall_array_isary((__in_ms.ms_arr != NULL) ? (*__in_ms.ms_arr) : NULL);


	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_function_calling_convs(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ecall_function_calling_convs();
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_function_public(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ecall_function_public();
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_function_private(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_function_private_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_function_private_t* ms = SGX_CAST(ms_ecall_function_private_t*, pms);
	ms_ecall_function_private_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_function_private_t), ms, sizeof(ms_ecall_function_private_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int _in_retval;


	_in_retval = ecall_function_private();
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_malloc_free(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ecall_malloc_free();
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_sgx_cpuid(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_sgx_cpuid_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_sgx_cpuid_t* ms = SGX_CAST(ms_ecall_sgx_cpuid_t*, pms);
	ms_ecall_sgx_cpuid_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_sgx_cpuid_t), ms, sizeof(ms_ecall_sgx_cpuid_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int* _tmp_cpuinfo = __in_ms.ms_cpuinfo;
	size_t _len_cpuinfo = 4 * sizeof(int);
	int* _in_cpuinfo = NULL;

	CHECK_UNIQUE_POINTER(_tmp_cpuinfo, _len_cpuinfo);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_cpuinfo != NULL && _len_cpuinfo != 0) {
		if ( _len_cpuinfo % sizeof(*_tmp_cpuinfo) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		if ((_in_cpuinfo = (int*)malloc(_len_cpuinfo)) == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		memset((void*)_in_cpuinfo, 0, _len_cpuinfo);
	}
	ecall_sgx_cpuid(_in_cpuinfo, __in_ms.ms_leaf);
	if (_in_cpuinfo) {
		if (memcpy_verw_s(_tmp_cpuinfo, _len_cpuinfo, _in_cpuinfo, _len_cpuinfo)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_cpuinfo) free(_in_cpuinfo);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_exception(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ecall_exception();
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_map(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ecall_map();
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_increase_counter(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_increase_counter_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_increase_counter_t* ms = SGX_CAST(ms_ecall_increase_counter_t*, pms);
	ms_ecall_increase_counter_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_increase_counter_t), ms, sizeof(ms_ecall_increase_counter_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	size_t _in_retval;


	_in_retval = ecall_increase_counter();
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_producer(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ecall_producer();
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_consumer(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ecall_consumer();
	return status;
}

static sgx_status_t SGX_CDECL sgx_RecursiveShuffle_M1(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_RecursiveShuffle_M1_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_RecursiveShuffle_M1_t* ms = SGX_CAST(ms_RecursiveShuffle_M1_t*, pms);
	ms_RecursiveShuffle_M1_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_RecursiveShuffle_M1_t), ms, sizeof(ms_RecursiveShuffle_M1_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buf = __in_ms.ms_buf;


	RecursiveShuffle_M1(_tmp_buf, __in_ms.ms_N, __in_ms.ms_block_size);


	return status;
}

static sgx_status_t SGX_CDECL sgx_RecursiveShuffle_M2(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_RecursiveShuffle_M2_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_RecursiveShuffle_M2_t* ms = SGX_CAST(ms_RecursiveShuffle_M2_t*, pms);
	ms_RecursiveShuffle_M2_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_RecursiveShuffle_M2_t), ms, sizeof(ms_RecursiveShuffle_M2_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buf = __in_ms.ms_buf;


	RecursiveShuffle_M2(_tmp_buf, __in_ms.ms_N, __in_ms.ms_block_size);


	return status;
}

static sgx_status_t SGX_CDECL sgx_RecursiveShuffle_M2_opt(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_RecursiveShuffle_M2_opt_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_RecursiveShuffle_M2_opt_t* ms = SGX_CAST(ms_RecursiveShuffle_M2_opt_t*, pms);
	ms_RecursiveShuffle_M2_opt_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_RecursiveShuffle_M2_opt_t), ms, sizeof(ms_RecursiveShuffle_M2_opt_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buf = __in_ms.ms_buf;
	double _in_retval;


	_in_retval = RecursiveShuffle_M2_opt(_tmp_buf, __in_ms.ms_N, __in_ms.ms_block_size);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_DecryptAndShuffleM1(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_DecryptAndShuffleM1_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_DecryptAndShuffleM1_t* ms = SGX_CAST(ms_DecryptAndShuffleM1_t*, pms);
	ms_DecryptAndShuffleM1_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_DecryptAndShuffleM1_t), ms, sizeof(ms_DecryptAndShuffleM1_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_result_buffer = __in_ms.ms_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;
	double _in_retval;


	_in_retval = DecryptAndShuffleM1(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_encrypted_block_size, _tmp_result_buffer, _tmp_ret);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_DecryptAndShuffleM2(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_DecryptAndShuffleM2_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_DecryptAndShuffleM2_t* ms = SGX_CAST(ms_DecryptAndShuffleM2_t*, pms);
	ms_DecryptAndShuffleM2_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_DecryptAndShuffleM2_t), ms, sizeof(ms_DecryptAndShuffleM2_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_result_buffer = __in_ms.ms_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;
	double _in_retval;


	_in_retval = DecryptAndShuffleM2(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_encrypted_block_size, __in_ms.ms_nthreads, _tmp_result_buffer, _tmp_ret);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_testTightCompaction(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_testTightCompaction_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_testTightCompaction_t* ms = SGX_CAST(ms_testTightCompaction_t*, pms);
	ms_testTightCompaction_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_testTightCompaction_t), ms, sizeof(ms_testTightCompaction_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buffer = __in_ms.ms_buffer;
	bool* _tmp_selected_list = __in_ms.ms_selected_list;
	enc_ret* _tmp_ret = __in_ms.ms_ret;
	double _in_retval;


	_in_retval = testTightCompaction(_tmp_buffer, __in_ms.ms_N, __in_ms.ms_block_size, __in_ms.ms_nthreads, _tmp_selected_list, _tmp_ret);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_testOPTightCompaction(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_testOPTightCompaction_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_testOPTightCompaction_t* ms = SGX_CAST(ms_testOPTightCompaction_t*, pms);
	ms_testOPTightCompaction_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_testOPTightCompaction_t), ms, sizeof(ms_testOPTightCompaction_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buffer = __in_ms.ms_buffer;
	bool* _tmp_selected_list = __in_ms.ms_selected_list;
	enc_ret* _tmp_ret = __in_ms.ms_ret;
	double _in_retval;


	_in_retval = testOPTightCompaction(_tmp_buffer, __in_ms.ms_N, __in_ms.ms_block_size, _tmp_selected_list, _tmp_ret);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_decryptAndSubSample(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_decryptAndSubSample_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_decryptAndSubSample_t* ms = SGX_CAST(ms_decryptAndSubSample_t*, pms);
	ms_decryptAndSubSample_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_decryptAndSubSample_t), ms, sizeof(ms_decryptAndSubSample_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_encryt_result_buffer = __in_ms.ms_encryt_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;


	decryptAndSubSample(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_M, __in_ms.ms_encrypted_block_size, _tmp_encryt_result_buffer, _tmp_ret);


	return status;
}

static sgx_status_t SGX_CDECL sgx_decryptAndSubSampleMulti(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_decryptAndSubSampleMulti_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_decryptAndSubSampleMulti_t* ms = SGX_CAST(ms_decryptAndSubSampleMulti_t*, pms);
	ms_decryptAndSubSampleMulti_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_decryptAndSubSampleMulti_t), ms, sizeof(ms_decryptAndSubSampleMulti_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_encryt_result_buffer = __in_ms.ms_encryt_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;


	decryptAndSubSampleMulti(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_M, __in_ms.ms_K, __in_ms.ms_encrypted_block_size, _tmp_encryt_result_buffer, _tmp_ret);


	return status;
}

static sgx_status_t SGX_CDECL sgx_decryptAndSubSampleMulti_opt(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_decryptAndSubSampleMulti_opt_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_decryptAndSubSampleMulti_opt_t* ms = SGX_CAST(ms_decryptAndSubSampleMulti_opt_t*, pms);
	ms_decryptAndSubSampleMulti_opt_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_decryptAndSubSampleMulti_opt_t), ms, sizeof(ms_decryptAndSubSampleMulti_opt_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_encryt_result_buffer = __in_ms.ms_encryt_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;


	decryptAndSubSampleMulti_opt(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_M, __in_ms.ms_K, __in_ms.ms_encrypted_block_size, _tmp_encryt_result_buffer, _tmp_ret);


	return status;
}

static sgx_status_t SGX_CDECL sgx_DecPSQF_single(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_DecPSQF_single_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_DecPSQF_single_t* ms = SGX_CAST(ms_DecPSQF_single_t*, pms);
	ms_DecPSQF_single_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_DecPSQF_single_t), ms, sizeof(ms_DecPSQF_single_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_encryt_result_buffer = __in_ms.ms_encryt_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;


	DecPSQF_single(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_M, __in_ms.ms_encrypted_block_size, _tmp_encryt_result_buffer, _tmp_ret);


	return status;
}

static sgx_status_t SGX_CDECL sgx_DecPSQF_SWO(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_DecPSQF_SWO_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_DecPSQF_SWO_t* ms = SGX_CAST(ms_DecPSQF_SWO_t*, pms);
	ms_DecPSQF_SWO_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_DecPSQF_SWO_t), ms, sizeof(ms_DecPSQF_SWO_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_encryt_result_buffer = __in_ms.ms_encryt_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;


	DecPSQF_SWO(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_M, __in_ms.ms_encrypted_block_size, _tmp_encryt_result_buffer, _tmp_ret);


	return status;
}

static sgx_status_t SGX_CDECL sgx_DecSuppleSWO(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_DecSuppleSWO_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_DecSuppleSWO_t* ms = SGX_CAST(ms_DecSuppleSWO_t*, pms);
	ms_DecSuppleSWO_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_DecSuppleSWO_t), ms, sizeof(ms_DecSuppleSWO_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_encrypt_result_buffer = __in_ms.ms_encrypt_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;


	DecSuppleSWO(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_M, __in_ms.ms_K, __in_ms.ms_encrypted_block_size, _tmp_encrypt_result_buffer, _tmp_ret);


	return status;
}

static sgx_status_t SGX_CDECL sgx_DecSuppleSWO_parallel(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_DecSuppleSWO_parallel_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_DecSuppleSWO_parallel_t* ms = SGX_CAST(ms_DecSuppleSWO_parallel_t*, pms);
	ms_DecSuppleSWO_parallel_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_DecSuppleSWO_parallel_t), ms, sizeof(ms_DecSuppleSWO_parallel_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_encrypted_buffer = __in_ms.ms_encrypted_buffer;
	unsigned char* _tmp_encrypt_result_buffer = __in_ms.ms_encrypt_result_buffer;
	enc_ret* _tmp_ret = __in_ms.ms_ret;


	DecSuppleSWO_parallel(_tmp_encrypted_buffer, __in_ms.ms_N, __in_ms.ms_M, __in_ms.ms_K, __in_ms.ms_encrypted_block_size, _tmp_encrypt_result_buffer, _tmp_ret, __in_ms.ms_nthreads);


	return status;
}

static sgx_status_t SGX_CDECL sgx_FMSCompactPrepare(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_FMSCompactPrepare_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_FMSCompactPrepare_t* ms = SGX_CAST(ms_FMSCompactPrepare_t*, pms);
	ms_FMSCompactPrepare_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_FMSCompactPrepare_t), ms, sizeof(ms_FMSCompactPrepare_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	uint8_t* _tmp_routing_tags = __in_ms.ms_routing_tags;
	size_t _tmp_n = __in_ms.ms_n;
	size_t _len_routing_tags = _tmp_n;
	uint8_t* _in_routing_tags = NULL;
	size_t* _tmp_control_words = __in_ms.ms_control_words;
	size_t _len_control_words = sizeof(size_t);
	size_t* _in_control_words = NULL;
	double* _tmp_control_us = __in_ms.ms_control_us;
	size_t _len_control_us = sizeof(double);
	double* _in_control_us = NULL;
	int _in_retval;

	CHECK_UNIQUE_POINTER(_tmp_routing_tags, _len_routing_tags);
	CHECK_UNIQUE_POINTER(_tmp_control_words, _len_control_words);
	CHECK_UNIQUE_POINTER(_tmp_control_us, _len_control_us);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_routing_tags != NULL && _len_routing_tags != 0) {
		if ( _len_routing_tags % sizeof(*_tmp_routing_tags) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_routing_tags = (uint8_t*)malloc(_len_routing_tags);
		if (_in_routing_tags == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_routing_tags, _len_routing_tags, _tmp_routing_tags, _len_routing_tags)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	if (_tmp_control_words != NULL && _len_control_words != 0) {
		if ( _len_control_words % sizeof(*_tmp_control_words) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		if ((_in_control_words = (size_t*)malloc(_len_control_words)) == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		memset((void*)_in_control_words, 0, _len_control_words);
	}
	if (_tmp_control_us != NULL && _len_control_us != 0) {
		if ( _len_control_us % sizeof(*_tmp_control_us) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		if ((_in_control_us = (double*)malloc(_len_control_us)) == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		memset((void*)_in_control_us, 0, _len_control_us);
	}
	_in_retval = FMSCompactPrepare(_in_routing_tags, _tmp_n, __in_ms.ms_n_left, __in_ms.ms_n_right, _in_control_words, _in_control_us);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}
	if (_in_control_words) {
		if (memcpy_verw_s(_tmp_control_words, _len_control_words, _in_control_words, _len_control_words)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}
	if (_in_control_us) {
		if (memcpy_verw_s(_tmp_control_us, _len_control_us, _in_control_us, _len_control_us)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_routing_tags) free(_in_routing_tags);
	if (_in_control_words) free(_in_control_words);
	if (_in_control_us) free(_in_control_us);
	return status;
}

static sgx_status_t SGX_CDECL sgx_FMSCompactOnline(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_FMSCompactOnline_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_FMSCompactOnline_t* ms = SGX_CAST(ms_FMSCompactOnline_t*, pms);
	ms_FMSCompactOnline_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_FMSCompactOnline_t), ms, sizeof(ms_FMSCompactOnline_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buffer = __in_ms.ms_buffer;
	double _in_retval;


	_in_retval = FMSCompactOnline(_tmp_buffer, __in_ms.ms_n, __in_ms.ms_block_size);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_OCompactOnline(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_OCompactOnline_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_OCompactOnline_t* ms = SGX_CAST(ms_OCompactOnline_t*, pms);
	ms_OCompactOnline_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_OCompactOnline_t), ms, sizeof(ms_OCompactOnline_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buffer = __in_ms.ms_buffer;
	double _in_retval;


	_in_retval = OCompactOnline(_tmp_buffer, __in_ms.ms_n, __in_ms.ms_block_size);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	return status;
}

static sgx_status_t SGX_CDECL sgx_FMSCompactRelease(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	FMSCompactRelease();
	return status;
}

SGX_EXTERNC const struct {
	size_t nr_ecall;
	struct {void* ecall_addr; uint8_t is_priv; uint8_t is_switchless;} ecall_table[54];
} g_ecall_table = {
	54,
	{
		{(void*)(uintptr_t)sgx_Enclave_loadTestKeys, 0, 0},
		{(void*)(uintptr_t)sgx_MeasureOSWAPBuffer, 0, 0},
		{(void*)(uintptr_t)sgx_MeasureObliviousPrimitive, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_type_char, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_type_int, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_type_float, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_type_double, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_type_size_t, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_type_wchar_t, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_type_struct, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_type_enum_union, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_user_check, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_in, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_out, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_in_out, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_string, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_string_const, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_size, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_count, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_pointer_isptr_readonly, 0, 0},
		{(void*)(uintptr_t)sgx_ocall_pointer_attr, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_array_user_check, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_array_in, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_array_out, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_array_in_out, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_array_isary, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_function_calling_convs, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_function_public, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_function_private, 1, 0},
		{(void*)(uintptr_t)sgx_ecall_malloc_free, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_sgx_cpuid, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_exception, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_map, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_increase_counter, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_producer, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_consumer, 0, 0},
		{(void*)(uintptr_t)sgx_RecursiveShuffle_M1, 0, 0},
		{(void*)(uintptr_t)sgx_RecursiveShuffle_M2, 0, 0},
		{(void*)(uintptr_t)sgx_RecursiveShuffle_M2_opt, 0, 0},
		{(void*)(uintptr_t)sgx_DecryptAndShuffleM1, 0, 0},
		{(void*)(uintptr_t)sgx_DecryptAndShuffleM2, 0, 0},
		{(void*)(uintptr_t)sgx_testTightCompaction, 0, 0},
		{(void*)(uintptr_t)sgx_testOPTightCompaction, 0, 0},
		{(void*)(uintptr_t)sgx_decryptAndSubSample, 0, 0},
		{(void*)(uintptr_t)sgx_decryptAndSubSampleMulti, 0, 0},
		{(void*)(uintptr_t)sgx_decryptAndSubSampleMulti_opt, 0, 0},
		{(void*)(uintptr_t)sgx_DecPSQF_single, 0, 0},
		{(void*)(uintptr_t)sgx_DecPSQF_SWO, 0, 0},
		{(void*)(uintptr_t)sgx_DecSuppleSWO, 0, 0},
		{(void*)(uintptr_t)sgx_DecSuppleSWO_parallel, 0, 0},
		{(void*)(uintptr_t)sgx_FMSCompactPrepare, 0, 0},
		{(void*)(uintptr_t)sgx_FMSCompactOnline, 0, 0},
		{(void*)(uintptr_t)sgx_OCompactOnline, 0, 0},
		{(void*)(uintptr_t)sgx_FMSCompactRelease, 0, 0},
	}
};

SGX_EXTERNC const struct {
	size_t nr_ocall;
	uint8_t entry_table[21][54];
} g_dyn_entry_table = {
	21,
	{
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	}
};


sgx_status_t SGX_CDECL ocall_print_string(const char* str)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_str = str ? strlen(str) + 1 : 0;

	ms_ocall_print_string_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_print_string_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(str, _len_str);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (str != NULL) ? _len_str : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_print_string_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_print_string_t));
	ocalloc_size -= sizeof(ms_ocall_print_string_t);

	if (str != NULL) {
		if (memcpy_verw_s(&ms->ms_str, sizeof(const char*), &__tmp, sizeof(const char*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_str % sizeof(*str) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, str, _len_str)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_str);
		ocalloc_size -= _len_str;
	} else {
		ms->ms_str = NULL;
	}

	status = sgx_ocall(0, ms);

	if (status == SGX_SUCCESS) {
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_print_string_with_rtclock(unsigned long int* retval, const char* str)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_str = str ? strlen(str) + 1 : 0;

	ms_ocall_print_string_with_rtclock_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_print_string_with_rtclock_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(str, _len_str);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (str != NULL) ? _len_str : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_print_string_with_rtclock_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_print_string_with_rtclock_t));
	ocalloc_size -= sizeof(ms_ocall_print_string_with_rtclock_t);

	if (str != NULL) {
		if (memcpy_verw_s(&ms->ms_str, sizeof(const char*), &__tmp, sizeof(const char*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_str % sizeof(*str) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, str, _len_str)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_str);
		ocalloc_size -= _len_str;
	} else {
		ms->ms_str = NULL;
	}

	status = sgx_ocall(1, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_print_string_with_rtclock_diff(unsigned long int* retval, const char* str, unsigned long int before)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_str = str ? strlen(str) + 1 : 0;

	ms_ocall_print_string_with_rtclock_diff_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_print_string_with_rtclock_diff_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(str, _len_str);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (str != NULL) ? _len_str : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_print_string_with_rtclock_diff_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_print_string_with_rtclock_diff_t));
	ocalloc_size -= sizeof(ms_ocall_print_string_with_rtclock_diff_t);

	if (str != NULL) {
		if (memcpy_verw_s(&ms->ms_str, sizeof(const char*), &__tmp, sizeof(const char*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_str % sizeof(*str) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, str, _len_str)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_str);
		ocalloc_size -= _len_str;
	} else {
		ms->ms_str = NULL;
	}

	if (memcpy_verw_s(&ms->ms_before, sizeof(ms->ms_before), &before, sizeof(before))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(2, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL untrustedMemAllocate(unsigned char** retval, uint64_t size)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_untrustedMemAllocate_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_untrustedMemAllocate_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_untrustedMemAllocate_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_untrustedMemAllocate_t));
	ocalloc_size -= sizeof(ms_untrustedMemAllocate_t);

	if (memcpy_verw_s(&ms->ms_size, sizeof(ms->ms_size), &size, sizeof(size))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(3, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL untrustedMemFree(unsigned char* mem)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_untrustedMemFree_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_untrustedMemFree_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_untrustedMemFree_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_untrustedMemFree_t));
	ocalloc_size -= sizeof(ms_untrustedMemFree_t);

	if (memcpy_verw_s(&ms->ms_mem, sizeof(ms->ms_mem), &mem, sizeof(mem))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(4, ms);

	if (status == SGX_SUCCESS) {
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_clock(long int* retval)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_ocall_clock_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_clock_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_clock_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_clock_t));
	ocalloc_size -= sizeof(ms_ocall_clock_t);

	status = sgx_ocall(5, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_wallclock(double* retval, int start_or_stop)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_ocall_wallclock_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_wallclock_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_wallclock_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_wallclock_t));
	ocalloc_size -= sizeof(ms_ocall_wallclock_t);

	if (memcpy_verw_s(&ms->ms_start_or_stop, sizeof(ms->ms_start_or_stop), &start_or_stop, sizeof(start_or_stop))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(6, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL pthread_wait_timeout_ocall(int* retval, unsigned long long waiter, unsigned long long timeout)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_pthread_wait_timeout_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_pthread_wait_timeout_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_pthread_wait_timeout_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_pthread_wait_timeout_ocall_t));
	ocalloc_size -= sizeof(ms_pthread_wait_timeout_ocall_t);

	if (memcpy_verw_s(&ms->ms_waiter, sizeof(ms->ms_waiter), &waiter, sizeof(waiter))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (memcpy_verw_s(&ms->ms_timeout, sizeof(ms->ms_timeout), &timeout, sizeof(timeout))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(7, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL pthread_create_ocall(int* retval, unsigned long long self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_pthread_create_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_pthread_create_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_pthread_create_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_pthread_create_ocall_t));
	ocalloc_size -= sizeof(ms_pthread_create_ocall_t);

	if (memcpy_verw_s(&ms->ms_self, sizeof(ms->ms_self), &self, sizeof(self))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(8, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL pthread_wakeup_ocall(int* retval, unsigned long long waiter)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_pthread_wakeup_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_pthread_wakeup_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_pthread_wakeup_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_pthread_wakeup_ocall_t));
	ocalloc_size -= sizeof(ms_pthread_wakeup_ocall_t);

	if (memcpy_verw_s(&ms->ms_waiter, sizeof(ms->ms_waiter), &waiter, sizeof(waiter))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(9, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_pointer_user_check(int* val)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_ocall_pointer_user_check_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_pointer_user_check_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_pointer_user_check_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_pointer_user_check_t));
	ocalloc_size -= sizeof(ms_ocall_pointer_user_check_t);

	if (memcpy_verw_s(&ms->ms_val, sizeof(ms->ms_val), &val, sizeof(val))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(10, ms);

	if (status == SGX_SUCCESS) {
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_pointer_in(int* val)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_val = sizeof(int);

	ms_ocall_pointer_in_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_pointer_in_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(val, _len_val);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (val != NULL) ? _len_val : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_pointer_in_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_pointer_in_t));
	ocalloc_size -= sizeof(ms_ocall_pointer_in_t);

	if (val != NULL) {
		if (memcpy_verw_s(&ms->ms_val, sizeof(int*), &__tmp, sizeof(int*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_val % sizeof(*val) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, val, _len_val)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_val);
		ocalloc_size -= _len_val;
	} else {
		ms->ms_val = NULL;
	}

	status = sgx_ocall(11, ms);

	if (status == SGX_SUCCESS) {
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_pointer_out(int* val)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_val = sizeof(int);

	ms_ocall_pointer_out_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_pointer_out_t);
	void *__tmp = NULL;

	void *__tmp_val = NULL;

	CHECK_ENCLAVE_POINTER(val, _len_val);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (val != NULL) ? _len_val : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_pointer_out_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_pointer_out_t));
	ocalloc_size -= sizeof(ms_ocall_pointer_out_t);

	if (val != NULL) {
		if (memcpy_verw_s(&ms->ms_val, sizeof(int*), &__tmp, sizeof(int*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp_val = __tmp;
		if (_len_val % sizeof(*val) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		memset_verw(__tmp_val, 0, _len_val);
		__tmp = (void *)((size_t)__tmp + _len_val);
		ocalloc_size -= _len_val;
	} else {
		ms->ms_val = NULL;
	}

	status = sgx_ocall(12, ms);

	if (status == SGX_SUCCESS) {
		if (val) {
			if (memcpy_s((void*)val, _len_val, __tmp_val, _len_val)) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_pointer_in_out(int* val)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_val = sizeof(int);

	ms_ocall_pointer_in_out_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_pointer_in_out_t);
	void *__tmp = NULL;

	void *__tmp_val = NULL;

	CHECK_ENCLAVE_POINTER(val, _len_val);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (val != NULL) ? _len_val : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_pointer_in_out_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_pointer_in_out_t));
	ocalloc_size -= sizeof(ms_ocall_pointer_in_out_t);

	if (val != NULL) {
		if (memcpy_verw_s(&ms->ms_val, sizeof(int*), &__tmp, sizeof(int*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp_val = __tmp;
		if (_len_val % sizeof(*val) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, val, _len_val)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_val);
		ocalloc_size -= _len_val;
	} else {
		ms->ms_val = NULL;
	}

	status = sgx_ocall(13, ms);

	if (status == SGX_SUCCESS) {
		if (val) {
			if (memcpy_s((void*)val, _len_val, __tmp_val, _len_val)) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL memccpy(void** retval, void* dest, const void* src, int val, size_t len)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_dest = len;
	size_t _len_src = len;

	ms_memccpy_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_memccpy_t);
	void *__tmp = NULL;

	void *__tmp_dest = NULL;

	CHECK_ENCLAVE_POINTER(dest, _len_dest);
	CHECK_ENCLAVE_POINTER(src, _len_src);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (dest != NULL) ? _len_dest : 0))
		return SGX_ERROR_INVALID_PARAMETER;
	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (src != NULL) ? _len_src : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_memccpy_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_memccpy_t));
	ocalloc_size -= sizeof(ms_memccpy_t);

	if (dest != NULL) {
		if (memcpy_verw_s(&ms->ms_dest, sizeof(void*), &__tmp, sizeof(void*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp_dest = __tmp;
		if (memcpy_verw_s(__tmp, ocalloc_size, dest, _len_dest)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_dest);
		ocalloc_size -= _len_dest;
	} else {
		ms->ms_dest = NULL;
	}

	if (src != NULL) {
		if (memcpy_verw_s(&ms->ms_src, sizeof(const void*), &__tmp, sizeof(const void*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, src, _len_src)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_src);
		ocalloc_size -= _len_src;
	} else {
		ms->ms_src = NULL;
	}

	if (memcpy_verw_s(&ms->ms_val, sizeof(ms->ms_val), &val, sizeof(val))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (memcpy_verw_s(&ms->ms_len, sizeof(ms->ms_len), &len, sizeof(len))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(14, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
		if (dest) {
			if (memcpy_s((void*)dest, _len_dest, __tmp_dest, _len_dest)) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_function_allow(void)
{
	sgx_status_t status = SGX_SUCCESS;
	status = sgx_ocall(15, NULL);

	return status;
}
sgx_status_t SGX_CDECL sgx_oc_cpuidex(int cpuinfo[4], int leaf, int subleaf)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_cpuinfo = 4 * sizeof(int);

	ms_sgx_oc_cpuidex_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_oc_cpuidex_t);
	void *__tmp = NULL;

	void *__tmp_cpuinfo = NULL;

	CHECK_ENCLAVE_POINTER(cpuinfo, _len_cpuinfo);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (cpuinfo != NULL) ? _len_cpuinfo : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_oc_cpuidex_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_oc_cpuidex_t));
	ocalloc_size -= sizeof(ms_sgx_oc_cpuidex_t);

	if (cpuinfo != NULL) {
		if (memcpy_verw_s(&ms->ms_cpuinfo, sizeof(int*), &__tmp, sizeof(int*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp_cpuinfo = __tmp;
		if (_len_cpuinfo % sizeof(*cpuinfo) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		memset_verw(__tmp_cpuinfo, 0, _len_cpuinfo);
		__tmp = (void *)((size_t)__tmp + _len_cpuinfo);
		ocalloc_size -= _len_cpuinfo;
	} else {
		ms->ms_cpuinfo = NULL;
	}

	if (memcpy_verw_s(&ms->ms_leaf, sizeof(ms->ms_leaf), &leaf, sizeof(leaf))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (memcpy_verw_s(&ms->ms_subleaf, sizeof(ms->ms_subleaf), &subleaf, sizeof(subleaf))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(16, ms);

	if (status == SGX_SUCCESS) {
		if (cpuinfo) {
			if (memcpy_s((void*)cpuinfo, _len_cpuinfo, __tmp_cpuinfo, _len_cpuinfo)) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_wait_untrusted_event_ocall(int* retval, const void* self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_wait_untrusted_event_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_wait_untrusted_event_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t);

	if (memcpy_verw_s(&ms->ms_self, sizeof(ms->ms_self), &self, sizeof(self))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(17, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_set_untrusted_event_ocall(int* retval, const void* waiter)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_set_untrusted_event_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_set_untrusted_event_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_set_untrusted_event_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_set_untrusted_event_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_set_untrusted_event_ocall_t);

	if (memcpy_verw_s(&ms->ms_waiter, sizeof(ms->ms_waiter), &waiter, sizeof(waiter))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(18, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_setwait_untrusted_events_ocall(int* retval, const void* waiter, const void* self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_setwait_untrusted_events_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_setwait_untrusted_events_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t);

	if (memcpy_verw_s(&ms->ms_waiter, sizeof(ms->ms_waiter), &waiter, sizeof(waiter))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (memcpy_verw_s(&ms->ms_self, sizeof(ms->ms_self), &self, sizeof(self))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(19, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_set_multiple_untrusted_events_ocall(int* retval, const void** waiters, size_t total)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_waiters = total * sizeof(void*);

	ms_sgx_thread_set_multiple_untrusted_events_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(waiters, _len_waiters);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (waiters != NULL) ? _len_waiters : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_set_multiple_untrusted_events_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t);

	if (waiters != NULL) {
		if (memcpy_verw_s(&ms->ms_waiters, sizeof(const void**), &__tmp, sizeof(const void**))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_waiters % sizeof(*waiters) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, waiters, _len_waiters)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_waiters);
		ocalloc_size -= _len_waiters;
	} else {
		ms->ms_waiters = NULL;
	}

	if (memcpy_verw_s(&ms->ms_total, sizeof(ms->ms_total), &total, sizeof(total))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(20, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

