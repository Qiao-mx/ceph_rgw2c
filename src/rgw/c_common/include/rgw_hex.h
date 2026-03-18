#ifndef RGW_HEX_H
#define RGW_HEX_H

#include <stddef.h>
#include <stdbool.h>

/**
 * Encode binary data to hexadecimal string
 * @param input Input binary data
 * @param len Length of input data
 * @param output Output buffer (must be at least len * 2 + 1 bytes)
 * @return 0 on success, negative on error
 */
int rgw_hex_encode(const void* input, size_t len, char* output);

/**
 * Decode hexadecimal string to binary data
 * @param input Input hexadecimal string
 * @param output Output buffer (must be at least strlen(input) / 2 bytes)
 * @param len Pointer to output length (in/out)
 * @return 0 on success, negative on error
 */
int rgw_hex_decode(const char* input, void* output, size_t* len);

/**
 * Encode binary data to hexadecimal string (with uppercase letters)
 * @param input Input binary data
 * @param len Length of input data
 * @param output Output buffer (must be at least len * 2 + 1 bytes)
 * @return 0 on success, negative on error
 */
int rgw_hex_encode_upper(const void* input, size_t len, char* output);

/**
 * Calculate the length of hex-encoded data
 * @param input_len Length of binary data
 * @return Required output buffer length (input_len * 2)
 */
size_t rgw_hex_encoded_length(size_t input_len);

/**
 * Calculate the maximum length of hex-decoded data
 * @param hex_len Length of hexadecimal string
 * @return Maximum output buffer length (hex_len / 2)
 */
size_t rgw_hex_decoded_length(size_t hex_len);

/**
 * Check if a character is a valid hexadecimal digit
 * @param c Character to check
 * @return true if valid hex digit, false otherwise
 */
bool rgw_hex_is_valid_char(char c);

#endif // RGW_HEX_H
