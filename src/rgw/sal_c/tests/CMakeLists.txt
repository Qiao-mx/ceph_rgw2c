# SAL C 测试 CMakeLists.txt

# 测试可执行文件
add_executable(test_rados_driver
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw/sal_c/tests/test_rados_driver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw/sal_c/src/rgw_sal_types.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw/sal_c/src/rgw_sal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw/sal_c/src/rgw_sal_rados.c
)

# 头文件目录
target_include_directories(test_rados_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw/sal_c/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rgw/c_common/include
)

# 启用 AddressSanitizer (可选)
# target_compile_options(test_rados_driver PRIVATE -fsanitize=address -g)
# target_link_options(test_rados_driver PRIVATE -fsanitize=address)

# 添加测试
add_test(NAME test_rados_driver COMMAND test_rados_driver)
