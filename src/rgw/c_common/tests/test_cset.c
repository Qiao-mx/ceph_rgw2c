#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/containers/rgw_cset.h"

int main(void) {
    printf("Testing set...\n");
    
    rgw_set_t *set = rgw_set_create_string();
    if (!set) { printf("FAIL: set create\n"); return 1; }
    
    if (rgw_set_insert_string(set, "item1") <= 0) {
        printf("FAIL: insert\n"); return 1;
    }
    
    if (!rgw_set_contains_string(set, "item1")) {
        printf("FAIL: contains\n"); return 1;
    }
    
    if (rgw_set_size(set) != 1) {
        printf("FAIL: size\n"); return 1;
    }
    
    rgw_set_destroy(set);
    printf("✓ Set tests passed!\n");
    return 0;
}
