/**
 * @file hilbert.c
 * @brief Core Hilbert curve implementation with single curve caching
 */

#define _POSIX_C_SOURCE 200112L

#include "../include/hilbert.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Dual Curve Cache (Even/Odd Hierarchy)
 * ============================================================================ */

typedef struct {
    // Even curve (for even-parity orders: 4, 6, 8, 10, 12, 14)
    hilbert_packed_t *even_curve;
    size_t even_size;
    uint32_t even_order;
    
    // Odd curve (for odd-parity orders: 5, 7, 9, 11, 13, 15)
    hilbert_packed_t *odd_curve;
    size_t odd_size;
    uint32_t odd_order;
    
    int valid;                /* Both curves initialized */
} hilbert_dual_cache_t;

hilbert_dual_cache_t g_dual_cache = {NULL, 0, 0, NULL, 0, 0, 0};

/* ============================================================================
 * Architecture Detection and SIMD Functions
 * ============================================================================ */

// External architecture detection functions
extern void hilbert_init_arch(void);
extern int hilbert_has_simd(void);
extern const char* hilbert_get_arch(void);
extern const char* hilbert_get_simd_name(void);

// External function pointers (set by arch_detect.c)
typedef void (*hilbert_d2xy_batch_func)(const uint32_t *distances, 
                                        hilbert_packed_t *packed,
                                        size_t count, uint32_t order);
extern hilbert_d2xy_batch_func g_hilbert_d2xy_batch_simd;

// Legacy function for backward compatibility
int hilbert_is_avx2_available(void) {
    return hilbert_has_simd();
}

/* ============================================================================
 * Core Functions
 * ============================================================================ */

int hilbert_init(uint32_t order) {
    // Initialize architecture-specific functions
    hilbert_init_arch();
    
    // Check SIMD availability early
    if (!hilbert_has_simd()) {
        return HILBERT_ERR_NO_AVX2;  // Keep error code for compatibility
    }
    
    // For dual-curve system, initialize both even and odd curves
    // The 'order' parameter is now used to set maximum capacity
    // We always initialize order 14 (even) and order 15 (odd)
    
    // Determine which curves to initialize based on requested order
    uint32_t even_order = (order >= 14) ? 14 : ((order % 2 == 0) ? order : order - 1);
    uint32_t odd_order = (order >= 15) ? 15 : ((order % 2 == 1) ? order : order + 1);
    
    // Ensure minimum orders
    if (even_order < 2) even_order = 2;
    if (odd_order < 3) odd_order = 3;
    
    // Clear existing cache if present
    if (g_dual_cache.valid) {
        if (g_dual_cache.even_curve != NULL) {
            free(g_dual_cache.even_curve);
            g_dual_cache.even_curve = NULL;
        }
        if (g_dual_cache.odd_curve != NULL) {
            free(g_dual_cache.odd_curve);
            g_dual_cache.odd_curve = NULL;
        }
        g_dual_cache.valid = 0;
    }
    
    // Initialize EVEN curve
    size_t even_points = HILBERT_TOTAL_POINTS(even_order);
    hilbert_packed_t *even_curve = NULL;
    if (posix_memalign((void**)&even_curve, 32, even_points * sizeof(hilbert_packed_t)) != 0) {
        return HILBERT_ERR_OUT_OF_MEMORY;
    }
    
    uint32_t *even_distances = (uint32_t*)malloc(even_points * sizeof(uint32_t));
    if (even_distances == NULL) {
        free(even_curve);
        return HILBERT_ERR_OUT_OF_MEMORY;
    }
    
    for (size_t i = 0; i < even_points; i++) {
        even_distances[i] = (uint32_t)i;
    }
    
    if (g_hilbert_d2xy_batch_simd != NULL) {
        g_hilbert_d2xy_batch_simd(even_distances, even_curve, even_points, even_order);
    } else {
        free(even_distances);
        free(even_curve);
        return HILBERT_ERR_NO_AVX2;
    }
    free(even_distances);
    
    // Initialize ODD curve
    size_t odd_points = HILBERT_TOTAL_POINTS(odd_order);
    hilbert_packed_t *odd_curve = NULL;
    if (posix_memalign((void**)&odd_curve, 32, odd_points * sizeof(hilbert_packed_t)) != 0) {
        free(even_curve);
        return HILBERT_ERR_OUT_OF_MEMORY;
    }
    
    uint32_t *odd_distances = (uint32_t*)malloc(odd_points * sizeof(uint32_t));
    if (odd_distances == NULL) {
        free(even_curve);
        free(odd_curve);
        return HILBERT_ERR_OUT_OF_MEMORY;
    }
    
    for (size_t i = 0; i < odd_points; i++) {
        odd_distances[i] = (uint32_t)i;
    }
    
    if (g_hilbert_d2xy_batch_simd != NULL) {
        g_hilbert_d2xy_batch_simd(odd_distances, odd_curve, odd_points, odd_order);
    } else {
        free(odd_distances);
        free(even_curve);
        free(odd_curve);
        return HILBERT_ERR_NO_AVX2;
    }
    free(odd_distances);
    
    // Update dual cache
    g_dual_cache.even_curve = even_curve;
    g_dual_cache.even_size = even_points;
    g_dual_cache.even_order = even_order;
    g_dual_cache.odd_curve = odd_curve;
    g_dual_cache.odd_size = odd_points;
    g_dual_cache.odd_order = odd_order;
    g_dual_cache.valid = 1;
    
    return HILBERT_OK;
}

int hilbert_d2xy_batch(const uint32_t *distances, hilbert_packed_t *packed,
                       size_t count, uint32_t order) {
    // Validate parameters
    if (distances == NULL || packed == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    // Check SIMD availability
    if (!hilbert_has_simd()) {
        return HILBERT_ERR_NO_AVX2;
    }
    
    // Use SIMD assembly
    if (g_hilbert_d2xy_batch_simd != NULL) {
        g_hilbert_d2xy_batch_simd(distances, packed, count, order);
        return HILBERT_OK;
    }
    
    return HILBERT_ERR_NO_AVX2;
}

int hilbert_get_cached_curve(hilbert_packed_t **curve, size_t *size, uint32_t *order) {
    if (curve == NULL || size == NULL || order == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    if (!g_dual_cache.valid) {
        return HILBERT_ERR_NO_CACHE;
    }
    
    // For backward compatibility, return the larger (odd) curve
    // This ensures existing code continues to work
    *curve = g_dual_cache.odd_curve;
    *size = g_dual_cache.odd_size;
    *order = g_dual_cache.odd_order;
    
    return HILBERT_OK;
}

int hilbert_clear_cache(void) {
    if (g_dual_cache.valid) {
        if (g_dual_cache.even_curve != NULL) {
            free(g_dual_cache.even_curve);
            g_dual_cache.even_curve = NULL;
        }
        if (g_dual_cache.odd_curve != NULL) {
            free(g_dual_cache.odd_curve);
            g_dual_cache.odd_curve = NULL;
        }
        g_dual_cache.valid = 0;
    }
    
    return HILBERT_OK;
}