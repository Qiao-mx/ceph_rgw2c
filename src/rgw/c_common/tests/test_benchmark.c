/**
 * @file test_benchmark.c
 * @brief Performance benchmark for all containers
 *
 * This benchmark measures the performance of each container
 * for various operations to ensure efficiency.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

#include "containers/rgw_cmemory.h"
#include "containers/rgw_carray.h"
#include "containers/rgw_cstring.h"
#include "containers/rgw_cmap.h"
#include "containers/rgw_cset.h"
#include "containers/rgw_cdeque.h"
#include "containers/rgw_cstack.h"
#include "containers/rgw_cqueue.h"
#include "containers/rgw_cpriority_queue.h"
#include "containers/rgw_clist.h"

/* Timing utilities */
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* Results structure */
typedef struct {
    const char *name;
    double insert_ms;
    double search_ms;
    double delete_ms;
    double total_ms;
    size_t memory_used;
} BenchmarkResult;

#define BENCHMARK_ITERATIONS 10000
#define SMALL_ITERATIONS 1000

/* Free callback */
static void benchmark_free(void *data) {
    if (data) {
        free(data);
    }
}

/* Comparison function for priority queue */
static int compare_int(const void *a, const void *b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return ia - ib;
}

/* Benchmark: rgw_carray */
void benchmark_array(BenchmarkResult *result) {
    double start, end;
    size_t mem_before, mem_after;

    result->name = "rgw_carray";

    /* Insert benchmark */
    start = get_time_ms();
    rgw_array_t *arr = rgw_array_create(0);
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        rgw_array_append(arr, val, sizeof(int));
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        int idx = i % BENCHMARK_ITERATIONS;
        int *val = (int*)rgw_array_get(arr, idx, NULL);
        if (!val || *val != idx) {
            printf("  ! Search error at index %d\n", idx);
        }
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark */
    start = get_time_ms();
    rgw_array_destroy(arr);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Benchmark: rgw_cstring */
void benchmark_string(BenchmarkResult *result) {
    double start, end;

    result->name = "rgw_cstring";

    /* Insert benchmark (append) */
    start = get_time_ms();
    rgw_string_t *str = rgw_string_create("");
    for (int i = 0; i < 1000; i++) {
        rgw_string_append(str, "a");
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark (find substring) */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        const char *pos = rgw_string_c_str(str);
        (void)pos;
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark */
    start = get_time_ms();
    rgw_string_destroy(str);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Benchmark: rgw_cmap */
void benchmark_map(BenchmarkResult *result) {
    double start, end;

    result->name = "rgw_cmap";

    /* Insert benchmark */
    start = get_time_ms();
    rgw_map_t *map = rgw_map_create(benchmark_free);
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        int *val = malloc(sizeof(int));
        *val = i;
        rgw_map_insert(map, key, val, sizeof(int));
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i % BENCHMARK_ITERATIONS);
        uint32_t len = 0;
        const void *found = rgw_map_find(map, key, &len);
        if (!found) {
            printf("  ! Map search error at key %s\n", key);
        }
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark */
    start = get_time_ms();
    rgw_map_destroy(map);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Benchmark: rgw_cset */
void benchmark_set(BenchmarkResult *result) {
    double start, end;

    result->name = "rgw_cset";

    /* Insert benchmark */
    start = get_time_ms();
    rgw_set_t *set = rgw_set_create_string();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        rgw_set_insert_string(set, key);
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i % BENCHMARK_ITERATIONS);
        bool found = rgw_set_contains_string(set, key);
        if (!found) {
            printf("  ! Set search error at key %s\n", key);
        }
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark */
    start = get_time_ms();
    rgw_set_destroy(set);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Benchmark: rgw_cdeque */
void benchmark_deque(BenchmarkResult *result) {
    double start, end;

    result->name = "rgw_cdeque";

    /* Insert benchmark (push_back and push_front) */
    start = get_time_ms();
    rgw_deque_t *deque = rgw_deque_create(benchmark_free);
    for (int i = 0; i < BENCHMARK_ITERATIONS / 2; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        rgw_deque_push_back(deque, val, sizeof(int));
    }
    for (int i = 0; i < BENCHMARK_ITERATIONS / 2; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        rgw_deque_push_front(deque, val, sizeof(int));
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark (access by index) */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        size_t idx = i % rgw_deque_size(deque);
        const int *val = (const int*)rgw_deque_get(deque, idx, NULL);
        if (!val) {
            printf("  ! Deque access error at index %zu\n", idx);
        }
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark */
    start = get_time_ms();
    rgw_deque_destroy(deque);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Benchmark: rgw_cstack */
void benchmark_stack(BenchmarkResult *result) {
    double start, end;

    result->name = "rgw_cstack";

    /* Insert benchmark (push) */
    start = get_time_ms();
    rgw_stack_t *stack = rgw_stack_create(benchmark_free);
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        rgw_stack_push(stack, val, sizeof(int));
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark (top) */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        const int *val = (const int*)rgw_stack_top(stack, NULL);
        if (!val) {
            printf("  ! Stack top error\n");
        }
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark (pop all) */
    start = get_time_ms();
    while (!rgw_stack_empty(stack)) {
        rgw_stack_pop(stack);
    }
    rgw_stack_destroy(stack);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Benchmark: rgw_queue */
void benchmark_queue(BenchmarkResult *result) {
    double start, end;

    result->name = "rgw_queue";

    /* Insert benchmark (push) */
    start = get_time_ms();
    rgw_queue_t *queue = rgw_queue_create(benchmark_free);
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        rgw_queue_push(queue, val, sizeof(int));
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark (front) */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        const int *val = (const int*)rgw_queue_front(queue, NULL);
        if (!val) {
            printf("  ! Queue front error\n");
        }
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark (pop all) */
    start = get_time_ms();
    while (!rgw_queue_empty(queue)) {
        rgw_queue_pop(queue);
    }
    rgw_queue_destroy(queue);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Benchmark: rgw_cpriority_queue */
void benchmark_priority_queue(BenchmarkResult *result) {
    double start, end;

    result->name = "rgw_priority_queue";

    /* Insert benchmark (push) */
    start = get_time_ms();
    rgw_priority_queue_t *pq = rgw_priority_queue_create(compare_int, benchmark_free);
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        int *val = malloc(sizeof(int));
        *val = rand() % 100000;
        rgw_priority_queue_push(pq, val, sizeof(int));
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark (top) */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        const int *val = (const int*)rgw_priority_queue_top(pq, NULL);
        if (!val) {
            printf("  ! Priority queue top error\n");
        }
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark (pop all) */
    start = get_time_ms();
    while (!rgw_priority_queue_empty(pq)) {
        rgw_priority_queue_pop(pq, NULL, NULL);
    }
    rgw_priority_queue_destroy(pq);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Benchmark: rgw_clist */
void benchmark_list(BenchmarkResult *result) {
    double start, end;

    result->name = "rgw_clist";

    /* Insert benchmark (add_tail) */
    start = get_time_ms();
    rgw_clist_t *list = rgw_clist_create(benchmark_free);
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        rgw_clist_add_tail(list, val, sizeof(int));
    }
    end = get_time_ms();
    result->insert_ms = end - start;

    result->memory_used = 0;

    /* Search benchmark (iterate) */
    start = get_time_ms();
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
        size_t count = 0;
        rgw_clist_iterator_t iter = rgw_clist_begin(list);
        while (rgw_clist_iterator_valid(&iter)) {
            count++;
            rgw_clist_iterator_next(&iter);
        }
        rgw_clist_iterator_destroy(&iter);
    }
    end = get_time_ms();
    result->search_ms = end - start;

    /* Delete benchmark */
    start = get_time_ms();
    rgw_clist_destroy(list);
    end = get_time_ms();
    result->delete_ms = end - start;

    result->total_ms = result->insert_ms + result->search_ms + result->delete_ms;
}

/* Print results */
void print_results(BenchmarkResult *results, int count) {
    printf("\n");
    printf("================================================================================\n");
    printf("                    RGW C Common Container Performance Benchmark\n");
    printf("================================================================================\n");
    printf("\n");
    printf("Operations: %d iterations (small operations: %d)\n", BENCHMARK_ITERATIONS, SMALL_ITERATIONS);
    printf("\n");

    /* Header */
    printf("%-20s | %12s | %12s | %12s | %12s\n",
           "Container", "Insert (ms)", "Search (ms)", "Delete (ms)", "Total (ms)");
    printf("%-20s-+-%12s-+-%12s-+-%12s-+-%12s\n",
           "--------------------", "------------", "------------", "------------", "------------");

    /* Each row */
    for (int i = 0; i < count; i++) {
        printf("%-20s | %12.2f | %12.2f | %12.2f | %12.2f\n",
               results[i].name,
               results[i].insert_ms,
               results[i].search_ms,
               results[i].delete_ms,
               results[i].total_ms);
    }

    printf("\n");

    /* Summary */
    double total_insert = 0, total_search = 0, total_delete = 0, total_total = 0;
    for (int i = 0; i < count; i++) {
        total_insert += results[i].insert_ms;
        total_search += results[i].search_ms;
        total_delete += results[i].delete_ms;
        total_total += results[i].total_ms;
    }

    printf("Total time: %.2f ms\n", total_total);
    printf("Average per container: %.2f ms\n", total_total / count);
    printf("\n");
}

int main(void) {
    printf("===========================================\n");
    printf("  RGW C Common - Performance Benchmark\n");
    printf("===========================================\n");
    printf("\n");
    printf("Benchmark configuration:\n");
    printf("  - Iterations: %d\n", BENCHMARK_ITERATIONS);
    printf("  - Small operations: %d\n", SMALL_ITERATIONS);
    printf("\n");

    BenchmarkResult results[9];
    int result_count = 0;

    /* Run benchmarks */
    printf("Running benchmarks...\n\n");

    printf("  [1/9] Testing rgw_carray...\n");
    benchmark_array(&results[result_count++]);

    printf("  [2/9] Testing rgw_cstring...\n");
    benchmark_string(&results[result_count++]);

    printf("  [3/9] Testing rgw_cmap...\n");
    benchmark_map(&results[result_count++]);

    printf("  [4/9] Testing rgw_cset...\n");
    benchmark_set(&results[result_count++]);

    printf("  [5/9] Testing rgw_cdeque...\n");
    benchmark_deque(&results[result_count++]);

    printf("  [6/9] Testing rgw_cstack...\n");
    benchmark_stack(&results[result_count++]);

    printf("  [7/9] Testing rgw_queue...\n");
    benchmark_queue(&results[result_count++]);

    printf("  [8/9] Testing rgw_priority_queue...\n");
    benchmark_priority_queue(&results[result_count++]);

    printf("  [9/9] Testing rgw_clist...\n");
    benchmark_list(&results[result_count++]);

    /* Print results */
    print_results(results, result_count);

    printf("Benchmark completed successfully!\n");

    return 0;
}
