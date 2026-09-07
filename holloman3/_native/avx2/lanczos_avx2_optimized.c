/*
 * AVX2-Optimized Lanczos-3 Downsampling Implementation
 * 
 * This implementation uses AVX2 SIMD instructions for performance
 * while maintaining bit-exact matching with the reference implementation.
 */

#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define BITS 14
#define SCALE_FACTOR (1 << BITS)
#define ROUND_OFFSET (1 << (BITS - 1))
#define PI 3.141592653589793

static inline uint8_t u8(int x) {
    if (x < 0) return 0;
    if (x > 255) return 255;
    return (uint8_t)x;
}

static inline int clip(int v, int min, int max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

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

typedef struct {
    double weight;
    int offset;
} Weight;

static int weight_compare(const void *a, const void *b) {
    const Weight *wa = (const Weight *)a;
    const Weight *wb = (const Weight *)b;
    double abs_a = fabs(wa->weight);
    double abs_b = fabs(wb->weight);
    
    if (abs_b < abs_a) return -1;
    if (abs_b > abs_a) return 1;
    return 0;
}

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

/* AVX2-optimized horizontal scaling */
static void h_scale_avx2(
    uint8_t *dst,
    const uint8_t *src,
    const int16_t *coeffs,
    const int16_t *offsets,
    int taps,
    int width,
    int height,
    int dst_pitch,
    int src_pitch
) {
    const __m256i round = _mm256_set1_epi32(ROUND_OFFSET);
    
    for (int y = 0; y < height; y++) {
        const uint8_t *src_row = &src[y * src_pitch];
        uint8_t *dst_row = &dst[y * dst_pitch];
        
        for (int x = 0; x < width; x++) {
            int offset = offsets[x];
            const int16_t *c = &coeffs[x * taps];
            
            // Process 8 taps at a time with AVX2
            __m256i acc = _mm256_setzero_si256();
            
            int i = 0;
            for (; i + 8 <= taps; i += 8) {
                // Load 8 pixels (uint8) and convert to int32
                __m128i pixels_u8 = _mm_loadl_epi64((__m128i*)&src_row[offset + i]);
                __m256i pixels_i32 = _mm256_cvtepu8_epi32(pixels_u8);
                
                // Load 8 coefficients (int16) and convert to int32
                __m128i coeff_i16 = _mm_loadu_si128((__m128i*)&c[i]);
                __m256i coeff_i32 = _mm256_cvtepi16_epi32(coeff_i16);
                
                // Multiply and accumulate
                __m256i prod = _mm256_mullo_epi32(pixels_i32, coeff_i32);
                acc = _mm256_add_epi32(acc, prod);
            }
            
            // Horizontal sum of acc
            __m128i acc_low = _mm256_castsi256_si128(acc);
            __m128i acc_high = _mm256_extracti128_si256(acc, 1);
            __m128i acc_sum = _mm_add_epi32(acc_low, acc_high);
            
            acc_sum = _mm_hadd_epi32(acc_sum, acc_sum);
            acc_sum = _mm_hadd_epi32(acc_sum, acc_sum);
            
            int pix = _mm_cvtsi128_si32(acc_sum);
            
            // Handle remaining taps
            for (; i < taps; i++) {
                pix += src_row[offset + i] * c[i];
            }
            
            dst_row[x] = u8((pix + ROUND_OFFSET) >> BITS);
        }
    }
}

/* AVX2-optimized vertical scaling */
static void v_scale_avx2(
    uint8_t *dst,
    const uint8_t *src,
    const int16_t *coeffs,
    const int16_t *offsets,
    int taps,
    int width,
    int height,
    int dst_pitch,
    int src_pitch
) {
    const __m256i round = _mm256_set1_epi32(ROUND_OFFSET);
    
    for (int y = 0; y < height; y++) {
        uint8_t *dst_row = &dst[y * dst_pitch];
        const int16_t *c = &coeffs[y * taps];
        int offset = offsets[y];
        
        // Process 8 pixels at a time
        int x = 0;
        for (; x + 8 <= width; x += 8) {
            __m256i acc = _mm256_setzero_si256();
            
            for (int i = 0; i < taps; i++) {
                // Load 8 pixels from source row
                __m128i pixels_u8 = _mm_loadl_epi64((__m128i*)&src[(offset + i) * src_pitch + x]);
                __m256i pixels_i32 = _mm256_cvtepu8_epi32(pixels_u8);
                
                // Broadcast coefficient
                __m256i coeff = _mm256_set1_epi32(c[i]);
                
                // Multiply and accumulate
                __m256i prod = _mm256_mullo_epi32(pixels_i32, coeff);
                acc = _mm256_add_epi32(acc, prod);
            }
            
            // Add rounding and shift
            acc = _mm256_add_epi32(acc, round);
            acc = _mm256_srai_epi32(acc, BITS);
            
            // Pack and clamp to uint8
            __m128i acc_low = _mm256_castsi256_si128(acc);
            __m128i acc_high = _mm256_extracti128_si256(acc, 1);
            __m128i packed = _mm_packus_epi32(acc_low, acc_high);
            packed = _mm_packus_epi16(packed, packed);
            
            // Store 8 pixels
            _mm_storel_epi64((__m128i*)&dst_row[x], packed);
        }
        
        // Handle remaining pixels
        for (; x < width; x++) {
            int pix = 0;
            for (int i = 0; i < taps; i++) {
                pix += src[(offset + i) * src_pitch + x] * c[i];
            }
            dst_row[x] = u8((pix + ROUND_OFFSET) >> BITS);
        }
    }
}

/* Main downsampling function with AVX2 */
void downsample_lanczos3_avx2(
    uint8_t output[16],
    const uint8_t *input,
    int size
) {
    uint8_t *intermediate = (uint8_t *)malloc(size * 4);
    
    // PASS 1: Vertical (size×size → size×4)
    {
        double *v_weights = (double *)malloc(4 * size * sizeof(double));
        double *v_sums = (double *)malloc(4 * sizeof(double));
        int16_t *v_offsets = (int16_t *)malloc(4 * sizeof(int16_t));
        int16_t *v_coeffs = (int16_t *)malloc(4 * size * sizeof(int16_t));
        int v_taps;
        
        make_double_kernel(size, 4, v_weights, v_sums, v_offsets, &v_taps);
        make_integer_kernel(v_taps, 4, v_weights, v_sums, v_coeffs);
        
        v_scale_avx2(intermediate, input, v_coeffs, v_offsets, v_taps, size, 4, size, size);
        
        free(v_weights);
        free(v_sums);
        free(v_offsets);
        free(v_coeffs);
    }
    
    // PASS 2: Horizontal (size×4 → 4×4)
    {
        double *h_weights = (double *)malloc(4 * size * sizeof(double));
        double *h_sums = (double *)malloc(4 * sizeof(double));
        int16_t *h_offsets = (int16_t *)malloc(4 * sizeof(int16_t));
        int16_t *h_coeffs = (int16_t *)malloc(4 * size * sizeof(int16_t));
        int h_taps;
        
        make_double_kernel(size, 4, h_weights, h_sums, h_offsets, &h_taps);
        make_integer_kernel(h_taps, 4, h_weights, h_sums, h_coeffs);
        
        h_scale_avx2(output, intermediate, h_coeffs, h_offsets, h_taps, 4, 4, 4, size);
        
        free(h_weights);
        free(h_sums);
        free(h_offsets);
        free(h_coeffs);
    }
    
    free(intermediate);
}