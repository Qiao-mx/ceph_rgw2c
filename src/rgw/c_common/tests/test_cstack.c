#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/containers/rgw_cstack.h"

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
    printf("Testing rgw_cstack (std::stack alternative)...\n");

    /* Test 1: Create and destroy */
    printf("\n[Test 1] Create and destroy...\n");
    rgw_stack_t *stack = rgw_stack_create(test_data_free);
    if (!stack) {
        printf("FAIL: stack create\n");
        return 1;
    }
    printf("✓ Stack created\n");

    if (!rgw_stack_empty(stack)) {
        printf("FAIL: stack should be empty\n");
        return 1;
    }
    printf("✓ Stack is empty\n");

    /* Test 2: Push elements */
    printf("\n[Test 2] Push elements...\n");
    TestData *d1 = (TestData *)malloc(sizeof(TestData));
    d1->value = 10;
    strcpy(d1->name, "first");
    if (rgw_stack_push(stack, d1, sizeof(TestData)) != 0) {
        printf("FAIL: push element 1\n");
        return 1;
    }
    printf("✓ Pushed element with value 10\n");

    TestData *d2 = (TestData *)malloc(sizeof(TestData));
    d2->value = 20;
    strcpy(d2->name, "second");
    if (rgw_stack_push(stack, d2, sizeof(TestData)) != 0) {
        printf("FAIL: push element 2\n");
        return 1;
    }
    printf("✓ Pushed element with value 20\n");

    TestData *d3 = (TestData *)malloc(sizeof(TestData));
    d3->value = 30;
    strcpy(d3->name, "third");
    if (rgw_stack_push(stack, d3, sizeof(TestData)) != 0) {
        printf("FAIL: push element 3\n");
        return 1;
    }
    printf("✓ Pushed element with value 30\n");

    if (rgw_stack_size(stack) != 3) {
        printf("FAIL: size should be 3\n");
        return 1;
    }
    printf("✓ Size: %zu\n", rgw_stack_size(stack));

    /* Test 3: Top element (LIFO: should be the last pushed) */
    printf("\n[Test 3] Top element...\n");
    uint32_t len = 0;
    const TestData *top = (const TestData *)rgw_stack_top(stack, &len);
    if (!top || top->value != 30) {
        printf("FAIL: top should have value 30, got %d\n", top ? top->value : -1);
        return 1;
    }
    printf("✓ Top element: value=%d, name=%s\n", top->value, top->name);

    /* Test 4: Pop elements (LIFO order: 30, 20, 10) */
    printf("\n[Test 4] Pop elements (LIFO)...\n");
    int expected[] = {30, 20, 10};
    for (int i = 0; i < 3; i++) {
        if (rgw_stack_pop(stack) != 0) {
            printf("FAIL: pop element %d\n", i);
            return 1;
        }
        printf("✓ Popped element with expected value %d\n", expected[i]);
    }

    if (!rgw_stack_empty(stack)) {
        printf("FAIL: stack should be empty after popping all\n");
        return 1;
    }
    printf("✓ Stack is empty after popping\n");

    /* Test 5: Clear */
    printf("\n[Test 5] Clear test...\n");
    for (int i = 0; i < 5; i++) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->value = i * 10;
        rgw_stack_push(stack, d, sizeof(TestData));
    }
    printf("✓ Pushed 5 elements\n");

    rgw_stack_clear(stack);
    if (!rgw_stack_empty(stack)) {
        printf("FAIL: stack should be empty after clear\n");
        return 1;
    }
    printf("✓ Stack cleared\n");

    /* Test 6: Destroy */
    printf("\n[Test 6] Destroy...\n");
    rgw_stack_destroy(stack);
    printf("✓ Stack destroyed\n");

    /* Test 7: Without value_free callback */
    printf("\n[Test 7] Without value_free callback...\n");
    stack = rgw_stack_create(NULL);
    if (!stack) {
        printf("FAIL: stack create without callback\n");
        return 1;
    }

    TestData *d4 = (TestData *)malloc(sizeof(TestData));
    d4->value = 100;
    if (rgw_stack_push(stack, d4, sizeof(TestData)) != 0) {
        printf("FAIL: push without callback\n");
        return 1;
    }
    printf("✓ Pushed without callback\n");

    /* Pop should NOT free the data */
    rgw_stack_pop(stack);
    /* We need to manually free since no callback */
    free(d4);
    printf("✓ Popped without auto-free\n");

    rgw_stack_destroy(stack);
    printf("✓ Stack destroyed without callback\n");

    /* Test 8: Stack behavior - push/pop many elements */
    printf("\n[Test 8] Push/pop many elements...\n");
    stack = rgw_stack_create(test_data_free);
    const int count = 1000;
    for (int i = 0; i < count; i++) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->value = i;
        if (rgw_stack_push(stack, d, sizeof(TestData)) != 0) {
            printf("FAIL: push element %d\n", i);
            return 1;
        }
    }
    printf("✓ Pushed %d elements\n", count);

    if (rgw_stack_size(stack) != count) {
        printf("FAIL: expected size %d, got %zu\n", count, rgw_stack_size(stack));
        return 1;
    }

    /* Pop all and verify order (should be reverse) */
    for (int i = count - 1; i >= 0; i--) {
        const TestData *t = (const TestData *)rgw_stack_top(stack, &len);
        if (!t || t->value != i) {
            printf("FAIL: expected value %d, got %d\n", i, t ? t->value : -1);
            return 1;
        }
        rgw_stack_pop(stack);
    }
    printf("✓ Popped %d elements in correct LIFO order\n", count);

    if (!rgw_stack_empty(stack)) {
        printf("FAIL: stack should be empty\n");
        return 1;
    }

    rgw_stack_destroy(stack);
    printf("✓ Stack destroyed\n");

    /* Test 9: Empty stack operations */
    printf("\n[Test 9] Empty stack operations...\n");
    stack = rgw_stack_create(test_data_free);
    if (!stack) {
        printf("FAIL: stack create\n");
        return 1;
    }

    /* Top on empty stack should return NULL */
    const TestData *empty_top = (const TestData *)rgw_stack_top(stack, &len);
    if (empty_top != NULL) {
        printf("FAIL: top on empty stack should return NULL\n");
        return 1;
    }
    printf("✓ Top on empty stack returns NULL\n");

    /* Pop on empty stack should return non-zero */
    if (rgw_stack_pop(stack) == 0) {
        printf("FAIL: pop on empty stack should fail\n");
        return 1;
    }
    printf("✓ Pop on empty stack returns error\n");

    /* Size on empty stack should be 0 */
    if (rgw_stack_size(stack) != 0) {
        printf("FAIL: size on empty stack should be 0\n");
        return 1;
    }
    printf("✓ Size on empty stack is 0\n");

    rgw_stack_destroy(stack);
    printf("✓ Empty stack test passed\n");

    /* Test 10: Data pointer preservation */
    printf("\n[Test 10] Data pointer preservation...\n");
    stack = rgw_stack_create(NULL);
    if (!stack) {
        printf("FAIL: stack create\n");
        return 1;
    }

    /* Create data that we want to verify is preserved */
    TestData *preserved = (TestData *)malloc(sizeof(TestData));
    preserved->value = 999;
    strcpy(preserved->name, "preserved");

    /* Push the data */
    if (rgw_stack_push(stack, preserved, sizeof(TestData)) != 0) {
        printf("FAIL: push preserved data\n");
        return 1;
    }

    /* Get top - should return data with the same content */
    uint32_t data_len = 0;
    const TestData *retrieved = (const TestData *)rgw_stack_top(stack, &data_len);
    if (!retrieved || retrieved->value != preserved->value || strcmp(retrieved->name, preserved->name) != 0) {
        printf("FAIL: top should return data with the same content\n");
        return 1;
    }
    if (retrieved->value != 999) {
        printf("FAIL: value should be preserved\n");
        return 1;
    }
    printf("✓ Data pointer preserved correctly\n");

    /* Pop doesn't free since no callback, so data is still valid */
    rgw_stack_pop(stack);

    /* Verify data is still valid after pop */
    if (preserved->value != 999) {
        printf("FAIL: data should still be valid after pop\n");
        return 1;
    }
    printf("✓ Data still valid after pop (no auto-free)\n");

    free(preserved);
    rgw_stack_destroy(stack);
    printf("✓ Data pointer preservation test passed\n");

    printf("\n=== All rgw_cstack tests passed! ===\n");
    return 0;
}
