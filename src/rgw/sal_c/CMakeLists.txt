# SAL C 接口 CMakeLists.txt

# SAL C 接口源文件列表
set(RGW_SAL_C_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw_sal_c/src/rgw_sal_types.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw_sal_c/src/rgw_sal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw_sal_c/src/rgw_sal_rados.c
)

# SAL C 接口头文件目录
set(RGW_SAL_C_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw_sal_c/include
)

# 创建 SAL C 库 (可选)
# 如果需要单独编译 SAL C 接口，取消注释以下行
# add_library(rgw_sal_c STATIC ${RGW_SAL_C_SOURCES})
# target_include_directories(rgw_sal_c PUBLIC ${RGW_SAL_C_INCLUDE_DIRS})
# target_link_libraries(rgw_sal_c PUBLIC rgw_c_common)

# 为其他模块提供 SAL C 接口头文件路径
set(RGW_SAL_C_INCLUDE_DIRS ${RGW_SAL_C_INCLUDE_DIRS} PARENT_SCOPE)
