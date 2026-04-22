
BITS 64

section .text

  global oswap_buffer_16x
  global oswap_buffer_byte
  global oswap_buffer_byte_v2
  global oswap_buffer_byte_16x
  global ogt_comp_swap


oswap_buffer_16x:
	; Take inputs,  1 ptr to buffer1, 2 ptr to buffer2, 3 buffer_size, 4 flag
	; Linux : 	rdi,rsi,rdx,rcx->rbp

	; Callee-saved : RBP, RBX, and R12–R15

	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15

	; Move ptr to buffer1 and buffer2 to r10 and r11
	mov r10, rdi
	mov r11, rsi

	;RCX will be lost for loop, store flag from rcx to rbp (1 byte , so bpl)
	mov bpl, cl

	; Oblivious evaluation of flag
	cmp bpl, 1

	;Set loop parameters
	mov ecx, edx
	shr ecx, 4

	; Loop to fetch iter & res chunks till blk_size
	loopstart_osb:
		cmp bpl, 1
		mov r14, qword [r10]    ; B1 data
    mov r12, qword [r10]    ; B1 data
		mov rbx, qword [r10+8]  ; B1 data (next qword)
    mov rdx, qword [r10+8]  ; B1 data
		mov r15, qword [r11]    ; B2 data
		mov r13, qword [r11+8]  ; B2 data (next qword)
		cmovz r14, r15 			    ; r14 <- r15 based on the flag (C1)
		cmovz rbx, r13 			    ; rbx <- r13 based on the flag (C1')
    cmovz r15, r12          ; r15 <- r12 based on the flag (C2)
    cmovz r13, rdx          ; r13 <- rdx based on the flag (C2')
		mov qword [r10], r14    ; B1 gets back r14, which is B2's data if flag is true from (C1)
                            ;   else it gets back the same B1 data
		mov qword [r10+8], rbx  ; B1 gets back rbx, which is B2's data if flag is true from (C1')
                            ;   else it gets back the same B1 data
    mov qword [r11], r15    ; B2 gets back r15, which is B1's original data if flag is true from (C2)
                            ;   else it gets back the same B2 data
    mov qword [r11+8], r13  ; B2 gets back r13, which is B1's original data if flag is true from (C2')
                            ;   else it gets back the same B2 data
		add r10, 16
		add r11, 16
		dec ecx
		jnz loopstart_osb

	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx

	ret


oswap_buffer_byte_16x:
	; Take inputs,  1 ptr to buffer1, 2 ptr to buffer2, 3 buffer_size, 4 flag
	; Linux : 	rdi,rsi,rdx,rcx->rbp

	; Callee-saved : RBP, RBX, and R12–R15

	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15

	; Move ptr to buffer1 and buffer2 to r10 and r11
	mov r10, rdi
	mov r11, rsi

	;RCX will be lost for loop, store flag from rcx to rbp (1 byte , so bpl)
	mov bpl, cl

	; Oblivious evaluation of flag
	cmp bpl, 1

	; Oblivious evaluation of flag
  mov r14, qword [r10]    ; B1 data
  mov r15, qword [r10]    ; B1 data
  mov rcx, qword [r11]    ; B2 data
  cmovz r14, rcx 			    ; r14 <- r15 based on the flag (C1)
  cmovz rcx, r15          ; r15 <- r12 based on the flag (C2)
  mov qword [r10], r10    ; B1 gets back r14, which is B2's data if flag is true from (C1)
                         ;   else it gets back the same B1 data
  mov qword [r11], rcx    ; B2 gets back r15, which is B1's original data if flag is true from (C2)
                          ;   else it gets back the same B2 data

  add r10, 8
  add r11, 8
  ; No need to remove 8 from buffer_size, as it gets handled by the divide by 16 for ecx

	;Set loop parameters
	mov ecx, edx
	shr ecx, 4

	; Loop to fetch iter & res chunks till blk_size
	loopstart_osbb16:
		cmp bpl, 1
		mov r14, qword [r10]    ; B1 data
    mov r12, qword [r10]    ; B1 data
		mov rbx, qword [r10+8]  ; B1 data (next qword)
    mov rdx, qword [r10+8]  ; B1 data
		mov r15, qword [r11]    ; B2 data
		mov r13, qword [r11+8]  ; B2 data (next qword)
		cmovz r14, r15 			    ; r14 <- r15 based on the flag (C1)
		cmovz rbx, r13 			    ; rbx <- r13 based on the flag (C1')
    cmovz r15, r12          ; r15 <- r12 based on the flag (C2)
    cmovz r13, rdx          ; r13 <- rdx based on the flag (C2')
		mov qword [r10], r14    ; B1 gets back r14, which is B2's data if flag is true from (C1)
                            ;   else it gets back the same B1 data
		mov qword [r10+8], rbx  ; B1 gets back rbx, which is B2's data if flag is true from (C1')
                            ;   else it gets back the same B1 data
    mov qword [r11], r15    ; B2 gets back r15, which is B1's original data if flag is true from (C2)
                            ;   else it gets back the same B2 data
    mov qword [r11+8], r13  ; B2 gets back r13, which is B1's original data if flag is true from (C2')
                            ;   else it gets back the same B2 data
		add r10, 16
		add r11, 16
		dec ecx
		jnz loopstart_osbb16

	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx

	ret



oswap_buffer_byte:
  ; Take inputs,  1 ptr to buffer1, 2 ptr to buffer2, 3 buffer_size, 4 flag
  ; Linux :   rdi,rsi,rdx,rcx->rbp

  ; Callee-saved : RBP, RBX, and R12–R15

  push rbx
  push rbp
  push r12
  push r13
  push r14
  push r15

  ; Move ptr to buffer1 and buffer2 to r10 and r11
  mov r10, rdi
  mov r11, rsi

  ;RCX will be lost for loop, store flag from rcx to rbp (1 byte , so bpl)
  mov bpl, cl

  ; Oblivious evaluation of flag
  cmp bpl, 1

  ;Set loop parameters
  mov ecx, edx
  shr ecx, 3

  ; Loop to fetch iter & res chunks till blk_size
  loopstart_osbb:
    cmp bpl, 1
    mov r14, qword [r10]    ; B1 data
    mov r12, qword [r10]    ; B1 data
    mov r15, qword [r11]    ; B2 data
    cmovz r14, r15          ; r14 <- r15 based on the flag (C1)
    cmovz r15, r12          ; r15 <- r12 based on the flag (C2)
    mov qword [r10], r14    ; B1 gets back r14, which is B2's data if flag is true from (C1)
                            ;   else it gets back the same B1 data
    mov qword [r11], r15    ; B2 gets back r15, which is B1's original data if flag is true from (C2)
                            ;   else it gets back the same B2 data
    add r10, 8
    add r11, 8
    dec ecx
    jnz loopstart_osbb

  pop r15
  pop r14
  pop r13
  pop r12
  pop rbp
  pop rbx

  ret

oswap_buffer_byte_v2:
	; Take inputs,  1 ptr to buffer1, 2 ptr to buffer2, 3 flag
	; Linux : 	rdi,rsi,rdx

	; Callee-saved : RBP, RBX, and R12–R15

	; Move ptr to buffer1 and buffer2 to r10 and r11
	mov r8, rdi
	mov r9, rsi

	; Oblivious evaluation of flag
	cmp dx, 1
  mov r10, qword [rdi]    ; B1 data
  mov r11, qword [rdi]    ; B1 data
  mov rcx, qword [rsi]    ; B2 data
  cmovz r10, rcx 			    ; r14 <- r15 based on the flag (C1)
  cmovz rcx, r11          ; r15 <- r12 based on the flag (C2)
  mov qword [rdi], r10    ; B1 gets back r14, which is B2's data if flag is true from (C1)
                         ;   else it gets back the same B1 data
  mov qword [rsi], rcx    ; B2 gets back r15, which is B1's original data if flag is true from (C2)
                          ;   else it gets back the same B2 data

	ret


ogt_comp_swap:
    ; Obliviously compare 2 keys (key1 and key2) and swap buffers
    ; if key1 is greater than key2. Not oblivious to buffersize!
    ; void ogt_comp_swap(uint64_t *key1, uint64_t *key2, unsigned char *buff1, unsigned char *buff2, uint32_t buffersize);
    ; Registers: rax, rbx, rcx, rdx, rsp, rbp, rsi, rdi, r8-r15
    ; Linux caller-saved : rax,rdi,rsi,rdx,rcx,r8-r11
    ; Callee-saved : RBP, RBX, and R12–R15
    ; rdi used to pass 1st argument to functions
    ; rsi used to pass 2nd argument to functions
    ; rdx used to pass 3rd argument to functions
    ; rcx used to pass 4th argument to functions
    ; r8 used to pass 5th argument to functions

    push r12
	push r13
    push r14
	push r15

    ;; Set flag if key1 is larger than key2
    xor r9, r9
    mov r10, qword [rdi]
    cmp r10, qword [rsi]
    seta r9b

    ;; Set loop parameter
    shr r8, 4  ; Divide by 16 as two 8-byte words swapped each iteration

    ;; Swap chunks until end of buffer. Each iterations swaps two 8-byte chunks
    ;; for speed (pipelining slow memory fetches).
    loopstart_ogt_comp_swap:
        mov r10, qword [rdx]    ; buff1 qword
        mov r11, qword [rdx]    ; buff1 qword
        mov r12, qword [rdx+8]  ; next buff1 qword
        mov r13, qword [rdx+8]  ; next buff1 qword
        mov r14, qword [rcx]    ; buff2 qword
        mov r15, qword [rcx+8]  ; next buff2 qword

        ;; Conditionally move based on flag
        cmp r9b, 1
        cmovz r10, r14
        cmovz r11, r15
        cmovz r14, r12
        cmovz r15, r13

        ;; Copy back into buffers
        mov qword [rdx], r10
        mov qword [rdx+8], r11
        mov qword [rcx], r14
        mov qword [rcx+8], r15

        ;; Loop if chunks remain
        add rdx, 16
		add rcx, 16
        dec r8
        jnz loopstart_ogt_comp_swap

    pop r15
	pop r14
    pop r13
	pop r12

    ret
