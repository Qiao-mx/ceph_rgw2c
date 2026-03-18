#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "containers/rgw_clist.h"
#include "containers/rgw_cmap.h"
#include "containers/rgw_carray.h"
#include "containers/rgw_cset.h"

#define MAX_PATH_LEN 256

typedef struct FileHandle {
    uint64_t fh_id;
    char path[MAX_PATH_LEN];
    uint32_t size;
    bool is_readonly;
} FileHandle;

static pthread_mutex_t g_container_mutex = PTHREAD_MUTEX_INITIALIZER;

static void file_handle_free(void *data) {
    if (data) {
        free(data);
    }
}

static FileHandle* file_handle_create(uint64_t id, const char *p, uint32_t size, bool ro) {
    FileHandle *fh = (FileHandle*)malloc(sizeof(FileHandle));
    if (!fh) return NULL;
    fh->fh_id = id;
    snprintf(fh->path, MAX_PATH_LEN, "%s", p);
    fh->size = size;
    fh->is_readonly = ro;
    return fh;
}

static FileHandle* file_handle_copy(const FileHandle *src) {
    if (!src) return NULL;
    return file_handle_create(src->fh_id, src->path, src->size, src->is_readonly);
}

static void file_handle_print(const FileHandle *fh) {
    if (!fh) return;
    printf("ID: %lu, Path: %s, Size: %u, Readonly: %s\n",
           (unsigned long)fh->fh_id,
           fh->path,
           fh->size,
           fh->is_readonly ? "Yes" : "No");
}

static int file_handle_compare(const void *a, const void *b) {
    const FileHandle *fa = (const FileHandle*)a;
    const FileHandle *fb = (const FileHandle*)b;
    if (fa->fh_id < fb->fh_id) return -1;
    if (fa->fh_id > fb->fh_id) return 1;
    return 0;
}

typedef struct ContainerManager {
    rgw_clist_t *fh_list;
    rgw_map_t *fh_map;
    rgw_array_t *fh_deque;
} ContainerManager;

static ContainerManager* container_manager_create(void) {
    ContainerManager *mgr = (ContainerManager*)malloc(sizeof(ContainerManager));
    if (!mgr) return NULL;
    mgr->fh_list = rgw_clist_create(file_handle_free);
    mgr->fh_map = rgw_map_create(file_handle_free);
    mgr->fh_deque = rgw_array_create(0);
    return mgr;
}

static void container_manager_destroy(ContainerManager *mgr) {
    if (!mgr) return;
    if (mgr->fh_list) rgw_clist_destroy(mgr->fh_list);
    if (mgr->fh_map) rgw_map_destroy(mgr->fh_map);
    if (mgr->fh_deque) rgw_array_destroy(mgr->fh_deque);
    free(mgr);
}

static void add_to_list(ContainerManager *mgr, const FileHandle *fh) {
    pthread_mutex_lock(&g_container_mutex);
    FileHandle *copy = file_handle_copy(fh);
    if (copy) {
        rgw_clist_add_tail(mgr->fh_list, copy, sizeof(FileHandle));
        printf("[List] Add element: %lu\n", (unsigned long)fh->fh_id);
    }
    pthread_mutex_unlock(&g_container_mutex);
}

static void remove_from_list(ContainerManager *mgr, uint64_t fh_id) {
    pthread_mutex_lock(&g_container_mutex);
    size_t size = rgw_clist_size(mgr->fh_list);
    for (size_t i = 0; i < size; i++) {
        uint32_t val_len = 0;
        const void *val = rgw_clist_get(mgr->fh_list, i, &val_len);
        if (val && val_len == sizeof(FileHandle)) {
            const FileHandle *fh = (const FileHandle*)val;
            if (fh->fh_id == fh_id) {
                rgw_clist_remove(mgr->fh_list, i);
                printf("[List] Remove element: %lu\n", (unsigned long)fh_id);
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_container_mutex);
}

static void traverse_list(ContainerManager *mgr) {
    pthread_mutex_lock(&g_container_mutex);
    printf("\n[List] Traverse all elements:\n");
    rgw_clist_iterator_t iter = rgw_clist_begin(mgr->fh_list);
    while (rgw_clist_iterator_valid(&iter)) {
        uint32_t val_len = 0;
        const void *val = rgw_clist_iterator_value(&iter, &val_len);
        if (val && val_len == sizeof(FileHandle)) {
            file_handle_print((const FileHandle*)val);
        }
        rgw_clist_iterator_next(&iter);
    }
    rgw_clist_iterator_destroy(&iter);
    pthread_mutex_unlock(&g_container_mutex);
}

typedef struct SetEntry {
    uint64_t fh_id;
    FileHandle fh;
} SetEntry;

static void set_entry_free(void *data) {
    if (data) {
        free(data);
    }
}

static int compare_set_entry(const void *a, const void *b) {
    const SetEntry *sa = (const SetEntry*)a;
    const SetEntry *sb = (const SetEntry*)b;
    if (sa->fh_id < sb->fh_id) return -1;
    if (sa->fh_id > sb->fh_id) return 1;
    return 0;
}

typedef struct SetManager {
    rgw_map_t *map;
} SetManager;

static SetManager* set_manager_create(void) {
    SetManager *sm = (SetManager*)malloc(sizeof(SetManager));
    if (!sm) return NULL;
    sm->map = rgw_map_create(set_entry_free);
    return sm;
}

static void set_manager_destroy(SetManager *sm) {
    if (!sm) return;
    if (sm->map) rgw_map_destroy(sm->map);
    free(sm);
}

static bool add_to_set(SetManager *sm, const FileHandle *fh) {
    pthread_mutex_lock(&g_container_mutex);
    char key[32];
    snprintf(key, sizeof(key), "%lu", (unsigned long)fh->fh_id);

    if (rgw_map_contains(sm->map, key)) {
        printf("[Set] Element already exists: %lu\n", (unsigned long)fh->fh_id);
        pthread_mutex_unlock(&g_container_mutex);
        return false;
    }

    SetEntry *entry = (SetEntry*)malloc(sizeof(SetEntry));
    if (!entry) {
        pthread_mutex_unlock(&g_container_mutex);
        return false;
    }
    entry->fh_id = fh->fh_id;
    memcpy(&entry->fh, fh, sizeof(FileHandle));

    int ret = rgw_map_insert(sm->map, key, entry, sizeof(SetEntry));
    if (ret == 0) {
        printf("[Set] Add element: %lu\n", (unsigned long)fh->fh_id);
        pthread_mutex_unlock(&g_container_mutex);
        return true;
    }
    free(entry);
    pthread_mutex_unlock(&g_container_mutex);
    return false;
}

static FileHandle* find_in_set(SetManager *sm, uint64_t fh_id) {
    pthread_mutex_lock(&g_container_mutex);
    char key[32];
    snprintf(key, sizeof(key), "%lu", (unsigned long)fh_id);

    uint32_t val_len = 0;
    const void *val = rgw_map_find(sm->map, key, &val_len);
    if (val && val_len == sizeof(SetEntry)) {
        const SetEntry *entry = (const SetEntry*)val;
        pthread_mutex_unlock(&g_container_mutex);
        return (FileHandle*)&entry->fh;
    }
    pthread_mutex_unlock(&g_container_mutex);
    return NULL;
}

static void traverse_set(SetManager *sm) {
    pthread_mutex_lock(&g_container_mutex);
    printf("\n[Set] Traverse all elements (sorted):\n");
    rgw_map_iterator_t iter = rgw_map_begin(sm->map);
    while (rgw_map_iterator_valid(&iter)) {
        uint32_t val_len = 0;
        const void *val = rgw_map_iterator_value(&iter, &val_len);
        if (val && val_len == sizeof(SetEntry)) {
            const SetEntry *entry = (const SetEntry*)val;
            file_handle_print(&entry->fh);
        }
        rgw_map_iterator_next(&iter);
    }
    rgw_map_iterator_destroy(&iter);
    pthread_mutex_unlock(&g_container_mutex);
}

static void add_to_map(ContainerManager *mgr, const FileHandle *fh) {
    pthread_mutex_lock(&g_container_mutex);
    char key[32];
    snprintf(key, sizeof(key), "%lu", (unsigned long)fh->fh_id);

    FileHandle *copy = file_handle_copy(fh);
    if (copy) {
        rgw_map_insert(mgr->fh_map, key, copy, sizeof(FileHandle));
        printf("[Map] Add/Update element: %lu\n", (unsigned long)fh->fh_id);
    }
    pthread_mutex_unlock(&g_container_mutex);
}

static bool remove_from_map(ContainerManager *mgr, uint64_t fh_id) {
    pthread_mutex_lock(&g_container_mutex);
    char key[32];
    snprintf(key, sizeof(key), "%lu", (unsigned long)fh_id);

    int ret = rgw_map_erase(mgr->fh_map, key);
    if (ret == 0) {
        printf("[Map] Remove element: %lu\n", (unsigned long)fh_id);
        pthread_mutex_unlock(&g_container_mutex);
        return true;
    }
    printf("[Map] Element not found: %lu\n", (unsigned long)fh_id);
    pthread_mutex_unlock(&g_container_mutex);
    return false;
}

static void traverse_map(ContainerManager *mgr) {
    pthread_mutex_lock(&g_container_mutex);
    printf("\n[Map] Traverse all elements (sorted by key):\n");
    rgw_map_iterator_t iter = rgw_map_begin(mgr->fh_map);
    while (rgw_map_iterator_valid(&iter)) {
        const char *key = rgw_map_iterator_key(&iter);
        uint32_t val_len = 0;
        const void *val = rgw_map_iterator_value(&iter, &val_len);
        if (val && val_len == sizeof(FileHandle)) {
            printf("Key: %s -> ", key);
            file_handle_print((const FileHandle*)val);
        }
        rgw_map_iterator_next(&iter);
    }
    rgw_map_iterator_destroy(&iter);
    pthread_mutex_unlock(&g_container_mutex);
}

static void add_to_deque_front(ContainerManager *mgr, const FileHandle *fh) {
    pthread_mutex_lock(&g_container_mutex);
    FileHandle *copy = file_handle_copy(fh);
    if (copy) {
        rgw_array_insert(mgr->fh_deque, 0, copy, sizeof(FileHandle));
        printf("[Deque] Add element at front: %lu\n", (unsigned long)fh->fh_id);
    }
    pthread_mutex_unlock(&g_container_mutex);
}

static void add_to_deque_back(ContainerManager *mgr, const FileHandle *fh) {
    pthread_mutex_lock(&g_container_mutex);
    FileHandle *copy = file_handle_copy(fh);
    if (copy) {
        size_t idx = rgw_array_size(mgr->fh_deque);
        rgw_array_insert(mgr->fh_deque, idx, copy, sizeof(FileHandle));
        printf("[Deque] Add element at back: %lu\n", (unsigned long)fh->fh_id);
    }
    pthread_mutex_unlock(&g_container_mutex);
}

static FileHandle get_deque_front(ContainerManager *mgr) {
    FileHandle empty = {0, "", 0, false};
    pthread_mutex_lock(&g_container_mutex);
    if (!rgw_array_empty(mgr->fh_deque)) {
        uint32_t val_len = 0;
        const void *val = rgw_array_get(mgr->fh_deque, 0, &val_len);
        if (val && val_len == sizeof(FileHandle)) {
            pthread_mutex_unlock(&g_container_mutex);
            return *(FileHandle*)val;
        }
    }
    pthread_mutex_unlock(&g_container_mutex);
    return empty;
}

static void traverse_deque(ContainerManager *mgr) {
    pthread_mutex_lock(&g_container_mutex);
    printf("\n[Deque] Traverse all elements:\n");
    size_t size = rgw_array_size(mgr->fh_deque);
    for (size_t i = 0; i < size; i++) {
        uint32_t val_len = 0;
        const void *val = rgw_array_get(mgr->fh_deque, i, &val_len);
        if (val && val_len == sizeof(FileHandle)) {
            printf("Index %zu: ", i);
            file_handle_print((const FileHandle*)val);
        }
    }
    pthread_mutex_unlock(&g_container_mutex);
}

typedef struct DequeSortContext {
    rgw_array_t *array;
} DequeSortContext;

static int deque_compare_for_sort(const void *a, const void *b, void *context) {
    (void)context;
    const FileHandle *fa = (const FileHandle*)a;
    const FileHandle *fb = (const FileHandle*)b;
    if (fa->size < fb->size) return 1;
    if (fa->size > fb->size) return -1;
    return 0;
}

static void sort_deque_by_size(ContainerManager *mgr) {
    pthread_mutex_lock(&g_container_mutex);
    size_t size = rgw_array_size(mgr->fh_deque);
    if (size <= 1) {
        pthread_mutex_unlock(&g_container_mutex);
        return;
    }

    for (size_t i = 0; i < size - 1; i++) {
        for (size_t j = 0; j < size - 1 - i; j++) {
            uint32_t len1 = 0, len2 = 0;
            const void *v1 = rgw_array_get(mgr->fh_deque, j, &len1);
            const void *v2 = rgw_array_get(mgr->fh_deque, j + 1, &len2);
            if (v1 && v2 && len1 == sizeof(FileHandle) && len2 == sizeof(FileHandle)) {
                const FileHandle *fh1 = (const FileHandle*)v1;
                const FileHandle *fh2 = (const FileHandle*)v2;
                if (fh1->size < fh2->size) {
                    rgw_array_swap(mgr->fh_deque, j, j + 1);
                }
            }
        }
    }
    printf("\n[Deque] Sort by size completed\n");
    pthread_mutex_unlock(&g_container_mutex);
}

static FileHandle* find_in_list_by_path(ContainerManager *mgr, const char *path) {
    pthread_mutex_lock(&g_container_mutex);
    size_t size = rgw_clist_size(mgr->fh_list);
    for (size_t i = 0; i < size; i++) {
        uint32_t val_len = 0;
        const void *val = rgw_clist_get(mgr->fh_list, i, &val_len);
        if (val && val_len == sizeof(FileHandle)) {
            const FileHandle *fh = (const FileHandle*)val;
            if (strcmp(fh->path, path) == 0) {
                pthread_mutex_unlock(&g_container_mutex);
                return (FileHandle*)fh;
            }
        }
    }
    pthread_mutex_unlock(&g_container_mutex);
    return NULL;
}

int main(void) {
    ContainerManager *manager = container_manager_create();
    if (!manager) {
        fprintf(stderr, "Failed to create container manager\n");
        return 1;
    }

    SetManager *set_mgr = set_manager_create();
    if (!set_mgr) {
        fprintf(stderr, "Failed to create set manager\n");
        container_manager_destroy(manager);
        return 1;
    }

    FileHandle fh1 = {101, "/data/file1.txt", 1024, false};
    FileHandle fh2 = {102, "/data/file2.txt", 2048, true};
    FileHandle fh3 = {103, "/data/file3.txt", 512, false};
    FileHandle fh4 = {102, "/data/duplicate.txt", 1536, true};

    printf("=== Test std::list ===\n");
    add_to_list(manager, &fh1);
    add_to_list(manager, &fh2);
    add_to_list(manager, &fh3);
    traverse_list(manager);
    remove_from_list(manager, 102);
    traverse_list(manager);

    printf("\n=== Test std::set (using map as set) ===\n");
    add_to_set(set_mgr, &fh1);
    add_to_set(set_mgr, &fh2);
    add_to_set(set_mgr, &fh4);
    traverse_set(set_mgr);
    FileHandle *found_set = find_in_set(set_mgr, 101);
    if (found_set) {
        printf("\n[Set] Found element: ");
        file_handle_print(found_set);
    }

    printf("\n=== Test std::map ===\n");
    add_to_map(manager, &fh1);
    add_to_map(manager, &fh2);
    add_to_map(manager, &fh3);
    traverse_map(manager);
    remove_from_map(manager, 103);
    traverse_map(manager);

    printf("\n=== Test std::deque (using array) ===\n");
    add_to_deque_back(manager, &fh1);
    add_to_deque_front(manager, &fh2);
    add_to_deque_back(manager, &fh3);
    traverse_deque(manager);
    sort_deque_by_size(manager);
    traverse_deque(manager);
    FileHandle front_deque = get_deque_front(manager);
    printf("\n[Deque] Front element: ");
    file_handle_print(&front_deque);

    printf("\n=== Test generic algorithm (find by path) ===\n");
    FileHandle *found_list = find_in_list_by_path(manager, "/data/file1.txt");
    if (found_list) {
        printf("\n[List] Found element by path: ");
        file_handle_print(found_list);
    }

    set_manager_destroy(set_mgr);
    container_manager_destroy(manager);

    return 0;
}
