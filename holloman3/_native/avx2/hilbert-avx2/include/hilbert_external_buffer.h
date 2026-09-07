/**
 * @file hilbert_external_buffer.h
 * @brief External buffer management API for Hilbert curve mapping
 * 
 * This header provides functions that accept pre-allocated buffers,
 * giving users full control over memory allocation and management.
 * 
 * Key differences from standard API:
 * - User allocates destination buffer
 * - No internal memory allocation
 * - User controls buffer lifetime
 * - Better for memory-constrained environments
 * - Allows buffer reuse across multiple operations
 */

#ifndef HILBERT_EXTERNAL_BUFFER_H
#define HILBERT_EXTERNAL_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate required destination buffer size for Hilbert mapping
 * 
 * Use this function to determine how much memory to allocate before
 * calling hilbert_map_buffer_external().
 * 
 * @param src_len Source buffer length in bytes
 * @return Required destination buffer size in bytes, or 0 on error
 * 
 * @note The returned size is always a perfect square (dimension^2)
 * @note For src_len=1000, returns 1024 (32x32 grid, order 5)
 * 
 * Example:
 * @code
 * size_t src_len = 1000;
 * size_t dst_len = hilbert_calculate_buffer_size(src_len);
 * if (dst_len == 0) {
 *     // Error: buffer too large
 * }
 * unsigned char *dst = malloc(dst_len);
 * @endcode
 */
size_t hilbert_calculate_buffer_size(size_t src_len);

/**
 * @brief Map buffer using Hilbert curve with external buffer
 * 
 * This function maps the source buffer to a 2D grid using the Hilbert curve,
 * writing results to a pre-allocated destination buffer. The user is responsible
 * for allocating and freeing the destination buffer.
 * 
 * @param src_buffer Source data buffer (input)
 * @param src_len Source buffer length in bytes
 * @param dst_buffer Pre-allocated destination buffer (output)
 * @param dst_len Destination buffer length in bytes
 * @param actual_len Pointer to store actual bytes written (can be NULL)
 * 
 * @return 0 on success, negative error code on failure:
 *         -1: AVX2 not available
 *         -2: Invalid order (buffer too large)
 *         -3: Invalid parameters (NULL pointers)
 *         -6: No curve cached (call hilbert_init first)
 *         -8: Destination buffer too small
 * 
 * @note Requires hilbert_init() to be called first
 * @note dst_buffer must be at least dst_len bytes (use hilbert_calculate_buffer_size)
 * @note dst_buffer will be zero-filled before mapping
 * @note actual_len will contain the actual size used (may be less than dst_len)
 * 
 * Example:
 * @code
 * // Initialize Hilbert curve
 * hilbert_init(13);
 * 
 * // Prepare buffers
 * unsigned char src[1000] = {...};
 * size_t dst_len = hilbert_calculate_buffer_size(sizeof(src));
 * unsigned char *dst = malloc(dst_len);
 * 
 * // Map with external buffer
 * size_t actual_len;
 * int ret = hilbert_map_buffer_external(src, sizeof(src), dst, dst_len, &actual_len);
 * if (ret == 0) {
 *     printf("Mapped %zu bytes to %zu bytes\n", sizeof(src), actual_len);
 *     // Use dst buffer...
 * }
 * 
 * // User controls cleanup
 * free(dst);
 * hilbert_cleanup();
 * @endcode
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
 * This is the cache-optimized version of hilbert_map_buffer_external().
 * It uses cache blocking to improve performance on large buffers.
 * 
 * @param src_buffer Source data buffer (input)
 * @param src_len Source buffer length in bytes
 * @param dst_buffer Pre-allocated destination buffer (output)
 * @param dst_len Destination buffer length in bytes
 * @param actual_len Pointer to store actual bytes written (can be NULL)
 * 
 * @return 0 on success, negative error code on failure (same as hilbert_map_buffer_external)
 * 
 * @note Use this for buffers larger than 1MB for better cache performance
 * @note Requires AVX2 support
 * @note All other requirements same as hilbert_map_buffer_external()
 * 
 * Example:
 * @code
 * // For large files, use cache-blocking version
 * size_t file_size = 10 * 1024 * 1024;  // 10 MB
 * unsigned char *file_data = read_file("large.bin", &file_size);
 * 
 * size_t dst_len = hilbert_calculate_buffer_size(file_size);
 * unsigned char *dst = malloc(dst_len);
 * 
 * size_t actual_len;
 * int ret = hilbert_map_buffer_external_cache_blocking(
 *     file_data, file_size, dst, dst_len, &actual_len
 * );
 * 
 * if (ret == 0) {
 *     // Process mapped data...
 * }
 * 
 * free(dst);
 * free(file_data);
 * @endcode
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
 * Helper function to determine the grid dimension that will be used
 * for mapping a buffer of the given size.
 * 
 * @param src_len Source buffer length in bytes
 * @return Grid dimension (2^order), or 0 on error
 * 
 * @note The returned dimension squared equals the buffer size from hilbert_calculate_buffer_size()
 * 
 * Example:
 * @code
 * size_t src_len = 1000;
 * uint32_t dimension = hilbert_get_dimension_for_buffer(src_len);
 * printf("Will map to %u x %u grid\n", dimension, dimension);
 * // Output: "Will map to 32 x 32 grid"
 * @endcode
 */
uint32_t hilbert_get_dimension_for_buffer(size_t src_len);

/**
 * @brief Get the order that will be used for a given buffer size
 * 
 * Helper function to determine the Hilbert curve order that will be used
 * for mapping a buffer of the given size.
 * 
 * @param src_len Source buffer length in bytes
 * @return Hilbert curve order, or 0 on error
 * 
 * Example:
 * @code
 * size_t src_len = 1000;
 * uint32_t order = hilbert_get_order_for_buffer(src_len);
 * printf("Will use order %u\n", order);
 * // Output: "Will use order 5"
 * @endcode
 */
uint32_t hilbert_get_order_for_buffer(size_t src_len);

/**
 * @brief Fast memory zeroing using AVX2 SIMD instructions
 * 
 * Optimized memory zeroing function that uses AVX2 to zero memory
 * significantly faster than standard memset(). Provides 2-2.5× speedup.
 * 
 * Performance:
 * - Throughput: 15-20 GB/s (vs 8 GB/s for memset)
 * - Speedup: 2-2.5×
 * - Best for buffers > 1 KB
 * 
 * @param buffer Pointer to buffer to zero
 * @param length Number of bytes to zero
 * 
 * @note Requires AVX2 support (Intel Haswell 2013+, AMD Zen 2017+)
 * @note Automatically handles alignment and remainder bytes
 * @note Safe to use with any buffer size (including small buffers)
 * 
 * Example:
 * @code
 * unsigned char *buffer = malloc(4096);
 * hilbert_zero_buffer_avx2(buffer, 4096);  // 2× faster than memset
 * @endcode
 */
void hilbert_zero_buffer_avx2(unsigned char *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* HILBERT_EXTERNAL_BUFFER_H */