/**
 * @file hilbert_external_buffer_asm.s
 * @brief Assembly wrapper for external buffer SIMD functions
 * 
 * This file provides assembly-level wrappers that expose the internal
 * SIMD mapping functions for use with external buffers.
 */

.intel_syntax noprefix
.text

/* Mark stack as non-executable for security */
.section .note.GNU-stack,"",@progbits

/* ============================================================================
 * External SIMD Function Wrappers
 * ============================================================================ */

/**
 * @brief Wrapper for standard SIMD mapping with external buffer
 * 
 * This function wraps the internal hilbert_map_buffer_simd to make it
 * accessible from the external buffer API.
 * 
 * C signature:
 * void hilbert_map_buffer_external_simd(
 *     const unsigned char *src_buffer,  // rdi
 *     unsigned char *dst_buffer,        // rsi
 *     const hilbert_packed_t *curve,    // rdx
 *     size_t src_len,                   // rcx
 *     uint32_t dimension                // r8d
 * );
 */
.globl g_hilbert_map_buffer_simd
.type g_hilbert_map_buffer_simd, @function
.align 16
g_hilbert_map_buffer_simd:
    /* Save callee-saved registers */
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    /* Parameters are already in correct registers for internal function */
    /* rdi = src_buffer */
    /* rsi = dst_buffer */
    /* rdx = curve */
    /* rcx = src_len */
    /* r8d = dimension */
    
    /* Call internal SIMD mapping function */
    call hilbert_map_buffer_simd
    
    /* Restore callee-saved registers */
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    
    ret
.size g_hilbert_map_buffer_simd, .-g_hilbert_map_buffer_simd

/**
 * @brief Wrapper for cache-blocking SIMD mapping with external buffer
 * 
 * This function wraps the internal cache-blocking SIMD function to make it
 * accessible from the external buffer API.
 * 
 * C signature:
 * void hilbert_map_buffer_external_cache_blocking_simd(
 *     const unsigned char *src_buffer,  // rdi
 *     unsigned char *dst_buffer,        // rsi
 *     const hilbert_packed_t *curve,    // rdx
 *     size_t src_len,                   // rcx
 *     uint32_t dimension                // r8d
 * );
 */
.globl g_hilbert_map_buffer_cache_blocking_simd
.type g_hilbert_map_buffer_cache_blocking_simd, @function
.align 16
g_hilbert_map_buffer_cache_blocking_simd:
    /* Save callee-saved registers */
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    /* Parameters are already in correct registers for internal function */
    /* rdi = src_buffer */
    /* rsi = dst_buffer */
    /* rdx = curve */
    /* rcx = src_len */
    /* r8d = dimension */
    
    /* Call internal cache-blocking SIMD mapping function */
    call hilbert_map_buffer_cache_blocking_simd
    
    /* Restore callee-saved registers */
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    
    ret
.size g_hilbert_map_buffer_cache_blocking_simd, .-g_hilbert_map_buffer_cache_blocking_simd

/**
 * @brief Fast memory zero function using AVX2
 * 
 * Optimized memory zeroing for external buffers using AVX2 instructions.
 * This is faster than memset for large buffers.
 * 
 * C signature:
 * void hilbert_zero_buffer_avx2(
 *     unsigned char *buffer,  // rdi
 *     size_t length           // rsi
 * );
 */
.globl hilbert_zero_buffer_avx2
.type hilbert_zero_buffer_avx2, @function
.align 16
hilbert_zero_buffer_avx2:
    /* Check if length is zero */
    test rsi, rsi
    jz .Lzero_done
    
    /* Create zero vector */
    vpxor ymm0, ymm0, ymm0
    
    /* Calculate number of 32-byte chunks */
    mov rcx, rsi
    shr rcx, 5          /* Divide by 32 */
    jz .Lzero_remainder
    
.Lzero_loop:
    /* Zero 32 bytes at a time */
    vmovdqa [rdi], ymm0
    add rdi, 32
    dec rcx
    jnz .Lzero_loop
    
.Lzero_remainder:
    /* Handle remaining bytes */
    and rsi, 31         /* Get remainder */
    jz .Lzero_done
    
    /* Zero remaining bytes using regular stores */
    xor eax, eax
.Lzero_byte_loop:
    mov [rdi], al
    inc rdi
    dec rsi
    jnz .Lzero_byte_loop
    
.Lzero_done:
    vzeroupper
    ret
.size hilbert_zero_buffer_avx2, .-hilbert_zero_buffer_avx2