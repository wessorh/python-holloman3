/**
 * @file hilbert_utils_avx2_win64.s
 * @brief Microsoft x64 ABI port of the fast AVX2 memory zeroing helper.
 *
 * C signature:
 * void hilbert_zero_buffer_avx2(unsigned char *buffer, size_t length)
 */

.intel_syntax noprefix

.text

.globl hilbert_zero_buffer_avx2
.def hilbert_zero_buffer_avx2; .scl 2; .type 32; .endef
.align 16
hilbert_zero_buffer_avx2:
    # Microsoft x64 ABI: RCX = buffer, RDX = length
    mov rdi, rcx              # buffer -> rdi
    mov rsi, rdx              # length -> rsi

    # rdi/rsi are non-volatile on Windows; save them.
    push rdi
    push rsi

    # Check if length is zero
    test rsi, rsi
    jz .Lzero_done

    # Create zero vector (256-bit)
    vpxor ymm0, ymm0, ymm0

    # Calculate number of 128-byte chunks (4 x 32 bytes)
    mov rcx, rsi
    shr rcx, 7                # Divide by 128
    jz .Lzero_small

.Lzero_loop_128:
    # Zero 128 bytes at a time (4 x 32 bytes) - loop unrolling
    vmovdqu [rdi], ymm0
    vmovdqu [rdi+32], ymm0
    vmovdqu [rdi+64], ymm0
    vmovdqu [rdi+96], ymm0
    add rdi, 128
    dec rcx
    jnz .Lzero_loop_128

.Lzero_small:
    # Handle remaining 32-byte chunks (0-96 bytes)
    mov rcx, rsi
    and rcx, 127              # Get remainder
    shr rcx, 5                # Divide by 32
    jz .Lzero_remainder

.Lzero_loop_32:
    vmovdqu [rdi], ymm0
    add rdi, 32
    dec rcx
    jnz .Lzero_loop_32

.Lzero_remainder:
    # Handle remaining bytes (0-31)
    and rsi, 31               # Get remainder
    jz .Lzero_done

    # Zero remaining bytes using scalar stores
    xor eax, eax
.Lzero_byte_loop:
    mov [rdi], al
    inc rdi
    dec rsi
    jnz .Lzero_byte_loop

.Lzero_done:
    # Clean up AVX state
    vzeroupper

    pop rsi
    pop rdi
    ret
