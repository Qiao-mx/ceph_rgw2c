/**
 * @file test_basic.c
 * @brief 简单测试SAL基本功能
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rgw_sal.h"
#include "rgw_sal_rados.h"

int main(void) {
    printf("Starting basic test...\n");
    fflush(stdout);

    printf("Calling rgw_sal_create_driver...\n");
    fflush(stdout);
    
    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    
    printf("Driver created: %p\n", (void*)driver);
    fflush(stdout);
    
    if (!driver) {
        printf("FAILED: driver is NULL\n");
        return 1;
    }

    printf("Driver vtable: %p\n", (void*)driver->vtable);
    fflush(stdout);

    if (!driver->vtable) {
        printf("FAILED: vtable is NULL\n");
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    printf("Calling get_name...\n");
    fflush(stdout);
    
    const char* name = driver->vtable->get_name(driver);
    
    printf("Driver name: '%s'\n", name ? name : "NULL");
    fflush(stdout);

    rgw_sal_destroy_driver(driver);
    
    printf("Test passed!\n");
    return 0;
}
