/**
 * @file hilbert_external_buffer.h
 * @brief External buffer management API for ARM NEON
 * 
 * This header provides the same external buffer API as AVX2,
 * but optimized for ARM64 with NEON SIMD instructions.
 */

#ifndef HILBERT_EXTERNAL_BUFFER_NEON_H
#define HILBERT_EXTERNAL_BUFFER_NEON_H

#include <stdint.h>
#include <stddef.h>

/* Error codes (match the AVX2 implementation). */
#ifndef HILBERT_OK
#define HILBERT_OK 0
#define HILBERT_ERR_NO_AVX2 -1
#define HILBERT_ERR_INVALID_ORDER -2
#define HILBERT_ERR_INVALID_PARAM -3
#define HILBERT_ERR_OUT_OF_MEMORY -5
#define HILBERT_ERR_NO_CACHE -6
#define HILBERT_ERR_BUFFER_TOO_SMALL -8
#endif


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate required destination buffer size for Hilbert mapping
 * 
 * @param src_len Source buffer length in bytes
 * @return Required destination buffer size in bytes, or 0 on error
 */
size_t hilbert_calculate_buffer_size(size_t src_len);

/**
 * @brief Map buffer using Hilbert curve with external buffer
 * 
 * @param src_buffer Source data buffer (input)
 * @param src_len Source buffer length in bytes
 * @param dst_buffer Pre-allocated destination buffer (output)
 * @param dst_len Destination buffer length in bytes
 * @param actual_len Pointer to store actual bytes written (can be NULL)
 * 
 * @return 0 on success, negative error code on failure
 */
int hilbert_map_buffer_external(
    const unsigned char *src_buffer,
    size_t src_len,
    unsigned char *dst_buffer,
    size_t dst_len,
    size_t *actual_len
);

/**
 * @brief Map buffer using cache-blocking optimization with external buffer
 * 
 * @param src_buffer Source data buffer (input)
 * @param src_len Source buffer length in bytes
 * @param dst_buffer Pre-allocated destination buffer (output)
 * @param dst_len Destination buffer length in bytes
 * @param actual_len Pointer to store actual bytes written (can be NULL)
 * 
 * @return 0 on success, negative error code on failure
 */
int hilbert_map_buffer_external_cache_blocking(
    const unsigned char *src_buffer,
    size_t src_len,
    unsigned char *dst_buffer,
    size_t dst_len,
    size_t *actual_len
);

/**
 * @brief Get the dimension (width/height) for a given buffer size
 * 
 * @param src_len Source buffer length in bytes
 * @return Grid dimension (2^order), or 0 on error
 */
uint32_t hilbert_get_dimension_for_buffer(size_t src_len);

/**
 * @brief Get the order that will be used for a given buffer size
 * 
 * @param src_len Source buffer length in bytes
 * @return Hilbert curve order, or 0 on error
 */
uint32_t hilbert_get_order_for_buffer(size_t src_len);

/**
 * @brief Fast buffer zeroing using NEON (C wrapper)
 * 
 * @param buffer Buffer to zero
 * @param length Buffer length in bytes
 */
void hilbert_zero_buffer_neon(unsigned char *buffer, size_t length);

/**
 * @brief Fast buffer zeroing using NEON (assembly implementation)
 * 
 * @param buffer Buffer to zero
 * @param length Buffer length in bytes
 */
void hilbert_zero_buffer_neon_asm(unsigned char *buffer, size_t length);

/**
 * @brief NEON-optimized Hilbert mapping (assembly implementation)
 * 
 * @param src_buffer Source buffer
 * @param dst_buffer Destination buffer
 * @param curve Hilbert curve coordinates
 * @param src_len Source length
 * @param dimension Grid dimension
 */
void hilbert_map_buffer_neon_asm(
    const unsigned char *src_buffer,
    unsigned char *dst_buffer,
    const uint64_t *curve,
    size_t src_len,
    uint32_t dimension
);

/**
 * @brief Cache-blocking NEON mapping (assembly implementation)
 * 
 * @param src_buffer Source buffer
 * @param dst_buffer Destination buffer
 * @param curve Hilbert curve coordinates
 * @param src_len Source length
 * @param dimension Grid dimension
 */
void hilbert_map_buffer_neon_cache_blocking_asm(
    const unsigned char *src_buffer,
    unsigned char *dst_buffer,
    const uint64_t *curve,
    size_t src_len,
    uint32_t dimension
);

/**
 * @brief Prefetch-optimized NEON mapping (assembly implementation)
 * 
 * @param src_buffer Source buffer
 * @param dst_buffer Destination buffer
 * @param curve Hilbert curve coordinates
 * @param src_len Source length
 * @param dimension Grid dimension
 */
void hilbert_map_buffer_neon_prefetch_asm(
    const unsigned char *src_buffer,
    unsigned char *dst_buffer,
    const uint64_t *curve,
    size_t src_len,
    uint32_t dimension
);

#ifdef __cplusplus
}
#endif

#endif /* HILBERT_EXTERNAL_BUFFER_NEON_H */