/**
 * @file test_ccontainer_edge_cases.c
 * @brief Edge case tests for rgw_carray, rgw_cstring, and rgw_coptional
 * 
 * This test suite focuses on edge cases and boundary conditions
 * to find potential bugs in the C container implementations.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include "../include/containers/rgw_carray.h"
#include "../include/containers/rgw_cstring.h"
#include "../include/containers/rgw_coptional.h"

#define TEST_PASSED(name) printf("[PASS] %s\n", name)
#define TEST_FAILED(name, msg) do { \
    printf("[FAIL] %s: %s\n", name, msg); \
    return 1; \
} while(0)

/* ==================== rgw_carray Edge Cases ==================== */

int test_carray_null_params(void) {
    printf("\n--- Testing rgw_carray NULL parameters ---\n");
    
    /* Test append with NULL array */
    if (rgw_array_append(NULL, "test", 4) != -EINVAL) {
        TEST_FAILED("carray append NULL array", "should return -EINVAL");
    }
    
    /* Test append with NULL data */
    rgw_array_t *arr = rgw_array_create(0);
    if (rgw_array_append(arr, NULL, 4) != -EINVAL) {
        TEST_FAILED("carray append NULL data", "should return -EINVAL");
    }
    rgw_array_destroy(arr);
    
    TEST_PASSED("carray NULL parameters");
    return 0;
}

int test_carray_boundary_index(void) {
    printf("\n--- Testing rgw_carray boundary indices ---\n");
    
    rgw_array_t *arr = rgw_array_create(0);
    
    /* Insert at the beginning (index 0) */
    if (rgw_array_insert(arr, 0, "first", 5) != 0) {
        TEST_FAILED("carray insert at 0", "should succeed");
    }
    
    /* Insert at the end (index = size) */
    if (rgw_array_insert(arr, 1, "second", 6) != 0) {
        TEST_FAILED("carray insert at end", "should succeed");
    }
    
    /* Insert beyond size should fail */
    if (rgw_array_insert(arr, 10, "invalid", 7) != -EINVAL) {
        TEST_FAILED("carray insert beyond size", "should return -EINVAL");
    }
    
    /* Erase at valid index */
    if (rgw_array_erase(arr, 0) != 0) {
        TEST_FAILED("carray erase at 0", "should succeed");
    }
    
    /* Erase beyond size should fail */
    if (rgw_array_erase(arr, 100) != -EINVAL) {
        TEST_FAILED("carray erase beyond size", "should return -EINVAL");
    }
    
    /* Get beyond size should return NULL */
    uint32_t len = 0;
    if (rgw_array_get(arr, 100, &len) != NULL) {
        TEST_FAILED("carray get beyond size", "should return NULL");
    }
    
    rgw_array_destroy(arr);
    TEST_PASSED("carray boundary indices");
    return 0;
}

int test_carray_resize(void) {
    printf("\n--- Testing rgw_carray resize ---\n");
    
    rgw_array_t *arr = rgw_array_create(0);
    uint32_t len = 0;
    
    /* Resize to larger */
    const char *data = "test";
    if (rgw_array_resize(arr, 5, data, 4) != 0) {
        TEST_FAILED("carray resize larger", "should succeed");
    }
    if (rgw_array_size(arr) != 5) {
        TEST_FAILED("carray resize larger size", "size should be 5");
    }
    
    /* Resize to smaller */
    if (rgw_array_resize(arr, 2, NULL, 0) != 0) {
        TEST_FAILED("carray resize smaller", "should succeed");
    }
    if (rgw_array_size(arr) != 2) {
        TEST_FAILED("carray resize smaller size", "size should be 2");
    }
    
    /* Resize to zero */
    if (rgw_array_resize(arr, 0, NULL, 0) != 0) {
        TEST_FAILED("carray resize to zero", "should succeed");
    }
    if (rgw_array_size(arr) != 0) {
        TEST_FAILED("carray resize to zero size", "size should be 0");
    }
    
    /* Resize with default value */
    if (rgw_array_resize(arr, 3, "X", 1) != 0) {
        TEST_FAILED("carray resize with default", "should succeed");
    }
    
    const void *elem = rgw_array_get(arr, 0, &len);
    if (memcmp(elem, "X", 1) != 0) {
        TEST_FAILED("carray resize default value", "default value should be X");
    }
    
    rgw_array_destroy(arr);
    TEST_PASSED("carray resize");
    return 0;
}

int test_carray_capacity_growth(void) {
    printf("\n--- Testing rgw_carray capacity growth ---\n");
    
    rgw_array_t *arr = rgw_array_create(2);  /* Initial capacity 2 */
    
    size_t cap1 = rgw_array_capacity(arr);
    
    /* Add more elements to trigger growth */
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "element%d", i);
        rgw_array_append(arr, buf, (uint32_t)strlen(buf));
    }
    
    size_t cap2 = rgw_array_capacity(arr);
    
    if (cap2 <= cap1) {
        TEST_FAILED("carray capacity growth", "capacity should grow");
    }
    
    if (rgw_array_size(arr) != 10) {
        TEST_FAILED("carray size after growth", "size should be 10");
    }
    
    /* Test reserve */
    if (rgw_array_reserve(arr, 100) != 0) {
        TEST_FAILED("carray reserve", "reserve should succeed");
    }
    
    if (rgw_array_capacity(arr) < 100) {
        TEST_FAILED("carray reserve capacity", "capacity should be at least 100");
    }
    
    /* Test reserve with smaller value - should be no-op */
    size_t cap_before = rgw_array_capacity(arr);
    if (rgw_array_reserve(arr, 50) != 0) {
        TEST_FAILED("carray reserve smaller", "should succeed");
    }
    
    if (rgw_array_capacity(arr) != cap_before) {
        TEST_FAILED("carray reserve smaller noop", "capacity should not decrease");
    }
    
    rgw_array_destroy(arr);
    TEST_PASSED("carray capacity growth");
    return 0;
}

int test_carray_clear_destroy(void) {
    printf("\n--- Testing rgw_carray clear and destroy ---\n");
    
    /* Test clear */
    rgw_array_t *arr = rgw_array_create(0);
    rgw_array_append(arr, "test1", 5);
    rgw_array_append(arr, "test2", 5);
    
    if (rgw_array_size(arr) != 2) {
        TEST_FAILED("carray before clear", "should have 2 elements");
    }
    
    rgw_array_clear(arr);
    
    if (!rgw_array_empty(arr)) {
        TEST_FAILED("carray after clear", "should be empty");
    }
    
    if (rgw_array_capacity(arr) == 0) {
        /* This is expected - clear doesn't free capacity */
        printf("  (Note: clear preserves capacity)\n");
    }
    
    /* After clear, we should be able to use the array again */
    rgw_array_append(arr, "new", 3);
    if (rgw_array_size(arr) != 1) {
        TEST_FAILED("carray reuse after clear", "should have 1 element");
    }
    
    rgw_array_destroy(arr);
    
    /* Test destroy with NULL */
    rgw_array_destroy(NULL);  /* Should not crash */
    
    TEST_PASSED("carray clear and destroy");
    return 0;
}

/* ==================== rgw_cstring Edge Cases ==================== */

int test_cstring_null_params(void) {
    printf("\n--- Testing rgw_cstring NULL parameters ---\n");
    
    /* Test create with NULL */
    rgw_string_t *str = rgw_string_create(NULL);
    if (!str) {
        TEST_FAILED("cstring create NULL", "should create empty string");
    }
    if (rgw_string_length(str) != 0) {
        TEST_FAILED("cstring create NULL length", "length should be 0");
    }
    rgw_string_destroy(str);
    
    /* Test append with NULL */
    str = rgw_string_create("test");
    if (rgw_string_append(str, NULL) != -EINVAL) {
        TEST_FAILED("cstring append NULL", "should return -EINVAL");
    }
    rgw_string_destroy(str);
    
    /* Test assign with NULL */
    str = rgw_string_create("test");
    if (rgw_string_assign(str, NULL) != -EINVAL) {
        TEST_FAILED("cstring assign NULL", "should return -EINVAL");
    }
    rgw_string_destroy(str);
    
    TEST_PASSED("cstring NULL parameters");
    return 0;
}

int test_cstring_boundary_operations(void) {
    printf("\n--- Testing rgw_cstring boundary operations ---\n");
    
    rgw_string_t *str = rgw_string_create("hello");
    
    /* Insert at beginning */
    if (rgw_string_insert(str, 0, "[") != 0) {
        TEST_FAILED("cstring insert at 0", "should succeed");
    }
    
    /* Insert at end */
    if (rgw_string_insert(str, rgw_string_length(str), "]") != 0) {
        TEST_FAILED("cstring insert at end", "should succeed");
    }
    
    /* Insert beyond length should fail */
    if (rgw_string_insert(str, 100, "x") != -EINVAL) {
        TEST_FAILED("cstring insert beyond length", "should return -EINVAL");
    }
    
    /* Erase more than length */
    if (rgw_string_erase(str, 0, 100) != 0) {
        TEST_FAILED("cstring erase more than length", "should succeed");
    }
    if (rgw_string_length(str) != 0) {
        TEST_FAILED("cstring erase result", "should be empty");
    }
    
    rgw_string_destroy(str);
    
    /* Test replace beyond length */
    str = rgw_string_create("abc");
    if (rgw_string_replace(str, 5, 2, "x") != -EINVAL) {
        TEST_FAILED("cstring replace beyond length", "should return -EINVAL");
    }
    rgw_string_destroy(str);
    
    TEST_PASSED("cstring boundary operations");
    return 0;
}

int test_cstring_with_null_chars(void) {
    printf("\n--- Testing rgw_cstring with embedded null chars ---\n");
    
    /* Create string with embedded null */
    char data[] = {'a', '\0', 'b', 'c'};
    rgw_string_t *str = rgw_string_create_from_data(data, 4);
    
    if (rgw_string_length(str) != 4) {
        TEST_FAILED("cstring embedded null length", "length should be 4");
    }
    
    /* c_str should still be null-terminated */
    const char *cstr = rgw_string_c_str(str);
    if (cstr[0] != 'a' || cstr[1] != '\0') {
        TEST_FAILED("cstring c_str", "c_str should match");
    }
    
    /* data() should return raw data without guarantee of null terminator */
    const void *raw = rgw_string_data(str);
    if (memcmp(raw, data, 4) != 0) {
        TEST_FAILED("cstring data", "data should match");
    }
    
    rgw_string_destroy(str);
    TEST_PASSED("cstring with embedded null chars");
    return 0;
}

int test_cstring_format_append(void) {
    printf("\n--- Testing rgw_cstring format append ---\n");
    
    rgw_string_t *str = rgw_string_create("Value: ");
    
    if (rgw_string_append_format(str, "%d %s", 42, "test") != 0) {
        TEST_FAILED("cstring format append", "should succeed");
    }
    
    if (rgw_string_compare_cstr(str, "Value: 42 test") != 0) {
        TEST_FAILED("cstring format append result", 
                    "should be 'Value: 42 test'");
    }
    
    /* Test with NULL format */
    if (rgw_string_append_format(NULL, "test") != -EINVAL) {
        TEST_FAILED("cstring format NULL", "should return -EINVAL");
    }
    
    rgw_string_destroy(str);
    TEST_PASSED("cstring format append");
    return 0;
}

int test_cstring_compare_edge_cases(void) {
    printf("\n--- Testing rgw_cstring compare edge cases ---\n");
    
    /* Compare with NULL */
    rgw_string_t *str1 = rgw_string_create("test");
    if (rgw_string_compare(str1, NULL) != 1) {
        TEST_FAILED("cstring compare with NULL", "should return 1");
    }
    if (rgw_string_compare(NULL, str1) != -1) {
        TEST_FAILED("cstring compare NULL first", "should return -1");
    }
    if (rgw_string_compare(NULL, NULL) != 0) {
        TEST_FAILED("cstring compare both NULL", "should return 0");
    }
    rgw_string_destroy(str1);
    
    /* Compare empty strings */
    rgw_string_t *empty1 = rgw_string_create("");
    rgw_string_t *empty2 = rgw_string_create(NULL);
    
    if (rgw_string_compare(empty1, empty2) != 0) {
        TEST_FAILED("cstring compare empty", "should be equal");
    }
    rgw_string_destroy(empty1);
    rgw_string_destroy(empty2);
    
    /* Compare with C string NULL */
    str1 = rgw_string_create("test");
    if (rgw_string_compare_cstr(str1, NULL) != 1) {
        TEST_FAILED("cstring compare_cstr NULL", "should return 1");
    }
    if (rgw_string_compare_cstr(NULL, "test") != -1) {
        TEST_FAILED("cstring compare_cstr NULL first", "should return -1");
    }
    rgw_string_destroy(str1);
    
    TEST_PASSED("cstring compare edge cases");
    return 0;
}

int test_cstring_find_edge_cases(void) {
    printf("\n--- Testing rgw_cstring find edge cases ---\n");
    
    rgw_string_t *str = rgw_string_create("hello world hello");
    
    /* Find empty string - should return 0 by convention */
    size_t pos = rgw_string_find(str, "");
    printf("  Find empty string: %zu (note: may vary)\n", pos);
    
    /* Find string not in content */
    pos = rgw_string_find(str, "xyz");
    if (pos != (size_t)-1) {
        TEST_FAILED("cstring find not found", "should return -1");
    }
    
    /* Find with NULL substring */
    pos = rgw_string_find(str, NULL);
    if (pos != (size_t)-1) {
        TEST_FAILED("cstring find NULL", "should return -1");
    }
    
    /* Find single char */
    pos = rgw_string_find_char(str, 'o');
    if (pos != 4) {  /* First 'o' in "hello" */
        TEST_FAILED("cstring find char", "should find 'o' at position 4");
    }
    
    /* Find char not in string */
    pos = rgw_string_find_char(str, 'z');
    if (pos != (size_t)-1) {
        TEST_FAILED("cstring find char not found", "should return -1");
    }
    
    rgw_string_destroy(str);
    TEST_PASSED("cstring find edge cases");
    return 0;
}

/* ==================== rgw_coptional Edge Cases ==================== */

int test_coptional_null_params(void) {
    printf("\n--- Testing rgw_coptional NULL parameters ---\n");
    
    /* Test set with NULL */
    rgw_optional_t opt = {0};
    if (rgw_optional_set(&opt, NULL, 10) != -EINVAL) {
        TEST_FAILED("coptional set NULL", "should return -EINVAL");
    }
    
    /* Test set with zero length */
    if (rgw_optional_set(&opt, "test", 0) != -EINVAL) {
        TEST_FAILED("coptional set zero len", "should return -EINVAL");
    }
    
    /* Test with NULL optional */
    if (rgw_optional_set(NULL, "test", 4) != -EINVAL) {
        TEST_FAILED("coptional set NULL opt", "should return -EINVAL");
    }
    
    TEST_PASSED("coptional NULL parameters");
    return 0;
}

int test_coptional_string_include_null(void) {
    printf("\n--- Testing rgw_coptional string with embedded null ---\n");
    
    rgw_optional_t opt = {0};
    
    /* Set a string value - this includes the null terminator! */
    if (rgw_optional_set_string(&opt, "hello") != 0) {
        TEST_FAILED("coptional set string", "should succeed");
    }
    
    /* Check the length - should include null terminator */
    uint32_t len = 0;
    const void *val = rgw_optional_get(&opt, &len);
    
    printf("  Stored value: %s, length: %u (includes null terminator)\n", (const char*)val, len);
    printf("  Expected: 6 ('hello' + '\\0')\n");
    
    if (len != 6) {
        printf("  WARNING: Length is %u, expected 6 (with null terminator)\n", len);
    }
    
    /* Get string should still work */
    const char *str = rgw_optional_get_string(&opt);
    if (strcmp(str, "hello") != 0) {
        TEST_FAILED("coptional get string", "should return 'hello'");
    }
    
    rgw_optional_destroy(&opt);
    TEST_PASSED("coptional string with embedded null");
    return 0;
}

int test_coptional_copy_move_weak(void) {
    printf("\n--- Testing rgw_coptional copy and move ---\n");
    
    /* Test copy to destination that already has a value */
    rgw_optional_t opt1 = {0};
    rgw_optional_set(&opt1, "value1", 7);
    
    rgw_optional_t opt2 = {0};
    rgw_optional_set(&opt2, "value2", 7);
    
    /* Copy should replace the old value */
    if (rgw_optional_copy(&opt1, &opt2) != 0) {
        TEST_FAILED("coptional copy", "should succeed");
    }
    
    if (!rgw_optional_equals(&opt1, &opt2)) {
        TEST_FAILED("coptional copy equals", "should be equal");
    }
    
    /* Original opt2 should still have value */
    if (!rgw_optional_has_value(&opt2)) {
        TEST_FAILED("coptional copy source", "source should still have value");
    }
    
    rgw_optional_destroy(&opt1);
    rgw_optional_destroy(&opt2);
    
    /* Test copy from empty optional */
    rgw_optional_t opt3 = {0};
    rgw_optional_set(&opt3, "value", 6);
    
    rgw_optional_t opt4 = {0};
    
    if (rgw_optional_copy(&opt3, &opt4) != 0) {
        TEST_FAILED("coptional copy from empty", "should succeed");
    }
    
    if (rgw_optional_has_value(&opt3)) {
        TEST_FAILED("coptional copy from empty source", "source should be empty");
    }
    
    rgw_optional_destroy(&opt3);
    rgw_optional_destroy(&opt4);
    
    TEST_PASSED("coptional copy and move");
    return 0;
}

int test_coptional_swap_with_null(void) {
    printf("\n--- Testing rgw_coptional swap with empty ---\n");
    
    rgw_optional_t opt1 = {0};
    rgw_optional_set(&opt1, "value", 6);
    
    rgw_optional_t opt2 = {0};  /* Empty */
    
    /* Swap - opt1 should become empty, opt2 should have value */
    rgw_optional_swap(&opt1, &opt2);
    
    if (rgw_optional_has_value(&opt1)) {
        TEST_FAILED("coptional swap result 1", "opt1 should be empty");
    }
    
    if (!rgw_optional_has_value(&opt2)) {
        TEST_FAILED("coptional swap result 2", "opt2 should have value");
    }
    
    /* Verify the value */
    const char *str = rgw_optional_get_string(&opt2);
    if (strcmp(str, "value") != 0) {
        TEST_FAILED("coptional swap value", "value should be 'value'");
    }
    
    rgw_optional_destroy(&opt1);
    rgw_optional_destroy(&opt2);
    
    /* Test swap with NULL pointers - should not crash */
    rgw_optional_swap(NULL, NULL);
    rgw_optional_swap(&opt1, NULL);
    rgw_optional_swap(NULL, &opt2);
    
    TEST_PASSED("coptional swap with empty");
    return 0;
}

int test_coptional_reassign(void) {
    printf("\n--- Testing rgw_coptional reassign value ---\n");
    
    rgw_optional_t opt = {0};
    
    /* Set first value */
    rgw_optional_set(&opt, "first", 6);
    if (!rgw_optional_has_value(&opt)) {
        TEST_FAILED("coptional reassign first", "should have first value");
    }
    
    /* Set second value - should replace first */
    rgw_optional_set(&opt, "second", 7);
    
    const char *val = rgw_optional_get_string(&opt);
    
    if (strcmp(val, "second") != 0) {
        TEST_FAILED("coptional reassign second", "should have second value");
    }
    
    rgw_optional_destroy(&opt);
    
    TEST_PASSED("coptional reassign value");
    return 0;
}

/* ==================== Main ==================== */

int main(void) {
    printf("========================================\n");
    printf("  C Container Edge Case Tests\n");
    printf("========================================\n");
    
    int failed = 0;
    
    /* rgw_carray tests */
    failed += test_carray_null_params();
    failed += test_carray_boundary_index();
    failed += test_carray_resize();
    failed += test_carray_capacity_growth();
    failed += test_carray_clear_destroy();
    
    /* rgw_cstring tests */
    failed += test_cstring_null_params();
    failed += test_cstring_boundary_operations();
    failed += test_cstring_with_null_chars();
    failed += test_cstring_format_append();
    failed += test_cstring_compare_edge_cases();
    failed += test_cstring_find_edge_cases();
    
    /* rgw_coptional tests */
    failed += test_coptional_null_params();
    failed += test_coptional_string_include_null();
    failed += test_coptional_copy_move_weak();
    failed += test_coptional_swap_with_null();
    failed += test_coptional_reassign();
    
    printf("\n========================================\n");
    if (failed == 0) {
        printf("  All tests passed!\n");
    } else {
        printf("  %d test(s) failed!\n", failed);
    }
    printf("========================================\n");
    
    return failed;
}
