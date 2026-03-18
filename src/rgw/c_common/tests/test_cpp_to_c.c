#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "containers/rgw_clist.h"
#include "containers/rgw_cmap.h"
#include "containers/rgw_carray.h"

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

typedef struct ContainerManager {
    rgw_clist_t *fh_list;
    rgw_map_t *fh_set;
    rgw_map_t *fh_map;
    rgw_array_t *fh_deque;
} ContainerManager;

static ContainerManager* container_manager_create(void) {
    ContainerManager *mgr = (ContainerManager*)malloc(sizeof(ContainerManager));
    if (!mgr) return NULL;
    mgr->fh_list = rgw_clist_create(file_handle_free);
    mgr->fh_set = rgw_map_create(file_handle_free);
    mgr->fh_map = rgw_map_create(file_handle_free);
    mgr->fh_deque = rgw_array_create(0);
    return mgr;
}

static void container_manager_destroy(ContainerManager *mgr) {
    if (!mgr) return;
    if (mgr->fh_list) rgw_clist_destroy(mgr->fh_list);
    if (mgr->fh_set) rgw_map_destroy(mgr->fh_set);
    if (mgr->fh_map) rgw_map_destroy(mgr->fh_map);
    if (mgr->fh_deque) rgw_array_destroy(mgr->fh_deque);
    free(mgr);
}

static void add_to_list(ContainerManager *mgr, const FileHandle *fh) {
    pthread_mutex_lock(&g_container_mutex);
    FileHandle *copy = file_handle_copy(fh);
    if (copy) {
        rgw_clist_add_tail(mgr->fh_list, copy, sizeof(FileHandle));
        printf("[List] 添加元素: %lu\n", (unsigned long)fh->fh_id);
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
                printf("[List] 删除元素: %lu\n", (unsigned long)fh_id);
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_container_mutex);
}

static void traverse_list(ContainerManager *mgr) {
    pthread_mutex_lock(&g_container_mutex);
    printf("\n[List] 遍历所有元素:\n");
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

static bool add_to_set(ContainerManager *mgr, const FileHandle *fh) {
    pthread_mutex_lock(&g_container_mutex);
    char key[32];
    snprintf(key, sizeof(key), "%lu", (unsigned long)fh->fh_id);

    if (rgw_map_contains(mgr->fh_set, key)) {
        printf("[Set] 元素已存在: %lu\n", (unsigned long)fh->fh_id);
        pthread_mutex_unlock(&g_container_mutex);
        return false;
    }

    FileHandle *copy = file_handle_copy(fh);
    if (copy) {
        rgw_map_insert(mgr->fh_set, key, copy, sizeof(FileHandle));
        printf("[Set] 添加元素: %lu\n", (unsigned long)fh->fh_id);
        pthread_mutex_unlock(&g_container_mutex);
        return true;
    }
    pthread_mutex_unlock(&g_container_mutex);
    return false;
}

static FileHandle* find_in_set(ContainerManager *mgr, uint64_t fh_id) {
    pthread_mutex_lock(&g_container_mutex);
    char key[32];
    snprintf(key, sizeof(key), "%lu", (unsigned long)fh_id);

    uint32_t val_len = 0;
    const void *val = rgw_map_find(mgr->fh_set, key, &val_len);
    if (val && val_len == sizeof(FileHandle)) {
        pthread_mutex_unlock(&g_container_mutex);
        return (FileHandle*)val;
    }
    pthread_mutex_unlock(&g_container_mutex);
    return NULL;
}

static void traverse_set(ContainerManager *mgr) {
    pthread_mutex_lock(&g_container_mutex);
    printf("\n[Set] 遍历所有元素（自动排序）:\n");
    rgw_map_iterator_t iter = rgw_map_begin(mgr->fh_set);
    while (rgw_map_iterator_valid(&iter)) {
        uint32_t val_len = 0;
        const void *val = rgw_map_iterator_value(&iter, &val_len);
        if (val && val_len == sizeof(FileHandle)) {
            file_handle_print((const FileHandle*)val);
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
        printf("[Map] 添加/更新元素: %lu\n", (unsigned long)fh->fh_id);
    }
    pthread_mutex_unlock(&g_container_mutex);
}

static bool remove_from_map(ContainerManager *mgr, uint64_t fh_id) {
    pthread_mutex_lock(&g_container_mutex);
    char key[32];
    snprintf(key, sizeof(key), "%lu", (unsigned long)fh_id);

    int ret = rgw_map_erase(mgr->fh_map, key);
    if (ret == 0) {
        printf("[Map] 删除元素: %lu\n", (unsigned long)fh_id);
        pthread_mutex_unlock(&g_container_mutex);
        return true;
    }
    printf("[Map] 元素不存在: %lu\n", (unsigned long)fh_id);
    pthread_mutex_unlock(&g_container_mutex);
    return false;
}

static void traverse_map(ContainerManager *mgr) {
    pthread_mutex_lock(&g_container_mutex);
    printf("\n[Map] 遍历所有元素（按键排序）:\n");
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
        printf("[Deque] 头部添加元素: %lu\n", (unsigned long)fh->fh_id);
    }
    pthread_mutex_unlock(&g_container_mutex);
}

static void add_to_deque_back(ContainerManager *mgr, const FileHandle *fh) {
    pthread_mutex_lock(&g_container_mutex);
    FileHandle *copy = file_handle_copy(fh);
    if (copy) {
        size_t idx = rgw_array_size(mgr->fh_deque);
        rgw_array_insert(mgr->fh_deque, idx, copy, sizeof(FileHandle));
        printf("[Deque] 尾部添加元素: %lu\n", (unsigned long)fh->fh_id);
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
    printf("\n[Deque] 遍历所有元素:\n");
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
    printf("\n[Deque] 按大小排序完成\n");
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

    printf("\n=== Test std::set (auto dedup + sort) ===\n");
    add_to_set(manager, &fh1);
    add_to_set(manager, &fh2);
    add_to_set(manager, &fh4);
    traverse_set(manager);
    FileHandle *found_set = find_in_set(manager, 101);
    if (found_set) {
        printf("\n[Set] 找到元素: ");
        file_handle_print(found_set);
    }

    printf("\n=== Test std::map ===\n");
    add_to_map(manager, &fh1);
    add_to_map(manager, &fh2);
    add_to_map(manager, &fh3);
    traverse_map(manager);
    remove_from_map(manager, 103);
    traverse_map(manager);

    printf("\n=== Test std::deque ===\n");
    add_to_deque_back(manager, &fh1);
    add_to_deque_front(manager, &fh2);
    add_to_deque_back(manager, &fh3);
    traverse_deque(manager);
    sort_deque_by_size(manager);
    traverse_deque(manager);
    FileHandle front_deque = get_deque_front(manager);
    printf("\n[Deque] 头部元素: ");
    file_handle_print(&front_deque);

    printf("\n=== Test generic algorithm (find by path) ===\n");
    FileHandle *found_list = find_in_list_by_path(manager, "/data/file1.txt");
    if (found_list) {
        printf("\n[List] 按路径找到元素: ");
        file_handle_print(found_list);
    }

    container_manager_destroy(manager);

    return 0;
}
