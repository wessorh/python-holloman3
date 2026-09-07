# hilbert_mapping_avx2_cache_blocking.s
# Cache blocking optimization for buffer mapping
# Process data in cache-friendly blocks to improve locality

    .text
    .globl hilbert_map_buffer_avx2_cache_blocking
    .type hilbert_map_buffer_avx2_cache_blocking, @function

hilbert_map_buffer_avx2_cache_blocking:
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
    
    # Cache blocking parameters
    # Block size: 256 bytes (fits in L1 cache)
    movq $256, %r9          # block_size
    
    xorq %r15, %r15         # outer_index = 0
    
.Louter_loop:
    # Calculate remaining bytes
    movq %r13, %rax
    subq %r15, %rax
    cmpq $0, %rax
    jle .Ldone
    
    # Determine block size (min of remaining and block_size)
    cmpq %r9, %rax
    cmovg %r9, %rax
    movq %rax, %rbx         # block_end = outer_index + block_size
    addq %r15, %rbx
    
    # Inner loop: process one block
    movq %r15, %rcx         # inner_index = outer_index
    
.Linner_loop:
    cmpq %rcx, %rbx
    jle .Lnext_block
    
    # Load byte from source
    movzbl (%r10,%rcx,1), %eax
    
    # Get packed coordinate from curve[i]
    movq (%r12,%rcx,8), %rdx
    
    # Extract x (upper 32 bits) and y (lower 32 bits)
    movq %rdx, %rsi
    shrq $32, %rsi              # x = upper 32 bits
    movl %edx, %edx             # y = lower 32 bits
    
    # Calculate position: pos = y * dimension + x
    imulq %r14, %rdx            # y * dimension
    addq %rsi, %rdx             # + x
    
    # Store byte at destination
    movb %al, (%r11,%rdx,1)
    
    incq %rcx
    jmp .Linner_loop
    
.Lnext_block:
    # Move to next block
    addq %r9, %r15
    jmp .Louter_loop

.Ldone:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret
# Mark stack as non-executable
.section .note.GNU-stack,"",@progbits
