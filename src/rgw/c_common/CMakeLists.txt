# -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
# vim: ts=8 sw=2 smarttab ft=c

cmake_minimum_required(VERSION 3.10)
project(rgw_c_common C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Enable C++ language for interoperability tests
enable_language(CXX)

# Compiler flags - disable Werror for development
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fPIC")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# AddressSanitizer option - disabled in sandbox environment
option(WITH_ASAN "启用 AddressSanitizer" OFF)
# if(WITH_ASAN)
#     message("-- AddressSanitizer enabled")
#     set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
#     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
#     set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
#     set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
# endif()

# Valgrind support
option(WITH_VALGRIND "启用 Valgrind 支持" OFF)
if(WITH_VALGRIND)
    message("-- Valgrind support enabled")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVALGRIND")
endif()

# MSan option (MemorySanitizer for detecting uninitialized reads)
option(WITH_MSAN "启用 MemorySanitizer" OFF)
if(WITH_MSAN)
    message("-- MemorySanitizer enabled")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=memory -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=memory")
endif()

# UBSan option (Undefined Behavior Sanitizer)
option(WITH_UBSAN "启用 Undefined Behavior Sanitizer" OFF)
if(WITH_UBSAN)
    message("-- Undefined Behavior Sanitizer enabled")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
endif()

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/data_structures
)

# Source files for the library
set(C_COMMON_SOURCES
    src/rgw_cmemory.c
    src/rgw_cmap.c
    src/rgw_carray.c
    src/rgw_cstring.c
    src/rgw_coptional.c
    src/rgw_cdeque.c
    src/rgw_cstack.c
    src/rgw_cqueue.c
    src/rgw_cpriority_queue.c
    src/rgw_oop.c
    src/rgw_errors.c
    src/rgw_xml.c
    src/rgw_b64.c
    src/rgw_buffer.c
    src/rgw_hex.c
    src/rgw_acl_serde.c
    src/rgw_policy_serde.c
    containers/rgw_cset.c
    containers/rgw_clist.c
    containers/rgw_chash_map.c
)

# Create static library
add_library(rgw_c_common STATIC ${C_COMMON_SOURCES})

# Create shared library
add_library(rgw_c_common_shared SHARED ${C_COMMON_SOURCES})
set_target_properties(rgw_c_common_shared PROPERTIES OUTPUT_NAME "rgw_c_common")

# Test executables
add_executable(test_memory tests/test_memory.c)
target_link_libraries(test_memory rgw_c_common)



add_executable(test_cmap tests/test_cmap.c)
target_link_libraries(test_cmap rgw_c_common)

add_executable(test_cset tests/test_cset.c)
target_link_libraries(test_cset rgw_c_common)

add_executable(test_carray tests/test_carray.c)
target_link_libraries(test_carray rgw_c_common)

add_executable(test_cstring tests/test_cstring.c)
target_link_libraries(test_cstring rgw_c_common)

add_executable(test_coptional tests/test_coptional.c)
target_link_libraries(test_coptional rgw_c_common)

add_executable(test_ccontainer tests/test_ccontainer.c)
target_link_libraries(test_ccontainer rgw_c_common pthread)



add_executable(test_cpp_to_c tests/test_cpp_to_c.c)
target_link_libraries(test_cpp_to_c rgw_c_common pthread)

add_executable(test_cdeque tests/test_cdeque.c)
target_link_libraries(test_cdeque rgw_c_common)

add_executable(test_cstack tests/test_cstack.c)
target_link_libraries(test_cstack rgw_c_common)

add_executable(test_cqueue tests/test_cqueue.c)
target_link_libraries(test_cqueue rgw_c_common)

add_executable(test_cpriority_queue tests/test_cpriority_queue.c)
target_link_libraries(test_cpriority_queue rgw_c_common)

add_executable(test_oop tests/test_oop.c)
target_link_libraries(test_oop rgw_c_common)

add_executable(test_errors tests/test_errors.c)
target_link_libraries(test_errors rgw_c_common)

add_executable(test_buffer tests/test_buffer.c)
target_link_libraries(test_buffer rgw_c_common)

add_executable(test_hex tests/test_hex.c)
target_link_libraries(test_hex rgw_c_common)

add_executable(test_b64 tests/test_b64.c)
target_link_libraries(test_b64 rgw_c_common)

add_executable(test_xml tests/test_rgw_xml.c)
target_link_libraries(test_xml rgw_c_common)

# Comprehensive test
add_executable(test_comprehensive tests/test_comprehensive.c)
target_link_libraries(test_comprehensive rgw_c_common)

# Benchmark test
add_executable(test_benchmark tests/test_benchmark.c)
target_link_libraries(test_benchmark rgw_c_common)

# C++ interoperability test
add_executable(cpp_test tests/cpp_test.cc)
target_link_libraries(cpp_test pthread)
set_target_properties(cpp_test PROPERTIES
    LINKER_LANGUAGE CXX
    CXX_STANDARD 11
)

# C wrapper for C++ example
add_executable(test_c_wrapper examples/test_c_wrapper.cpp)
target_link_libraries(test_c_wrapper pthread)
set_target_properties(test_c_wrapper PROPERTIES
    LINKER_LANGUAGE CXX
    CXX_STANDARD 11
)

# Install targets
install(TARGETS rgw_c_common rgw_c_common_shared
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include/rgw/c_common)

# Enable testing
enable_testing()
add_test(NAME test_memory COMMAND test_memory)
add_test(NAME test_cmap COMMAND test_cmap)
add_test(NAME test_cset COMMAND test_cset)
add_test(NAME test_carray COMMAND test_carray)
add_test(NAME test_cstring COMMAND test_cstring)
add_test(NAME test_coptional COMMAND test_coptional)
add_test(NAME test_cdeque COMMAND test_cdeque)
add_test(NAME test_cstack COMMAND test_cstack)
add_test(NAME test_cqueue COMMAND test_cqueue)
add_test(NAME test_cpriority_queue COMMAND test_cpriority_queue)
add_test(NAME test_oop COMMAND test_oop)
add_test(NAME test_errors COMMAND test_errors)
add_test(NAME test_buffer COMMAND test_buffer)
add_test(NAME test_hex COMMAND test_hex)
add_test(NAME test_xml COMMAND test_xml)

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS test_memory test_cmap test_cset test_carray test_cstring test_coptional test_cdeque test_cstack test_cqueue test_cpriority_queue test_oop test_errors test_comprehensive test_benchmark cpp_test test_buffer test_hex test_xml
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all c_common tests"
)

# Print configuration summary
message(STATUS "")
message(STATUS "RGW C Common Library Configuration:")
message(STATUS "  Version: 0.1.0")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C Flags: ${CMAKE_C_FLAGS}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Sources: ${C_COMMON_SOURCES}")
message(STATUS "")
