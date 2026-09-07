/**
 * @file hilbert.h
 * @brief High-performance Hilbert curve library with AVX2 acceleration
 * 
 * Research project focused on AVX2 curve generation speed.
 * - Single curve cache (largest order only)
 * - AVX2 assembly ONLY (no scalar fallback)
 * - Returns HILBERT_ERR_NO_AVX2 if AVX2 is unavailable
 * - Early error detection
 * - Performance prediction and measurement
 * 
 * IMPORTANT: This implementation requires AVX2 support.
 * All functions will return HILBERT_ERR_NO_AVX2 if AVX2 is not available.
 * There is NO fallback to portable C implementations.
 */

#ifndef HILBERT_H
#define HILBERT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    HILBERT_OK = 0,                   /* Success */
    HILBERT_ERR_NO_AVX2 = -1,         /* AVX2 not available */
    HILBERT_ERR_INVALID_ORDER = -2,   /* Invalid order parameter */
    HILBERT_ERR_INVALID_PARAM = -3,   /* Invalid parameter (NULL pointer, etc.) */
    HILBERT_ERR_CACHE_FULL = -4,      /* Cache is full */
    HILBERT_ERR_OUT_OF_MEMORY = -5,   /* Memory allocation failed */
    HILBERT_ERR_NO_CACHE = -6,        /* No curve cached (must call hilbert_init first) */
    HILBERT_ERR_PADDING = -7          /* Cannot satisfy padding constraint (<=74%) */
} hilbert_error_t;

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Packed coordinate format: upper 32 bits = X, lower 32 bits = Y
 */
typedef uint64_t hilbert_packed_t;

/**
 * @brief Performance prediction structure
 */
typedef struct {
    double predicted_throughput_gbps;  /* Predicted throughput in GB/s */
    double predicted_cycles_per_byte;  /* Predicted cycles per byte */
    size_t predicted_cache_misses;     /* Predicted cache misses */
    double predicted_memory_bw_gbps;   /* Predicted memory bandwidth in GB/s */
} hilbert_perf_prediction_t;

/**
 * @brief Performance measurement structure
 */
typedef struct {
    double actual_throughput_gbps;     /* Measured throughput in GB/s */
    double actual_cycles_per_byte;     /* Measured cycles per byte */
    double elapsed_time_sec;           /* Elapsed time in seconds */
    size_t bytes_processed;            /* Total bytes processed */
} hilbert_perf_measurement_t;

/* ============================================================================
 * Utility Macros
 * ============================================================================ */

/** Pack X and Y coordinates into single uint64 */
#define HILBERT_PACK(x, y) (((uint64_t)(x) << 32) | (uint64_t)(y))

/** Extract X coordinate from packed value */
#define HILBERT_UNPACK_X(packed) ((uint32_t)((packed) >> 32))

/** Extract Y coordinate from packed value */
#define HILBERT_UNPACK_Y(packed) ((uint32_t)((packed) & 0xFFFFFFFF))

/** Get curve dimension (side length) for given order: 2^order */
#define HILBERT_CURVE_SIZE(order) (1U << (order))

/** Get total number of points in curve: 4^order */
#define HILBERT_TOTAL_POINTS(order) (1ULL << ((order) * 2))

/* ============================================================================
 * Core Functions
 * ============================================================================ */

/**
 * @brief Initialize and cache a Hilbert curve
 * 
 * Generates a Hilbert curve of the specified order and caches it.
 * Only ONE curve is cached at a time (the largest).
 * A curve of order N contains all curves of orders < N.
 * 
 * @param order Curve order (grid size = 2^order x 2^order)
 * @return HILBERT_OK on success, error code on failure
 * 
 * @note Requires AVX2. Returns HILBERT_ERR_NO_AVX2 if unavailable.
 * @note Maximum order is 15 (4^15 = 1,073,741,824 points, 1 GB memory)
    * @note Memory required: 4^N bytes (Order 13: 64 MB, Order 14: 256 MB, Order 15: 1 GB)
 */
int hilbert_init(uint32_t order);

/**
 * @brief Convert distances to packed coordinates using cached curve
 * 
 * @param distances Array of distances to convert
 * @param packed Output array of packed coordinates
 * @param count Number of distances to convert
 * @param order Order of the curve to use
 * @return HILBERT_OK on success, error code on failure
 * 
 * @note Requires cached curve. Call hilbert_init() first.
 * @note Uses AVX2 assembly for processing 8 coordinates at a time
 */
int hilbert_d2xy_batch(const uint32_t *distances, hilbert_packed_t *packed, 
                       size_t count, uint32_t order);

/**
 * @brief Get the cached curve
 * 
 * @param curve Output: pointer to cached curve array
 * @param size Output: number of points in curve
 * @param order Output: order of cached curve
 * @return HILBERT_OK on success, error code on failure
 */
int hilbert_get_cached_curve(hilbert_packed_t **curve, size_t *size, uint32_t *order);

/**
 * @brief Clear the cached curve
 * 
 * @return HILBERT_OK on success, error code on failure
 */
int hilbert_clear_cache(void);

/**
 * @brief Check if AVX2 is available
 * 
 * @return 1 if AVX2 is available, 0 otherwise
 */
int hilbert_is_avx2_available(void);

/* ============================================================================
 * Buffer Mapping Functions
 * ============================================================================ */

/**
 * @brief Map a source buffer to a Hilbert curve-ordered destination buffer
 * 
 * This function:
 * 1. Calculates appropriate order ensuring padding <= 74% of buffer size
 * 2. Allocates destination buffer via posix_memalign (32-byte aligned)
 * 3. Zeros destination buffer with memset
 * 4. Maps each byte from source to destination using cached curve coordinates
 * 
 * @param src_buffer Source buffer to map
 * @param src_len Length of source buffer in bytes
 * @param dst_buffer Output: pointer to allocated destination buffer (caller must free)
 * @param dst_len Output: length of destination buffer in bytes
 * @return HILBERT_OK on success, error code on failure
 * 
 * @note Requires AVX2. Returns HILBERT_ERR_NO_AVX2 if unavailable.
 * @note Requires cached curve. Call hilbert_init() first.
 * @note Caller is responsible for freeing dst_buffer
 * @note Padding constraint: (dst_len - src_len) / src_len <= 0.74
 * @note Uses AVX2 assembly for mapping (single-threaded)
 */
int hilbert_map_buffer(const unsigned char *src_buffer, size_t src_len,
                       unsigned char **dst_buffer, size_t *dst_len);

/**
 * Map a buffer to Hilbert curve order with cache blocking optimization
 * Same as hilbert_map_buffer but processes data in cache-friendly blocks
 * 
 * @param src_buffer Source buffer to map
 * @param src_len Length of source buffer in bytes
 * @param dst_buffer Pointer to receive allocated destination buffer
 * @param dst_len Pointer to receive destination buffer length
 * @return HILBERT_SUCCESS on success, error code otherwise
 */
int hilbert_map_buffer_cache_blocking(const unsigned char *src_buffer, size_t src_len,
                                      unsigned char **dst_buffer, size_t *dst_len);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Calculate the minimum Hilbert curve order needed to enclose a buffer
 * 
 * Algorithm: Find highest set bit position in buffer_size, then return ceil(position/2).
 * This ensures the curve has sufficient capacity: 4^order >= buffer_size.
 * 
 * @param buffer_size Size of buffer in bytes
 * @return Minimum order n where 4^n >= buffer_size
 * 
 * Examples:
 *   - buffer_size = 1         -> order = 1  (2x2 = 4 points)
 *   - buffer_size = 4         -> order = 2  (4x4 = 16 points)
 *   - buffer_size = 16777216  -> order = 13 (8192x8192 = 67108864 points)
 *   - buffer_size = 268435456 -> order = 15 (32768x32768 = 1073741824 points)
 */
uint32_t hilbert_calculate_order(size_t buffer_size);

/**
 * @brief Get the dimension (side length) of a Hilbert curve
 * 
 * Since curves are square, only one dimension is needed: 2^order
 * 
 * @param order Curve order
 * @return Side length of the curve (2^order)
 */
uint32_t hilbert_get_dimension(uint32_t order);

/**
 * @brief Validate that a buffer fits within a curve of given order
 * 
 * @param buffer_size Size of buffer in bytes
 * @param order Curve order
 * @return 1 if buffer fits, 0 otherwise
 */
int hilbert_validate_buffer(size_t buffer_size, uint32_t order);

/* ============================================================================
 * Performance Prediction Functions
 * ============================================================================ */

/**
 * @brief Predict performance for buffer mapping operation
 * 
 * Generates theoretical performance predictions based on:
 * - Buffer size
 * - Curve order
 * - AVX2 characteristics (8 bytes per iteration)
 * - Estimated cycles per byte
 * - Memory bandwidth constraints
 * 
 * @param buffer_size Size of buffer to map
 * @param order Curve order
 * @param prediction Output: performance prediction
 * @return HILBERT_OK on success, error code on failure
 */
int hilbert_predict_performance(size_t buffer_size, uint32_t order,
                                hilbert_perf_prediction_t *prediction);

/**
 * @brief Measure actual performance of buffer mapping operation
 * 
 * @param src_buffer Source buffer
 * @param src_len Source buffer length
 * @param dst_buffer Destination buffer
 * @param dst_len Destination buffer length
 * @param measurement Output: performance measurement
 * @return HILBERT_OK on success, error code on failure
 */
int hilbert_measure_performance(const unsigned char *src_buffer, size_t src_len,
                                unsigned char *dst_buffer, size_t dst_len,
                                hilbert_perf_measurement_t *measurement);

/**
 * @brief Compare prediction to measurement and calculate error
 * 
 * @param prediction Performance prediction
 * @param measurement Performance measurement
 * @param throughput_error Output: throughput error percentage
 * @param cycles_error Output: cycles per byte error percentage
 * @return HILBERT_OK on success, error code on failure
 */
int hilbert_compare_performance(const hilbert_perf_prediction_t *prediction,
                               const hilbert_perf_measurement_t *measurement,
                               double *throughput_error, double *cycles_error);

#ifdef __cplusplus
}
#endif

#endif /* HILBERT_H */