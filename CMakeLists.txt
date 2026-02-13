cmake_minimum_required(VERSION 3.14)
project(dingusppc)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(PlatformGlob)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)

if (NOT WIN32 AND NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
    include_directories(${SDL2_INCLUDE_DIRS})
    if (UNIX AND NOT APPLE)
        find_package (Threads)
    endif()

elseif (WIN32) # Windows build relies on vcpkg
    # pick up system wide vcpkg if exists
    if (DEFINED ENV{VCPKG_ROOT} AND EXISTS $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
        message(STATUS "Using system vcpkg at $ENV{VCPKG_ROOT}")
        set(vcpkg_toolchain_file $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

    # check Github Actions vcpkg installation
    elseif (DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND EXISTS $ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake)
        message(STATUS "Using system vcpkg at $ENV{VCPKG_INSTALLATION_ROOT}")
        set(vcpkg_toolchain_file $ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake)

    # otherwise, fetch vcpkg from Github
    else()
        message(STATUS "Fetching latest vcpkg from Github...")

        include(FetchContent)
        FetchContent_Declare(vcpkg GIT_REPOSITORY https://github.com/microsoft/vcpkg.git)
        FetchContent_MakeAvailable(vcpkg)
        set(vcpkg_toolchain_file ${vcpkg_SOURCE_DIR}/scripts/buildsystems/vcpkg.cmake)
    endif()

    set(CMAKE_TOOLCHAIN_FILE ${vcpkg_toolchain_file})
    find_package(SDL2 CONFIG REQUIRED)
    add_compile_definitions(SDL_MAIN_HANDLED)
endif()

if (EMSCRIPTEN)
    message(STATUS "Targeting Emscripten")
    # loguru tries to include excinfo.h, which is not available under Emscripten.
    add_compile_definitions(LOGURU_STACKTRACES=0)
endif()

option(DPPC_BUILD_PPC_TESTS  "Build PowerPC tests" OFF)
option(DPPC_BUILD_BENCHMARKS "Build benchmarking programs" OFF)
option(DPPC_BUILD_FUZZERS    "Build fuzz targets" OFF)

option(DPPC_68K_DEBUGGER   "Enable 68k debugging" OFF)

if (DPPC_68K_DEBUGGER)
    # Turn off anything unnecessary.
    set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "")
    set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "")
    set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "")
    set(CAPSTONE_BUILD_DIET OFF CACHE BOOL "")
    set(CAPSTONE_OSXKERNEL_SUPPORT OFF CACHE BOOL "")

    # Disable unused Capstone architectures.
    set(CAPSTONE_ARM_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_ARM64_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_MIPS_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_PPC_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_SPARC_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_SYSZ_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_XCORE_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_X86_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_TMS320C64X_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_M680X_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_EVM_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_MOS65XX_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_WASM_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_BPF_SUPPORT OFF CACHE BOOL "")
    set(CAPSTONE_RISCV_SUPPORT OFF CACHE BOOL "")

    ADD_DEFINITIONS(-DENABLE_68K_DEBUGGER)

    add_subdirectory(thirdparty/capstone EXCLUDE_FROM_ALL)
endif()

add_subdirectory("${PROJECT_SOURCE_DIR}/core")
add_subdirectory("${PROJECT_SOURCE_DIR}/cpu/ppc/")
add_subdirectory("${PROJECT_SOURCE_DIR}/debugger/")
add_subdirectory("${PROJECT_SOURCE_DIR}/devices/")
add_subdirectory("${PROJECT_SOURCE_DIR}/machines/")
add_subdirectory("${PROJECT_SOURCE_DIR}/utils/")
add_subdirectory("${PROJECT_SOURCE_DIR}/thirdparty/loguru/")

if (NOT EMSCRIPTEN)
    set(BUILD_TESTS OFF CACHE BOOL "Build Cubeb tests")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
    set(BUILD_TOOLS OFF CACHE BOOL "Build Cubeb tools")
    add_subdirectory(thirdparty/cubeb EXCLUDE_FROM_ALL)

    # Warn if no audio backends were detected on Linux/Unix systems
    if (UNIX AND NOT APPLE)
        if (NOT USE_PULSE AND NOT USE_ALSA AND NOT USE_JACK AND
            NOT USE_SNDIO AND NOT USE_OSS AND NOT USE_SUN)
            message(WARNING
                "No Cubeb audio backends detected on Linux/Unix system!\n"
                "Audio will NOT work at runtime.\n"
                "Please install development headers for at least one of:\n"
                "  - PulseAudio (libpulse-dev / pulseaudio-libs-devel)\n"
                "  - ALSA (libasound2-dev / alsa-lib-devel)\n"
                "  - JACK (libjack-dev / jack-audio-connection-kit-devel)\n"
                "  - sndio (libsndio-dev)")
        endif()
    endif()
endif()

set(CLI11_ROOT ${PROJECT_SOURCE_DIR}/thirdparty/CLI11)

include_directories("${PROJECT_SOURCE_DIR}"
                    "${PROJECT_SOURCE_DIR}/core"
                    "${PROJECT_SOURCE_DIR}/devices"
                    "${PROJECT_SOURCE_DIR}/cpu/ppc"
                    "${PROJECT_SOURCE_DIR}/debugger"
                    "${PROJECT_SOURCE_DIR}/utils"
					"${PROJECT_SOURCE_DIR}/thirdparty/loguru/"
                    "${PROJECT_SOURCE_DIR}/thirdparty/CLI11/"
                    "${PROJECT_SOURCE_DIR}/thirdparty/cubeb/include")

platform_glob(SOURCES "${PROJECT_SOURCE_DIR}/*.cpp"
                      "${PROJECT_SOURCE_DIR}/*.c"
                      "${PROJECT_SOURCE_DIR}/*.hpp"
                      "${PROJECT_SOURCE_DIR}/*.h")

if (APPLE)
    platform_glob(APPLE_SOURCES "${PROJECT_SOURCE_DIR}/*.m")
    list(APPEND SOURCES ${APPLE_SOURCES})
endif()

file(GLOB TEST_SOURCES "${PROJECT_SOURCE_DIR}/cpu/ppc/test/*.cpp")

add_executable(dingusppc ${SOURCES} $<TARGET_OBJECTS:core>
                                    $<TARGET_OBJECTS:cpu_ppc>
                                    $<TARGET_OBJECTS:debugger>
                                    $<TARGET_OBJECTS:devices>
                                    $<TARGET_OBJECTS:machines>
                                    $<TARGET_OBJECTS:utils>
                                    $<TARGET_OBJECTS:loguru>)

if (WIN32)
    target_link_libraries(dingusppc PRIVATE SDL2::SDL2 cubeb)
    target_compile_definitions(dingusppc PRIVATE SDL_MAIN_HANDLED)
elseif (EMSCRIPTEN)
    target_link_libraries(dingusppc PRIVATE
                                    ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT}
                                    "-gsource-map"
                                    # 256 MB max for emulated Mac RAM, plus 32 MB of emulator overhead
                                    "-s INITIAL_MEMORY=301989888"
                                    "-s MODULARIZE"
                                    "-s EXPORT_ES6"
                                    "-s EXPORT_NAME=emulator"
                                    "-s 'EXTRA_EXPORTED_RUNTIME_METHODS=[\"FS\"]'")
else()
    target_link_libraries(dingusppc PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb
                                    ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

if (APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    target_link_libraries(dingusppc PRIVATE ${COCOA_LIBRARY})
endif()


if (DPPC_68K_DEBUGGER)
    target_link_libraries(dingusppc PRIVATE capstone)
endif()

if (DPPC_BUILD_PPC_TESTS)
    add_executable(testppc ${TEST_SOURCES} $<TARGET_OBJECTS:core>
                                           $<TARGET_OBJECTS:cpu_ppc>
                                           $<TARGET_OBJECTS:debugger>
                                           $<TARGET_OBJECTS:devices>
                                           $<TARGET_OBJECTS:machines>
                                           $<TARGET_OBJECTS:utils>
                                           $<TARGET_OBJECTS:loguru>)

    if (WIN32)
        target_link_libraries(testppc PRIVATE SDL2::SDL2 cubeb)
        target_compile_definitions(testppc PRIVATE SDL_MAIN_HANDLED)
    else()
        target_link_libraries(testppc PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb
                                    ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    endif()

    if (DPPC_68K_DEBUGGER)
        target_link_libraries(testppc PRIVATE capstone)
    endif()
endif()

if (DPPC_BUILD_BENCHMARKS)
    add_compile_options("-DPPC_BENCHMARKS")
    file(GLOB BENCH_SOURCES "${PROJECT_SOURCE_DIR}/benchmark/*.cpp"
                            "${PROJECT_SOURCE_DIR}/cpu/ppc/*.cpp"
                            "${PROJECT_SOURCE_DIR}/cpu/ppc/*.h"
                            )
    add_executable(bench1 ${BENCH_SOURCES} $<TARGET_OBJECTS:core>
                                           # $<TARGET_OBJECTS:cpu_ppc>
                                           $<TARGET_OBJECTS:debugger>
                                           $<TARGET_OBJECTS:devices>
                                           $<TARGET_OBJECTS:machines>
                                           $<TARGET_OBJECTS:utils>
                                           $<TARGET_OBJECTS:loguru>)

    target_link_libraries(bench1 PRIVATE cubeb SDL2::SDL2 ${CMAKE_DL_LIBS}
            ${CMAKE_THREAD_LIBS_INIT})
    if (WIN32)
        target_compile_definitions(bench1 PRIVATE SDL_MAIN_HANDLED)
    endif()

    if (DPPC_68K_DEBUGGER)
        target_link_libraries(bench1 PRIVATE capstone)
    endif()
endif()

if (DPPC_BUILD_PPC_TESTS)
    add_custom_command(
        TARGET testppc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcinttests.csv"
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcfloattests.csv"
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcdisasmtest.csv"
        $<TARGET_FILE_DIR:testppc>)
endif()

if (DPPC_BUILD_FUZZERS)
    include(CheckCXXSourceCompiles)
    set(HAVE_LIBFUZZER OFF)

    # Extra flags for brew/llvm libc++ on macOS to match fuzzer runtime
    set(FUZZ_CLANG_EXTRA "")
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        get_filename_component(CLANG_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(LLVM_PREFIX "${CLANG_BIN_DIR}/.." REALPATH)
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir OUTPUT_VARIABLE CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(LLVM_LIBCXX_DIR "${LLVM_PREFIX}/lib/c++")
        if (EXISTS "${LLVM_LIBCXX_DIR}/libc++.a" OR EXISTS "${LLVM_LIBCXX_DIR}/libc++.dylib")
            list(APPEND FUZZ_CLANG_EXTRA "-stdlib=libc++" "-L${LLVM_LIBCXX_DIR}" "-Wl,-rpath,${LLVM_LIBCXX_DIR}")
        endif()
        if (EXISTS "${LLVM_PREFIX}/include/c++/v1")
            list(APPEND FUZZ_CLANG_EXTRA "-I${LLVM_PREFIX}/include/c++/v1")
        endif()
        if (CLANG_RESOURCE_DIR AND EXISTS "${CLANG_RESOURCE_DIR}/lib/darwin")
            list(APPEND FUZZ_CLANG_EXTRA "-L${CLANG_RESOURCE_DIR}/lib/darwin" "-Wl,-rpath,${CLANG_RESOURCE_DIR}/lib/darwin")
        endif()
    endif()

    if (DEFINED ENV{LIB_FUZZING_ENGINE})
        set(HAVE_LIBFUZZER ON)
    endif()

    if (NOT HAVE_LIBFUZZER)
        # Try compiler resource dir detection
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir OUTPUT_VARIABLE CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (CLANG_RESOURCE_DIR)
            if (EXISTS "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.fuzzer_osx.a" OR EXISTS "${CLANG_RESOURCE_DIR}/lib/linux/libclang_rt.fuzzer-x86_64.a" OR EXISTS "${CLANG_RESOURCE_DIR}/lib/linux/libclang_rt.fuzzer-aarch64.a")
                set(HAVE_LIBFUZZER ON)
            endif()
        endif()
    endif()

    if (NOT HAVE_LIBFUZZER)
        string(JOIN " " CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer" ${FUZZ_CLANG_EXTRA})
        check_cxx_source_compiles("int main(){return 0;}" HAVE_LIBFUZZER)
        unset(CMAKE_REQUIRED_FLAGS)
    endif()
    if (NOT HAVE_LIBFUZZER)
        message(WARNING "libFuzzer not found. Fuzz targets will be skipped. Install clang with libFuzzer (macOS: brew install llvm; Ubuntu: apt-get install clang-15 libclang-rt-15-dev) or set LIB_FUZZING_ENGINE")
    endif()

    if (HAVE_LIBFUZZER)
    set(FUZZ_SAN_FLAGS "-fsanitize=fuzzer,address,undefined" "-fno-sanitize-recover=all" ${FUZZ_CLANG_EXTRA})

    add_executable(fuzz_ppc_insn
        fuzz/fuzz_ppc_single_instruction.cpp
        $<TARGET_OBJECTS:core>
        $<TARGET_OBJECTS:cpu_ppc>
        $<TARGET_OBJECTS:devices>
        $<TARGET_OBJECTS:machines>
        $<TARGET_OBJECTS:utils>
        $<TARGET_OBJECTS:loguru>)
    target_include_directories(fuzz_ppc_insn PRIVATE ${PROJECT_SOURCE_DIR})
    target_compile_definitions(fuzz_ppc_insn PRIVATE PPC_TESTS)
    target_compile_options(fuzz_ppc_insn PRIVATE ${FUZZ_SAN_FLAGS})
    target_link_options(fuzz_ppc_insn PRIVATE ${FUZZ_SAN_FLAGS})
    if (DEFINED ENV{LIB_FUZZING_ENGINE})
        target_link_libraries(fuzz_ppc_insn PRIVATE $ENV{LIB_FUZZING_ENGINE} SDL2::SDL2main SDL2::SDL2 cubeb ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    else()
        target_link_libraries(fuzz_ppc_insn PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    endif()

    add_executable(fuzz_ppc_sequence
        fuzz/fuzz_ppc_sequence.cpp
        $<TARGET_OBJECTS:core>
        $<TARGET_OBJECTS:cpu_ppc>
        $<TARGET_OBJECTS:devices>
        $<TARGET_OBJECTS:machines>
        $<TARGET_OBJECTS:utils>
        $<TARGET_OBJECTS:loguru>)
    target_include_directories(fuzz_ppc_sequence PRIVATE ${PROJECT_SOURCE_DIR})
    target_compile_definitions(fuzz_ppc_sequence PRIVATE PPC_TESTS)
    target_compile_options(fuzz_ppc_sequence PRIVATE ${FUZZ_SAN_FLAGS})
    target_link_options(fuzz_ppc_sequence PRIVATE ${FUZZ_SAN_FLAGS})
    if (DEFINED ENV{LIB_FUZZING_ENGINE})
        target_link_libraries(fuzz_ppc_sequence PRIVATE $ENV{LIB_FUZZING_ENGINE} SDL2::SDL2main SDL2::SDL2 cubeb ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    else()
        target_link_libraries(fuzz_ppc_sequence PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    endif()

    add_executable(fuzz_ppc_disasm
        fuzz/fuzz_disasm.cpp
        $<TARGET_OBJECTS:cpu_ppc>
        $<TARGET_OBJECTS:core>
        $<TARGET_OBJECTS:devices>
        $<TARGET_OBJECTS:machines>
        $<TARGET_OBJECTS:utils>
        $<TARGET_OBJECTS:loguru>)
    target_include_directories(fuzz_ppc_disasm PRIVATE ${PROJECT_SOURCE_DIR})
    target_compile_definitions(fuzz_ppc_disasm PRIVATE PPC_TESTS)
    target_compile_options(fuzz_ppc_disasm PRIVATE ${FUZZ_SAN_FLAGS})
    target_link_options(fuzz_ppc_disasm PRIVATE ${FUZZ_SAN_FLAGS})
    target_link_libraries(fuzz_ppc_disasm PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    if (DEFINED ENV{LIB_FUZZING_ENGINE})
        target_link_libraries(fuzz_ppc_disasm PRIVATE $ENV{LIB_FUZZING_ENGINE})
    endif()

    endif() # HAVE_LIBFUZZER
endif()

install (TARGETS dingusppc DESTINATION ${PROJECT_SOURCE_DIR}/build)
