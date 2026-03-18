#include <stdio.h>
#include <string.h>
#include "../include/containers/rgw_coptional.h"

int main(void) {
    printf("Testing rgw_coptional (std::optional alternative)...\n");
    
    rgw_optional_t opt = {0};
    
    if (rgw_optional_has_value(&opt)) {
        printf("FAIL: empty should have no value\n"); return 1;
    }
    printf("✓ Empty optional has no value\n");
    
    const char *value = "hello";
    if (rgw_optional_set(&opt, value, (uint32_t)(strlen(value) + 1)) != 0) {
        printf("FAIL: optional set\n"); return 1;
    }
    
    if (!rgw_optional_has_value(&opt)) {
        printf("FAIL: should have value\n"); return 1;
    }
    printf("✓ Optional has value\n");
    
    uint32_t len = 0;
    const void *ret = rgw_optional_get(&opt, &len);
    if (!ret || len != 6 || memcmp(ret, "hello", 6) != 0) {
        printf("FAIL: optional get\n"); return 1;
    }
    printf("✓ Get value: \"%s\", len: %u\n", (const char*)ret, len);
    
    const char *str_val = rgw_optional_get_string(&opt);
    if (!str_val || strcmp(str_val, "hello") != 0) {
        printf("FAIL: get string\n"); return 1;
    }
    printf("✓ Get string: \"%s\"\n", str_val);
    
    rgw_optional_clear(&opt);
    if (rgw_optional_has_value(&opt)) {
        printf("FAIL: clear should remove value\n"); return 1;
    }
    printf("✓ Clear works\n");
    
    if (rgw_optional_set_string(&opt, "test string") != 0) {
        printf("FAIL: set string\n"); return 1;
    }
    if (!rgw_optional_has_value(&opt)) {
        printf("FAIL: should have value after set string\n"); return 1;
    }
    printf("✓ Set string works\n");
    
    rgw_optional_destroy(&opt);
    if (rgw_optional_has_value(&opt)) {
        printf("FAIL: destroy should clear\n"); return 1;
    }
    printf("✓ Destroy works\n");
    
    rgw_optional_t opt2 = {0};
    rgw_optional_set(&opt2, "value1", 7);
    
    rgw_optional_t opt3 = {0};
    if (rgw_optional_copy(&opt3, &opt2) != 0) {
        printf("FAIL: copy\n"); return 1;
    }
    if (!rgw_optional_has_value(&opt3)) {
        printf("FAIL: copy has no value\n"); return 1;
    }
    printf("✓ Copy works\n");
    
    if (!rgw_optional_equals(&opt2, &opt3)) {
        printf("FAIL: equals\n"); return 1;
    }
    printf("✓ Equals works\n");
    
    rgw_optional_clear(&opt3);
    if (rgw_optional_equals(&opt2, &opt3)) {
        printf("FAIL: not equals\n"); return 1;
    }
    printf("✓ Not equals works\n");
    
    rgw_optional_swap(&opt2, &opt3);
    // After swap: opt2 should be empty (since opt3 was cleared), opt3 should have value1
    if (rgw_optional_has_value(&opt2) || !rgw_optional_has_value(&opt3)) {
        printf("FAIL: swap\n"); return 1;
    }
    // Verify the value moved to opt3
    uint32_t len3 = 0;
    const char *val3 = rgw_optional_get(&opt3, &len3);
    if (strcmp(val3, "value1") != 0) {
        printf("FAIL: swap values\n"); return 1;
    }
    printf("✓ Swap works\n");
    
    rgw_optional_move(&opt2, &opt3);
    // After move: opt2 should have value1 (moved from opt3), opt3 should be empty
    if (!rgw_optional_has_value(&opt2) || rgw_optional_has_value(&opt3)) {
        printf("FAIL: move\n"); return 1;
    }
    // Verify the value moved to opt2
    uint32_t len2 = 0;
    const char *val2 = rgw_optional_get(&opt2, &len2);
    if (strcmp(val2, "value1") != 0) {
        printf("FAIL: move value\n"); return 1;
    }
    printf("✓ Move works\n");
    
    rgw_optional_destroy(&opt2);
    rgw_optional_destroy(&opt3);
    
    printf("\n=== All rgw_coptional tests passed! ===\n");
    return 0;
}
