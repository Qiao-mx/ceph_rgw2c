# -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
# vim: ts=8 sw=2 smarttab ft=cmake

# ===========================================================================
# SAL C 测试 CMakeLists.txt
# 完整测试套件 - 支持 librados
# ===========================================================================

cmake_minimum_required(VERSION 3.10)

# 如果作为独立项目，需要定义 project
if(NOT PROJECT_NAME)
  project(sal_c_tests C)
endif()

# 获取 sal_c 根目录 (tests 的父目录)
get_filename_component(SAL_C_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 获取 c_common 目录 - 相对于 sal_c 根目录
# 路径: sal_c/tests -> sal_c -> rgw -> c_common
set(C_COMMON_ROOT "${SAL_C_ROOT}/../c_common")

# ===========================================================================
# librados 检测
# ===========================================================================

# 首先检查是否已经在父级 CMake 中找到了 librados
if(NOT LIBRADOS_LIBRARY OR NOT LIBRADOS_INCLUDE_DIR)
  # 尝试查找 librados
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
endif()

if(LIBRADOS_LIBRARY AND LIBRADOS_INCLUDE_DIR)
    message(STATUS "Found librados: library=${LIBRADOS_LIBRARY}, include=${LIBRADOS_INCLUDE_DIR}")
    set(HAVE_LIBRADOS 1)
else()
    message(WARNING "librados not found - driver tests will be skipped")
    set(HAVE_LIBRADOS 0)
endif()

# AddressSanitizer 选项
option(WITH_ASAN "Enable AddressSanitizer for tests" OFF)

# ===========================================================================
# c_common 库检测/构建
# ===========================================================================

# 检查是否有预构建的 c_common 库
set(C_COMMON_LIB "${C_COMMON_ROOT}/build/librgw_c_common.a")
if(EXISTS "${C_COMMON_LIB}")
  message(STATUS "Found pre-built c_common library: ${C_COMMON_LIB}")
  set(HAVE_C_COMMON 1)
else()
  message(WARNING "Pre-built c_common library not found at ${C_COMMON_LIB}")
  # 尝试查找共享库
  if(EXISTS "${C_COMMON_ROOT}/build/librgw_c_common.so")
    set(C_COMMON_LIB "${C_COMMON_ROOT}/build/librgw_c_common.so")
    set(HAVE_C_COMMON 1)
  else()
    set(HAVE_C_COMMON 0)
  endif()
endif()

# ========================================================================
# 基础测试 - 仅测试核心类型 (不依赖 librados)
# ========================================================================

add_executable(test_basic
    ${CMAKE_CURRENT_SOURCE_DIR}/test_basic.c
)

target_include_directories(test_basic PRIVATE
    ${SAL_C_ROOT}/include
    ${C_COMMON_ROOT}/include
)

if(HAVE_C_COMMON)
  target_link_libraries(test_basic
      ${C_COMMON_LIB}
      m
  )
else()
  # 如果没有预构建库，链接源文件
  target_link_libraries(test_basic
      ${C_COMMON_ROOT}/src/rgw_sal_types.c
      ${C_COMMON_ROOT}/src/rgw_sal_attrs.c
      ${C_COMMON_ROOT}/src/rgw_sal.c
      m
  )
endif()

if(WITH_ASAN)
    target_compile_options(test_basic PRIVATE -fsanitize=address -g -O1)
    target_link_options(test_basic PRIVATE -fsanitize=address)
endif()

add_test(NAME test_basic COMMAND test_basic)

# ========================================================================
# RADOS 驱动测试 (需要 librados)
# ========================================================================

if(HAVE_LIBRADOS)
    # 公共包含目录 - 注意顺序很重要！
    set(SAL_INCLUDE_DIRS
        ${SAL_C_ROOT}/include
        ${SAL_C_ROOT}/include/core
        ${SAL_C_ROOT}/include/drivers
        ${C_COMMON_ROOT}/include
        ${LIBRADOS_INCLUDE_DIR}
    )

    # RADOS 驱动测试 - 核心 sal_c 源文件
    set(SAL_C_DRIVER_SOURCES
        ${SAL_C_ROOT}/src/drivers/rgw_sal_rados.c
        ${SAL_C_ROOT}/src/rgw_sal_usage.c
    )

    # RADOS 驱动测试
    add_executable(test_rados_driver
        ${CMAKE_CURRENT_SOURCE_DIR}/test_rados_driver.c
        ${SAL_C_DRIVER_SOURCES}
    )

    target_include_directories(test_rados_driver PRIVATE ${SAL_INCLUDE_DIRS})
    if(HAVE_C_COMMON)
      target_link_libraries(test_rados_driver
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_LIB}
          m
      )
    else()
      target_link_libraries(test_rados_driver
          ${LIBRADOS_LIBRARY}
          m
      )
    endif()

    if(WITH_ASAN)
        target_compile_options(test_rados_driver PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_rados_driver PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_rados_driver COMMAND test_rados_driver)

    # DBStore 驱动测试 (已禁用 - 文件已删除)
    # add_executable(test_dbstore_driver
    #     ${CMAKE_CURRENT_SOURCE_DIR}/test_dbstore_driver.c
    #     ${SAL_C_ROOT}/src/drivers/rgw_sal_dbstore.c
    # )

    # target_include_directories(test_dbstore_driver PRIVATE ${SAL_INCLUDE_DIRS})
    # if(HAVE_C_COMMON)
    #   target_link_libraries(test_dbstore_driver
    #       ${LIBRADOS_LIBRARY}
    #       ${C_COMMON_LIB}
    #       m
    #   )
    # else()
    #   target_link_libraries(test_dbstore_driver
    #       ${LIBRADOS_LIBRARY}
    #       ${C_COMMON_ROOT}/src/rgw_sal_types.c
    #       ${C_COMMON_ROOT}/src/rgw_sal_attrs.c
    #       ${C_COMMON_ROOT}/src/rgw_sal_errors.c
    #       m
    #   )
    # endif()

    # if(WITH_ASAN)
    #     target_compile_options(test_dbstore_driver PRIVATE -fsanitize=address -g -O1)
    #     target_link_options(test_dbstore_driver PRIVATE -fsanitize=address)
    # endif()

    # add_test(NAME test_dbstore_driver COMMAND test_dbstore_driver)

    # 集成测试 - 仅包含 RADOS 驱动以避免 DBStore 缺失问题
    add_executable(test_integration
        ${CMAKE_CURRENT_SOURCE_DIR}/test_integration.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_rados.c
    )

    target_include_directories(test_integration PRIVATE ${SAL_INCLUDE_DIRS})
    if(HAVE_C_COMMON)
      target_link_libraries(test_integration
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_LIB}
          m
      )
    else()
      target_link_libraries(test_integration
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_ROOT}/src/rgw_sal_types.c
          ${C_COMMON_ROOT}/src/rgw_sal_attrs.c
          ${C_COMMON_ROOT}/src/rgw_sal.c
          m
      )
    endif()

    if(WITH_ASAN)
        target_compile_options(test_integration PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_integration PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_integration COMMAND test_integration)

    # ========================================================================
    # Ceph 集群集成测试
    # ========================================================================

    add_executable(test_ceph_cluster
        ${CMAKE_CURRENT_SOURCE_DIR}/test_ceph_cluster.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_rados.c
    )

    target_include_directories(test_ceph_cluster PRIVATE ${SAL_INCLUDE_DIRS})
    if(HAVE_C_COMMON)
      target_link_libraries(test_ceph_cluster
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_LIB}
          m
      )
    else()
      target_link_libraries(test_ceph_cluster
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_ROOT}/src/rgw_sal_types.c
          ${C_COMMON_ROOT}/src/rgw_sal_attrs.c
          ${C_COMMON_ROOT}/src/rgw_sal.c
          m
      )
    endif()

    if(WITH_ASAN)
        target_compile_options(test_ceph_cluster PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_ceph_cluster PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_ceph_cluster COMMAND test_ceph_cluster)

    # ========================================================================
    # RADOS SAL API 测试 (使用 sal_c 驱动封装，不直接使用 librados)
    # ========================================================================

    add_executable(test_rados_sal_api
        ${CMAKE_CURRENT_SOURCE_DIR}/test_rados_sal_api.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_rados.c
        ${SAL_C_ROOT}/src/rgw_sal_usage.c
    )

    target_include_directories(test_rados_sal_api PRIVATE ${SAL_INCLUDE_DIRS})
    if(HAVE_C_COMMON)
      target_link_libraries(test_rados_sal_api
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_LIB}
          m
      )
    else()
      target_link_libraries(test_rados_sal_api
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_ROOT}/src/rgw_sal_types.c
          ${C_COMMON_ROOT}/src/rgw_sal_attrs.c
          ${C_COMMON_ROOT}/src/rgw_sal.c
          m
      )
    endif()

    if(WITH_ASAN)
        target_compile_options(test_rados_sal_api PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_rados_sal_api PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_rados_sal_api COMMAND test_rados_sal_api)

    # ========================================================================
    # RADOS 全面测试套件
    # ========================================================================

    add_executable(test_rados_comprehensive
        ${CMAKE_CURRENT_SOURCE_DIR}/test_rados_comprehensive.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_rados.c
        ${SAL_C_ROOT}/src/rgw_sal_usage.c
    )

    target_include_directories(test_rados_comprehensive PRIVATE ${SAL_INCLUDE_DIRS})
    if(HAVE_C_COMMON)
      target_link_libraries(test_rados_comprehensive
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_LIB}
          m
      )
    else()
      target_link_libraries(test_rados_comprehensive
          ${LIBRADOS_LIBRARY}
          ${C_COMMON_ROOT}/src/rgw_sal_types.c
          ${C_COMMON_ROOT}/src/rgw_sal_attrs.c
          ${C_COMMON_ROOT}/src/rgw_sal.c
          m
      )
    endif()

    if(WITH_ASAN)
        target_compile_options(test_rados_comprehensive PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_rados_comprehensive PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_rados_comprehensive COMMAND test_rados_comprehensive)

    # ========================================================================
    # Ceph 集群连接测试 (独立程序，不需要 C SAL)
    # ========================================================================

    add_executable(test_ceph_cluster_connect
        ${CMAKE_CURRENT_SOURCE_DIR}/test_ceph_cluster_connect.c
    )

    target_include_directories(test_ceph_cluster_connect PRIVATE
        ${LIBRADOS_INCLUDE_DIR}
    )

    target_link_libraries(test_ceph_cluster_connect
        ${LIBRADOS_LIBRARY}
        m
    )

    if(WITH_ASAN)
        target_compile_options(test_ceph_cluster_connect PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_ceph_cluster_connect PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_ceph_cluster_connect COMMAND test_ceph_cluster_connect)
endif()

# ========================================================================
# DAOS 驱动测试 (需要 DS3 库)
# ========================================================================

# 检查 DS3 库是否可用
find_library(DS3_LIBRARY
    NAMES ds3
    PATHS /usr/lib /usr/local/lib /lib
)

find_path(DS3_INCLUDE_DIR
    NAMES ds3.h
    PATHS /usr/include /usr/local/include
)

if(DS3_LIBRARY AND DS3_INCLUDE_DIR)
    message(STATUS "Found DS3: library=${DS3_LIBRARY}, include=${DS3_INCLUDE_DIR}")
    set(HAVE_DS3 1)
    add_definitions(-DHAVE_DS3)
else()
    message(WARNING "DS3 library not found - DAOS tests will be skipped")
    message(WARNING "To enable DAOS tests, install DAOS SDK from https://docs.daos.io/")
    set(HAVE_DS3 0)
endif()

if(HAVE_DS3)
    # DAOS 驱动测试
    add_executable(test_daos_driver
        ${CMAKE_CURRENT_SOURCE_DIR}/test_daos_driver.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_daos.c
        ${SAL_C_ROOT}/src/drivers/rgw_sal_daos_serde.c
    )

    target_include_directories(test_daos_driver PRIVATE ${SAL_INCLUDE_DIRS})
    if(HAVE_C_COMMON)
      target_link_libraries(test_daos_driver
          ${DS3_LIBRARY}
          ${C_COMMON_LIB}
          m
      )
    else()
      target_link_libraries(test_daos_driver
          ${DS3_LIBRARY}
          ${C_COMMON_ROOT}/src/rgw_sal_types.c
          ${C_COMMON_ROOT}/src/rgw_sal_attrs.c
          ${C_COMMON_ROOT}/src/rgw_sal.c
          m
      )
    endif()

    if(WITH_ASAN)
        target_compile_options(test_daos_driver PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_daos_driver PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_daos_driver COMMAND test_daos_driver)
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
message(STATUS "DS3 (DAOS): ${HAVE_DS3}")
message(STATUS "AddressSanitizer: ${WITH_ASAN}")
message(STATUS "")
message(STATUS "Available tests:")
message(STATUS "  - test_basic (core types) [BUILD]")
if(HAVE_LIBRADOS)
message(STATUS "  - test_rados_driver (RADOS) [BUILD]")
message(STATUS "  - test_dbstore_driver (DBStore) [SKIP - disabled]")
message(STATUS "  - test_integration (RADOS driver) [BUILD]")
message(STATUS "  - test_ceph_cluster (cluster integration) [BUILD]")
message(STATUS "  - test_rados_sal_api (SAL API tests) [BUILD]")
message(STATUS "  - test_rados_comprehensive (comprehensive tests) [BUILD]")
message(STATUS "  - test_ceph_cluster_connect (cluster connect) [BUILD]")
else()
message(STATUS "  - test_rados_driver (requires librados) [SKIP]")
message(STATUS "  - test_dbstore_driver (DBStore) [SKIP - disabled]")
message(STATUS "  - test_integration (requires librados) [SKIP]")
message(STATUS "  - test_ceph_cluster (requires librados) [SKIP]")
endif()
if(HAVE_DS3)
message(STATUS "  - test_daos_driver (DAOS) [BUILD]")
else()
message(STATUS "  - test_daos_driver (requires DS3/DAOS) [SKIP]")
endif()
message(STATUS "")
message(STATUS "To run tests:")
message(STATUS "  ctest --output-on-failure")
message(STATUS "========================================")
