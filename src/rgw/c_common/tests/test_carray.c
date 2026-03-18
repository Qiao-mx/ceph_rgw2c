#include <stdio.h>
#include <string.h>
#include "../include/containers/rgw_carray.h"

int main(void) {
    printf("Testing rgw_carray (std::vector alternative)...\n");
    
    rgw_array_t *arr = rgw_array_create(0);
    if (!arr) { printf("FAIL: array create\n"); return 1; }
    printf("✓ Array created\n");
    
    const char *data1 = "hello";
    if (rgw_array_append(arr, data1, (uint32_t)strlen(data1)) != 0) {
        printf("FAIL: array append\n"); return 1;
    }
    printf("✓ Appended element 1\n");
    
    const char *data2 = "world";
    if (rgw_array_append(arr, data2, (uint32_t)strlen(data2)) != 0) {
        printf("FAIL: array append 2\n"); return 1;
    }
    printf("✓ Appended element 2\n");
    
    if (rgw_array_size(arr) != 2) {
        printf("FAIL: array size\n"); return 1;
    }
    printf("✓ Size check: %zu\n", rgw_array_size(arr));
    
    uint32_t len = 0;
    const void *elem = rgw_array_get(arr, 0, &len);
    if (!elem || len != 5 || memcmp(elem, "hello", 5) != 0) {
        printf("FAIL: array get\n"); return 1;
    }
    printf("✓ Get element 0: %.*s\n", len, (const char*)elem);
    
    elem = rgw_array_get(arr, 1, &len);
    if (!elem || len != 5 || memcmp(elem, "world", 5) != 0) {
        printf("FAIL: array get 2\n"); return 1;
    }
    printf("✓ Get element 1: %.*s\n", len, (const char*)elem);
    
    if (rgw_array_empty(arr)) {
        printf("FAIL: array empty\n"); return 1;
    }
    printf("✓ Array is not empty\n");
    
    if (rgw_array_insert(arr, 1, "插入", (uint32_t)strlen("插入")) != 0) {
        printf("FAIL: array insert\n"); return 1;
    }
    printf("✓ Insert at position 1\n");
    
    if (rgw_array_size(arr) != 3) {
        printf("FAIL: size after insert\n"); return 1;
    }
    printf("✓ Size after insert: %zu\n", rgw_array_size(arr));
    
    if (rgw_array_erase(arr, 1) != 0) {
        printf("FAIL: array erase\n"); return 1;
    }
    printf("✓ Erased element at position 1\n");
    
    if (rgw_array_size(arr) != 2) {
        printf("FAIL: size after erase\n"); return 1;
    }
    
    rgw_array_clear(arr);
    if (!rgw_array_empty(arr)) {
        printf("FAIL: array clear\n"); return 1;
    }
    printf("✓ Array cleared\n");
    
    const char *data3 = "test1";
    const char *data4 = "test2";
    const char *data5 = "test3";
    rgw_array_append(arr, data3, (uint32_t)strlen(data3));
    rgw_array_append(arr, data4, (uint32_t)strlen(data4));
    rgw_array_append(arr, data5, (uint32_t)strlen(data5));
    
    if (rgw_array_swap(arr, 0, 2) != 0) {
        printf("FAIL: array swap\n"); return 1;
    }
    
    elem = rgw_array_get(arr, 0, &len);
    if (memcmp(elem, "test3", len) != 0) {
        printf("FAIL: swap result\n"); return 1;
    }
    printf("✓ Swap works correctly\n");
    
    rgw_array_destroy(arr);
    printf("✓ Array destroyed\n");
    
    printf("\n=== All rgw_carray tests passed! ===\n");
    return 0;
}
