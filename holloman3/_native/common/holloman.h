/**
 * @file holloman.h
 * @brief Holloman file fingerprinting library
 * 
 * Generates compact 16-byte fingerprints from arbitrary files using
 * Hilbert curve mapping and Lanczos-3 downsampling.
 * 
 * IMPORTANT: The AVX2 implementation requires AVX2 CPU support.
 * All functions will return error code -2 if AVX2 is not available.
 * There is NO fallback to portable C implementations in the AVX2 build.
 */

#ifndef HOLLOMAN_H
#define HOLLOMAN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Hilbert curve (call once at startup)
 * 
 * This function must be called before any fingerprinting operations.
 * It initializes the Hilbert curve with the specified order.
 * For general use, order 13 is recommended (supports files up to ~16MB efficiently).
 * 
 * @param order Hilbert curve order (recommended: 13)
 * @return 0 on success, negative error code on failure
 */
int holloman_init(uint32_t order);

/**
 * @brief Clean up Hilbert curve cache
 * 
 * Call this when done with all fingerprinting operations to free memory.
 * 
 * @return 0 on success, negative error code on failure
 */
int holloman_cleanup(void);

/**
 * @brief Generate fingerprint from file
 * 
 * @param filename Path to file
 * @param fingerprint Output buffer (must be 16 bytes)
 * @return 0 on success, negative error code on failure
 * 
 * @note holloman_init() must be called before using this function
 */
int holloman_fingerprint_file(const char *filename, uint8_t fingerprint[16]);

/**
 * @brief Generate fingerprint from buffer
 * 
 * @param data Input buffer
 * @param size Size of input buffer in bytes
 * @param fingerprint Output buffer (must be 16 bytes)
 * @return 0 on success, negative error code on failure
 * 
 * @note holloman_init() must be called before using this function
 */
int holloman_fingerprint_buffer(const uint8_t *data, size_t size, uint8_t fingerprint[16]);

/**
 * @brief Generate fingerprint from buffer using external scratch workspace
 * 
 * This function allows the caller to provide a pre-allocated scratch buffer
 * for the Hilbert mapping operation, avoiding internal memory allocation.
 * This is useful for:
 * - Memory-constrained environments
 * - Real-time systems requiring predictable memory usage
 * - High-performance scenarios where buffer reuse is beneficial
 * 
 * @param data Input buffer
 * @param size Size of input buffer in bytes
 * @param fingerprint Output buffer (must be 16 bytes)
 * @param scratch_buffer Pre-allocated scratch workspace for Hilbert mapping
 * @param scratch_size Size of scratch buffer in bytes
 * @return 0 on success, negative error code on failure
 *         -3 if scratch_buffer is NULL or scratch_size is too small
 *         -6 if not initialized (call holloman_init first)
 * 
 * @note holloman_init() must be called before using this function
 * @note Use hilbert_calculate_buffer_size() to determine required scratch_size
 * @note The scratch buffer can be reused across multiple calls for better performance
 * 
 * Example:
 * @code
 * holloman_init(13);
 * size_t scratch_size = hilbert_calculate_buffer_size(data_size);
 * uint8_t *scratch = malloc(scratch_size);
 * uint8_t fingerprint[16];
 * holloman_fingerprint_buffer_external(data, data_size, fingerprint, scratch, scratch_size);
 * free(scratch);
 * holloman_cleanup();
 * @endcode
 */
int holloman_fingerprint_buffer_external(
    const uint8_t *data, 
    size_t size, 
    uint8_t fingerprint[16],
    uint8_t *scratch_buffer,
    size_t scratch_size
);

/**
 * @brief Check if AVX2 is available (for AVX2 implementation)
 * 
 * @return 1 if AVX2 is available, 0 otherwise
 */
int holloman_is_avx2_available(void);

/**
 * @brief Get version string
 * 
 * @return Version string (e.g., "2.0.0")
 */
const char* holloman_version(void);

#ifdef __cplusplus
}
#endif

#endif /* HOLLOMAN_H */