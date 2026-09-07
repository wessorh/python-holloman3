/**
 * @file hilbert_external_buffer.c
 * @brief Implementation of external buffer management for Hilbert mapping
 * 
 * This file implements functions that accept pre-allocated buffers,
 * giving users full control over memory allocation.
 */

#include "hilbert_external_buffer.h"
#include "hilbert.h"
#include <string.h>
#include <math.h>

/* External declarations from hilbert.c and hilbert_mapping.c */
extern int hilbert_has_simd(void);
extern uint32_t hilbert_calculate_order(size_t buffer_size);
extern uint32_t hilbert_get_dimension(uint32_t order);

/* External fast zero function from hilbert_utils_avx2.s */
extern void hilbert_zero_buffer_avx2(unsigned char *buffer, size_t length);

/* External dual cache structure */
typedef struct {
    hilbert_packed_t *even_curve;
    size_t even_size;
    uint32_t even_order;
    hilbert_packed_t *odd_curve;
    size_t odd_size;
    uint32_t odd_order;
    int valid;
} hilbert_dual_cache_t;

extern hilbert_dual_cache_t g_dual_cache;

/* External SIMD mapping function pointers */
typedef void (*hilbert_map_buffer_func)(
    const unsigned char *src_buffer,
    unsigned char *dst_buffer,
    const hilbert_packed_t *curve,
    size_t src_len,
    uint32_t dimension
);

extern hilbert_map_buffer_func g_hilbert_map_buffer_simd;
extern hilbert_map_buffer_func g_hilbert_map_buffer_cache_blocking;

/**
 * Calculate required destination buffer size
 */
size_t hilbert_calculate_buffer_size(size_t src_len) {
    if (src_len == 0) {
        return 0;
    }
    
    // Calculate minimum required order
    uint32_t order = hilbert_calculate_order(src_len);
    if (order == 0 || order > 15) {
        return 0;  // Invalid order
    }
    
    // Calculate destination size (4^order)
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
    
    uint32_t order = hilbert_calculate_order(src_len);
    if (order == 0 || order > 15) {
        return 0;
    }
    
    return hilbert_get_dimension(order);
}

/**
 * Get order for buffer size
 */
uint32_t hilbert_get_order_for_buffer(size_t src_len) {
    if (src_len == 0) {
        return 0;
    }
    
    return hilbert_calculate_order(src_len);
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
    
    // Check SIMD availability
    if (!hilbert_has_simd()) {
        return HILBERT_ERR_NO_AVX2;
    }
    
    // Calculate minimum required order
    uint32_t required_order = hilbert_calculate_order(src_len);
    if (required_order == 0 || required_order > 15) {
        return HILBERT_ERR_INVALID_ORDER;
    }
    
    // Select curve based on parity
    hilbert_packed_t *curve;
    uint32_t max_order;
    
    if (!g_dual_cache.valid) {
        return HILBERT_ERR_NO_CACHE;
    }
    
    if (required_order % 2 == 0) {
        // Even order
        curve = g_dual_cache.even_curve;
        max_order = g_dual_cache.even_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    } else {
        // Odd order
        curve = g_dual_cache.odd_curve;
        max_order = g_dual_cache.odd_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    }
    
    // Calculate required destination size
    size_t required_dst_size = 1ULL << (required_order * 2);
    
    // Check if destination buffer is large enough
    if (dst_len < required_dst_size) {
        return -8;  // HILBERT_ERR_BUFFER_TOO_SMALL
    }
    
    // Zero destination buffer using fast AVX2 zeroing (2-2.5× faster than memset)
    hilbert_zero_buffer_avx2(dst_buffer, required_dst_size);
    
    // Get dimension
    uint32_t dimension = hilbert_get_dimension(required_order);
    
    // Map buffer using SIMD
    g_hilbert_map_buffer_simd(src_buffer, dst_buffer, curve, src_len, dimension);
    
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
    
    // Check SIMD availability
    if (!hilbert_has_simd()) {
        return HILBERT_ERR_NO_AVX2;
    }
    
    // Calculate minimum required order
    uint32_t required_order = hilbert_calculate_order(src_len);
    if (required_order == 0 || required_order > 15) {
        return HILBERT_ERR_INVALID_ORDER;
    }
    
    // Select curve based on parity
    hilbert_packed_t *curve;
    uint32_t max_order;
    
    if (!g_dual_cache.valid) {
        return HILBERT_ERR_NO_CACHE;
    }
    
    if (required_order % 2 == 0) {
        // Even order
        curve = g_dual_cache.even_curve;
        max_order = g_dual_cache.even_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    } else {
        // Odd order
        curve = g_dual_cache.odd_curve;
        max_order = g_dual_cache.odd_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    }
    
    // Calculate required destination size
    size_t required_dst_size = 1ULL << (required_order * 2);
    
    // Check if destination buffer is large enough
    if (dst_len < required_dst_size) {
        return -8;  // HILBERT_ERR_BUFFER_TOO_SMALL
    }
    
    // Zero destination buffer using fast AVX2 zeroing (2-2.5× faster than memset)
    hilbert_zero_buffer_avx2(dst_buffer, required_dst_size);
    
    // Get dimension
    uint32_t dimension = hilbert_get_dimension(required_order);
    
    // Map buffer using cache-blocking SIMD
    g_hilbert_map_buffer_cache_blocking(src_buffer, dst_buffer, curve, src_len, dimension);
    
    // Set actual length if requested
    if (actual_len) {
        *actual_len = required_dst_size;
    }
    
    return HILBERT_OK;
}