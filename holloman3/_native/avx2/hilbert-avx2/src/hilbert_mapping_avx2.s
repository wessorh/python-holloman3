# hilbert_mapping_avx2.s
# AVX2 assembly implementation for buffer mapping
# Maps source buffer bytes to destination using Hilbert curve coordinates

# void hilbert_map_buffer_avx2_internal(const unsigned char *src, unsigned char *dst,
#                                        const uint64_t *curve, size_t count, 
#                                        uint32_t dimension)
.text
.globl hilbert_map_buffer_avx2_internal
.type hilbert_map_buffer_avx2_internal, @function
hilbert_map_buffer_avx2_internal:
    # System V AMD64 ABI: RDI, RSI, RDX, RCX, R8
    # src = RDI, dst = RSI, curve = RDX, count = RCX, dimension = R8D
    
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    movq %rdi, %r10         # src pointer
    movq %rsi, %r11         # dst pointer
    movq %rdx, %r12         # curve pointer
    movq %rcx, %r13         # count
    movl %r8d, %r14d        # dimension
    
    # Check if we have at least 8 bytes to process
    cmpq $8, %r13
    jl scalar_mapping
    
    # Broadcast dimension to YMM register for multiplication
    movd %r14d, %xmm15
    vpbroadcastd %xmm15, %ymm15

mapping_loop_8:
    # Load 8 packed coordinates from curve
    vmovdqu (%r12), %ymm0       # Load 4 packed coords (lower)
    vmovdqu 32(%r12), %ymm1     # Load 4 packed coords (upper)
    
    # Extract Y coordinates (lower 32 bits) from first 4
    vextracti128 $0, %ymm0, %xmm2
    vpmovzxdq %xmm2, %ymm2      # y0, y1 as 64-bit
    vextracti128 $1, %ymm0, %xmm3
    vpmovzxdq %xmm3, %ymm3      # y2, y3 as 64-bit
    
    # Extract Y coordinates from second 4
    vextracti128 $0, %ymm1, %xmm4
    vpmovzxdq %xmm4, %ymm4      # y4, y5 as 64-bit
    vextracti128 $1, %ymm1, %xmm5
    vpmovzxdq %xmm5, %ymm5      # y6, y7 as 64-bit
    
    # Extract X coordinates (upper 32 bits) from first 4
    vpsrlq $32, %ymm0, %ymm6
    vextracti128 $0, %ymm6, %xmm7
    vpmovzxdq %xmm7, %ymm7      # x0, x1 as 64-bit
    vextracti128 $1, %ymm6, %xmm8
    vpmovzxdq %xmm8, %ymm8      # x2, x3 as 64-bit
    
    # Extract X coordinates from second 4
    vpsrlq $32, %ymm1, %ymm9
    vextracti128 $0, %ymm9, %xmm10
    vpmovzxdq %xmm10, %ymm10    # x4, x5 as 64-bit
    vextracti128 $1, %ymm9, %xmm11
    vpmovzxdq %xmm11, %ymm11    # x6, x7 as 64-bit
    
    # Calculate positions: pos = y * dimension + x
    # Note: AVX2 lacks 64-bit multiply, using scalar for position calculation
    # AVX2 limitation: must use scalar for scattered writes
    
    # Fall back to scalar for actual byte placement (8 iterations)
    movq $8, %rbx
    
scalar_store_loop:
    # Load one byte from source
    movzbl (%r10), %eax
    
    # Get packed coordinate
    movq (%r12), %r15
    
    # Extract x and y
    movq %r15, %rcx
    shrq $32, %rcx              # x = upper 32 bits
    movl %r15d, %edx            # y = lower 32 bits
    
    # Calculate position: pos = y * dimension + x
    imulq %r14, %rdx            # y * dimension
    addq %rcx, %rdx             # + x
    
    # Store byte at destination
    movb %al, (%r11, %rdx, 1)
    
    # Advance pointers
    incq %r10                   # src++
    addq $8, %r12               # curve++ (8 bytes per packed coord)
    
    decq %rbx
    jnz scalar_store_loop
    
    # Update count
    subq $8, %r13
    cmpq $8, %r13
    jge mapping_loop_8

scalar_mapping:
    # Process remaining bytes one at a time
    testq %r13, %r13
    jz done
    
scalar_loop:
    # Load one byte from source
    movzbl (%r10), %eax
    
    # Get packed coordinate
    movq (%r12), %r15
    
    # Extract x and y
    movq %r15, %rcx
    shrq $32, %rcx              # x = upper 32 bits
    movl %r15d, %edx            # y = lower 32 bits
    
    # Calculate position: pos = y * dimension + x
    imulq %r14, %rdx            # y * dimension
    addq %rcx, %rdx             # + x
    
    # Store byte at destination
    movb %al, (%r11, %rdx, 1)
    
    # Advance pointers
    incq %r10                   # src++
    addq $8, %r12               # curve++
    
    decq %r13
    jnz scalar_loop

done:
    vzeroupper
    
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret

.size hilbert_map_buffer_avx2_internal, .-hilbert_map_buffer_avx2_internal
# Mark stack as non-executable
.section .note.GNU-stack,"",@progbits
