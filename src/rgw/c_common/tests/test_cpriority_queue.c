#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/containers/rgw_cpriority_queue.h"

/* Test data structure */
typedef struct {
    int priority;
    char name[32];
} TestData;

/* Compare function for min-heap: returns negative if a < b */
static int compare_min(const void *a, const void *b)
{
    const TestData *ta = (const TestData *)a;
    const TestData *tb = (const TestData *)b;
    return ta->priority - tb->priority;
}

/* Compare function for max-heap: returns positive if a < b */
static int compare_max(const void *a, const void *b)
{
    const TestData *ta = (const TestData *)a;
    const TestData *tb = (const TestData *)b;
    return tb->priority - ta->priority;
}

/* Free callback */
static void test_data_free(void *data)
{
    free(data);
}

int main(void) {
    printf("Testing rgw_cpriority_queue (std::priority_queue alternative)...\n");

    /* Test 1: Create and destroy */
    printf("\n[Test 1] Create and destroy...\n");
    rgw_priority_queue_t *pq = rgw_priority_queue_create(compare_min, test_data_free);
    if (!pq) {
        printf("FAIL: priority queue create\n");
        return 1;
    }
    printf("✓ Priority queue created\n");

    if (!rgw_priority_queue_empty(pq)) {
        printf("FAIL: queue should be empty\n");
        return 1;
    }
    printf("✓ Queue is empty\n");

    /* Test 2: Push elements */
    printf("\n[Test 2] Push elements...\n");
    TestData *d1 = (TestData *)malloc(sizeof(TestData));
    d1->priority = 5;
    strcpy(d1->name, "task1");
    if (rgw_priority_queue_push(pq, d1, sizeof(TestData)) != 0) {
        printf("FAIL: push element 1\n");
        return 1;
    }
    printf("✓ Pushed element with priority 5\n");

    TestData *d2 = (TestData *)malloc(sizeof(TestData));
    d2->priority = 10;
    strcpy(d2->name, "task2");
    if (rgw_priority_queue_push(pq, d2, sizeof(TestData)) != 0) {
        printf("FAIL: push element 2\n");
        return 1;
    }
    printf("✓ Pushed element with priority 10\n");

    TestData *d3 = (TestData *)malloc(sizeof(TestData));
    d3->priority = 3;
    strcpy(d3->name, "task3");
    if (rgw_priority_queue_push(pq, d3, sizeof(TestData)) != 0) {
        printf("FAIL: push element 3\n");
        return 1;
    }
    printf("✓ Pushed element with priority 3\n");

    if (rgw_priority_queue_size(pq) != 3) {
        printf("FAIL: size should be 3\n");
        return 1;
    }
    printf("✓ Size: %zu\n", rgw_priority_queue_size(pq));

    /* Test 3: Top element (min-heap: should be priority 3) */
    printf("\n[Test 3] Top element (min-heap)...\n");
    uint32_t len = 0;
    const TestData *top = (const TestData *)rgw_priority_queue_top(pq, &len);
    if (!top || top->priority != 3) {
        printf("FAIL: top should have priority 3, got %d\n", top ? top->priority : -1);
        return 1;
    }
    printf("✓ Top element: priority=%d, name=%s\n", top->priority, top->name);

    /* Test 4: Pop elements (should come out in priority order: 3, 5, 10) */
    printf("\n[Test 4] Pop elements...\n");
    TestData popped;
    for (int i = 0; i < 3; i++) {
        if (rgw_priority_queue_pop(pq, &popped, &len) != 0) {
            printf("FAIL: pop element %d\n", i);
            return 1;
        }
        printf("✓ Popped: priority=%d, name=%s\n", popped.priority, popped.name);
    }

    if (!rgw_priority_queue_empty(pq)) {
        printf("FAIL: queue should be empty after popping all\n");
        return 1;
    }
    printf("✓ Queue is empty after popping\n");

    /* Test 5: Max-heap */
    printf("\n[Test 5] Max-heap test...\n");
    rgw_priority_queue_destroy(pq);
    pq = rgw_priority_queue_create(compare_max, test_data_free);
    if (!pq) {
        printf("FAIL: max-heap create\n");
        return 1;
    }

    for (int i = 5; i >= 1; i--) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->priority = i;
        sprintf(d->name, "task%d", i);
        rgw_priority_queue_push(pq, d, sizeof(TestData));
    }
    printf("✓ Pushed 5 elements with priorities 1-5\n");

    top = (const TestData *)rgw_priority_queue_top(pq, &len);
    if (top->priority != 5) {
        printf("FAIL: max-heap top should be 5, got %d\n", top->priority);
        return 1;
    }
    printf("✓ Max-heap top: priority=%d\n", top->priority);

    /* Pop all and verify order */
    int expected[] = {5, 4, 3, 2, 1};
    for (int i = 0; i < 5; i++) {
        rgw_priority_queue_pop(pq, &popped, &len);
        if (popped.priority != expected[i]) {
            printf("FAIL: expected %d, got %d\n", expected[i], popped.priority);
            return 1;
        }
    }
    printf("✓ Max-heap pop order correct\n");

    /* Test 6: Clear */
    printf("\n[Test 6] Clear test...\n");
    for (int i = 0; i < 3; i++) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->priority = i;
        rgw_priority_queue_push(pq, d, sizeof(TestData));
    }
    printf("✓ Pushed 3 elements\n");

    rgw_priority_queue_clear(pq);
    if (!rgw_priority_queue_empty(pq)) {
        printf("FAIL: queue should be empty after clear\n");
        return 1;
    }
    printf("✓ Queue cleared\n");

    /* Test 7: Destroy */
    printf("\n[Test 7] Destroy...\n");
    rgw_priority_queue_destroy(pq);
    printf("✓ Priority queue destroyed\n");

    /* Test 8: Empty queue operations */
    printf("\n[Test 8] Empty queue operations...\n");
    pq = rgw_priority_queue_create(compare_min, test_data_free);
    if (!pq) {
        printf("FAIL: priority queue create\n");
        return 1;
    }

    /* Top on empty queue should return NULL */
    const TestData *empty_top = (const TestData *)rgw_priority_queue_top(pq, &len);
    if (empty_top != NULL) {
        printf("FAIL: top on empty queue should return NULL\n");
        return 1;
    }
    printf("✓ Top on empty queue returns NULL\n");

    /* Pop on empty queue should return non-zero */
    TestData dummy;
    if (rgw_priority_queue_pop(pq, &dummy, &len) == 0) {
        printf("FAIL: pop on empty queue should fail\n");
        return 1;
    }
    printf("✓ Pop on empty queue returns error\n");

    /* Size on empty queue should be 0 */
    if (rgw_priority_queue_size(pq) != 0) {
        printf("FAIL: size on empty queue should be 0\n");
        return 1;
    }
    printf("✓ Size on empty queue is 0\n");

    rgw_priority_queue_destroy(pq);
    printf("✓ Empty queue operations test passed\n");

    /* Test 9: Without value_free callback */
    printf("\n[Test 9] Without value_free callback...\n");
    pq = rgw_priority_queue_create(compare_min, NULL);
    if (!pq) {
        printf("FAIL: priority queue create without callback\n");
        return 1;
    }

    TestData *d_no_free = (TestData *)malloc(sizeof(TestData));
    d_no_free->priority = 42;
    strcpy(d_no_free->name, "no_free");

    if (rgw_priority_queue_push(pq, d_no_free, sizeof(TestData)) != 0) {
        printf("FAIL: push without callback\n");
        return 1;
    }
    printf("✓ Pushed without callback\n");

    /* Pop should NOT free the data */
    TestData popped_no_free;
    if (rgw_priority_queue_pop(pq, &popped_no_free, &len) != 0) {
        printf("FAIL: pop without callback\n");
        return 1;
    }

    /* The popped data is a copy, so we need to verify the original is still valid */
    if (d_no_free->priority != 42) {
        printf("FAIL: original data should be preserved\n");
        return 1;
    }
    printf("✓ Original data preserved after pop (no auto-free)\n");

    free(d_no_free);
    rgw_priority_queue_destroy(pq);
    printf("✓ Without callback test passed\n");

    /* Test 10: Many elements stress test */
    printf("\n[Test 10] Many elements stress test...\n");
    pq = rgw_priority_queue_create(compare_min, test_data_free);
    if (!pq) {
        printf("FAIL: priority queue create\n");
        return 1;
    }

    const int count = 1000;
    for (int i = 0; i < count; i++) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->priority = (count - i);  /* Reverse order to test heapify */
        sprintf(d->name, "task%d", i);
        if (rgw_priority_queue_push(pq, d, sizeof(TestData)) != 0) {
            printf("FAIL: push element %d\n", i);
            return 1;
        }
    }
    printf("✓ Pushed %d elements\n", count);

    if (rgw_priority_queue_size(pq) != count) {
        printf("FAIL: expected size %d, got %zu\n", count, rgw_priority_queue_size(pq));
        return 1;
    }
    printf("✓ Size correct: %zu\n", rgw_priority_queue_size(pq));

    /* Top should be the minimum (priority 1) */
    top = (const TestData *)rgw_priority_queue_top(pq, &len);
    if (top->priority != 1) {
        printf("FAIL: top should be priority 1, got %d\n", top->priority);
        return 1;
    }
    printf("✓ Top is minimum: priority=%d\n", top->priority);

    /* Pop all and verify ascending order */
    int prev_priority = 0;
    for (int i = 0; i < count; i++) {
        if (rgw_priority_queue_pop(pq, &popped, &len) != 0) {
            printf("FAIL: pop element %d\n", i);
            return 1;
        }
        if (popped.priority <= prev_priority) {
            printf("FAIL: pop order not ascending: %d <= %d\n", popped.priority, prev_priority);
            return 1;
        }
        prev_priority = popped.priority;
    }
    printf("✓ Popped %d elements in correct priority order\n", count);

    if (!rgw_priority_queue_empty(pq)) {
        printf("FAIL: queue should be empty\n");
        return 1;
    }

    rgw_priority_queue_destroy(pq);
    printf("✓ Many elements stress test passed\n");

    printf("\n=== All rgw_cpriority_queue tests passed! ===\n");
    return 0;
}
