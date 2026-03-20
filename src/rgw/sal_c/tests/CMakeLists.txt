# SAL C 测试 CMakeLists.txt
# 完整测试套件 - 支持 librados

cmake_minimum_required(VERSION 3.10)

# 获取 sal_c 根目录 (tests 的父目录)
get_filename_component(SAL_C_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 获取 c_common 目录 - 相对于 sal_c 根目录
# 路径: sal_c/tests -> sal_c -> rgw -> c_common
set(C_COMMON_ROOT "${SAL_C_ROOT}/../c_common")

# 查找 librados - 使用正确的搜索路径
set(LIBRADOS_SEARCH_PATHS
    /lib/x86_64-linux-gnu
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
    /usr/lib
    /lib
)

find_library(LIBRADOS_LIBRARY 
    NAMES rados 
    PATHS ${LIBRADOS_SEARCH_PATHS}
    NO_DEFAULT_PATH
)

find_path(LIBRADOS_INCLUDE_DIR 
    NAMES rados/librados.h 
    PATHS /usr/include /usr/local/include /include
    NO_DEFAULT_PATH
)

if(LIBRADOS_LIBRARY AND LIBRADOS_INCLUDE_DIR)
    message(STATUS "Found librados: library=${LIBRADOS_LIBRARY}, include=${LIBRADOS_INCLUDE_DIR}")
    set(HAVE_LIBRADOS 1)
else()
    message(WARNING "librados not found - driver tests will be skipped")
    set(HAVE_LIBRADOS 0)
endif()

# AddressSanitizer 选项
option(WITH_ASAN "Enable AddressSanitizer for tests" OFF)

# ========================================================================
# 基础测试 - 仅测试核心类型 (不依赖 librados)
# ========================================================================

add_executable(test_basic
    ${CMAKE_CURRENT_SOURCE_DIR}/test_basic.c
    ${SAL_C_ROOT}/src/core/rgw_sal_types.c
)

target_include_directories(test_basic PRIVATE
    ${SAL_C_ROOT}/include
    ${SAL_C_ROOT}/include/core
    ${C_COMMON_ROOT}/include
)

target_link_libraries(test_basic m)

if(WITH_ASAN)
    target_compile_options(test_basic PRIVATE -fsanitize=address -g -O1)
    target_link_options(test_basic PRIVATE -fsanitize=address)
endif()

add_test(NAME test_basic COMMAND test_basic)

# ========================================================================
# RADOS 驱动测试 (需要 librados)
# ========================================================================

if(HAVE_LIBRADOS)
    # 公共源文件
    set(SAL_COMMON_SOURCES
        ${SAL_C_ROOT}/src/core/rgw_sal_types.c
        ${SAL_C_ROOT}/src/core/rgw_sal.c
    )

    # 公共包含目录 - 注意顺序很重要！
    set(SAL_INCLUDE_DIRS
        ${SAL_C_ROOT}/include
        ${SAL_C_ROOT}/include/core
        ${SAL_C_ROOT}/include/drivers
        ${C_COMMON_ROOT}/include
        ${LIBRADOS_INCLUDE_DIR}
    )

    # RADOS 驱动测试
    add_executable(test_rados_driver
        ${CMAKE_CURRENT_SOURCE_DIR}/test_rados_driver.c
        ${SAL_COMMON_SOURCES}
        ${SAL_C_ROOT}/src/rgw_sal_usage.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_rados.c
    )

    target_include_directories(test_rados_driver PRIVATE ${SAL_INCLUDE_DIRS})
    target_link_libraries(test_rados_driver ${LIBRADOS_LIBRARY} m)

    if(WITH_ASAN)
        target_compile_options(test_rados_driver PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_rados_driver PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_rados_driver COMMAND test_rados_driver)

    # DBStore 驱动测试
    add_executable(test_dbstore_driver
        ${CMAKE_CURRENT_SOURCE_DIR}/test_dbstore_driver.c
        ${SAL_COMMON_SOURCES}
        ${SAL_C_ROOT}/src/rgw_sal_usage.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_dbstore.c
    )

    target_include_directories(test_dbstore_driver PRIVATE ${SAL_INCLUDE_DIRS})
    target_link_libraries(test_dbstore_driver ${LIBRADOS_LIBRARY} m)

    if(WITH_ASAN)
        target_compile_options(test_dbstore_driver PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_dbstore_driver PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_dbstore_driver COMMAND test_dbstore_driver)

    # 集成测试
    add_executable(test_integration
        ${CMAKE_CURRENT_SOURCE_DIR}/test_integration.c
        ${SAL_COMMON_SOURCES}
        ${SAL_C_ROOT}/src/rgw_sal_usage.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_rados.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_dbstore.c
    )

    target_include_directories(test_integration PRIVATE ${SAL_INCLUDE_DIRS})
    target_link_libraries(test_integration ${LIBRADOS_LIBRARY} m)

    if(WITH_ASAN)
        target_compile_options(test_integration PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_integration PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_integration COMMAND test_integration)
endif()

# ========================================================================
# 配置信息
# ========================================================================

message(STATUS "========================================")
message(STATUS "SAL C Test Suite Configuration")
message(STATUS "========================================")
message(STATUS "SAL C Root: ${SAL_C_ROOT}")
message(STATUS "C Common Root: ${C_COMMON_ROOT}")
message(STATUS "librados: ${HAVE_LIBRADOS}")
message(STATUS "AddressSanitizer: ${WITH_ASAN}")
message(STATUS "")
message(STATUS "Available tests:")
message(STATUS "  - test_basic (core types) [BUILD]")
if(HAVE_LIBRADOS)
message(STATUS "  - test_rados_driver (RADOS) [BUILD]")
message(STATUS "  - test_dbstore_driver (DBStore) [BUILD]")
message(STATUS "  - test_integration (all drivers) [BUILD]")
else()
message(STATUS "  - test_rados_driver (requires librados) [SKIP]")
message(STATUS "  - test_dbstore_driver (requires librados) [SKIP]")
message(STATUS "  - test_integration (requires librados) [SKIP]")
endif()
message(STATUS "")
message(STATUS "To run tests:")
message(STATUS "  ctest --output-on-failure")
message(STATUS "========================================")
