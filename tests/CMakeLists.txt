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

add_executable(test_ppcdisasm test_ppcdisasm.cpp
    "${DPPC_ROOT}/cpu/ppc/ppcdisasm.cpp")
target_include_directories(test_ppcdisasm PRIVATE "${DPPC_ROOT}")

add_executable(test_hwcomponent test_hwcomponent.cpp)
target_include_directories(test_hwcomponent PRIVATE "${DPPC_ROOT}")

add_executable(test_timermanager test_timermanager.cpp)
target_include_directories(test_timermanager PRIVATE "${DPPC_ROOT}")
if (UNIX)
    target_link_libraries(test_timermanager PRIVATE ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(test_pciconv test_pciconv.cpp)
target_include_directories(test_pciconv PRIVATE "${DPPC_ROOT}")

add_executable(test_dmacore test_dmacore.cpp)
target_include_directories(test_dmacore PRIVATE "${DPPC_ROOT}")

add_executable(test_mmudefs test_mmudefs.cpp)
target_include_directories(test_mmudefs PRIVATE "${DPPC_ROOT}")

add_executable(test_machineprops test_machineprops.cpp
    "${DPPC_ROOT}/machines/machineproperties.cpp"
    "${DPPC_ROOT}/thirdparty/loguru/loguru.cpp")
target_include_directories(test_machineprops PRIVATE
    "${DPPC_ROOT}"
    "${DPPC_ROOT}/thirdparty/loguru")
if (UNIX)
    target_link_libraries(test_machineprops PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(test_pcicfglog test_pcicfglog.cpp)
target_include_directories(test_pcicfglog PRIVATE "${DPPC_ROOT}")

add_executable(test_banditdefs test_banditdefs.cpp)
target_include_directories(test_banditdefs PRIVATE "${DPPC_ROOT}")

add_executable(test_checksums test_checksums.cpp)
target_include_directories(test_checksums PRIVATE "${DPPC_ROOT}")

add_executable(test_athens test_athens.cpp
    "${DPPC_ROOT}/devices/common/clockgen/athens.cpp"
    "${DPPC_ROOT}/thirdparty/loguru/loguru.cpp")
target_include_directories(test_athens PRIVATE
    "${DPPC_ROOT}"
    "${DPPC_ROOT}/thirdparty/loguru")
if (UNIX)
    target_link_libraries(test_athens PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(test_displayid test_displayid.cpp
    "${DPPC_ROOT}/devices/video/displayid.cpp"
    "${DPPC_ROOT}/machines/machineproperties.cpp"
    "${DPPC_ROOT}/thirdparty/loguru/loguru.cpp")
target_include_directories(test_displayid PRIVATE
    "${DPPC_ROOT}"
    "${DPPC_ROOT}/thirdparty/loguru")
if (UNIX)
    target_link_libraries(test_displayid PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(test_pixconv test_pixconv.cpp)
target_include_directories(test_pixconv PRIVATE "${DPPC_ROOT}")

add_executable(test_i2cprom test_i2cprom.cpp
    "${DPPC_ROOT}/devices/common/i2c/i2cprom.cpp"
    "${DPPC_ROOT}/thirdparty/loguru/loguru.cpp")
target_include_directories(test_i2cprom PRIVATE
    "${DPPC_ROOT}"
    "${DPPC_ROOT}/thirdparty/loguru")
if (UNIX)
    target_link_libraries(test_i2cprom PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(test_audioprocessor test_audioprocessor.cpp
    "${DPPC_ROOT}/thirdparty/loguru/loguru.cpp")
target_include_directories(test_audioprocessor PRIVATE
    "${DPPC_ROOT}"
    "${DPPC_ROOT}/thirdparty/loguru")
if (UNIX)
    target_link_libraries(test_audioprocessor PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(test_pcidevice test_pcidevice.cpp
    "${DPPC_ROOT}/devices/common/pci/pcibase.cpp"
    "${DPPC_ROOT}/devices/common/pci/pcidevice.cpp"
    "${DPPC_ROOT}/thirdparty/loguru/loguru.cpp")
target_include_directories(test_pcidevice PRIVATE
    "${DPPC_ROOT}"
    "${DPPC_ROOT}/thirdparty/loguru")
if (UNIX)
    target_link_libraries(test_pcidevice PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

enable_testing()
add_test(NAME test_bitops COMMAND test_bitops)
add_test(NAME test_endianswap COMMAND test_endianswap)
add_test(NAME test_mathutils COMMAND test_mathutils)
add_test(NAME test_memaccess COMMAND test_memaccess)
add_test(NAME test_coresignal COMMAND test_coresignal)
add_test(NAME test_ppcdisasm COMMAND test_ppcdisasm)
add_test(NAME test_hwcomponent COMMAND test_hwcomponent)
add_test(NAME test_timermanager COMMAND test_timermanager)
add_test(NAME test_pciconv COMMAND test_pciconv)
add_test(NAME test_dmacore COMMAND test_dmacore)
add_test(NAME test_mmudefs COMMAND test_mmudefs)
add_test(NAME test_machineprops COMMAND test_machineprops)
add_test(NAME test_pcicfglog COMMAND test_pcicfglog)
add_test(NAME test_banditdefs COMMAND test_banditdefs)
add_test(NAME test_checksums COMMAND test_checksums)
add_test(NAME test_athens COMMAND test_athens)
add_test(NAME test_displayid COMMAND test_displayid)
add_test(NAME test_pixconv COMMAND test_pixconv)
add_test(NAME test_i2cprom COMMAND test_i2cprom)
add_test(NAME test_audioprocessor COMMAND test_audioprocessor)
add_test(NAME test_pcidevice COMMAND test_pcidevice)
