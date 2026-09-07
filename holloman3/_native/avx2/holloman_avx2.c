/**
 * @file holloman_avx2.c
 * @brief AVX2-optimized Holloman fingerprinting implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../common/holloman.h"
#include "hilbert-avx2/include/hilbert.h"
#include "hilbert-avx2/include/hilbert_external_buffer.h"

// External function from lanczos_avx2_optimized.c
extern void downsample_lanczos3_avx2(uint8_t output[16], const uint8_t *input, int size);

// Version string
const char* holloman_version(void) {
    return "3.0.2-avx2";
}

// Check if AVX2 is available
int holloman_is_avx2_available(void) {
    return hilbert_is_avx2_available();
}

// Read file into buffer
static uint8_t* read_file(const char *filename, size_t *file_size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return NULL;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    *file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Allocate buffer
    uint8_t *buffer = (uint8_t*)malloc(*file_size);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    
    // Read file
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

// Initialize Hilbert curve (call once at startup)
int holloman_init(uint32_t order) {
    // Check if AVX2 is available
    if (!hilbert_is_avx2_available()) {
        return -2;
    }
    
    // Initialize Hilbert curve with specified order
    int ret = hilbert_init(order);
    if (ret != HILBERT_OK) {
        return -3;
    }
    
    return 0;
}

// Clean up Hilbert curve cache
int holloman_cleanup(void) {
    return hilbert_clear_cache();
}

// Generate fingerprint from buffer
int holloman_fingerprint_buffer(const uint8_t *data, size_t size, uint8_t fingerprint[16]) {
    if (!data || size == 0 || !fingerprint) {
        return -1;
    }
    
    // Check if AVX2 is available
    if (!hilbert_is_avx2_available()) {
        return -2;
    }
    
    // Map buffer using Hilbert curve (must be initialized first)
    uint8_t *mapped_buffer = NULL;
    size_t mapped_len = 0;
    
    int ret = hilbert_map_buffer(data, size, &mapped_buffer, &mapped_len);
    if (ret != HILBERT_OK) {
        return ret;  // Pass through actual error code
    }
    
    // Calculate the dimension of the mapped buffer (it's a square)
    int dimension = (int)sqrt((double)mapped_len);
    
    // Verify it's actually a perfect square
    if (dimension * dimension != (int)mapped_len) {
        free(mapped_buffer);
        return -5;
    }
    
    // Downsample to 4x4 (16 bytes) using AVX2-optimized Lanczos-3
    uint8_t temp_result[16];
    downsample_lanczos3_avx2(temp_result, mapped_buffer, dimension);
    
    // Rotate 90 degrees clockwise
    rotate_matrix_90_clockwise(fingerprint, temp_result);
    
    // Cleanup
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
    
    // Check if AVX2 is available
    if (!hilbert_is_avx2_available()) {
        return -2;  // HILBERT_ERR_NO_AVX2
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
    
    // Downsample to 4x4 (16 bytes) using AVX2-optimized Lanczos-3
    uint8_t temp_result[16];
    downsample_lanczos3_avx2(temp_result, scratch_buffer, dimension);
    
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