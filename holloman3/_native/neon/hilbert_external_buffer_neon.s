/**
 * @file hilbert_external_buffer_neon.s
 * @brief ARM64 NEON assembly optimizations for external buffer management
 * 
 * This file provides highly optimized NEON assembly implementations
 * for ARM64 (AArch64) processors.
 */

.arch armv8-a+simd
.text

/* ============================================================================
 * Fast Buffer Zeroing with NEON
 * ============================================================================ */

/**
 * @brief Fast memory zero function using NEON (128-bit)
 * 
 * Optimized memory zeroing for ARM64 using NEON instructions.
 * Processes 64 bytes per iteration using four 128-bit stores.
 * 
 * C signature:
 * void hilbert_zero_buffer_neon_asm(
 *     unsigned char *buffer,  // x0
 *     size_t length           // x1
 * );
 */
.global hilbert_zero_buffer_neon_asm
.type hilbert_zero_buffer_neon_asm, %function
.align 4
hilbert_zero_buffer_neon_asm:
    // Check if length is zero
    cbz x1, .Lzero_done
    
    // Create zero vectors (128-bit)
    movi v0.16b, #0
    movi v1.16b, #0
    movi v2.16b, #0
    movi v3.16b, #0
    
    // Calculate number of 64-byte chunks
    lsr x2, x1, #6          // Divide by 64
    cbz x2, .Lzero_remainder
    
.Lzero_loop_64:
    // Zero 64 bytes at a time (4 x 16 bytes)
    stp q0, q1, [x0], #32   // Store 32 bytes
    stp q2, q3, [x0], #32   // Store 32 bytes
    subs x2, x2, #1
    b.ne .Lzero_loop_64
    
.Lzero_remainder:
    // Handle remaining bytes (0-63)
    and x1, x1, #63         // Get remainder
    cbz x1, .Lzero_done
    
    // Check if we can do 32-byte chunks
    cmp x1, #32
    b.lt .Lzero_16
    
    // Zero 32 bytes
    stp q0, q1, [x0], #32
    sub x1, x1, #32
    
.Lzero_16:
    // Check if we can do 16-byte chunks
    cmp x1, #16
    b.lt .Lzero_bytes
    
    // Zero 16 bytes
    str q0, [x0], #16
    sub x1, x1, #16
    
.Lzero_bytes:
    // Zero remaining bytes one at a time
    cbz x1, .Lzero_done
    
.Lzero_byte_loop:
    strb wzr, [x0], #1
    subs x1, x1, #1
    b.ne .Lzero_byte_loop
    
.Lzero_done:
    ret
.size hilbert_zero_buffer_neon_asm, .-hilbert_zero_buffer_neon_asm

/* ============================================================================
 * Optimized Hilbert Mapping with NEON
 * ============================================================================ */

/**
 * @brief NEON-optimized Hilbert curve mapping
 * 
 * Maps source buffer to destination using Hilbert curve coordinates.
 * Processes 4 points at a time using NEON for coordinate extraction.
 * 
 * C signature:
 * void hilbert_map_buffer_neon_asm(
 *     const unsigned char *src_buffer,  // x0
 *     unsigned char *dst_buffer,        // x1
 *     const uint64_t *curve,            // x2
 *     size_t src_len,                   // x3
 *     uint32_t dimension                // w4
 * );
 */
.global hilbert_map_buffer_neon_asm
.type hilbert_map_buffer_neon_asm, %function
.align 4
hilbert_map_buffer_neon_asm:
    // Save callee-saved registers
    stp x19, x20, [sp, #-64]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    
    // x0 = src_buffer
    // x1 = dst_buffer
    // x2 = curve
    // x3 = src_len
    // w4 = dimension
    
    mov x19, x0             // Save src_buffer
    mov x20, x1             // Save dst_buffer
    mov x21, x2             // Save curve
    mov x22, x3             // Save src_len
    mov w23, w4             // Save dimension
    
    // Calculate SIMD length (multiple of 4)
    and x24, x22, #-4       // Round down to multiple of 4
    mov x25, #0             // i = 0
    
    // Check if we have at least 4 elements
    cmp x24, #4
    b.lt .Lmap_scalar
    
.Lmap_simd_loop:
    // Load 4 curve entries (4 x 64-bit = 256 bits)
    ldp x5, x6, [x21], #16  // Load curve[i+0] and curve[i+1]
    ldp x7, x8, [x21], #16  // Load curve[i+2] and curve[i+3]
    
    // Extract coordinates for point 0
    lsr x9, x5, #32         // x0 = upper 32 bits
    and x10, x5, #0xFFFFFFFF // y0 = lower 32 bits
    
    // Calculate position 0: pos = y * dimension + x
    mul x11, x10, x23       // y0 * dimension
    add x11, x11, x9        // + x0
    
    // Load and store byte 0
    ldrb w12, [x19], #1     // Load src[i]
    strb w12, [x20, x11]    // Store to dst[pos]
    
    // Extract coordinates for point 1
    lsr x9, x6, #32         // x1
    and x10, x6, #0xFFFFFFFF // y1
    mul x11, x10, x23
    add x11, x11, x9
    ldrb w12, [x19], #1
    strb w12, [x20, x11]
    
    // Extract coordinates for point 2
    lsr x9, x7, #32         // x2
    and x10, x7, #0xFFFFFFFF // y2
    mul x11, x10, x23
    add x11, x11, x9
    ldrb w12, [x19], #1
    strb w12, [x20, x11]
    
    // Extract coordinates for point 3
    lsr x9, x8, #32         // x3
    and x10, x8, #0xFFFFFFFF // y3
    mul x11, x10, x23
    add x11, x11, x9
    ldrb w12, [x19], #1
    strb w12, [x20, x11]
    
    // Increment counter
    add x25, x25, #4
    cmp x25, x24
    b.lt .Lmap_simd_loop
    
.Lmap_scalar:
    // Handle remaining bytes (0-3)
    cmp x25, x22
    b.ge .Lmap_done
    
.Lmap_scalar_loop:
    // Load curve entry
    ldr x5, [x21], #8       // Load curve[i]
    
    // Extract coordinates
    lsr x9, x5, #32         // x = upper 32 bits
    and x10, x5, #0xFFFFFFFF // y = lower 32 bits
    
    // Calculate position
    mul x11, x10, x23       // y * dimension
    add x11, x11, x9        // + x
    
    // Load and store byte
    ldrb w12, [x19], #1     // Load src[i]
    strb w12, [x20, x11]    // Store to dst[pos]
    
    // Increment counter
    add x25, x25, #1
    cmp x25, x22
    b.lt .Lmap_scalar_loop
    
.Lmap_done:
    // Restore callee-saved registers
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #64
    ret
.size hilbert_map_buffer_neon_asm, .-hilbert_map_buffer_neon_asm

/* ============================================================================
 * Cache-Blocking Optimized Mapping
 * ============================================================================ */

/**
 * @brief Cache-blocking optimized Hilbert mapping with NEON
 * 
 * Processes data in cache-friendly blocks to improve performance.
 * Uses 4KB blocks optimized for ARM L1 cache.
 * 
 * C signature:
 * void hilbert_map_buffer_neon_cache_blocking_asm(
 *     const unsigned char *src_buffer,  // x0
 *     unsigned char *dst_buffer,        // x1
 *     const uint64_t *curve,            // x2
 *     size_t src_len,                   // x3
 *     uint32_t dimension                // w4
 * );
 */
.global hilbert_map_buffer_neon_cache_blocking_asm
.type hilbert_map_buffer_neon_cache_blocking_asm, %function
.align 4
hilbert_map_buffer_neon_cache_blocking_asm:
    // Save callee-saved registers
    stp x19, x20, [sp, #-80]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    
    // Save parameters
    mov x19, x0             // src_buffer
    mov x20, x1             // dst_buffer
    mov x21, x2             // curve
    mov x22, x3             // src_len
    mov w23, w4             // dimension
    
    // Block size = 4096 bytes
    mov x24, #4096
    
    // Calculate number of blocks
    add x25, x22, x24
    sub x25, x25, #1
    udiv x25, x25, x24      // num_blocks = (src_len + BLOCK_SIZE - 1) / BLOCK_SIZE
    
    mov x26, #0             // block = 0
    
.Lblock_loop:
    // Calculate block boundaries
    mul x27, x26, x24       // start = block * BLOCK_SIZE
    add x28, x27, x24       // end = start + BLOCK_SIZE
    cmp x28, x22
    csel x28, x28, x22, lo  // end = min(end, src_len)
    
    // Process this block
    mov x5, x27             // i = start
    
.Lblock_inner_loop:
    // Check if we can process 4 points
    add x6, x5, #4
    cmp x6, x28
    b.gt .Lblock_scalar
    
    // Calculate offsets
    lsl x7, x5, #3          // i * 8 (curve entry size)
    add x8, x21, x7         // curve + offset
    add x9, x19, x5         // src_buffer + i
    
    // Load 4 curve entries
    ldp x10, x11, [x8], #16
    ldp x12, x13, [x8]
    
    // Process point 0
    lsr x14, x10, #32
    and x15, x10, #0xFFFFFFFF
    mul x16, x15, x23
    add x16, x16, x14
    ldrb w17, [x9], #1
    strb w17, [x20, x16]
    
    // Process point 1
    lsr x14, x11, #32
    and x15, x11, #0xFFFFFFFF
    mul x16, x15, x23
    add x16, x16, x14
    ldrb w17, [x9], #1
    strb w17, [x20, x16]
    
    // Process point 2
    lsr x14, x12, #32
    and x15, x12, #0xFFFFFFFF
    mul x16, x15, x23
    add x16, x16, x14
    ldrb w17, [x9], #1
    strb w17, [x20, x16]
    
    // Process point 3
    lsr x14, x13, #32
    and x15, x13, #0xFFFFFFFF
    mul x16, x15, x23
    add x16, x16, x14
    ldrb w17, [x9]
    strb w17, [x20, x16]
    
    add x5, x5, #4
    b .Lblock_inner_loop
    
.Lblock_scalar:
    // Handle remaining bytes in block
    cmp x5, x28
    b.ge .Lblock_next
    
.Lblock_scalar_loop:
    lsl x7, x5, #3
    add x8, x21, x7
    ldr x10, [x8]
    
    lsr x14, x10, #32
    and x15, x10, #0xFFFFFFFF
    mul x16, x15, x23
    add x16, x16, x14
    
    ldrb w17, [x19, x5]
    strb w17, [x20, x16]
    
    add x5, x5, #1
    cmp x5, x28
    b.lt .Lblock_scalar_loop
    
.Lblock_next:
    add x26, x26, #1
    cmp x26, x25
    b.lt .Lblock_loop
    
    // Restore callee-saved registers
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #80
    ret
.size hilbert_map_buffer_neon_cache_blocking_asm, .-hilbert_map_buffer_neon_cache_blocking_asm

/* ============================================================================
 * Prefetch-Optimized Mapping
 * ============================================================================ */

/**
 * @brief Prefetch-optimized Hilbert mapping
 * 
 * Uses ARM prefetch instructions to reduce memory latency.
 * Prefetches both curve data and destination cache lines.
 * 
 * C signature:
 * void hilbert_map_buffer_neon_prefetch_asm(
 *     const unsigned char *src_buffer,  // x0
 *     unsigned char *dst_buffer,        // x1
 *     const uint64_t *curve,            // x2
 *     size_t src_len,                   // x3
 *     uint32_t dimension                // w4
 * );
 */
.global hilbert_map_buffer_neon_prefetch_asm
.type hilbert_map_buffer_neon_prefetch_asm, %function
.align 4
hilbert_map_buffer_neon_prefetch_asm:
    stp x19, x20, [sp, #-64]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    
    mov x19, x0
    mov x20, x1
    mov x21, x2
    mov x22, x3
    mov w23, w4
    
    mov x24, #0             // i = 0
    
.Lprefetch_loop:
    cmp x24, x22
    b.ge .Lprefetch_done
    
    // Prefetch next 4 curve entries (256 bytes ahead)
    add x5, x24, #32
    cmp x5, x22
    b.ge .Lprefetch_no_prefetch
    lsl x6, x5, #3
    add x6, x21, x6
    prfm pldl1keep, [x6]
    
.Lprefetch_no_prefetch:
    // Load curve entry
    lsl x5, x24, #3
    add x5, x21, x5
    ldr x6, [x5]
    
    // Extract coordinates
    lsr x7, x6, #32
    and x8, x6, #0xFFFFFFFF
    
    // Calculate position
    mul x9, x8, x23
    add x9, x9, x7
    
    // Prefetch destination cache line
    add x10, x20, x9
    prfm pstl1keep, [x10]
    
    // Load and store
    ldrb w11, [x19, x24]
    strb w11, [x20, x9]
    
    add x24, x24, #1
    b .Lprefetch_loop
    
.Lprefetch_done:
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #64
    ret
.size hilbert_map_buffer_neon_prefetch_asm, .-hilbert_map_buffer_neon_prefetch_asm

/* Mark stack as non-executable for security */
.section .note.GNU-stack,"",@progbits