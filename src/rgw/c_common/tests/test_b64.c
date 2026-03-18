/**
 * @file test_b64.c
 * @brief RGW Base64 编解码功能测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../include/rgw_b64.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf("✓ PASSED\n"); \
} while(0)

#define ASSERT(cond) do { if (!(cond)) { \
    printf("✗ FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    exit(1); \
} } while(0)

// Helper function to print binary data as hex
static void print_hex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
}

TEST(test_b64_encode_basic)
{
    // Test encoding "Hello"
    const char *input = "Hello";
    uint8_t *output = NULL;
    
    int ret = rgw_b64_encode((const uint8_t *)input, strlen(input), (char **)&output);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(output != NULL);
    ASSERT(strcmp((char *)output, "SGVsbG8=") == 0);
    
    free(output);
}

TEST(test_b64_encode_empty)
{
    const char *input = "";
    uint8_t *output = NULL;
    
    int ret = rgw_b64_encode((const uint8_t *)input, 0, (char **)&output);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(output != NULL);
    ASSERT(strcmp((char *)output, "") == 0);
    
    free(output);
}

TEST(test_b64_encode_binary_data)
{
    // Test with binary data
    uint8_t binary[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    uint8_t *output = NULL;
    size_t output_len = 0;
    
    int ret = rgw_b64_encode_binary(binary, sizeof(binary), &output, &output_len);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(output != NULL);
    ASSERT(output_len == 8);  // 5 bytes -> 8 base64 chars
    
    // Expected: AAECAwQ=
    ASSERT(strcmp((char *)output, "AAECAwQ=") == 0);
    
    free(output);
}

TEST(test_b64_decode_basic)
{
    const char *encoded = "SGVsbG8=";
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    
    int ret = rgw_b64_decode(encoded, &decoded, &decoded_len);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(decoded != NULL);
    ASSERT(decoded_len == 5);
    ASSERT(memcmp(decoded, "Hello", 5) == 0);
    
    free(decoded);
}

TEST(test_b64_decode_empty)
{
    const char *encoded = "";
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    
    int ret = rgw_b64_decode(encoded, &decoded, &decoded_len);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(decoded != NULL);
    ASSERT(decoded_len == 0);
    
    free(decoded);
}

TEST(test_b64_roundtrip)
{
    const char *original = "This is a test string with special chars: !@#$%^&*()";
    uint8_t *encoded = NULL;
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    
    // Encode
    int ret = rgw_b64_encode((const uint8_t *)original, strlen(original), (char **)&encoded);
    ASSERT(ret == RGW_B64_OK);
    
    // Decode
    ret = rgw_b64_decode_binary(encoded, strlen((char *)encoded), &decoded, &decoded_len);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(decoded_len == strlen(original));
    ASSERT(memcmp(decoded, original, strlen(original)) == 0);
    
    free(encoded);
    free(decoded);
}

TEST(test_b64_decode_with_whitespace)
{
    // Test that whitespace is ignored
    const char *encoded_with_spaces = " SGVs bG8= \n";
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    
    int ret = rgw_b64_decode(encoded_with_spaces, &decoded, &decoded_len);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(decoded != NULL);
    ASSERT(decoded_len == 5);
    ASSERT(memcmp(decoded, "Hello", 5) == 0);
    
    free(decoded);
}

TEST(test_b64_length_calculations)
{
    // Test encoded length calculation
    ASSERT(rgw_b64_encoded_len(0) == 0);
    ASSERT(rgw_b64_encoded_len(1) == 4);
    ASSERT(rgw_b64_encoded_len(2) == 4);
    ASSERT(rgw_b64_encoded_len(3) == 4);
    ASSERT(rgw_b64_encoded_len(4) == 8);
    ASSERT(rgw_b64_encoded_len(5) == 8);
    
    // Test max decoded length calculation
    ASSERT(rgw_b64_decoded_len_max(0) == 0);
    ASSERT(rgw_b64_decoded_len_max(4) == 3);
    ASSERT(rgw_b64_decoded_len_max(8) == 6);
    ASSERT(rgw_b64_decoded_len_max(12) == 9);
}

TEST(test_b64_error_cases)
{
    // Test invalid input - NULL pointers
    ASSERT(rgw_b64_encode(NULL, 0, NULL) == RGW_B64_ERR_INVALID);
    ASSERT(rgw_b64_decode(NULL, NULL, NULL) == RGW_B64_ERR_INVALID);
    
    // Test invalid base64 string (wrong length)
    const char *invalid_b64 = "SGVsbG";  // Not multiple of 4
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    
    // This should fail or handle gracefully
    int ret = rgw_b64_decode(invalid_b64, &decoded, &decoded_len);
    // The implementation might handle this, just check it doesn't crash
    if (ret == RGW_B64_OK && decoded) {
        free(decoded);
    }
}

TEST(test_b64_various_padding)
{
    // Test different padding scenarios
    
    // No padding needed (multiple of 3)
    const char *input1 = "ABC";  // 3 bytes
    uint8_t *encoded1 = NULL;
    int ret = rgw_b64_encode((const uint8_t *)input1, 3, (char **)&encoded1);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(strcmp((char *)encoded1, "QUJD") == 0);  // No padding
    
    uint8_t *decoded1 = NULL;
    size_t decoded_len1 = 0;
    ret = rgw_b64_decode((char *)encoded1, &decoded1, &decoded_len1);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(decoded_len1 == 3);
    ASSERT(memcmp(decoded1, "ABC", 3) == 0);
    
    free(encoded1);
    free(decoded1);
    
    // One padding character
    const char *input2 = "AB";  // 2 bytes
    uint8_t *encoded2 = NULL;
    ret = rgw_b64_encode((const uint8_t *)input2, 2, (char **)&encoded2);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(strcmp((char *)encoded2, "QUI=") == 0);  // One '='
    
    uint8_t *decoded2 = NULL;
    size_t decoded_len2 = 0;
    ret = rgw_b64_decode((char *)encoded2, &decoded2, &decoded_len2);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(decoded_len2 == 2);
    ASSERT(memcmp(decoded2, "AB", 2) == 0);
    
    free(encoded2);
    free(decoded2);
    
    // Two padding characters
    const char *input3 = "A";  // 1 byte
    uint8_t *encoded3 = NULL;
    ret = rgw_b64_encode((const uint8_t *)input3, 1, (char **)&encoded3);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(strcmp((char *)encoded3, "QQ==") == 0);  // Two '='
    
    uint8_t *decoded3 = NULL;
    size_t decoded_len3 = 0;
    ret = rgw_b64_decode((char *)encoded3, &decoded3, &decoded_len3);
    ASSERT(ret == RGW_B64_OK);
    ASSERT(decoded_len3 == 1);
    ASSERT(memcmp(decoded3, "A", 1) == 0);
    
    free(encoded3);
    free(decoded3);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    printf("\n========================================\n");
    printf("RGW Base64 - Test Suite\n");
    printf("========================================\n\n");
    
    RUN_TEST(test_b64_encode_basic);
    RUN_TEST(test_b64_encode_empty);
    RUN_TEST(test_b64_encode_binary_data);
    RUN_TEST(test_b64_decode_basic);
    RUN_TEST(test_b64_decode_empty);
    RUN_TEST(test_b64_roundtrip);
    RUN_TEST(test_b64_decode_with_whitespace);
    RUN_TEST(test_b64_length_calculations);
    RUN_TEST(test_b64_error_cases);
    RUN_TEST(test_b64_various_padding);
    
    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("========================================\n\n");
    
    return (tests_passed == tests_run) ? 0 : 1;
}
