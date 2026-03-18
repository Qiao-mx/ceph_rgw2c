#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/containers/rgw_cmap.h"

int main(void) {
    printf("Testing map...\n");
    
    rgw_map_t *map = rgw_map_create(free);
    if (!map) { printf("FAIL: map create\n"); return 1; }
    
    if (rgw_map_insert(map, "key1", "value1", 7) <= 0) {
        printf("FAIL: insert\n"); return 1;
    }
    
    uint32_t len;
    const char *val = rgw_map_find(map, "key1", &len);
    if (!val || strcmp(val, "value1") != 0) {
        printf("FAIL: find\n"); return 1;
    }
    
    int count = 0;
    rgw_map_iterator_t iter = rgw_map_begin(map);
    while (rgw_map_iterator_valid(&iter)) {
        count++;
        rgw_map_iterator_next(&iter);
    }
    if (count != 1) { printf("FAIL: iteration\n"); return 1; }
    rgw_map_iterator_destroy(&iter);
    
    rgw_map_destroy(map);
    printf("✓ Map tests passed!\n");
    return 0;
}
