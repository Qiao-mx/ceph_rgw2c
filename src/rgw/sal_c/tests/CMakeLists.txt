# SAL C 测试 CMakeLists.txt

# 获取 sal_c 根目录 (tests 的父目录)
get_filename_component(SAL_C_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 测试可执行文件
add_executable(test_rados_driver
    ${CMAKE_CURRENT_SOURCE_DIR}/test_rados_driver.c
    ${SAL_C_ROOT}/src/core/rgw_sal_types.c
    ${SAL_C_ROOT}/src/core/rgw_sal.c
    ${SAL_C_ROOT}/src/drivers/rgw_sal_rados.c
    ${SAL_C_ROOT}/src/drivers/rgw_sal_dbstore.c
)

# 头文件目录
target_include_directories(test_rados_driver PRIVATE
    ${SAL_C_ROOT}/include
    ${SAL_C_ROOT}/include/core
    ${SAL_C_ROOT}/include/drivers
)

# 链接 math 库
target_link_libraries(test_rados_driver m)

# 启用 AddressSanitizer (开发调试时取消注释)
# target_compile_options(test_rados_driver PRIVATE -fsanitize=address -g)
# target_link_options(test_rados_driver PRIVATE -fsanitize=address)

# 添加测试
add_test(NAME test_rados_driver COMMAND test_rados_driver)
