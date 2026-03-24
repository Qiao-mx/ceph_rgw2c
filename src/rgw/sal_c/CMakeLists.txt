# -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
# vim: ts=8 sw=2 smarttab ft=cmake

# ===========================================================================
# SAL C (C-based Storage Abstraction Layer)
# CMakeLists.txt - 构建配置
# ===========================================================================

cmake_minimum_required(VERSION 3.10)
project(rgw_sal_c C CXX)

# C 和 C++ 标准设置
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 启用 C++ 以支持包装器
enable_language(CXX)

# ===========================================================================
# 编译器标志
# ===========================================================================

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -fPIC")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# AddressSanitizer 选项
option(WITH_ASAN "启用 AddressSanitizer" OFF)
if(WITH_ASAN)
    message("-- AddressSanitizer enabled for SAL C")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
endif()

# ===========================================================================
# 包含目录
# ===========================================================================

# 获取 c_common 目录路径 (sal_c/../c_common)
get_filename_component(SAL_C_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
set(C_COMMON_ROOT "${SAL_C_ROOT}/../c_common")

# librados 查找
find_library(LIBRADOS_LIBRARY NAMES rados)
find_path(LIBRADOS_INCLUDE_DIR rados/librados.h
    PATHS /usr/include /usr/local/include /usr/include/rados
)

if(LIBRADOS_LIBRARY AND LIBRADOS_INCLUDE_DIR)
    message(STATUS "Found librados: ${LIBRADOS_LIBRARY}")
    set(HAVE_LIBRADOS 1)
else()
    message(WARNING "librados not found - some features will be disabled")
    set(HAVE_LIBRADOS 0)
endif()

# 公共包含目录
set(SAL_C_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/drivers
    ${C_COMMON_ROOT}/include
)

# 添加 librados 头文件路径
if(HAVE_LIBRADOS)
    list(APPEND SAL_C_INCLUDE_DIRS ${LIBRADOS_INCLUDE_DIR})
endif()

include_directories(${SAL_C_INCLUDE_DIRS})

# ===========================================================================
# SAL C 核心源文件
# ===========================================================================

# 核心源文件列表 - 使用实际存在的文件
set(SAL_C_CORE_SOURCES
    src/rgw_sal.c
    src/rgw_sal_usage.c
)

# ===========================================================================
# SAL C 驱动源文件
# ===========================================================================

set(SAL_C_DRIVER_SOURCES
    src/drivers/rgw_sal_rados.c
)

# 条件添加其他驱动
# DBStore 驱动
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_dbstore.c")
    list(APPEND SAL_C_DRIVER_SOURCES src/drivers/rgw_sal_dbstore.c)
endif()

# DAOS 驱动
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_daos.c")
    list(APPEND SAL_C_DRIVER_SOURCES src/drivers/rgw_sal_daos.c)
endif()

# DAOS Serde 驱动
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_daos_serde.c")
    list(APPEND SAL_C_DRIVER_SOURCES src/drivers/rgw_sal_daos_serde.c)
endif()

# POSIX 驱动
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_posix.c")
    list(APPEND SAL_C_DRIVER_SOURCES src/drivers/rgw_sal_posix.c)
endif()

# Motr 驱动
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_motr.c")
    list(APPEND SAL_C_DRIVER_SOURCES src/drivers/rgw_sal_motr.c)
endif()

# D4N 驱动
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_d4n.c")
    list(APPEND SAL_C_DRIVER_SOURCES src/drivers/rgw_sal_d4n.c)
endif()

# ===========================================================================
# C++ 包装器源文件
# ===========================================================================

set(SAL_C_WRAPPER_SOURCES
    src/rgw_sal_c_wrapper.cc
)

# ===========================================================================
# 创建 SAL C 静态库
# ===========================================================================

add_library(rgw_sal_c STATIC
    ${SAL_C_CORE_SOURCES}
    ${SAL_C_DRIVER_SOURCES}
)

set_target_properties(rgw_sal_c PROPERTIES
    OUTPUT_NAME "rgw_sal_c"
    CLEAN_DIRECT_OUTPUT_1 ON
)

target_include_directories(rgw_sal_c PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/drivers
    ${C_COMMON_ROOT}/include
)

# 链接 c_common 库
if(TARGET rgw_c_common)
    target_link_libraries(rgw_sal_c PUBLIC rgw_c_common)
elseif(EXISTS "${C_COMMON_ROOT}/build/librgw_c_common.a")
    target_link_libraries(rgw_sal_c PUBLIC ${C_COMMON_ROOT}/build/librgw_c_common.a)
else()
    message(WARNING "rgw_c_common library not found, SAL C may not compile properly")
endif()

# 链接 librados (如果可用)
if(HAVE_LIBRADOS)
    target_link_libraries(rgw_sal_c PUBLIC ${LIBRADOS_LIBRARY})
endif()

# ===========================================================================
# 创建 SAL C++ 包装器库 (共享库)
# ===========================================================================

add_library(rgw_sal_c_wrapper STATIC
    ${SAL_C_WRAPPER_SOURCES}
)

set_target_properties(rgw_sal_c_wrapper PROPERTIES
    OUTPUT_NAME "rgw_sal_c_wrapper"
    CLEAN_DIRECT_OUTPUT_1 ON
)

target_include_directories(rgw_sal_c_wrapper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${C_COMMON_ROOT}/include
)

# 链接 SAL C 库到包装器
target_link_libraries(rgw_sal_c_wrapper PUBLIC rgw_sal_c)

# ===========================================================================
# 可选: 创建 SAL C 共享库
# ===========================================================================

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
if(BUILD_SHARED_LIBS)
    add_library(rgw_sal_c_shared SHARED
        ${SAL_C_CORE_SOURCES}
        ${SAL_C_DRIVER_SOURCES}
    )

    set_target_properties(rgw_sal_c_shared PROPERTIES
        OUTPUT_NAME "rgw_sal_c"
        CLEAN_DIRECT_OUTPUT_1 ON
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )

    target_include_directories(rgw_sal_c_shared PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/core
        ${CMAKE_CURRENT_SOURCE_DIR}/include/drivers
        ${C_COMMON_ROOT}/include
    )

    if(TARGET rgw_c_common)
        target_link_libraries(rgw_sal_c_shared PUBLIC rgw_c_common)
    endif()

    if(HAVE_LIBRADOS)
        target_link_libraries(rgw_sal_c_shared PUBLIC ${LIBRADOS_LIBRARY})
    endif()
endif()

# ===========================================================================
# 可选: 可执行文件测试
# ===========================================================================

option(BUILD_SAL_C_TESTS "Build SAL C test executables" OFF)
if(BUILD_SAL_C_TESTS)
    message(STATUS "SAL C tests: ENABLED")

    # 基础测试 - 仅测试核心类型
    add_executable(test_sal_basic
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_basic.c
    )

    target_include_directories(test_sal_basic PRIVATE
        ${SAL_C_INCLUDE_DIRS}
    )

    target_link_libraries(test_sal_basic PRIVATE
        rgw_sal_c
    )

    if(WITH_ASAN)
        target_compile_options(test_sal_basic PRIVATE -fsanitize=address -g -O1)
        target_link_options(test_sal_basic PRIVATE -fsanitize=address)
    endif()

    add_test(NAME test_sal_basic COMMAND test_sal_basic)

    # RADOS 驱动测试
    if(HAVE_LIBRADOS)
        add_executable(test_sal_rados_driver
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rados_driver.c
        )

        target_include_directories(test_sal_rados_driver PRIVATE
            ${SAL_C_INCLUDE_DIRS}
        )

        target_link_libraries(test_sal_rados_driver PRIVATE
            rgw_sal_c
            ${LIBRADOS_LIBRARY}
        )

        if(WITH_ASAN)
            target_compile_options(test_sal_rados_driver PRIVATE -fsanitize=address -g -O1)
            target_link_options(test_sal_rados_driver PRIVATE -fsanitize=address)
        endif()

        add_test(NAME test_sal_rados_driver COMMAND test_sal_rados_driver)
    endif()
endif()

# ===========================================================================
# 安装目标
# ===========================================================================

install(TARGETS rgw_sal_c rgw_sal_c_wrapper
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include/rgw/sal_c)

# ===========================================================================
# 配置信息输出
# ===========================================================================

message(STATUS "")
message(STATUS "SAL C Library Configuration:")
message(STATUS "  Version: 1.0.0")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  C Common: ${C_COMMON_ROOT}")
message(STATUS "  librados: ${HAVE_LIBRADOS}")
message(STATUS "  AddressSanitizer: ${WITH_ASAN}")
message(STATUS "  Build Shared Libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  Core Sources: ${SAL_C_CORE_SOURCES}")
message(STATUS "  Driver Sources: ${SAL_C_DRIVER_SOURCES}")
message(STATUS "")
