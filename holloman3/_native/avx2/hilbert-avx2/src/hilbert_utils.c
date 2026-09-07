/**
 * @file hilbert_utils.c
 * @brief Utility functions for Hilbert curve operations
 */

#include "../include/hilbert.h"
#include <stdint.h>
#include <stddef.h>

/**
 * Calculate the minimum Hilbert curve order needed to enclose a buffer.
 *
 * Returns ceil(log2(buffer_size)/2) — the smallest order n with 4^n >=
 * buffer_size. Measured via the bit length of (buffer_size - 1), which equals
 * ceil(log2(buffer_size)); using buffer_size directly caused an off-by-one at
 * exact power-of-4 sizes (e.g. 1024 -> order 6 instead of 5).
 */
uint32_t hilbert_calculate_order(size_t buffer_size) {
    if (buffer_size == 0) return 0;

    size_t order = 0;
    size_t n = buffer_size - 1;

    // Bit length of (buffer_size - 1) = ceil(log2(buffer_size)).
    while (n > 0) {
        n >>= 1;
        order++;
    }

    // Ceiling of order/2 = ceil(log2(buffer_size)/2).
    order = (order + 1) >> 1;

    return (uint32_t)order;
}

/**
 * Get the dimension (side length) of a Hilbert curve.
 */
uint32_t hilbert_get_dimension(uint32_t order) {
    return HILBERT_CURVE_SIZE(order);
}

/**
 * Validate that a buffer fits within a curve of given order.
 */
int hilbert_validate_buffer(size_t buffer_size, uint32_t order) {
    size_t curve_capacity = HILBERT_TOTAL_POINTS(order);
    return curve_capacity >= buffer_size ? 1 : 0;
}