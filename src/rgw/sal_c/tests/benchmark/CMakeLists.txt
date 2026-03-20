cmake_minimum_required(VERSION 3.10)
project(RGW_SALC_BENCHMARK C)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src/rgw/sal_c/include
    ${CMAKE_SOURCE_DIR}/src/rgw/sal_c/include/core
    ${CMAKE_SOURCE_DIR}/src/rgw/sal_c/include/drivers
)

# Source files
set(BENCHMARK_SOURCES
    sal_c_test_framework.c
)

# Create library
add_library(rgw_salc_test STATIC ${BENCHMARK_SOURCES})

# Link libraries
target_link_libraries(rgw_salc_test
    PUBLIC
        rgw_salc
        ceph-common
)
