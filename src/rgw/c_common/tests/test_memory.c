#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/containers/rgw_cmemory.h"

int main(void) {
    printf("Testing memory management...\n");
    
    void *ptr = rgw_c_alloc(1024);
    if (!ptr) { printf("FAIL: alloc\n"); return 1; }
    printf("✓ Allocated 1024 bytes\n");
    
    ptr = rgw_c_realloc(ptr, 2048);
    if (!ptr) { printf("FAIL: realloc\n"); return 1; }
    printf("✓ Reallocated to 2048 bytes\n");
    
    char *str = rgw_c_strdup("test");
    if (!str || strcmp(str, "test") != 0) { printf("FAIL: strdup\n"); return 1; }
    printf("✓ String duplicate works\n");
    rgw_c_free(str);
    rgw_c_free(ptr);
    
    printf("All memory tests passed!\n");
    return 0;
}
