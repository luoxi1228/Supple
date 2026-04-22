#ifndef __OASM_LIB_TCC__
#define __OASM_LIB_TCC__

#include "Enclave_globals.h"
#include "foav.h"

template<> inline void oswap_buffer<OSWAP_4>(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag)
{
    #ifdef COUNT_OSWAPS
      OSWAP_COUNTER++;
    #endif

    #if 0
    oswap_buffer_byte_v2(dest, source, flag);
    #else
    __asm__ (
        "# inline oswap_buffer<OSWAP_4>\n"
        "test %[flag], %[flag]\n"
        "movl (%[dest]), %%r10d\n"
        "movl (%[dest]), %%r11d\n"
        "movl (%[source]), %%ecx\n"
        "cmovnz %%ecx, %%r10d\n"
        "cmovnz %%r11d, %%ecx\n"
        "movl %%r10d, (%[dest])\n"
        "movl %%ecx, (%[source])\n"
        :
        : [dest] "r" (dest), [source] "r" (source), [flag] "r" (flag)
        : "cc", "memory", "r10", "r11", "ecx"
    );
    #endif
}

template<> inline void oswap_buffer<OSWAP_8>(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag)
{
    #ifdef COUNT_OSWAPS
      OSWAP_COUNTER++;
    #endif

    #if 0
    oswap_buffer_byte_v2(dest, source, flag);
    #else
    __asm__ (
        "# inline oswap_buffer<OSWAP_8>\n"
        "test %[flag], %[flag]\n"
        "movq (%[dest]), %%r10\n"
        "movq (%[dest]), %%r11\n"
        "movq (%[source]), %%rcx\n"
        "cmovnz %%rcx, %%r10\n"
        "cmovnz %%r11, %%rcx\n"
        "movq %%r10, (%[dest])\n"
        "movq %%rcx, (%[source])\n"
        :
        : [dest] "r" (dest), [source] "r" (source), [flag] "r" (flag)
        : "cc", "memory", "r10", "r11", "rcx"
    );
    #endif
}

template<> inline void oswap_buffer<OSWAP_12>(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag)
{
    #ifdef COUNT_OSWAPS
      OSWAP_COUNTER++;
    #endif

    #if 0
    oswap_buffer_byte_v2(dest, source, flag);
    #else
    __asm__ (
        "# inline oswap_buffer<OSWAP_12>\n"
        "test %[flag], %[flag]\n"
        "movq (%[dest]), %%r14\n"   // dest data
        "movq (%[dest]), %%r12\n"   // dest data
        "movl 8(%[dest]), %%ebx\n"  // dest data (next word)
        "movl 8(%[dest]), %%edx\n"  // dest data (next word)
        "movq (%[source]), %%r15\n"   // source data
        "movl 8(%[source]), %%r13d\n"  // source data (next word)

        "cmovnz %%r15, %%r14\n"    // r14 <- r15 based on the flag (C1)
        "cmovnz %%r13d, %%ebx\n"    // rbx <- r13 based on the flag (C1')
        "cmovnz %%r12, %%r15\n"    // r15 <- r12 based on the flag (C2)
        "cmovnz %%edx, %%r13d\n"    // r13 <- rdx based on the flag (C2')

        "movq %%r14, (%[dest])\n"   // dest gets back r14, which is source's data if flag is true from (C1)
                                  // else it gets back the same dest data
        "movl %%ebx, 8(%[dest])\n"  // dest+8 gets back ebx, which is source+8's data if flag is true from
                                  // (C1'), else it gets back the same dest+8 data
        "movq %%r15, (%[source])\n"   // source gets back r15, which is dest's original data if flag is true
                                  // from (C2), else it gets back the same B2 data
        "movl %%r13d, 8(%[source])\n"  // source+8 gets back r13d, which is dest+8's original data if flag is
                                  // true from (C2'), else it gets back the same B2 data
        :
        : [dest] "r" (dest), [source] "r" (source), [flag] "r" (flag)
        : "cc", "memory", "rcx", "r12", "r13", "r14", "r15", "rbx", "rdx"
    );
    #endif
}

template<> inline void oswap_buffer<OSWAP_16X>(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag)
{
    #ifdef COUNT_OSWAPS
      OSWAP_COUNTER++;
    #endif

    __asm__ (
      "# inline oswap_buffer<OSWAP_16X>\n"

      //Move ptr to dest and source buffers to r10 and r11
      "movq %[dest], %%r10\n"
      "movq %[source], %%r11\n"

      //Set loop parameters
      "movl %[buffersize], %%ecx\n"
      "shr $4, %%ecx\n"

      //Loop to fetch iter & res chunks till blk_size
      "1:\n"
        "test %[flag], %[flag]\n"
        "movq (%%r10), %%r14\n"   // dest data
        "movq (%%r10), %%r12\n"   // dest data
        "movq 8(%%r10), %%rbx\n"  // dest data (next qword)
        "movq 8(%%r10), %%rdx\n"  // dest data (next qword)
        "movq (%%r11), %%r15\n"   // source data
        "movq 8(%%r11), %%r13\n"  // source data (next qword)        
        
        "cmovnz %%r15, %%r14\n"    // r14 <- r15 based on the flag (C1)
        "cmovnz %%r13, %%rbx\n"    // rbx <- r13 based on the flag (C1')
        "cmovnz %%r12, %%r15\n"    // r15 <- r12 based on the flag (C2)
        "cmovnz %%rdx, %%r13\n"    // r13 <- rdx based on the flag (C2') 

        "movq %%r14, (%%r10)\n"   // dest gets back r14, which is source's data if flag is true from (C1)
                                  // else it gets back the same dest data 
        "movq %%rbx, 8(%%r10)\n"  // dest+8 gets back rbx, which is source+8's data if flag is true from
                                  // (C1'), else it gets back the same dest+8 data
        "movq %%r15, (%%r11)\n"   // source gets back r15, which is dest's original data if flag is true
                                  // from (C2), else it gets back the same B2 data
        "movq %%r13, 8(%%r11)\n"  // source+8 gets back r13, which is dest+8's original data if flag is 
                                  // true from (C2'), else it gets back the same B2 data
        "add $16, %%r10\n"
        "add $16, %%r11\n"
        "dec %%ecx\n"
        "# FOAV oswap_buffer_16X ctr (%%ecx)\n"
        "jnz 1b\n"
        :
        : [dest] "r" (dest), [source] "r" (source), [buffersize] "r" (buffersize), [flag] "r" (flag)
        : "cc", "memory", "r10", "r11", "rcx", "r12", "r13", "r14", "r15", "rbx", "rdx"
    );
}

template<> inline void oswap_buffer<OSWAP_8_16X>(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag)
{
    #ifdef COUNT_OSWAPS 
      OSWAP_COUNTER++;
    #endif

    __asm__ (
      "# inline oswap_buffer<OSWAP_8_16X>\n"

      //Move ptr to dest and source buffers to r10 and r11
      "movq %[dest], %%r10\n"
      "movq %[source], %%r11\n"

      // Move first 8 bytes obliviously:
      "test %[flag], %[flag]\n"
      "movq (%%r10), %%r14\n"   // dest data
      "movq (%%r10), %%r12\n"   // dest data
      "movq (%%r11), %%r15\n"   // source data
      
      "cmovnz %%r15, %%r14\n"    // r14 <- r15 based on the flag (C1)
      "cmovnz %%r12, %%r15\n"    // r15 <- r12 based on the flag (C2)

      "movq %%r14, (%%r10)\n"   // dest gets back r14, which is source's data if flag is true from (C1)
                                // else it gets back the same dest data 
      "movq %%r15, (%%r11)\n"   // source gets back r15, which is dest's original data if flag is true
                                // from (C2), else it gets back the same B2 data
      "add $8, %%r10\n"
      "add $8, %%r11\n" 

      //Set loop parameters
      "movl %[buffersize], %%ecx\n"
      "shr $4, %%ecx\n"

      //Loop to fetch iter & res chunks till blk_size
      "1:\n"
        "test %[flag], %[flag]\n"
        "movq (%%r10), %%r14\n"   // dest data
        "movq (%%r10), %%r12\n"   // dest data
        "movq 8(%%r10), %%rbx\n"  // dest data (next qword)
        "movq 8(%%r10), %%rdx\n"  // dest data (next qword)
        "movq (%%r11), %%r15\n"   // source data
        "movq 8(%%r11), %%r13\n"  // source data (next qword)        
        
        "cmovnz %%r15, %%r14\n"    // r14 <- r15 based on the flag (C1)
        "cmovnz %%r13, %%rbx\n"    // rbx <- r13 based on the flag (C1')
        "cmovnz %%r12, %%r15\n"    // r15 <- r12 based on the flag (C2)
        "cmovnz %%rdx, %%r13\n"    // r13 <- rdx based on the flag (C2') 

        "movq %%r14, (%%r10)\n"   // dest gets back r14, which is source's data if flag is true from (C1)
                                  // else it gets back the same dest data 
        "movq %%rbx, 8(%%r10)\n"  // dest+8 gets back rbx, which is source+8's data if flag is true from
                                  // (C1'), else it gets back the same dest+8 data
        "movq %%r15, (%%r11)\n"   // source gets back r15, which is dest's original data if flag is true
                                  // from (C2), else it gets back the same B2 data
        "movq %%r13, 8(%%r11)\n"  // source+8 gets back r13, which is dest+8's original data if flag is 
                                  // true from (C2'), else it gets back the same B2 data
        "add $16, %%r10\n"
        "add $16, %%r11\n"
        "dec %%ecx\n"
        " # FOAV oswap_buffer_16X ctr (%%ecx)\n"
        "jnz 1b\n"
        :
        : [dest] "r" (dest), [source] "r" (source), [buffersize] "r" (buffersize), [flag] "r" (flag)
        : "cc", "memory", "r10", "r11", "rcx", "r12", "r13", "r14", "r15", "rbx", "rdx" 
    );


}

template<> inline void oswap_key<uint32_t>(unsigned char *dest, unsigned char *source, uint8_t flag)
{
    oswap_buffer<OSWAP_4>(dest, source, 4, flag);
}

template<> inline void oswap_key<uint64_t>(unsigned char *dest, unsigned char *source, uint8_t flag)
{
    oswap_buffer<OSWAP_8>(dest, source, 8, flag);
}

template<> inline void oswap_key<__uint128_t>(unsigned char *dest, unsigned char *source, uint8_t flag)
{
    oswap_buffer<OSWAP_16X>(dest, source, 16, flag);
}

template<> inline void omove_buffer<OSWAP_8>(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag)
{
  __asm__ (
      "# inline omove_buffer<OSWAP_8>\n"

      "test %[flag], %[flag]\n"
      "movq (%[dest]), %%r10\n"
      //"movq (%[source]), %%rcx\n"
      "cmovnz (%[source]), %%r10\n"
      "movq %%r10, (%[dest])\n"
      :
      : [dest] "r" (dest), [source] "r" (source), [flag] "r" (flag)
      : "cc", "memory", "r10"
  );
}


template<> inline void omove_buffer<OSWAP_16X>(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag)
{
    __asm__ (
      "# inline omove_buffer<OSWAP_16X>\n"

      //Move ptr to dest and source buffers to r10 and r11
      "movq %[dest], %%r10\n"
      "movq %[source], %%r11\n"

      //Set loop parameters
      "movl %[buffersize], %%ecx\n"
      "shr $4, %%ecx\n"

      //Loop to fetch iter & res chunks till blk_size
      "1:\n"
        "test %[flag], %[flag]\n"
        "movq (%%r10), %%r14\n"   // dest data
        "movq 8(%%r10), %%rbx\n"  // dest data (next qword)
        "movq (%%r11), %%r15\n"   // source data
        "movq 8(%%r11), %%r13\n"  // source data (next qword)        
        
        "cmovnz %%r15, %%r14\n"    // r14 <- r15 based on the flag (C1)
        "cmovnz %%r13, %%rbx\n"    // rbx <- r13 based on the flag (C1')

        "movq %%r14, (%%r10)\n"   // dest gets back r14, which is source's data if flag is true from (C1)
                                  // else it gets back the same dest data 
        "movq %%rbx, 8(%%r10)\n"  // dest+8 gets back rbx, which is source+8's data if flag is true from
                                  // (C1'), else it gets back the same dest+8 data
        "add $16, %%r10\n"
        "add $16, %%r11\n"
        "dec %%ecx\n"
        " # FOAV oswap_buffer_16X ctr (%%ecx)\n"
        "jnz 1b\n"
        :
        : [dest] "r" (dest), [source] "r" (source), [buffersize] "r" (buffersize), [flag] "r" (flag)
        : "cc", "memory", "r10", "r11", "rcx", "r13", "r14", "r15", "rbx"
    );
}


template<> inline void omove_buffer<OSWAP_8_16X>(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag)
{
    __asm__ (
      "# inline omove_buffer<OSWAP_8_16X>\n"

      //Move ptr to dest and source buffers to r10 and r11
      "movq %[dest], %%r10\n"
      "movq %[source], %%r11\n"

      // Move first 8 bytes obliviously:
      "test %[flag], %[flag]\n"
      "movq (%%r10), %%r14\n"   // dest data
      "movq (%%r11), %%r15\n"   // source data
      
      "cmovnz %%r15, %%r14\n"    // r14 <- r15 based on the flag (C1)

      "movq %%r14, (%%r10)\n"   // dest gets back r14, which is source's data if flag is true from (C1)
                                // else it gets back the same dest data 
      "add $8, %%r10\n"
      "add $8, %%r11\n" 

      //Set loop parameters
      "movl %[buffersize], %%ecx\n"
      "shr $4, %%ecx\n"

      //Loop to fetch iter & res chunks till blk_size
      "1:\n"
        "test %[flag], %[flag]\n"
        "movq (%%r10), %%r14\n"   // dest data
        "movq 8(%%r10), %%rbx\n"  // dest data (next qword)
        "movq (%%r11), %%r15\n"   // source data
        "movq 8(%%r11), %%r13\n"  // source data (next qword)        
        
        "cmovnz %%r15, %%r14\n"    // r14 <- r15 based on the flag (C1)
        "cmovnz %%r13, %%rbx\n"    // rbx <- r13 based on the flag (C1')

        "movq %%r14, (%%r10)\n"   // dest gets back r14, which is source's data if flag is true from (C1)
                                  // else it gets back the same dest data 
        "movq %%rbx, 8(%%r10)\n"  // dest+8 gets back rbx, which is source+8's data if flag is true from
                                  // (C1'), else it gets back the same dest+8 data
        "add $16, %%r10\n"
        "add $16, %%r11\n"
        "dec %%ecx\n"
        " # FOAV oswap_buffer_16X ctr (%%ecx)\n"
        "jnz 1b\n"
        :
        : [dest] "r" (dest), [source] "r" (source), [buffersize] "r" (buffersize), [flag] "r" (flag)
        : "cc", "memory", "r10", "r11", "rcx", "r13", "r14", "r15", "rbx"
    );
}

/*
omove_buffer:
	; Take inputs,  1 ptr to dest_buffer, 2 ptr to source_buffer, 3 buffer_size, 4 flag
	; Linux : 	rdi,rsi,rdx,rcx->rbp

	; Callee-saved : RBP, RBX, and R12–R15

	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15

	; Move ptr to data from serialized_dest_block and serialized_source_blk
	mov r10, rdi
	mov r11, rsi

	;RCX will be lost for loop, store flag from rcx to rbp (1 byte , so bpl)
	mov bpl, cl

	; Oblivious evaluation of flag
	cmp bpl, 1

	;Set loop parameters
	mov ax, dx
	xor rdx, rdx
	mov bx, 8
	div bx
	mov cx, ax

	; Loop to fetch iter & res chunks till blk_size
	loopstart_omb:
		cmp bpl, 1
		mov r14, qword [r10]
		mov r15, qword [r11]
		cmovz r14, r15 				;r14 / r15 based on the compare
		mov qword [r10], r14
		add r10, 8
		add r11, 8
		loop loopstart_omb

	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx

	ret
*/

#endif
