#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/containers/rgw_cdeque.h"

/* Test data structure */
typedef struct {
    int value;
    char name[32];
} TestData;

/* Free callback */
static void test_data_free(void *data)
{
    free(data);
}

int main(void) {
    printf("Testing rgw_cdeque (std::deque alternative)...\n");

    /* Test 1: Create and destroy */
    printf("\n[Test 1] Create and destroy...\n");
    rgw_deque_t *deque = rgw_deque_create(test_data_free);
    if (!deque) {
        printf("FAIL: deque create\n");
        return 1;
    }
    printf("✓ Deque created\n");

    if (!rgw_deque_empty(deque)) {
        printf("FAIL: deque should be empty\n");
        return 1;
    }
    printf("✓ Deque is empty\n");

    /* Test 2: Push back */
    printf("\n[Test 2] Push back...\n");
    TestData *d1 = (TestData *)malloc(sizeof(TestData));
    d1->value = 1;
    strcpy(d1->name, "first");
    if (rgw_deque_push_back(deque, d1, sizeof(TestData)) != 0) {
        printf("FAIL: push_back element 1\n");
        return 1;
    }
    printf("✓ Pushed back element 1\n");

    TestData *d2 = (TestData *)malloc(sizeof(TestData));
    d2->value = 2;
    strcpy(d2->name, "second");
    if (rgw_deque_push_back(deque, d2, sizeof(TestData)) != 0) {
        printf("FAIL: push_back element 2\n");
        return 1;
    }
    printf("✓ Pushed back element 2\n");

    if (rgw_deque_size(deque) != 2) {
        printf("FAIL: size should be 2\n");
        return 1;
    }
    printf("✓ Size: %zu\n", rgw_deque_size(deque));

    /* Test 3: Push front */
    printf("\n[Test 3] Push front...\n");
    TestData *d0 = (TestData *)malloc(sizeof(TestData));
    d0->value = 0;
    strcpy(d0->name, "zeroth");
    if (rgw_deque_push_front(deque, d0, sizeof(TestData)) != 0) {
        printf("FAIL: push_front\n");
        return 1;
    }
    printf("✓ Pushed front element 0\n");

    if (rgw_deque_size(deque) != 3) {
        printf("FAIL: size should be 3\n");
        return 1;
    }
    printf("✓ Size: %zu\n", rgw_deque_size(deque));

    /* Test 4: Get by index */
    printf("\n[Test 4] Get by index...\n");
    uint32_t len = 0;
    const TestData *elem = (const TestData *)rgw_deque_get(deque, 0, &len);
    if (!elem || elem->value != 0) {
        printf("FAIL: get index 0, expected 0, got %d\n", elem ? elem->value : -1);
        return 1;
    }
    printf("✓ Index 0: value=%d, name=%s\n", elem->value, elem->name);

    elem = (const TestData *)rgw_deque_get(deque, 1, &len);
    if (!elem || elem->value != 1) {
        printf("FAIL: get index 1, expected 1, got %d\n", elem ? elem->value : -1);
        return 1;
    }
    printf("✓ Index 1: value=%d, name=%s\n", elem->value, elem->name);

    elem = (const TestData *)rgw_deque_get(deque, 2, &len);
    if (!elem || elem->value != 2) {
        printf("FAIL: get index 2, expected 2, got %d\n", elem ? elem->value : -1);
        return 1;
    }
    printf("✓ Index 2: value=%d, name=%s\n", elem->value, elem->name);

    /* Test 5: Front and back */
    printf("\n[Test 5] Front and back...\n");
    const TestData *front = (const TestData *)rgw_deque_front(deque, &len);
    const TestData *back = (const TestData *)rgw_deque_back(deque, &len);
    if (!front || front->value != 0 || !back || back->value != 2) {
        printf("FAIL: front/back\n");
        return 1;
    }
    printf("✓ Front: value=%d, Back: value=%d\n", front->value, back->value);

    /* Test 6: Pop back */
    printf("\n[Test 6] Pop back...\n");
    if (rgw_deque_pop_back(deque) != 0) {
        printf("FAIL: pop_back\n");
        return 1;
    }
    printf("✓ Popped back\n");

    if (rgw_deque_size(deque) != 2) {
        printf("FAIL: size after pop_back\n");
        return 1;
    }
    printf("✓ Size: %zu\n", rgw_deque_size(deque));

    /* Test 7: Pop front */
    printf("\n[Test 7] Pop front...\n");
    if (rgw_deque_pop_front(deque) != 0) {
        printf("FAIL: pop_front\n");
        return 1;
    }
    printf("✓ Popped front\n");

    if (rgw_deque_size(deque) != 1) {
        printf("FAIL: size after pop_front\n");
        return 1;
    }
    printf("✓ Size: %zu\n", rgw_deque_size(deque));

    /* Test 8: Clear */
    printf("\n[Test 8] Clear...\n");
    for (int i = 0; i < 5; i++) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->value = i;
        rgw_deque_push_back(deque, d, sizeof(TestData));
    }
    printf("✓ Pushed 5 elements\n");

    rgw_deque_clear(deque);
    if (!rgw_deque_empty(deque)) {
        printf("FAIL: deque should be empty after clear\n");
        return 1;
    }
    printf("✓ Deque cleared\n");

    /* Test 9: Many elements */
    printf("\n[Test 9] Many elements...\n");
    const int count = 1000;
    for (int i = 0; i < count; i++) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->value = i;
        rgw_deque_push_back(deque, d, sizeof(TestData));
    }
    printf("✓ Pushed %d elements\n", count);

    if (rgw_deque_size(deque) != count) {
        printf("FAIL: size mismatch\n");
        return 1;
    }

    /* Verify order */
    for (int i = 0; i < count; i++) {
        elem = (const TestData *)rgw_deque_get(deque, i, &len);
        if (!elem || elem->value != i) {
            printf("FAIL: element at index %d, expected %d, got %d\n", i, i, elem ? elem->value : -1);
            return 1;
        }
    }
    printf("✓ All %d elements verified\n", count);

    /* Test 10: Destroy */
    printf("\n[Test 10] Destroy...\n");
    rgw_deque_destroy(deque);
    printf("✓ Deque destroyed\n");

    printf("\n=== All rgw_cdeque tests passed! ===\n");
    return 0;
}
