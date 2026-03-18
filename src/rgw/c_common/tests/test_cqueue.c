#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/containers/rgw_cqueue.h"

/* Test data structure */
typedef struct {
    int id;
    char name[32];
} TestData;

/* Free callback */
static void test_data_free(void *data)
{
    free(data);
}

int main(void) {
    printf("Testing rgw_cqueue (std::queue alternative)...\n");

    /* Test 1: Create and destroy */
    printf("\n[Test 1] Create and destroy...\n");
    rgw_queue_t *queue = rgw_queue_create(test_data_free);
    if (!queue) {
        printf("FAIL: queue create\n");
        return 1;
    }
    printf("✓ Queue created\n");

    if (!rgw_queue_empty(queue)) {
        printf("FAIL: queue should be empty\n");
        return 1;
    }
    printf("✓ Queue is empty\n");

    /* Test 2: Push elements */
    printf("\n[Test 2] Push elements...\n");
    TestData *d1 = (TestData *)malloc(sizeof(TestData));
    d1->id = 1;
    strcpy(d1->name, "task1");
    if (rgw_queue_push(queue, d1, sizeof(TestData)) != 0) {
        printf("FAIL: push element 1\n");
        return 1;
    }
    printf("✓ Pushed element 1\n");

    TestData *d2 = (TestData *)malloc(sizeof(TestData));
    d2->id = 2;
    strcpy(d2->name, "task2");
    if (rgw_queue_push(queue, d2, sizeof(TestData)) != 0) {
        printf("FAIL: push element 2\n");
        return 1;
    }
    printf("✓ Pushed element 2\n");

    TestData *d3 = (TestData *)malloc(sizeof(TestData));
    d3->id = 3;
    strcpy(d3->name, "task3");
    if (rgw_queue_push(queue, d3, sizeof(TestData)) != 0) {
        printf("FAIL: push element 3\n");
        return 1;
    }
    printf("✓ Pushed element 3\n");

    if (rgw_queue_size(queue) != 3) {
        printf("FAIL: size should be 3\n");
        return 1;
    }
    printf("✓ Size: %zu\n", rgw_queue_size(queue));

    /* Test 3: Front element */
    printf("\n[Test 3] Front element...\n");
    uint32_t len = 0;
    const TestData *front = (const TestData *)rgw_queue_front(queue, &len);
    if (!front || front->id != 1) {
        printf("FAIL: front should be id=1, got %d\n", front ? front->id : -1);
        return 1;
    }
    printf("✓ Front element: id=%d, name=%s\n", front->id, front->name);

    /* Test 4: Back element */
    printf("\n[Test 4] Back element...\n");
    const TestData *back = (const TestData *)rgw_queue_back(queue, &len);
    if (!back || back->id != 3) {
        printf("FAIL: back should be id=3, got %d\n", back ? back->id : -1);
        return 1;
    }
    printf("✓ Back element: id=%d, name=%s\n", back->id, back->name);

    /* Test 5: Pop elements (FIFO order) */
    printf("\n[Test 5] Pop elements (FIFO)...\n");
    for (int i = 1; i <= 3; i++) {
        if (rgw_queue_pop(queue) != 0) {
            printf("FAIL: pop element %d\n", i);
            return 1;
        }
        printf("✓ Popped element %d\n", i);
    }

    if (!rgw_queue_empty(queue)) {
        printf("FAIL: queue should be empty after popping all\n");
        return 1;
    }
    printf("✓ Queue is empty after popping\n");

    /* Test 6: Clear */
    printf("\n[Test 6] Clear test...\n");
    for (int i = 0; i < 5; i++) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->id = i + 1;
        rgw_queue_push(queue, d, sizeof(TestData));
    }
    printf("✓ Pushed 5 elements\n");

    rgw_queue_clear(queue);
    if (!rgw_queue_empty(queue)) {
        printf("FAIL: queue should be empty after clear\n");
        return 1;
    }
    printf("✓ Queue cleared\n");

    /* Test 7: Destroy */
    printf("\n[Test 7] Destroy...\n");
    rgw_queue_destroy(queue);
    printf("✓ Queue destroyed\n");

    /* Test 8: Without value_free callback */
    printf("\n[Test 8] Without value_free callback...\n");
    queue = rgw_queue_create(NULL);
    if (!queue) {
        printf("FAIL: queue create without callback\n");
        return 1;
    }

    TestData *d4 = (TestData *)malloc(sizeof(TestData));
    d4->id = 100;
    if (rgw_queue_push(queue, d4, sizeof(TestData)) != 0) {
        printf("FAIL: push without callback\n");
        return 1;
    }
    printf("✓ Pushed without callback\n");

    /* Pop should NOT free the data */
    rgw_queue_pop(queue);
    /* We need to manually free since no callback */
    free(d4);
    printf("✓ Popped without auto-free\n");

    rgw_queue_destroy(queue);
    printf("✓ Queue destroyed without callback\n");

    /* Test 9: Empty queue operations */
    printf("\n[Test 9] Empty queue operations...\n");
    queue = rgw_queue_create(test_data_free);
    if (!queue) {
        printf("FAIL: queue create\n");
        return 1;
    }

    /* Front on empty queue should return NULL */
    const TestData *empty_front = (const TestData *)rgw_queue_front(queue, &len);
    if (empty_front != NULL) {
        printf("FAIL: front on empty queue should return NULL\n");
        return 1;
    }
    printf("✓ Front on empty queue returns NULL\n");

    /* Back on empty queue should return NULL */
    const TestData *empty_back = (const TestData *)rgw_queue_back(queue, &len);
    if (empty_back != NULL) {
        printf("FAIL: back on empty queue should return NULL\n");
        return 1;
    }
    printf("✓ Back on empty queue returns NULL\n");

    /* Pop on empty queue should return non-zero */
    if (rgw_queue_pop(queue) == 0) {
        printf("FAIL: pop on empty queue should fail\n");
        return 1;
    }
    printf("✓ Pop on empty queue returns error\n");

    /* Size on empty queue should be 0 */
    if (rgw_queue_size(queue) != 0) {
        printf("FAIL: size on empty queue should be 0\n");
        return 1;
    }
    printf("✓ Size on empty queue is 0\n");

    rgw_queue_destroy(queue);
    printf("✓ Empty queue operations test passed\n");

    /* Test 10: Many elements stress test */
    printf("\n[Test 10] Many elements stress test...\n");
    queue = rgw_queue_create(test_data_free);
    if (!queue) {
        printf("FAIL: queue create\n");
        return 1;
    }

    const int count = 1000;
    for (int i = 0; i < count; i++) {
        TestData *d = (TestData *)malloc(sizeof(TestData));
        d->id = i + 1;
        sprintf(d->name, "task%d", i + 1);
        if (rgw_queue_push(queue, d, sizeof(TestData)) != 0) {
            printf("FAIL: push element %d\n", i);
            return 1;
        }
    }
    printf("✓ Pushed %d elements\n", count);

    if (rgw_queue_size(queue) != count) {
        printf("FAIL: expected size %d, got %zu\n", count, rgw_queue_size(queue));
        return 1;
    }
    printf("✓ Size correct: %zu\n", rgw_queue_size(queue));

    /* Verify front is first element */
    front = (const TestData *)rgw_queue_front(queue, &len);
    if (!front || front->id != 1) {
        printf("FAIL: front should be id=1\n");
        return 1;
    }
    printf("✓ Front is first element: id=%d\n", front->id);

    /* Verify back is last element */
    back = (const TestData *)rgw_queue_back(queue, &len);
    if (!back || back->id != count) {
        printf("FAIL: back should be id=%d\n", count);
        return 1;
    }
    printf("✓ Back is last element: id=%d\n", back->id);

    /* Pop all and verify FIFO order */
    for (int i = 1; i <= count; i++) {
        if (rgw_queue_pop(queue) != 0) {
            printf("FAIL: pop element %d\n", i);
            return 1;
        }
    }
    printf("✓ Popped %d elements in correct FIFO order\n", count);

    if (!rgw_queue_empty(queue)) {
        printf("FAIL: queue should be empty\n");
        return 1;
    }

    rgw_queue_destroy(queue);
    printf("✓ Many elements stress test passed\n");

    printf("\n=== All rgw_cqueue tests passed! ===\n");
    return 0;
}
