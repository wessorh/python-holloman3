/* hilbert_sse41.c — 128-bit SSE4.1 Hilbert path for CPUs with AVX1 but not AVX2.
 *
 * AVX1 CPUs (Sandy/Ivy Bridge, Bulldozer) have 128-bit integer SIMD (SSE4.1)
 * but lack the 256-bit integer instructions used by hilbert_avx2.s. These
 * functions implement the same Hilbert d2xy conversion and buffer mapping with
 * 128-bit SSE4.1 intrinsics (4 lanes per iteration for d2xy; the buffer mapping
 * is inherently a per-byte scatter and is scalar, matching the AVX2 path).
 *
 * Output is byte-identical to hilbert_avx2.s / hilbert_mapping_avx2.s.
 */

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

/* void hilbert_d2xy_batch_sse41(const uint32_t *distances, uint64_t *packed,
 *                               size_t count, uint32_t order)
 *
 * Same bit-level algorithm as hilbert_avx2.s, at 4 lanes (XMM) instead of 8.
 * x, y, and the scale factor s are tracked as 32-bit lanes; the final pack is
 * (x << 32) | y with x sign-extended and y zero-extended (as in the AVX2 path).
 */
void hilbert_d2xy_batch_sse41(const uint32_t *distances, uint64_t *packed,
                              size_t count, uint32_t order) {
    const __m128i one = _mm_set1_epi32(1);
    const __m128i zero = _mm_setzero_si128();

    size_t vec_count = count / 4;
    size_t i = 0;
    for (; i < vec_count; i++) {
        __m128i d = _mm_loadu_si128((const __m128i *)&distances[i * 4]);
        __m128i x = zero;
        __m128i y = zero;
        __m128i s = one;

        for (uint32_t bit = 0; bit < order; bit++) {
            __m128i rx = _mm_and_si128(_mm_srli_epi32(d, 1), one);   /* (d>>1)&1 */
            __m128i ry = _mm_and_si128(_mm_xor_si128(d, rx), one);   /* (d^rx)&1 */
            __m128i ry_eq_0 = _mm_cmpeq_epi32(ry, zero);
            __m128i rx_eq_1 = _mm_cmpeq_epi32(rx, one);
            __m128i cond = _mm_and_si128(ry_eq_0, rx_eq_1);          /* ry==0 && rx==1 */
            __m128i s1 = _mm_sub_epi32(s, one);                      /* s-1 */

            /* x = cond ? (s-1-x) : x ; y = cond ? (s-1-y) : y */
            x = _mm_blendv_epi8(x, _mm_sub_epi32(s1, x), cond);
            y = _mm_blendv_epi8(y, _mm_sub_epi32(s1, y), cond);

            /* swap x,y when ry == 0 */
            __m128i temp = _mm_blendv_epi8(x, y, ry_eq_0);
            y = _mm_blendv_epi8(y, x, ry_eq_0);
            x = temp;

            /* x += s*rx ; y += s*ry */
            x = _mm_add_epi32(x, _mm_mullo_epi32(s, rx));
            y = _mm_add_epi32(y, _mm_mullo_epi32(s, ry));

            d = _mm_srli_epi32(d, 2);
            s = _mm_slli_epi32(s, 1);
        }

        /* Pack 4 x (int32) + 4 y (int32) -> 4 uint64 = (x<<32) | y. */
        __m128i x_lo = _mm_cvtepi32_epi64(x);                 /* x[0..1] -> int64 */
        __m128i x_hi = _mm_cvtepi32_epi64(_mm_srli_si128(x, 8)); /* x[2..3] */
        __m128i y_lo = _mm_cvtepu32_epi64(y);                 /* y[0..1] -> int64 */
        __m128i y_hi = _mm_cvtepu32_epi64(_mm_srli_si128(y, 8)); /* y[2..3] */
        __m128i o_lo = _mm_or_si128(_mm_slli_epi64(x_lo, 32), y_lo);
        __m128i o_hi = _mm_or_si128(_mm_slli_epi64(x_hi, 32), y_hi);
        _mm_storeu_si128((__m128i *)&packed[i * 4 + 0], o_lo);
        _mm_storeu_si128((__m128i *)&packed[i * 4 + 2], o_hi);
    }

    /* Scalar tail (count % 4). */
    for (size_t j = i * 4; j < count; j++) {
        uint32_t d = distances[j];
        uint32_t x = 0, y = 0, s = 1;
        for (uint32_t bit = 0; bit < order; bit++) {
            uint32_t rx = (d >> 1) & 1;
            uint32_t ry = (d ^ rx) & 1;
            if (ry == 0) {
                if (rx == 1) {
                    x = s - 1 - x;
                    y = s - 1 - y;
                }
                uint32_t t = x;
                x = y;
                y = t;
            }
            x += s * rx;
            y += s * ry;
            d >>= 2;
            s <<= 1;
        }
        packed[j] = ((uint64_t)x << 32) | (uint64_t)y;
    }
}

/* void hilbert_map_buffer_sse41(const unsigned char *src, unsigned char *dst,
 *                               const uint64_t *curve, size_t count,
 *                               uint32_t dimension)
 *
 * Byte-for-byte identical to hilbert_mapping_avx2.s, which already falls back
 * to scalar stores for the scatter (its SIMD prologue is discarded). We keep
 * only the scalar path.
 */
void hilbert_map_buffer_sse41(const unsigned char *src, unsigned char *dst,
                              const uint64_t *curve, size_t count,
                              uint32_t dimension) {
    for (size_t i = 0; i < count; i++) {
        uint64_t packed = curve[i];
        uint32_t x = (uint32_t)(packed >> 32);
        uint32_t y = (uint32_t)(packed & 0xFFFFFFFFu);
        dst[(size_t)y * dimension + x] = src[i];
    }
}
