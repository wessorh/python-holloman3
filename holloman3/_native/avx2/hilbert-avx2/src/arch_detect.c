// arch_detect.c
// Architecture detection and function pointer initialization

#include <stddef.h>
#include <stdint.h>

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
#elif defined(__aarch64__) || defined(__ARM_NEON) || defined(__arm__)
extern void hilbert_d2xy_batch_neon(const uint32_t *distances, uint64_t *packed,
                                    size_t count, uint32_t order);
extern void hilbert_map_buffer_neon_internal(const unsigned char *src, unsigned char *dst,
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

// SIMD capability detection
int hilbert_has_simd(void) {
#if defined(__x86_64__) || defined(_M_X64)
    // Check for AVX2 support
    unsigned int eax, ebx, ecx, edx;
    
    // Check if CPUID is supported
    __asm__ __volatile__(
        "pushfq\n\t"
        "pushfq\n\t"
        "xorq $0x200000, (%%rsp)\n\t"
        "popfq\n\t"
        "pushfq\n\t"
        "popq %%rax\n\t"
        "xorq (%%rsp), %%rax\n\t"
        "popfq\n\t"
        "andq $0x200000, %%rax\n\t"
        : "=a" (eax)
        :
        : "cc"
    );
    
    if (eax == 0) {
        return 0;  // CPUID not supported
    }
    
    // Check for AVX2 (EBX bit 5 in leaf 7, sub-leaf 0)
    __asm__ __volatile__(
        "movl $7, %%eax\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "cpuid\n\t"
        : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
        : 
        : 
    );
    
    return (ebx & (1 << 5)) != 0;  // AVX2 bit
    
#elif defined(__aarch64__) || defined(__ARM_NEON) || defined(__arm__)
    // ARM NEON is always available on ARMv8 and can be checked on ARMv7
    #if defined(__aarch64__)
        return 1;  // NEON is mandatory on ARMv8
    #elif defined(__ARM_NEON)
        return 1;  // Compiled with NEON support
    #else
        // On ARMv7, we'd need to check /proc/cpuinfo or use runtime detection
        // For simplicity, assume NEON is available if compiled with it
        return 0;
    #endif
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
    g_hilbert_d2xy_batch_simd = hilbert_d2xy_batch_avx2;
    g_hilbert_map_buffer_simd = hilbert_map_buffer_avx2_internal;
    g_hilbert_map_buffer_cache_blocking = hilbert_map_buffer_avx2_cache_blocking;
#elif defined(__aarch64__) || defined(__ARM_NEON) || defined(__arm__)
    g_hilbert_d2xy_batch_simd = hilbert_d2xy_batch_neon;
    g_hilbert_map_buffer_simd = hilbert_map_buffer_neon_internal;
    g_hilbert_map_buffer_cache_blocking = NULL;  // Not implemented for ARM yet
#endif
}

// Get SIMD instruction set name
const char* hilbert_get_simd_name(void) {
    if (!hilbert_has_simd()) {
        return "none";
    }
    
#if defined(__x86_64__) || defined(_M_X64)
    return "AVX2";
#elif defined(__aarch64__)
    return "NEON (ARMv8)";
#elif defined(__ARM_NEON) || defined(__arm__)
    return "NEON (ARMv7)";
#else
    return "unknown";
#endif
}