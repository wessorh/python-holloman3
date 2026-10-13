/**
 * @file hilbert_external_buffer_neon.c
 * @brief ARM NEON implementation of external buffer management
 * 
 * This file implements external buffer functions for ARM64 with NEON SIMD.
 */

#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include "hilbert_external_buffer.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/* External dual cache structure from holloman_neon.c */
typedef struct {
    uint64_t *even_curve;
    size_t even_size;
    uint32_t even_order;
    uint64_t *odd_curve;
    size_t odd_size;
    uint32_t odd_order;
    int valid;
} hilbert_dual_cache_neon_t;

extern hilbert_dual_cache_neon_t g_neon_cache;

/**
 * Calculate minimum required order for buffer size
 */
static uint32_t calculate_order(size_t buffer_size) {
    if (buffer_size == 0) return 0;
    
    size_t order = 0;
    size_t n = buffer_size - 1;
    
    while (n > 0) {
        n >>= 1;
        order++;
    }
    
    order = (order + 1) >> 1;
    return (uint32_t)order;
}

/**
 * Get dimension for order
 */
static uint32_t get_dimension(uint32_t order) {
    return 1U << order;
}

/**
 * Calculate required destination buffer size
 */
size_t hilbert_calculate_buffer_size(size_t src_len) {
    if (src_len == 0) {
        return 0;
    }
    
    uint32_t order = calculate_order(src_len);
    if (order == 0 || order > 15) {
        return 0;
    }
    
    size_t dst_size = 1ULL << (order * 2);
    return dst_size;
}

/**
 * Get dimension for buffer size
 */
uint32_t hilbert_get_dimension_for_buffer(size_t src_len) {
    if (src_len == 0) {
        return 0;
    }
    
    uint32_t order = calculate_order(src_len);
    if (order == 0 || order > 15) {
        return 0;
    }
    
    return get_dimension(order);
}

/**
 * Get order for buffer size
 */
uint32_t hilbert_get_order_for_buffer(size_t src_len) {
    if (src_len == 0) {
        return 0;
    }
    
    return calculate_order(src_len);
}

/**
 * Fast buffer zeroing using NEON
 */
void hilbert_zero_buffer_neon(unsigned char *buffer, size_t length) {
#ifdef __ARM_NEON
    if (length == 0) return;
    
    // Create zero vector (128-bit)
    uint8x16_t zero = vdupq_n_u8(0);
    
    // Calculate number of 16-byte chunks
    size_t chunks = length >> 4;  // Divide by 16
    
    // Zero 16 bytes at a time
    for (size_t i = 0; i < chunks; i++) {
        vst1q_u8(buffer, zero);
        buffer += 16;
    }
    
    // Handle remaining bytes
    size_t remainder = length & 15;
    for (size_t i = 0; i < remainder; i++) {
        buffer[i] = 0;
    }
#else
    // Fallback to memset
    memset(buffer, 0, length);
#endif
}

/**
 * NEON-optimized Hilbert mapping
 */
static void map_buffer_neon_simd(
    const unsigned char *src_buffer,
    unsigned char *dst_buffer,
    const uint64_t *curve,
    size_t src_len,
    uint32_t dimension
) {
#ifdef __ARM_NEON
    // Process 4 points at a time using NEON
    size_t i = 0;
    size_t simd_len = src_len & ~3ULL;  // Round down to multiple of 4
    
    for (; i < simd_len; i += 4) {
        // Load 4 curve entries
        uint64_t packed0 = curve[i + 0];
        uint64_t packed1 = curve[i + 1];
        uint64_t packed2 = curve[i + 2];
        uint64_t packed3 = curve[i + 3];
        
        // Extract coordinates
        uint32_t x0 = (uint32_t)(packed0 >> 32);
        uint32_t y0 = (uint32_t)(packed0 & 0xFFFFFFFF);
        uint32_t x1 = (uint32_t)(packed1 >> 32);
        uint32_t y1 = (uint32_t)(packed1 & 0xFFFFFFFF);
        uint32_t x2 = (uint32_t)(packed2 >> 32);
        uint32_t y2 = (uint32_t)(packed2 & 0xFFFFFFFF);
        uint32_t x3 = (uint32_t)(packed3 >> 32);
        uint32_t y3 = (uint32_t)(packed3 & 0xFFFFFFFF);
        
        // Calculate positions
        size_t pos0 = (size_t)y0 * dimension + x0;
        size_t pos1 = (size_t)y1 * dimension + x1;
        size_t pos2 = (size_t)y2 * dimension + x2;
        size_t pos3 = (size_t)y3 * dimension + x3;
        
        // Store bytes
        dst_buffer[pos0] = src_buffer[i + 0];
        dst_buffer[pos1] = src_buffer[i + 1];
        dst_buffer[pos2] = src_buffer[i + 2];
        dst_buffer[pos3] = src_buffer[i + 3];
    }
    
    // Handle remaining bytes
    for (; i < src_len; i++) {
        uint64_t packed = curve[i];
        uint32_t x = (uint32_t)(packed >> 32);
        uint32_t y = (uint32_t)(packed & 0xFFFFFFFF);
        size_t pos = (size_t)y * dimension + x;
        dst_buffer[pos] = src_buffer[i];
    }
#else
    // Scalar fallback
    for (size_t i = 0; i < src_len; i++) {
        uint64_t packed = curve[i];
        uint32_t x = (uint32_t)(packed >> 32);
        uint32_t y = (uint32_t)(packed & 0xFFFFFFFF);
        size_t pos = (size_t)y * dimension + x;
        dst_buffer[pos] = src_buffer[i];
    }
#endif
}

/**
 * NEON-optimized cache-blocking Hilbert mapping
 */
static void map_buffer_neon_cache_blocking(
    const unsigned char *src_buffer,
    unsigned char *dst_buffer,
    const uint64_t *curve,
    size_t src_len,
    uint32_t dimension
) {
#ifdef __ARM_NEON
    // Cache blocking parameters
    const size_t BLOCK_SIZE = 4096;  // 4KB blocks for L1 cache
    
    size_t num_blocks = (src_len + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    for (size_t block = 0; block < num_blocks; block++) {
        size_t start = block * BLOCK_SIZE;
        size_t end = start + BLOCK_SIZE;
        if (end > src_len) end = src_len;
        
        // Process this block with NEON
        for (size_t i = start; i < end; i += 4) {
            if (i + 4 <= end) {
                // Process 4 points with NEON
                uint64_t packed0 = curve[i + 0];
                uint64_t packed1 = curve[i + 1];
                uint64_t packed2 = curve[i + 2];
                uint64_t packed3 = curve[i + 3];
                
                uint32_t x0 = (uint32_t)(packed0 >> 32);
                uint32_t y0 = (uint32_t)(packed0 & 0xFFFFFFFF);
                uint32_t x1 = (uint32_t)(packed1 >> 32);
                uint32_t y1 = (uint32_t)(packed1 & 0xFFFFFFFF);
                uint32_t x2 = (uint32_t)(packed2 >> 32);
                uint32_t y2 = (uint32_t)(packed2 & 0xFFFFFFFF);
                uint32_t x3 = (uint32_t)(packed3 >> 32);
                uint32_t y3 = (uint32_t)(packed3 & 0xFFFFFFFF);
                
                size_t pos0 = (size_t)y0 * dimension + x0;
                size_t pos1 = (size_t)y1 * dimension + x1;
                size_t pos2 = (size_t)y2 * dimension + x2;
                size_t pos3 = (size_t)y3 * dimension + x3;
                
                dst_buffer[pos0] = src_buffer[i + 0];
                dst_buffer[pos1] = src_buffer[i + 1];
                dst_buffer[pos2] = src_buffer[i + 2];
                dst_buffer[pos3] = src_buffer[i + 3];
            } else {
                // Handle remaining bytes in block
                for (size_t j = i; j < end; j++) {
                    uint64_t packed = curve[j];
                    uint32_t x = (uint32_t)(packed >> 32);
                    uint32_t y = (uint32_t)(packed & 0xFFFFFFFF);
                    size_t pos = (size_t)y * dimension + x;
                    dst_buffer[pos] = src_buffer[j];
                }
                break;
            }
        }
    }
#else
    // Fallback to standard mapping
    map_buffer_neon_simd(src_buffer, dst_buffer, curve, src_len, dimension);
#endif
}

/**
 * Map buffer with external buffer (standard version)
 */
int hilbert_map_buffer_external(
    const unsigned char *src_buffer,
    size_t src_len,
    unsigned char *dst_buffer,
    size_t dst_len,
    size_t *actual_len
) {
    // Validate parameters
    if (src_buffer == NULL || dst_buffer == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    if (src_len == 0) {
        if (actual_len) *actual_len = 0;
        return HILBERT_OK;
    }
    
    // Calculate minimum required order
    uint32_t required_order = calculate_order(src_len);
    if (required_order == 0 || required_order > 15) {
        return HILBERT_ERR_INVALID_ORDER;
    }
    
    // Check if cache is initialized
    if (!g_neon_cache.valid) {
        return HILBERT_ERR_NO_CACHE;
    }
    
    // Select curve based on parity
    uint64_t *curve;
    uint32_t max_order;
    
    if (required_order % 2 == 0) {
        // Even order
        curve = g_neon_cache.even_curve;
        max_order = g_neon_cache.even_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    } else {
        // Odd order
        curve = g_neon_cache.odd_curve;
        max_order = g_neon_cache.odd_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    }
    
    // Calculate required destination size
    size_t required_dst_size = 1ULL << (required_order * 2);
    
    // Check if destination buffer is large enough
    if (dst_len < required_dst_size) {
        return HILBERT_ERR_BUFFER_TOO_SMALL;
    }
    
    // Zero destination buffer using NEON
    hilbert_zero_buffer_neon(dst_buffer, required_dst_size);
    
    // Get dimension
    uint32_t dimension = get_dimension(required_order);
    
    // Map buffer using NEON
    map_buffer_neon_simd(src_buffer, dst_buffer, curve, src_len, dimension);
    
    // Set actual length if requested
    if (actual_len) {
        *actual_len = required_dst_size;
    }
    
    return HILBERT_OK;
}

/**
 * Map buffer with external buffer (cache-blocking version)
 */
int hilbert_map_buffer_external_cache_blocking(
    const unsigned char *src_buffer,
    size_t src_len,
    unsigned char *dst_buffer,
    size_t dst_len,
    size_t *actual_len
) {
    // Validate parameters
    if (src_buffer == NULL || dst_buffer == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    if (src_len == 0) {
        if (actual_len) *actual_len = 0;
        return HILBERT_OK;
    }
    
    // Calculate minimum required order
    uint32_t required_order = calculate_order(src_len);
    if (required_order == 0 || required_order > 15) {
        return HILBERT_ERR_INVALID_ORDER;
    }
    
    // Check if cache is initialized
    if (!g_neon_cache.valid) {
        return HILBERT_ERR_NO_CACHE;
    }
    
    // Select curve based on parity
    uint64_t *curve;
    uint32_t max_order;
    
    if (required_order % 2 == 0) {
        // Even order
        curve = g_neon_cache.even_curve;
        max_order = g_neon_cache.even_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    } else {
        // Odd order
        curve = g_neon_cache.odd_curve;
        max_order = g_neon_cache.odd_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    }
    
    // Calculate required destination size
    size_t required_dst_size = 1ULL << (required_order * 2);
    
    // Check if destination buffer is large enough
    if (dst_len < required_dst_size) {
        return HILBERT_ERR_BUFFER_TOO_SMALL;
    }
    
    // Zero destination buffer using NEON
    hilbert_zero_buffer_neon(dst_buffer, required_dst_size);
    
    // Get dimension
    uint32_t dimension = get_dimension(required_order);
    
    // Map buffer using cache-blocking NEON
    map_buffer_neon_cache_blocking(src_buffer, dst_buffer, curve, src_len, dimension);
    
    // Set actual length if requested
    if (actual_len) {
        *actual_len = required_dst_size;
    }
    
    return HILBERT_OK;
}