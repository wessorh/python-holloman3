/**
 * @file hilbert_mapping.c
 * @brief Buffer mapping implementation with performance prediction
 */

#define _POSIX_C_SOURCE 200112L

#include "../include/hilbert.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* ============================================================================
 * External SIMD Functions and Dual Cache
 * ============================================================================ */

// Dual curve cache structure (defined in hilbert.c)
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

// External architecture detection functions
extern int hilbert_has_simd(void);

// External function pointers (set by arch_detect.c)
typedef void (*hilbert_map_buffer_func)(const unsigned char *src,
                                        unsigned char *dst,
                                        const hilbert_packed_t *curve,
                                        size_t count,
                                        uint32_t dimension);
extern hilbert_map_buffer_func g_hilbert_map_buffer_simd;
extern hilbert_map_buffer_func g_hilbert_map_buffer_cache_blocking;


/* ============================================================================
 * Performance Prediction
 * ============================================================================ */

int hilbert_predict_performance(size_t buffer_size, uint32_t order,
                                hilbert_perf_prediction_t *prediction) {
    if (prediction == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    // Theoretical model for AVX2 assembly mapping
    // Assumptions:
    // - 8 bytes processed per iteration (AVX2 width)
    // - ~2-3 cycles per byte for coordinate extraction and position calculation
    // - Memory bandwidth: ~20-30 GB/s on modern CPUs
    // - Cache miss penalty: ~200 cycles
    
    double cycles_per_byte = 2.5;  // Conservative estimate
    double cpu_freq_ghz = 3.0;     // Assume 3 GHz CPU
    double memory_bw_gbps = 25.0;  // Assume 25 GB/s memory bandwidth
    
    // Calculate predicted throughput (compute-bound)
    double compute_throughput = (cpu_freq_ghz * 1e9) / cycles_per_byte / 1e9;  // GB/s
    
    // Memory bandwidth limited throughput
    double memory_throughput = memory_bw_gbps;
    
    // Actual throughput is minimum of compute and memory bandwidth
    prediction->predicted_throughput_gbps = (compute_throughput < memory_throughput) 
                                           ? compute_throughput : memory_throughput;
    
    prediction->predicted_cycles_per_byte = cycles_per_byte;
    
    // Estimate cache misses (assume ~10% cache miss rate for random access)
    size_t curve_size = HILBERT_TOTAL_POINTS(order);
    prediction->predicted_cache_misses = (size_t)(buffer_size * 0.1);
    
    prediction->predicted_memory_bw_gbps = memory_bw_gbps;
    
    return HILBERT_OK;
}

/* ============================================================================
 * Performance Measurement
 * ============================================================================ */

static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int hilbert_measure_performance(const unsigned char *src_buffer, size_t src_len,
                                unsigned char *dst_buffer, size_t dst_len,
                                hilbert_perf_measurement_t *measurement) {
    if (src_buffer == NULL || dst_buffer == NULL || measurement == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    // Get cached curve
    hilbert_packed_t *curve;
    size_t curve_size;
    uint32_t order;
    int ret = hilbert_get_cached_curve(&curve, &curve_size, &order);
    if (ret != HILBERT_OK) {
        return ret;
    }
    
    uint32_t dimension = hilbert_get_dimension(order);
    
    // Measure time
    double start_time = get_time_sec();
    
    g_hilbert_map_buffer_simd(src_buffer, dst_buffer, curve, src_len, dimension);
    
    double end_time = get_time_sec();
    double elapsed = end_time - start_time;
    
    // Calculate metrics
    measurement->elapsed_time_sec = elapsed;
    measurement->bytes_processed = src_len;
    measurement->actual_throughput_gbps = (src_len / 1e9) / elapsed;
    
    // Estimate cycles per byte (assuming 3 GHz CPU)
    double cpu_freq_ghz = 3.0;
    measurement->actual_cycles_per_byte = (elapsed * cpu_freq_ghz * 1e9) / src_len;
    
    return HILBERT_OK;
}

/* ============================================================================
 * Performance Comparison
 * ============================================================================ */

int hilbert_compare_performance(const hilbert_perf_prediction_t *prediction,
                               const hilbert_perf_measurement_t *measurement,
                               double *throughput_error, double *cycles_error) {
    if (prediction == NULL || measurement == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    if (throughput_error != NULL) {
        *throughput_error = ((measurement->actual_throughput_gbps - 
                             prediction->predicted_throughput_gbps) / 
                             prediction->predicted_throughput_gbps) * 100.0;
    }
    
    if (cycles_error != NULL) {
        *cycles_error = ((measurement->actual_cycles_per_byte - 
                         prediction->predicted_cycles_per_byte) / 
                         prediction->predicted_cycles_per_byte) * 100.0;
    }
    
    return HILBERT_OK;
}

/* ============================================================================
 * Buffer Mapping
 * ============================================================================ */

int hilbert_map_buffer(const unsigned char *src_buffer, size_t src_len,
                       unsigned char **dst_buffer, size_t *dst_len) {
    // Validate parameters
    if (src_buffer == NULL || dst_buffer == NULL || dst_len == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    // Check SIMD availability early
    if (!hilbert_has_simd()) {
        return HILBERT_ERR_NO_AVX2;
    }
    
    // Calculate minimum required order for this buffer
    uint32_t required_order = hilbert_calculate_order(src_len);
    
    // Select curve based on parity (even/odd hierarchy)
    hilbert_packed_t *curve;
    size_t curve_size;
    uint32_t max_order;
    
    // Access dual cache directly (extern declaration)
    
    if (!g_dual_cache.valid) {
        return HILBERT_ERR_NO_CACHE;
    }
    
    if (required_order % 2 == 0) {
        // Even order - use even curve
        curve = g_dual_cache.even_curve;
        curve_size = g_dual_cache.even_size;
        max_order = g_dual_cache.even_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    } else {
        // Odd order - use odd curve
        curve = g_dual_cache.odd_curve;
        curve_size = g_dual_cache.odd_size;
        max_order = g_dual_cache.odd_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    }
    
    // Use the required order (not max_order) for deterministic fingerprints
    uint32_t order = required_order;
    
    // Calculate destination size based on required order
    size_t dst_size = HILBERT_TOTAL_POINTS(order);
    
    // Verify buffer fits
    if (src_len > dst_size) {
        return HILBERT_ERR_INVALID_ORDER;
    }
    
    // Allocate destination buffer (32-byte aligned for SIMD)
    unsigned char *dst = NULL;
    if (posix_memalign((void**)&dst, 32, dst_size) != 0) {
        return HILBERT_ERR_OUT_OF_MEMORY;
    }
    
    // Zero destination buffer
    memset(dst, 0, dst_size);
    
    // Get dimension
    uint32_t dimension = hilbert_get_dimension(order);
    
    // Map buffer using SIMD assembly
    g_hilbert_map_buffer_simd(src_buffer, dst, curve, src_len, dimension);
    
    // Set output parameters
    *dst_buffer = dst;
    *dst_len = dst_size;
    
    return HILBERT_OK;
}

/* ============================================================================
 * Prefetch-Optimized Buffer Mapping
 * ============================================================================ */

int hilbert_map_buffer_cache_blocking(const unsigned char *src_buffer, size_t src_len,
                                      unsigned char **dst_buffer, size_t *dst_len) {
    // Validate parameters
    if (src_buffer == NULL || dst_buffer == NULL || dst_len == NULL) {
        return HILBERT_ERR_INVALID_PARAM;
    }
    
    // Check SIMD availability early
    if (!hilbert_has_simd()) {
        return HILBERT_ERR_NO_AVX2;
    }
    
    // Calculate minimum required order for this buffer
    uint32_t required_order = hilbert_calculate_order(src_len);
    
    // Select curve based on parity (even/odd hierarchy)
    hilbert_packed_t *curve;
    size_t curve_size;
    uint32_t max_order;
    
    // Access dual cache directly (extern declaration)
    
    if (!g_dual_cache.valid) {
        return HILBERT_ERR_NO_CACHE;
    }
    
    if (required_order % 2 == 0) {
        // Even order - use even curve
        curve = g_dual_cache.even_curve;
        curve_size = g_dual_cache.even_size;
        max_order = g_dual_cache.even_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    } else {
        // Odd order - use odd curve
        curve = g_dual_cache.odd_curve;
        curve_size = g_dual_cache.odd_size;
        max_order = g_dual_cache.odd_order;
        
        if (required_order > max_order) {
            return HILBERT_ERR_INVALID_ORDER;
        }
    }
    
    // Use the required order (not max_order) for deterministic fingerprints
    uint32_t order = required_order;
    
    // Calculate destination size based on required order
    size_t dst_size = HILBERT_TOTAL_POINTS(order);
    
    // Verify buffer fits
    if (src_len > dst_size) {
        return HILBERT_ERR_INVALID_ORDER;
    }
    
    // Allocate destination buffer (32-byte aligned for SIMD)
    unsigned char *dst = NULL;
    if (posix_memalign((void**)&dst, 32, dst_size) != 0) {
        return HILBERT_ERR_OUT_OF_MEMORY;
    }
    
    // Zero destination buffer
    memset(dst, 0, dst_size);
    
    // Get dimension
    uint32_t dimension = hilbert_get_dimension(order);
    
    // Map buffer using cache blocking optimized AVX2 assembly
    // No fallback - cache blocking optimization required for this function
    if (g_hilbert_map_buffer_cache_blocking != NULL) {
        g_hilbert_map_buffer_cache_blocking(src_buffer, dst, curve, src_len, dimension);
    } else {
        // Cache blocking not available - return error instead of falling back
        free(dst);
        return HILBERT_ERR_NO_AVX2;
    }
    
    // Set output parameters
    *dst_buffer = dst;
    *dst_len = dst_size;
    
    return HILBERT_OK;
}