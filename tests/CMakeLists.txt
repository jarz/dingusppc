cmake_minimum_required(VERSION 3.14)
project(dingusppc_unit_tests CXX)

set(CMAKE_CXX_STANDARD 20)

# Root of the dingusppc source tree
set(DPPC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(test_bitops test_bitops.cpp)
target_include_directories(test_bitops PRIVATE "${DPPC_ROOT}")

add_executable(test_endianswap test_endianswap.cpp)
target_include_directories(test_endianswap PRIVATE "${DPPC_ROOT}")

add_executable(test_mathutils test_mathutils.cpp)
target_include_directories(test_mathutils PRIVATE "${DPPC_ROOT}")

enable_testing()
add_test(NAME test_bitops COMMAND test_bitops)
add_test(NAME test_endianswap COMMAND test_endianswap)
add_test(NAME test_mathutils COMMAND test_mathutils)
