#include <stdio.h>
#include <string.h>
#include "../include/containers/rgw_cstring.h"

int main(void) {
    printf("Testing rgw_cstring (std::string alternative)...\n");
    
    rgw_string_t *str = rgw_string_create("hello");
    if (!str) { printf("FAIL: string create\n"); return 1; }
    printf("✓ String created: \"%s\"\n", rgw_string_c_str(str));
    
    if (rgw_string_length(str) != 5) {
        printf("FAIL: length\n"); return 1;
    }
    printf("✓ Length: %zu\n", rgw_string_length(str));
    
    if (rgw_string_append(str, " world") != 0) {
        printf("FAIL: append\n"); return 1;
    }
    printf("✓ Appended: \"%s\"\n", rgw_string_c_str(str));
    
    if (rgw_string_length(str) != 11) {
        printf("FAIL: length after append\n"); return 1;
    }
    
    if (rgw_string_compare_cstr(str, "hello world") != 0) {
        printf("FAIL: compare\n"); return 1;
    }
    printf("✓ Compare works\n");
    
    rgw_string_t *str2 = rgw_string_dup(str);
    if (!str2) { printf("FAIL: dup\n"); return 1; }
    if (rgw_string_compare(str, str2) != 0) {
        printf("FAIL: dup compare\n"); return 1;
    }
    printf("✓ Duplicate works\n");
    
    rgw_string_destroy(str2);
    
    if (rgw_string_assign(str, "assigned") != 0) {
        printf("FAIL: assign\n"); return 1;
    }
    if (rgw_string_length(str) != 8) {
        printf("FAIL: assign length\n"); return 1;
    }
    printf("✓ Assign works: \"%s\"\n", rgw_string_c_str(str));
    
    if (rgw_string_insert(str, 0, "[") != 0 ||
        rgw_string_insert(str, 9, "]") != 0) {
        printf("FAIL: insert\n"); return 1;
    }
    printf("✓ Insert works: \"%s\"\n", rgw_string_c_str(str));
    
    if (rgw_string_erase(str, 1, 4) != 0) {
        printf("FAIL: erase\n"); return 1;
    }
    printf("✓ Erase works: \"%s\"\n", rgw_string_c_str(str));
    
    if (rgw_string_replace(str, 0, 3, "NEW") != 0) {
        printf("FAIL: replace\n"); return 1;
    }
    printf("✓ Replace works: \"%s\"\n", rgw_string_c_str(str));
    
    size_t pos = rgw_string_find(str, "E");
    if (pos != 1) {
        printf("FAIL: find\n"); return 1;
    }
    printf("✓ Find works: position %zu\n", pos);
    
    pos = rgw_string_find_char(str, 'N');
    if (pos != 0) {
        printf("FAIL: find char\n"); return 1;
    }
    printf("✓ Find char works: position %zu\n", pos);
    
    rgw_string_t *sub = rgw_string_substring(str, 1, 3);
    if (!sub) { printf("FAIL: substring\n"); return 1; }
    printf("✓ Substring: \"%s\"\n", rgw_string_c_str(sub));
    rgw_string_destroy(sub);
    
    rgw_string_t *str3 = rgw_string_create("  hello world  ");
    rgw_string_trim(str3);
    if (rgw_string_compare_cstr(str3, "hello world") != 0) {
        printf("FAIL: trim\n"); return 1;
    }
    printf("✓ Trim works: \"%s\"\n", rgw_string_c_str(str3));
    rgw_string_destroy(str3);
    
    rgw_string_t *str4 = rgw_string_create("hello");
    rgw_string_to_upper(str4);
    if (rgw_string_compare_cstr(str4, "HELLO") != 0) {
        printf("FAIL: to_upper\n"); return 1;
    }
    printf("✓ To upper: \"%s\"\n", rgw_string_c_str(str4));
    
    rgw_string_to_lower(str4);
    if (rgw_string_compare_cstr(str4, "hello") != 0) {
        printf("FAIL: to_lower\n"); return 1;
    }
    printf("✓ To lower: \"%s\"\n", rgw_string_c_str(str4));
    
    rgw_string_destroy(str4);
    rgw_string_destroy(str);
    
    printf("\n=== All rgw_cstring tests passed! ===\n");
    return 0;
}
