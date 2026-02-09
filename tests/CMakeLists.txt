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

add_executable(test_memaccess test_memaccess.cpp
    "${DPPC_ROOT}/thirdparty/loguru/loguru.cpp")
target_include_directories(test_memaccess PRIVATE
    "${DPPC_ROOT}"
    "${DPPC_ROOT}/thirdparty/loguru")
if (UNIX)
    find_package(Threads)
    target_link_libraries(test_memaccess PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(test_coresignal test_coresignal.cpp)
target_include_directories(test_coresignal PRIVATE "${DPPC_ROOT}")

enable_testing()
add_test(NAME test_bitops COMMAND test_bitops)
add_test(NAME test_endianswap COMMAND test_endianswap)
add_test(NAME test_mathutils COMMAND test_mathutils)
add_test(NAME test_memaccess COMMAND test_memaccess)
add_test(NAME test_coresignal COMMAND test_coresignal)
