# SAL C 接口 CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(RGWSALC C)

# SAL C 接口头文件目录
set(RGW_SAL_C_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/drivers
)

# SAL C 接口核心源文件
set(RGW_SAL_C_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/rgw_sal_types.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/rgw_sal.c
)

# SAL C 接口驱动源文件
set(RGW_SAL_C_DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_rados.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_dbstore.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/rgw_sal_posix.c
)

# 合并所有源文件
set(RGW_SAL_C_SOURCES
    ${RGW_SAL_C_CORE_SOURCES}
    ${RGW_SAL_C_DRIVER_SOURCES}
)

# 包含 tests 子目录
add_subdirectory(tests)
