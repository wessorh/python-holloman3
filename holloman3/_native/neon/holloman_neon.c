/**
 * @file holloman_neon.c
 * @brief ARM NEON-optimized Holloman fingerprinting implementation
 * 
 * This implementation uses ARM NEON SIMD instructions for performance
 * on ARM platforms (ARMv7+ with NEON or ARMv8+).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../common/holloman.h"
#include "hilbert_external_buffer.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#define PI 3.141592653589793
#define BITS 14
#define SCALE_FACTOR (1 << BITS)
#define ROUND_OFFSET (1 << (BITS - 1))

// Version string
const char* holloman_version(void) {
    return "3.0.2-neon";
}

// Check if NEON is available
int holloman_is_avx2_available(void) {
#ifdef __ARM_NEON
    return 1;
#else
    return 0;
#endif
}

// Clamp value to uint8 range
static inline uint8_t u8(int x) {
    if (x < 0) return 0;
    if (x > 255) return 255;
    return (uint8_t)x;
}

// Clip value to range
static inline int clip(int v, int min, int max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

// Lanczos-3 kernel function
static double lanczos3(double x) {
    const double alpha = 3.0;
    
    if (x > alpha) {
        return 0.0;
    } else if (x == 0.0) {
        return 1.0;
    }
    
    double b = x * PI;
    double c = b / alpha;
    return sin(b) * sin(c) / (b * c);
}

// Weight structure for sorting
typedef struct {
    double weight;
    int offset;
} Weight;

// Compare function for qsort
static int weight_compare(const void *a, const void *b) {
    const Weight *wa = (const Weight *)a;
    const Weight *wb = (const Weight *)b;
    double abs_a = fabs(wa->weight);
    double abs_b = fabs(wb->weight);
    
    if (abs_b < abs_a) return -1;
    if (abs_b > abs_a) return 1;
    return 0;
}

// Create double-precision kernel weights
static void make_double_kernel(
    int input_size,
    int output_size,
    double *weights_out,
    double *sums_out,
    int16_t *offsets_out,
    int *taps_out
) {
    double scale = (double)output_size / (double)input_size;
    double step = (scale < 1.0) ? scale : 1.0;
    double support = 3.0 / step;
    int taps = (int)ceil(support) * 2;
    
    if (taps > input_size) {
        taps = input_size & ~1;
    }
    
    *taps_out = taps;
    
    double xstep = 1.0 / scale;
    double xmid = (double)(input_size - output_size) / (double)(output_size * 2);
    
    for (int i = 0; i < output_size; i++) {
        int left = (int)ceil(xmid) - taps / 2;
        int x = clip(left, 0, (input_size - taps > 0) ? (input_size - taps) : 0);
        offsets_out[i] = (int16_t)x;
        
        double sum = 0.0;
        double *weights = &weights_out[i * taps];
        
        for (int j = 0; j < taps; j++) {
            weights[j] = 0.0;
        }
        
        for (int j = 0; j < taps; j++) {
            int src = left + j;
            double weight = lanczos3(fabs(xmid - (double)src) * step);
            int src_clamped = clip(src, x, input_size - 1) - x;
            weights[src_clamped] += weight;
            sum += weight;
        }
        
        sums_out[i] = sum;
        xmid += xstep;
    }
}

// Convert double weights to integer coefficients
static void make_integer_kernel(
    int taps,
    int size,
    const double *weights,
    const double *sums,
    int16_t *coeffs_out
) {
    Weight *sorted_weights = (Weight *)malloc(taps * sizeof(Weight));
    
    for (int i = 0; i < size; i++) {
        const double *w = &weights[i * taps];
        int16_t *c = &coeffs_out[i * taps];
        double sum = sums[i];
        
        for (int j = 0; j < taps; j++) {
            sorted_weights[j].weight = w[j];
            sorted_weights[j].offset = j;
        }
        
        qsort(sorted_weights, taps, sizeof(Weight), weight_compare);
        
        double diff = 0.0;
        double scale = (double)SCALE_FACTOR / sum;
        
        for (int j = 0; j < taps; j++) {
            double weight_val = sorted_weights[j].weight * scale + diff;
            double iw = floor(weight_val + 0.5);
            c[sorted_weights[j].offset] = (int16_t)iw;
            diff = weight_val - iw;
        }
    }
    
    free(sorted_weights);
}

// Apply vertical scaling with NEON optimization
static void apply_vertical_scaling(
    uint8_t *output,
    const uint8_t *input,
    const int16_t *coeffs,
    const int16_t *offsets,
    int taps,
    int input_dim,
    int output_dim
) {
#ifdef __ARM_NEON
    // NEON-optimized version
    for (int x = 0; x < input_dim; x++) {
        for (int y = 0; y < output_dim; y++) {
            int offset = offsets[y];
            int32_t sum = 0;
            
            // Process 4 taps at a time with NEON
            int t = 0;
            int32x4_t acc = vdupq_n_s32(0);
            
            for (; t + 4 <= taps; t += 4) {
                // Load 4 pixels
                uint8_t pixels[4];
                for (int i = 0; i < 4; i++) {
                    int src_y = offset + t + i;
                    pixels[i] = (src_y >= 0 && src_y < input_dim) ? 
                                input[src_y * input_dim + x] : 0;
                }
                uint8x8_t pixels_u8 = vld1_u8(pixels);
                uint16x4_t pixels_u16 = vget_low_u16(vmovl_u8(pixels_u8));
                int32x4_t pixels_i32 = vreinterpretq_s32_u32(vmovl_u16(pixels_u16));
                
                // Load 4 coefficients
                int16x4_t coeff_i16 = vld1_s16(&coeffs[y * taps + t]);
                int32x4_t coeff_i32 = vmovl_s16(coeff_i16);
                
                // Multiply and accumulate
                acc = vmlaq_s32(acc, pixels_i32, coeff_i32);
            }
            
            // Horizontal sum
            int32x2_t acc_low = vget_low_s32(acc);
            int32x2_t acc_high = vget_high_s32(acc);
            int32x2_t acc_sum = vadd_s32(acc_low, acc_high);
            acc_sum = vpadd_s32(acc_sum, acc_sum);
            sum = vget_lane_s32(acc_sum, 0);
            
            // Handle remaining taps
            for (; t < taps; t++) {
                int src_y = offset + t;
                if (src_y >= 0 && src_y < input_dim) {
                    sum += input[src_y * input_dim + x] * coeffs[y * taps + t];
                }
            }
            
            sum = (sum + ROUND_OFFSET) >> BITS;
            output[y * input_dim + x] = u8(sum);
        }
    }
#else
    // Fallback to scalar version
    for (int x = 0; x < input_dim; x++) {
        for (int y = 0; y < output_dim; y++) {
            int offset = offsets[y];
            int32_t sum = 0;
            
            for (int t = 0; t < taps; t++) {
                int src_y = offset + t;
                if (src_y >= 0 && src_y < input_dim) {
                    sum += input[src_y * input_dim + x] * coeffs[y * taps + t];
                }
            }
            
            sum = (sum + ROUND_OFFSET) >> BITS;
            output[y * input_dim + x] = u8(sum);
        }
    }
#endif
}

// Apply horizontal scaling with NEON optimization
static void apply_horizontal_scaling(
    uint8_t *output,
    const uint8_t *input,
    const int16_t *coeffs,
    const int16_t *offsets,
    int taps,
    int input_dim,
    int output_dim
) {
#ifdef __ARM_NEON
    // NEON-optimized version
    for (int y = 0; y < output_dim; y++) {
        for (int x = 0; x < output_dim; x++) {
            int offset = offsets[x];
            int32_t sum = 0;
            
            // Process 4 taps at a time with NEON
            int t = 0;
            int32x4_t acc = vdupq_n_s32(0);
            
            for (; t + 4 <= taps; t += 4) {
                // Load 4 pixels
                uint8_t pixels[4];
                for (int i = 0; i < 4; i++) {
                    int src_x = offset + t + i;
                    pixels[i] = (src_x >= 0 && src_x < input_dim) ? 
                                input[y * input_dim + src_x] : 0;
                }
                uint8x8_t pixels_u8 = vld1_u8(pixels);
                uint16x4_t pixels_u16 = vget_low_u16(vmovl_u8(pixels_u8));
                int32x4_t pixels_i32 = vreinterpretq_s32_u32(vmovl_u16(pixels_u16));
                
                // Load 4 coefficients
                int16x4_t coeff_i16 = vld1_s16(&coeffs[x * taps + t]);
                int32x4_t coeff_i32 = vmovl_s16(coeff_i16);
                
                // Multiply and accumulate
                acc = vmlaq_s32(acc, pixels_i32, coeff_i32);
            }
            
            // Horizontal sum
            int32x2_t acc_low = vget_low_s32(acc);
            int32x2_t acc_high = vget_high_s32(acc);
            int32x2_t acc_sum = vadd_s32(acc_low, acc_high);
            acc_sum = vpadd_s32(acc_sum, acc_sum);
            sum = vget_lane_s32(acc_sum, 0);
            
            // Handle remaining taps
            for (; t < taps; t++) {
                int src_x = offset + t;
                if (src_x >= 0 && src_x < input_dim) {
                    sum += input[y * input_dim + src_x] * coeffs[x * taps + t];
                }
            }
            
            sum = (sum + ROUND_OFFSET) >> BITS;
            output[y * output_dim + x] = u8(sum);
        }
    }
#else
    // Fallback to scalar version
    for (int y = 0; y < output_dim; y++) {
        for (int x = 0; x < output_dim; x++) {
            int offset = offsets[x];
            int32_t sum = 0;
            
            for (int t = 0; t < taps; t++) {
                int src_x = offset + t;
                if (src_x >= 0 && src_x < input_dim) {
                    sum += input[y * input_dim + src_x] * coeffs[x * taps + t];
                }
            }
            
            sum = (sum + ROUND_OFFSET) >> BITS;
            output[y * output_dim + x] = u8(sum);
        }
    }
#endif
}

// Downsample using Lanczos-3
static void downsample_lanczos3(uint8_t output[16], const uint8_t *input, int dimension) {
    // Allocate temporary buffers
    int max_taps = dimension;
    double *v_weights = (double *)malloc(4 * max_taps * sizeof(double));
    double *v_sums = (double *)malloc(4 * sizeof(double));
    int16_t *v_offsets = (int16_t *)malloc(4 * sizeof(int16_t));
    int16_t *v_coeffs = (int16_t *)malloc(4 * max_taps * sizeof(int16_t));
    
    double *h_weights = (double *)malloc(4 * max_taps * sizeof(double));
    double *h_sums = (double *)malloc(4 * sizeof(double));
    int16_t *h_offsets = (int16_t *)malloc(4 * sizeof(int16_t));
    int16_t *h_coeffs = (int16_t *)malloc(4 * max_taps * sizeof(int16_t));
    
    uint8_t *intermediate = (uint8_t *)malloc(dimension * 4);
    
    // PASS 1: Vertical scaling
    int v_taps;
    make_double_kernel(dimension, 4, v_weights, v_sums, v_offsets, &v_taps);
    make_integer_kernel(v_taps, 4, v_weights, v_sums, v_coeffs);
    apply_vertical_scaling(intermediate, input, v_coeffs, v_offsets, v_taps, dimension, 4);
    
    // PASS 2: Horizontal scaling
    int h_taps;
    make_double_kernel(dimension, 4, h_weights, h_sums, h_offsets, &h_taps);
    make_integer_kernel(h_taps, 4, h_weights, h_sums, h_coeffs);
    apply_horizontal_scaling(output, intermediate, h_coeffs, h_offsets, h_taps, dimension, 4);
    
    // Cleanup
    free(v_weights);
    free(v_sums);
    free(v_offsets);
    free(v_coeffs);
    free(h_weights);
    free(h_sums);
    free(h_offsets);
    free(h_coeffs);
    free(intermediate);
}

/* ============================================================================
 * Dual Curve Cache System (Even/Odd Hierarchy)
 * ============================================================================ */

typedef struct {
    uint64_t *even_curve;    // Packed coordinates for even orders
    size_t even_size;
    uint32_t even_order;
    uint64_t *odd_curve;     // Packed coordinates for odd orders
    size_t odd_size;
    uint32_t odd_order;
    int valid;
} hilbert_dual_cache_neon_t;

static hilbert_dual_cache_neon_t g_neon_cache = {NULL, 0, 0, NULL, 0, 0, 0};

// Hilbert D2XY conversion (exact match to AVX2)
static uint64_t hilbert_d2xy(uint32_t d, uint32_t order) {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t s = 1;
    
    for (int i = 0; i < order; i++) {
        uint32_t rx = (d >> 1) & 1;
        uint32_t ry = (d ^ rx) & 1;
        
        if (ry == 0) {
            if (rx == 1) {
                x = s - 1 - x;
                y = s - 1 - y;
            }
            uint32_t temp = x;
            x = y;
            y = temp;
        }
        
        x += s * rx;
        y += s * ry;
        
        d >>= 2;
        s <<= 1;
    }
    
    return ((uint64_t)x << 32) | (uint64_t)y;
}

// Generate Hilbert curve
static uint64_t* generate_hilbert_curve(uint32_t order) {
    size_t total_points = 1ULL << (order * 2);
    uint64_t *curve = (uint64_t*)malloc(total_points * sizeof(uint64_t));
    if (!curve) return NULL;
    
    for (size_t i = 0; i < total_points; i++) {
        curve[i] = hilbert_d2xy((uint32_t)i, order);
    }
    
    return curve;
}

// Calculate minimum required order
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

// Initialize dual curves
int holloman_init(uint32_t order) {
    if (g_neon_cache.valid) {
        // Already initialized
        return 0;
    }
    
    // Determine even and odd orders
    uint32_t even_order = (order >= 14) ? 14 : ((order % 2 == 0) ? order : order - 1);
    uint32_t odd_order = (order >= 15) ? 15 : ((order % 2 == 1) ? order : order + 1);
    
    if (even_order < 2) even_order = 2;
    if (odd_order < 3) odd_order = 3;
    
    // Clear existing cache
    if (g_neon_cache.even_curve) free(g_neon_cache.even_curve);
    if (g_neon_cache.odd_curve) free(g_neon_cache.odd_curve);
    
    // Generate even curve
    g_neon_cache.even_curve = generate_hilbert_curve(even_order);
    if (!g_neon_cache.even_curve) {
        return -1;
    }
    g_neon_cache.even_size = 1ULL << (even_order * 2);
    g_neon_cache.even_order = even_order;
    
    // Generate odd curve
    g_neon_cache.odd_curve = generate_hilbert_curve(odd_order);
    if (!g_neon_cache.odd_curve) {
        free(g_neon_cache.even_curve);
        g_neon_cache.even_curve = NULL;
        return -1;
    }
    g_neon_cache.odd_size = 1ULL << (odd_order * 2);
    g_neon_cache.odd_order = odd_order;
    
    g_neon_cache.valid = 1;
    return 0;
}

// Cleanup dual curves
int holloman_cleanup(void) {
    if (g_neon_cache.valid) {
        if (g_neon_cache.even_curve) {
            free(g_neon_cache.even_curve);
            g_neon_cache.even_curve = NULL;
        }
        if (g_neon_cache.odd_curve) {
            free(g_neon_cache.odd_curve);
            g_neon_cache.odd_curve = NULL;
        }
        g_neon_cache.valid = 0;
    }
    return 0;
}

// Map buffer using dual-curve system
// Returns: pointer to mapped buffer on success, NULL on failure
// Sets error_code to indicate failure reason:
//   -2: Cache not initialized
//   -3: Required order exceeds max order
//   -5: Memory allocation failed
static uint8_t* map_buffer_dual_curve(const uint8_t *data, size_t size, size_t *mapped_len, int *dimension_out, int *error_code) {
    if (!g_neon_cache.valid) {
        *error_code = -2;  // Not initialized
        return NULL;
    }
    
    // Calculate minimum required order
    uint32_t required_order = calculate_order(size);
    
    // Select curve based on parity
    uint64_t *selected_curve;
    uint32_t max_order;
    
    if (required_order % 2 == 0) {
        // Even order - use even curve
        selected_curve = g_neon_cache.even_curve;
        max_order = g_neon_cache.even_order;
        
        if (required_order > max_order) {
            *error_code = -3;  // Invalid order
            return NULL;
        }
    } else {
        // Odd order - use odd curve
        selected_curve = g_neon_cache.odd_curve;
        max_order = g_neon_cache.odd_order;
        
        if (required_order > max_order) {
            *error_code = -3;  // Invalid order
            return NULL;
        }
    }
    
    // Use required order for deterministic fingerprints
    uint32_t order = required_order;
    uint32_t dimension = 1U << order;
    size_t buffer_size = (size_t)dimension * dimension;
    
    uint8_t *mapped = (uint8_t *)calloc(buffer_size, 1);
    if (!mapped) {
        *error_code = -5;  // Out of memory
        return NULL;
    }
    
    // Map using selected curve
    for (size_t i = 0; i < size; i++) {
        uint64_t packed = selected_curve[i];
        uint32_t x = (uint32_t)(packed >> 32);
        uint32_t y = (uint32_t)(packed & 0xFFFFFFFF);
        
        size_t pos = (size_t)y * dimension + x;
        if (pos < buffer_size) {
            mapped[pos] = data[i];
        }
    }
    
    *mapped_len = buffer_size;
    *dimension_out = dimension;
    return mapped;
}

// Read file into buffer
static uint8_t* read_file(const char *filename, size_t *file_size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return NULL;
    }
    
    fseek(fp, 0, SEEK_END);
    *file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    uint8_t *buffer = (uint8_t*)malloc(*file_size);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    
    size_t bytes_read = fread(buffer, 1, *file_size, fp);
    fclose(fp);
    
    if (bytes_read != *file_size) {
        free(buffer);
        return NULL;
    }
    
    return buffer;
}

// Rotate 4x4 matrix 90 degrees clockwise
static void rotate_matrix_90_clockwise(uint8_t output[16], const uint8_t input[16]) {
    // Pre-computed mapping for 90° clockwise rotation
    const int mapping[16] = {12, 8, 4, 0, 13, 9, 5, 1, 14, 10, 6, 2, 15, 11, 7, 3};
    
    for (int i = 0; i < 16; i++) {
        output[i] = input[mapping[i]];
    }
}

// Generate fingerprint from buffer
int holloman_fingerprint_buffer(const uint8_t *data, size_t size, uint8_t fingerprint[16]) {
    if (!data || size == 0 || !fingerprint) {
        return -1;
    }
    
    if (!g_neon_cache.valid) {
        return -2;  // Not initialized
    }
    
    size_t mapped_len;
    int dimension;
    int error_code = 0;
    uint8_t *mapped_buffer = map_buffer_dual_curve(data, size, &mapped_len, &dimension, &error_code);
    if (!mapped_buffer) {
        return error_code;  // Pass through actual error code
    }
    
    // Downsample to 4x4
    uint8_t temp_result[16];
    downsample_lanczos3(temp_result, mapped_buffer, dimension);
    
    // Rotate 90 degrees clockwise
    rotate_matrix_90_clockwise(fingerprint, temp_result);
    
    free(mapped_buffer);
    return 0;
}

// Generate fingerprint from buffer using external scratch workspace
int holloman_fingerprint_buffer_external(
    const uint8_t *data, 
    size_t size, 
    uint8_t fingerprint[16],
    uint8_t *scratch_buffer,
    size_t scratch_size
) {
    if (!data || size == 0 || !fingerprint || !scratch_buffer) {
        return -3;  // HILBERT_ERR_INVALID_PARAM
    }
    
    if (!g_neon_cache.valid) {
        return -6;  // HILBERT_ERR_NO_CACHE (not initialized)
    }
    
    // Map buffer using Hilbert curve with external scratch buffer
    size_t actual_len = 0;
    int ret = hilbert_map_buffer_external(
        data, 
        size, 
        scratch_buffer, 
        scratch_size, 
        &actual_len
    );
    
    if (ret != HILBERT_OK) {
        return ret;  // Pass through actual error code
    }
    
    // Calculate the dimension of the mapped buffer (it's a square)
    int dimension = (int)sqrt((double)actual_len);
    
    // Verify it's actually a perfect square
    if (dimension * dimension != (int)actual_len) {
        return -5;  // HILBERT_ERR_OUT_OF_MEMORY (reusing error code)
    }
    
    // Downsample to 4x4 (16 bytes) using NEON-optimized Lanczos-3
    uint8_t temp_result[16];
    downsample_lanczos3(temp_result, scratch_buffer, dimension);
    
    // Rotate 90 degrees clockwise
    rotate_matrix_90_clockwise(fingerprint, temp_result);
    
    // Note: scratch_buffer is NOT freed - caller manages it
    
    return 0;
}

// Generate fingerprint from file
int holloman_fingerprint_file(const char *filename, uint8_t fingerprint[16]) {
    if (!filename || !fingerprint) {
        return -1;
    }
    
    size_t file_size;
    uint8_t *file_buffer = read_file(filename, &file_size);
    if (!file_buffer) {
        return -2;
    }
    
    int ret = holloman_fingerprint_buffer(file_buffer, file_size, fingerprint);
    free(file_buffer);
    
    return ret;
}