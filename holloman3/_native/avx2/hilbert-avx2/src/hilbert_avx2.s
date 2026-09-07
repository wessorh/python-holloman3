# hilbert_avx2.s
# AVX2 implementation that packs x,y coordinates into single uint64
# Upper 32 bits = x, Lower 32 bits = y

# void hilbert_d2xy_batch_avx2(const uint32_t *distances, uint64_t *packed, 
#                               size_t count, uint32_t order)
.text
.globl hilbert_d2xy_batch_avx2
.type hilbert_d2xy_batch_avx2, @function
hilbert_d2xy_batch_avx2:
    # System V AMD64 ABI: RDI, RSI, RDX, RCX
    # distances = RDI, packed = RSI, count = RDX, order = ECX
    
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    # Save parameters to non-volatile registers
    # RDI = distances, RSI = packed, RDX = count, RCX = order
    movq %rdi, %r10         # distances pointer -> R10
    movq %rsi, %r11         # packed pointer -> R11
    movq %rdx, %r12         # count -> R12
    movl %ecx, %r9d         # order -> R9D
    
    cmpq $8, %r12
    jl scalar_fallback
    
    movq %r12, %r13
    shrq $3, %r13
    
    vpcmpeqd %ymm15, %ymm15, %ymm15
    vpsrld $31, %ymm15, %ymm15
    vpxor %ymm14, %ymm14, %ymm14

avx2_batch_loop:
    vmovdqu (%r10), %ymm0
    
    vpxor %ymm1, %ymm1, %ymm1
    vpxor %ymm2, %ymm2, %ymm2
    vmovdqa %ymm15, %ymm3
    
    # Use R14D as bit level loop counter (NOT R11D!)
    movl %r9d, %r14d

bit_level_loop:
    vpsrld $1, %ymm0, %ymm4
    vpand %ymm15, %ymm4, %ymm4
    
    vpxor %ymm0, %ymm4, %ymm5
    vpand %ymm15, %ymm5, %ymm5
    
    vpcmpeqd %ymm14, %ymm5, %ymm6
    vpcmpeqd %ymm15, %ymm4, %ymm7
    vpand %ymm6, %ymm7, %ymm8
    
    vpsubd %ymm15, %ymm3, %ymm9
    
    vpsubd %ymm1, %ymm9, %ymm10
    vpblendvb %ymm8, %ymm10, %ymm1, %ymm1
    
    vpsubd %ymm2, %ymm9, %ymm10
    vpblendvb %ymm8, %ymm10, %ymm2, %ymm2
    
    vpblendvb %ymm6, %ymm2, %ymm1, %ymm10
    vpblendvb %ymm6, %ymm1, %ymm2, %ymm2
    vmovdqa %ymm10, %ymm1
    
    vpmulld %ymm3, %ymm4, %ymm10
    vpaddd %ymm10, %ymm1, %ymm1
    
    vpmulld %ymm3, %ymm5, %ymm10
    vpaddd %ymm10, %ymm2, %ymm2
    
    vpsrld $2, %ymm0, %ymm0
    vpslld $1, %ymm3, %ymm3
    
    decl %r14d
    jnz bit_level_loop
    
    # Pack x,y into 64-bit values
    # Extract lower 128 bits and convert to 64-bit
    vextracti128 $0, %ymm1, %xmm4
    vpmovsxdq %xmm4, %ymm4
    
    # Extract upper 128 bits and convert to 64-bit
    vextracti128 $1, %ymm1, %xmm5
    vpmovsxdq %xmm5, %ymm5
    
    # Shift x left by 32
    vpsllq $32, %ymm4, %ymm4
    vpsllq $32, %ymm5, %ymm5
    
    # Extract lower 128 bits of y and convert to 64-bit (zero-extended)
    vextracti128 $0, %ymm2, %xmm6
    vpmovzxdq %xmm6, %ymm6
    
    # Extract upper 128 bits of y and convert to 64-bit (zero-extended)
    vextracti128 $1, %ymm2, %xmm7
    vpmovzxdq %xmm7, %ymm7
    
    # OR x and y together
    vpor %ymm4, %ymm6, %ymm4
    vpor %ymm5, %ymm7, %ymm5
    
    # Store packed values
    vmovdqu %ymm4, (%r11)
    vmovdqu %ymm5, 32(%r11)
    
    addq $32, %r10
    addq $64, %r11
    
    decq %r13
    jnz avx2_batch_loop
    
    movq %r12, %r12
    andq $7, %r12
    jz done

scalar_fallback:
    movl (%r10), %eax
    
    xorl %ebx, %ebx
    xorl %edx, %edx
    movl $1, %r14d
    
    # Use R15D as scalar bit loop counter
    movl %r9d, %r15d

scalar_bit_loop:
    movl %eax, %edi
    shrl $1, %edi
    andl $1, %edi
    
    movl %eax, %esi
    xorl %edi, %esi
    andl $1, %esi
    
    testl %esi, %esi
    jnz skip_rot_scalar
    
    testl %edi, %edi
    jz swap_scalar
    
    movl %r14d, %ecx
    decl %ecx
    subl %ebx, %ecx
    movl %ecx, %ebx
    
    movl %r14d, %ecx
    decl %ecx
    subl %edx, %ecx
    movl %ecx, %edx

swap_scalar:
    movl %ebx, %ecx
    movl %edx, %ebx
    movl %ecx, %edx

skip_rot_scalar:
    movl %r14d, %ecx
    imull %edi, %ecx
    addl %ecx, %ebx
    
    movl %r14d, %ecx
    imull %esi, %ecx
    addl %ecx, %edx
    
    shrl $2, %eax
    shll $1, %r14d
    
    decl %r15d
    jnz scalar_bit_loop
    
    # Pack x,y into 64-bit: (x << 32) | y
    movq %rbx, %rdi
    shlq $32, %rdi
    movq %rdx, %rsi
    orq %rsi, %rdi
    movq %rdi, (%r11)
    
    addq $4, %r10
    addq $8, %r11
    
    decq %r12
    jnz scalar_fallback

done:
    vzeroupper
    
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret

.size hilbert_d2xy_batch_avx2, .-hilbert_d2xy_batch_avx2
# Mark stack as non-executable
.section .note.GNU-stack,"",@progbits
