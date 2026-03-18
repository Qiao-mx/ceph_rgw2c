#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/rgw_oop.h"
#include "../include/containers/rgw_cmemory.h"

/* Test derived object type */
typedef struct {
    rgw_object_t base;
    int value;
    char name[32];
} TestObject;

static const char* test_object_type_name(void)
{
    return "TestObject";
}

static bool test_object_is_a(const rgw_object_t *self, const char *type_name)
{
    return strcmp(type_name, "TestObject") == 0;
}

static void test_object_destroy(rgw_object_t *self)
{
    if (self) {
        printf("  -> Destroying TestObject with value %d\n", ((TestObject*)self)->value);
        rgw_c_free(self);
    }
}

static rgw_object_vtable_t test_object_vtable = {
    .destroy = test_object_destroy,
    .type_name = test_object_type_name,
    .is_a = test_object_is_a
};

/* Another derived type */
typedef struct {
    rgw_object_t base;
    double data;
} AnotherObject;

static const char* another_object_type_name(void)
{
    return "AnotherObject";
}

static bool another_object_is_a(const rgw_object_t *self, const char *type_name)
{
    return strcmp(type_name, "AnotherObject") == 0;
}

static void another_object_destroy(rgw_object_t *self)
{
    if (self) {
        printf("  -> Destroying AnotherObject with data %f\n", ((AnotherObject*)self)->data);
        rgw_c_free(self);
    }
}

static rgw_object_vtable_t another_object_vtable = {
    .destroy = another_object_destroy,
    .type_name = another_object_type_name,
    .is_a = another_object_is_a
};

int main(void) {
    printf("Testing rgw_oop (OOP framework)...\n");

    /* Test 1: Create object */
    printf("\n[Test 1] Create object...\n");
    TestObject *obj = (TestObject *)rgw_c_alloc(sizeof(TestObject));
    if (!obj) {
        printf("FAIL: allocation failed\n");
        return 1;
    }

    obj->base.vtable = &test_object_vtable;
    obj->base.ref_count = 1;
    strncpy(obj->base.type_id, "TestObject", sizeof(obj->base.type_id) - 1);
    obj->value = 42;
    strncpy(obj->name, "test", sizeof(obj->name) - 1);

    printf("✓ Created TestObject with value=%d\n", obj->value);

    /* Test 2: Reference counting */
    printf("\n[Test 2] Reference counting...\n");
    if (obj->base.ref_count != 1) {
        printf("FAIL: initial ref_count should be 1\n");
        return 1;
    }

    rgw_object_ref(&obj->base);
    if (obj->base.ref_count != 2) {
        printf("FAIL: ref_count should be 2 after ref\n");
        return 1;
    }
    printf("✓ Ref count: %u\n", obj->base.ref_count);

    rgw_object_ref(&obj->base);
    if (obj->base.ref_count != 3) {
        printf("FAIL: ref_count should be 3\n");
        return 1;
    }
    printf("✓ Ref count after second ref: %u\n", obj->base.ref_count);

    rgw_object_unref(&obj->base);
    if (obj->base.ref_count != 2) {
        printf("FAIL: ref_count should be 2 after unref\n");
        return 1;
    }
    printf("✓ Ref count after unref: %u\n", obj->base.ref_count);

    /* Test 3: Type checking */
    printf("\n[Test 3] Type checking...\n");
    if (!RGW_IS_A(&obj->base, "TestObject")) {
        printf("FAIL: should be TestObject\n");
        return 1;
    }
    printf("✓ RGW_IS_A(obj, \"TestObject\") = true\n");

    if (RGW_IS_A(&obj->base, "AnotherObject")) {
        printf("FAIL: should not be AnotherObject\n");
        return 1;
    }
    printf("✓ RGW_IS_A(obj, \"AnotherObject\") = false\n");

    /* Test 4: Type name */
    printf("\n[Test 4] Type name...\n");
    const char *type_name = RGW_TYPE_NAME(&obj->base);
    if (strcmp(type_name, "TestObject") != 0) {
        printf("FAIL: type_name should be TestObject\n");
        return 1;
    }
    printf("✓ RGW_TYPE_NAME(obj) = %s\n", type_name);

    /* Test 5: Destroy */
    printf("\n[Test 5] Destroy...\n");
    rgw_object_unref(&obj->base);
    if (obj->base.ref_count != 1) {
        printf("FAIL: ref_count should be 1\n");
        return 1;
    }
    printf("✓ Ref count: %u (will destroy on unref)\n", obj->base.ref_count);

    rgw_object_unref(&obj->base);
    printf("✓ Object destroyed via reference counting\n");

    /* Test 6: Smart pointer */
    printf("\n[Test 6] Smart pointer...\n");
    AnotherObject *another = (AnotherObject *)rgw_c_alloc(sizeof(AnotherObject));
    another->base.vtable = &another_object_vtable;
    another->base.ref_count = 1;
    strncpy(another->base.type_id, "AnotherObject", sizeof(another->base.type_id) - 1);
    another->data = 3.14;

    rgw_shared_ptr_t ptr = rgw_shared_ptr_create(&another->base);
    if (!rgw_shared_ptr_valid(&ptr)) {
        printf("FAIL: shared_ptr should be valid\n");
        return 1;
    }
    printf("✓ Created shared_ptr, ref_count: %u\n", another->base.ref_count);

    if (another->base.ref_count != 2) {
        printf("FAIL: ref_count should be 2 after shared_ptr create\n");
        return 1;
    }

    /* Test 7: Smart pointer copy */
    printf("\n[Test 7] Smart pointer copy...\n");
    rgw_shared_ptr_t ptr2 = rgw_shared_ptr_copy(&ptr);
    if (!rgw_shared_ptr_valid(&ptr2)) {
        printf("FAIL: copied shared_ptr should be valid\n");
        return 1;
    }
    if (another->base.ref_count != 3) {
        printf("FAIL: ref_count should be 3 after copy\n");
        return 1;
    }
    printf("✓ Copied shared_ptr, ref_count: %u\n", another->base.ref_count);

    /* Test 8: Smart pointer destroy */
    printf("\n[Test 8] Smart pointer destroy...\n");
    rgw_shared_ptr_destroy(&ptr);
    if (another->base.ref_count != 2) {
        printf("FAIL: ref_count should be 2 after destroy\n");
        return 1;
    }
    printf("✓ Destroyed first shared_ptr, ref_count: %u\n", another->base.ref_count);

    rgw_shared_ptr_destroy(&ptr2);
    printf("✓ Destroyed second shared_ptr (object should be destroyed)\n");

    /* Test 9: NULL smart pointer */
    printf("\n[Test 9] NULL smart pointer...\n");
    rgw_shared_ptr_t null_ptr = { .obj = NULL };
    if (rgw_shared_ptr_valid(&null_ptr)) {
        printf("FAIL: NULL shared_ptr should not be valid\n");
        return 1;
    }
    printf("✓ NULL shared_ptr is not valid\n");

    rgw_shared_ptr_destroy(&null_ptr);
    printf("✓ Destroying NULL shared_ptr is safe\n");

    /* Test 10: RGW_DESTROY macro */
    printf("\n[Test 10] RGW_DESTROY macro...\n");
    TestObject *obj2 = (TestObject *)rgw_c_alloc(sizeof(TestObject));
    obj2->base.vtable = &test_object_vtable;
    obj2->base.ref_count = 1;
    strncpy(obj2->base.type_id, "TestObject", sizeof(obj2->base.type_id) - 1);
    obj2->value = 100;

    RGW_DESTROY(&obj2->base);
    printf("✓ RGW_DESTROY macro works\n");

    printf("\n=== All rgw_oop tests passed! ===\n");
    return 0;
}
