/**
 * @file test_rgw_string_c.c
 * @brief rgw_string_c 功能测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "rgw_string_c.h"

#define TEST_PASSED printf("[PASS] %s\n", __func__)
#define TEST_FAILED(msg) do { printf("[FAIL] %s: %s\n", __func__, msg); return 1; } while(0)

/* Test rgw_str_casecmp */
int test_casecmp(void)
{
    if (rgw_str_casecmp("Hello", "hello") != 0) TEST_FAILED("casecmp should be equal");
    if (rgw_str_casecmp("Hello", "HELLO") != 0) TEST_FAILED("casecmp should be equal");
    if (rgw_str_casecmp("Hello", "world") == 0) TEST_FAILED("should not be equal");
    TEST_PASSED;
    return 0;
}

/* Test rgw_str_ncasecmp */
int test_ncasecmp(void)
{
    if (rgw_str_ncasecmp("Hello", "hell", 4) != 0) TEST_FAILED("ncasecmp should be equal");
    if (rgw_str_ncasecmp("Hello", "HELLO", 5) != 0) TEST_FAILED("ncasecmp should be equal");
    TEST_PASSED;
    return 0;
}

/* Test rgw_str_casecmp_with_offset */
int test_casecmp_offset(void)
{
    if (rgw_str_casecmp_with_offset("HelloWorld", 5, 5, "world") != 0) TEST_FAILED("offset cmp should be equal");
    if (rgw_str_casecmp_with_offset("HelloWorld", 5, 3, "WOR") != 0) TEST_FAILED("offset cmp should be equal");
    TEST_PASSED;
    return 0;
}

/* Test rgw_str_to_ll */
int test_to_ll(void)
{
    int64_t val;
    if (rgw_str_to_ll("12345", &val) != 0) TEST_FAILED("should convert");
    if (val != 12345) TEST_FAILED("wrong value");
    if (rgw_str_to_ll("abc", &val) == 0) TEST_FAILED("should fail on invalid");
    TEST_PASSED;
    return 0;
}

/* Test rgw_str_to_ull */
int test_to_ull(void)
{
    uint64_t val;
    if (rgw_str_to_ull("99999999999", &val) != 0) TEST_FAILED("should convert");
    if (val != 99999999999ULL) TEST_FAILED("wrong value");
    TEST_PASSED;
    return 0;
}

/* Test rgw_str_match_wildcards */
int test_match_wildcards(void)
{
    if (!rgw_str_match_wildcards("*.txt", "file.txt", 0)) TEST_FAILED("should match");
    if (!rgw_str_match_wildcards("test?.dat", "test1.dat", 0)) TEST_FAILED("should match");
    if (rgw_str_match_wildcards("*.txt", "file.pdf", 0)) TEST_FAILED("should not match");
    if (!rgw_str_match_wildcards("*", "anything", 0)) TEST_FAILED("* should match anything");
    TEST_PASSED;
    return 0;
}

/* Test rgw_str_dup */
int test_str_dup(void)
{
    char *s = rgw_str_dup("test");
    if (s == NULL) TEST_FAILED("dup failed");
    if (strcmp(s, "test") != 0) TEST_FAILED("wrong value");
    free(s);

    /* Test NULL input */
    if (rgw_str_dup(NULL) != NULL) TEST_FAILED("should return NULL for NULL input");

    TEST_PASSED;
    return 0;
}

/* Test rgw_str_dup_n */
int test_str_dup_n(void)
{
    char *s = rgw_str_dup_n("hello", 3);
    if (s == NULL) TEST_FAILED("dup_n failed");
    if (strcmp(s, "hel") != 0) TEST_FAILED("wrong value");
    free(s);

    /* Test with longer length */
    s = rgw_str_dup_n("hi", 10);
    if (s == NULL) TEST_FAILED("dup_n failed");
    if (strcmp(s, "hi") != 0) TEST_FAILED("wrong value");
    free(s);

    TEST_PASSED;
    return 0;
}

/* Test rgw_str_case */
int test_str_case(void)
{
    char *lower = rgw_str_case("Hello", RGW_STR_CASE_LOWER);
    if (lower == NULL) TEST_FAILED("case failed");
    if (strcmp(lower, "hello") != 0) TEST_FAILED("wrong lower value");
    free(lower);

    char *upper = rgw_str_case("Hello", RGW_STR_CASE_UPPER);
    if (upper == NULL) TEST_FAILED("case failed");
    if (strcmp(upper, "HELLO") != 0) TEST_FAILED("wrong upper value");
    free(upper);

    TEST_PASSED;
    return 0;
}

/* Test rgw_str_trim */
int test_str_trim(void)
{
    char *trimmed = rgw_str_trim("  hello  ", RGW_STR_TRIM_BOTH);
    if (trimmed == NULL) TEST_FAILED("trim failed");
    if (strcmp(trimmed, "hello") != 0) TEST_FAILED("wrong trim value");
    free(trimmed);

    /* Test left trim */
    trimmed = rgw_str_trim("  hello", RGW_STR_TRIM_LEFT);
    if (trimmed == NULL) TEST_FAILED("trim left failed");
    if (strcmp(trimmed, "hello") != 0) TEST_FAILED("wrong left trim value");
    free(trimmed);

    /* Test right trim */
    trimmed = rgw_str_trim("hello  ", RGW_STR_TRIM_RIGHT);
    if (trimmed == NULL) TEST_FAILED("trim right failed");
    if (strcmp(trimmed, "hello") != 0) TEST_FAILED("wrong right trim value");
    free(trimmed);

    TEST_PASSED;
    return 0;
}

/* Test rgw_str_starts_with */
int test_starts_with(void)
{
    if (!rgw_str_starts_with("hello world", "hello")) TEST_FAILED("should start with");
    if (rgw_str_starts_with("hello", "world")) TEST_FAILED("should not start with");
    if (!rgw_str_starts_with("test", "test")) TEST_FAILED("should start with exact");
    TEST_PASSED;
    return 0;
}

/* Test rgw_str_ends_with */
int test_ends_with(void)
{
    if (!rgw_str_ends_with("hello world", "world")) TEST_FAILED("should end with");
    if (rgw_str_ends_with("hello", "world")) TEST_FAILED("should not end with");
    if (!rgw_str_ends_with("test", "test")) TEST_FAILED("should end with exact");
    TEST_PASSED;
    return 0;
}

/* Test rgw_str_replace */
int test_str_replace(void)
{
    char *result = rgw_str_replace("hello world", "world", "c");
    if (result == NULL) TEST_FAILED("replace failed");
    if (strcmp(result, "hello c") != 0) TEST_FAILED("wrong replace value");
    free(result);

    /* Test no match */
    result = rgw_str_replace("hello", "xyz", "abc");
    if (result == NULL) TEST_FAILED("replace no match failed");
    if (strcmp(result, "hello") != 0) TEST_FAILED("should be unchanged");
    free(result);

    TEST_PASSED;
    return 0;
}

/* Test rgw_str_size_calc */
int test_size_calc(void)
{
    size_t total = rgw_str_size_calc("hello", "world", NULL);
    if (total != 10) TEST_FAILED("wrong size");

    TEST_PASSED;
    return 0;
}

/* Test rgw_str_cat */
int test_str_cat(void)
{
    char buf[64];
    size_t written = rgw_str_cat(buf, sizeof(buf), "hello", " ", "world", NULL);
    if (written != 11) TEST_FAILED("wrong written count");
    if (strcmp(buf, "hello world") != 0) TEST_FAILED("wrong cat result");

    TEST_PASSED;
    return 0;
}

/* Test rgw_str_join */
int test_str_join(void)
{
    char buf[64];
    size_t written = rgw_str_join(',', buf, sizeof(buf), "a", "b", "c", NULL);
    if (written != 5) TEST_FAILED("wrong written count");
    if (strcmp(buf, "a,b,c") != 0) TEST_FAILED("wrong join result");

    TEST_PASSED;
    return 0;
}

/* Test RGW_SARRLEN macro */
int test_sarrlen(void)
{
    if (RGW_SARRLEN("hello") != 5) TEST_FAILED("wrong sarrlen");
    if (RGW_SARRLEN("") != 0) TEST_FAILED("wrong sarrlen for empty");

    TEST_PASSED;
    return 0;
}

int main(void)
{
    printf("=== Testing rgw_string_c ===\n\n");

    int failed = 0;

    failed += test_casecmp();
    failed += test_ncasecmp();
    failed += test_casecmp_offset();
    failed += test_to_ll();
    failed += test_to_ull();
    failed += test_match_wildcards();
    failed += test_str_dup();
    failed += test_str_dup_n();
    failed += test_str_case();
    failed += test_str_trim();
    failed += test_starts_with();
    failed += test_ends_with();
    failed += test_str_replace();
    failed += test_size_calc();
    failed += test_str_cat();
    failed += test_str_join();
    failed += test_sarrlen();

    printf("\n=== Results: %d tests failed ===\n", failed);
    return failed;
}
