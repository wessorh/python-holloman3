// arch_detect.c
// Architecture detection and function pointer initialization

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

// Function pointer types
typedef void (*hilbert_d2xy_batch_func)(const uint32_t *distances, uint64_t *packed,
                                        size_t count, uint32_t order);
typedef void (*hilbert_map_buffer_func)(const unsigned char *src, unsigned char *dst,
                                        const uint64_t *curve, size_t count,
                                        uint32_t dimension);

// External function declarations
#if defined(__x86_64__) || defined(_M_X64)
extern void hilbert_d2xy_batch_avx2(const uint32_t *distances, uint64_t *packed,
                                    size_t count, uint32_t order);
extern void hilbert_map_buffer_avx2_internal(const unsigned char *src, unsigned char *dst,
                                             const uint64_t *curve, size_t count,
                                             uint32_t dimension);
extern void hilbert_map_buffer_avx2_cache_blocking(const unsigned char *src,
                                                   unsigned char *dst,
                                                   const uint64_t *curve,
                                                   size_t count,
                                                   uint32_t dimension);
extern void hilbert_d2xy_batch_sse41(const uint32_t *distances, uint64_t *packed,
                                     size_t count, uint32_t order);
extern void hilbert_map_buffer_sse41(const unsigned char *src, unsigned char *dst,
                                     const uint64_t *curve, size_t count,
                                     uint32_t dimension);
#endif

// Global function pointers
hilbert_d2xy_batch_func g_hilbert_d2xy_batch_simd = NULL;
hilbert_map_buffer_func g_hilbert_map_buffer_simd = NULL;
hilbert_map_buffer_func g_hilbert_map_buffer_cache_blocking = NULL;

// Architecture detection
const char* hilbert_get_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#elif defined(__ARM_NEON) || defined(__arm__)
    return "armv7";
#else
    return "unknown";
#endif
}

// AVX2 (256-bit integer SIMD) capability detection.
int hilbert_has_avx2(void) {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
    // MSVC: __cpuid/__cpuidex + _xgetbv. Also verify the OS has enabled
    // the XMM/YMM register state (XCR0[2:1] == 0b11), which Windows requires
    // before AVX2 instructions can be used.
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, 0);
    if ((unsigned int)regs[0] < 7) {
        return 0;  // CPUID leaf 7 unavailable
    }
    __cpuidex(regs, 7, 0);
    if ((_xgetbv(0) & 0x6) != 0x6) {
        return 0;  // OS does not support AVX2 state
    }
    return (regs[1] & (1 << 5)) != 0;  // AVX2 = EBX bit 5
#else
    // gcc / clang / MinGW: __builtin_cpu_supports checks CPUID leaf 7 + OSXSAVE.
    return __builtin_cpu_supports("avx2") ? 1 : 0;
#endif
#else
    return 0;
#endif
}

// AVX1 (128-bit integer SIMD, 256-bit FP) capability detection. Covers CPUs
// that have AVX but not AVX2 (Sandy/Ivy Bridge, Bulldozer); the SSE4.1 path
// runs on these.
int hilbert_has_avx1(void) {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, 1);
    if (!(regs[2] & (1 << 27))) {
        return 0;  // OSXSAVE not set
    }
    if ((_xgetbv(0) & 0x6) != 0x6) {
        return 0;  // OS has not enabled XMM/YMM state
    }
    return (regs[2] & (1 << 28)) != 0;  // AVX = ECX bit 28
#else
    return __builtin_cpu_supports("avx") ? 1 : 0;
#endif
#else
    return 0;
#endif
}

// SIMD capability detection: true if the compiled SIMD path (AVX2 or AVX1) is
// available. On ARM this is always NEON (the NEON library is self-contained).
int hilbert_has_simd(void) {
#if defined(__aarch64__) || defined(__ARM_NEON) || defined(__arm__)
    // ARM NEON is always available on ARMv8 and can be checked on ARMv7
    #if defined(__aarch64__)
        return 1;  // NEON is mandatory on ARMv8
    #elif defined(__ARM_NEON)
        return 1;  // Compiled with NEON support
    #else
        return 0;
    #endif
#elif defined(__x86_64__) || defined(_M_X64)
    return hilbert_has_avx2() || hilbert_has_avx1();
#else
    return 0;
#endif
}

// Initialize function pointers based on architecture
void hilbert_init_arch(void) {
    if (!hilbert_has_simd()) {
        g_hilbert_d2xy_batch_simd = NULL;
        g_hilbert_map_buffer_simd = NULL;
        g_hilbert_map_buffer_cache_blocking = NULL;
        return;
    }
    
#if defined(__x86_64__) || defined(_M_X64)
    if (hilbert_has_avx2()) {
        g_hilbert_d2xy_batch_simd = hilbert_d2xy_batch_avx2;
        g_hilbert_map_buffer_simd = hilbert_map_buffer_avx2_internal;
        g_hilbert_map_buffer_cache_blocking = hilbert_map_buffer_avx2_cache_blocking;
    } else {
        // AVX1-only: 128-bit SSE4.1 path; no cache-blocking variant.
        g_hilbert_d2xy_batch_simd = hilbert_d2xy_batch_sse41;
        g_hilbert_map_buffer_simd = hilbert_map_buffer_sse41;
        g_hilbert_map_buffer_cache_blocking = NULL;
    }
#endif
}

// Get SIMD instruction set name
const char* hilbert_get_simd_name(void) {
    if (!hilbert_has_simd()) {
        return "none";
    }
    
#if defined(__x86_64__) || defined(_M_X64)
    return hilbert_has_avx2() ? "AVX2" : "AVX1 (SSE4.1)";
#elif defined(__aarch64__)
    return "NEON (ARMv8)";
#elif defined(__ARM_NEON) || defined(__arm__)
    return "NEON (ARMv7)";
#else
    return "unknown";
#endif
}