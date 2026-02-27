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
option(DPPC_BUILD_NET_TESTS  "Build networking tests" OFF)
option(DPPC_BUILD_BENCHMARKS "Build benchmarking programs" OFF)
option(DPPC_BUILD_FUZZ       "Build fuzz targets (requires Clang and libFuzzer)" OFF)

# Networking backends (optional)
option(DINGUS_NET_ENABLE_SLIRP "Enable libslirp networking backend" OFF)
option(DINGUS_NET_ENABLE_PCAP  "Enable libpcap networking backend" OFF)
option(DINGUS_NET_ENABLE_VMNET "Enable vmnet.framework backend (macOS)" OFF)
option(DINGUS_NET_ENABLE_TAP   "Enable Linux TUN/TAP networking backend" OFF)

# Auto-detect libslirp/libpcap when the option has not been explicitly set to ON.
# Always run pkg_check_modules so SLIRP_LIBRARIES / PCAP_LIBRARIES are populated
# for use in target_link_libraries below, regardless of whether the option was
# set by auto-detection or by the user passing -DDINGUS_NET_ENABLE_SLIRP=ON.
find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(SLIRP slirp)  # Debian/Ubuntu ships as 'slirp.pc'
    if (SLIRP_FOUND AND NOT DINGUS_NET_ENABLE_SLIRP)
        message(STATUS "libslirp found; enabling slirp backend")
        set(DINGUS_NET_ENABLE_SLIRP ON CACHE BOOL "Enable libslirp networking backend" FORCE)
    endif()
    pkg_check_modules(PCAP libpcap)
    if (PCAP_FOUND AND NOT DINGUS_NET_ENABLE_PCAP)
        message(STATUS "libpcap found; enabling pcap backend")
        set(DINGUS_NET_ENABLE_PCAP ON CACHE BOOL "Enable libpcap networking backend" FORCE)
    endif()
endif()
# Auto-enable the TAP backend on Linux (no external library required).
if (UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    if (EXISTS "/dev/net/tun")
        message(STATUS "/dev/net/tun present; enabling tap backend")
        set(DINGUS_NET_ENABLE_TAP ON CACHE BOOL "Enable Linux TUN/TAP networking backend")
    else()
        message(STATUS "/dev/net/tun not found; tap backend will be stubbed")
    endif()
endif()
# Ensure the CMake option also becomes a preprocessor define for all targets
# (useful for shared code paths like ppcmmu logging throttling).
if (DPPC_BUILD_FUZZ)
    add_compile_definitions(DPPC_BUILD_FUZZ=1)
endif()

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

if (APPLE)
add_executable(dingusppc MACOSX_BUNDLE ${SOURCES} 
    $<TARGET_OBJECTS:core>
    $<TARGET_OBJECTS:cpu_ppc>
    $<TARGET_OBJECTS:debugger>
    $<TARGET_OBJECTS:devices>
    $<TARGET_OBJECTS:machines>
    $<TARGET_OBJECTS:utils>
    $<TARGET_OBJECTS:loguru>)
else()
add_executable(dingusppc ${SOURCES} $<TARGET_OBJECTS:core>
                                    $<TARGET_OBJECTS:cpu_ppc>
                                    $<TARGET_OBJECTS:debugger>
                                    $<TARGET_OBJECTS:devices>
                                    $<TARGET_OBJECTS:machines>
                                    $<TARGET_OBJECTS:utils>
                                    $<TARGET_OBJECTS:loguru>)
endif()

# 2. Configure Apple-specific Bundle Properties
if (APPLE)
    # Identify if we are targeting iOS or macOS
    if (IOS)
        set(BUNDLE_ID "com.dingusdev.dingusppc-ios")
        message(STATUS "Configuring for iOS Bundle")
    else()
        set(BUNDLE_ID "com.dingusdev.dingusppc")
        message(STATUS "Configuring for macOS Bundle")
    endif()

    set_target_properties(dingusppc PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "${BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "DingusPPC"
        MACOSX_BUNDLE_BUNDLE_VERSION "alpha 1.0.4"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "a1.0.4"
        MACOSX_BUNDLE_EXECUTABLE_NAME "DingusPPC"
        # Placeholder for the icon file:
        # MACOSX_BUNDLE_ICON_FILE "icon.icns"
    )

    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    
    target_link_libraries(dingusppc PRIVATE 
        ${COCOA_LIBRARY} 
        ${IOKIT_LIBRARY} 
        ${COREAUDIO_LIBRARY} 
        ${AUDIOTOOLBOX_LIBRARY}
    )

    if (IOS)
        target_link_libraries(dingusppc PRIVATE "-framework Foundation" "-framework UIKit")
    endif()
endif()

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
                                    "-s 'EXPORTED_RUNTIME_METHODS=[\"FS\"]'")
else()
    target_link_libraries(dingusppc PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb
                                    ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

if (APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    target_link_libraries(dingusppc PRIVATE ${COCOA_LIBRARY})
    if (DINGUS_VMNET_FRAMEWORK)
        target_link_libraries(dingusppc PRIVATE ${DINGUS_VMNET_FRAMEWORK})
    endif()
endif()

# Collect extra link libraries needed for optional networking backends.
# These can't rely on OBJECT library INTERFACE propagation when consumers
# use $<TARGET_OBJECTS:utils> directly, so we link them explicitly here.
set(DINGUS_NET_EXTRA_LIBS "")
if (DINGUS_NET_ENABLE_SLIRP AND SLIRP_FOUND)
    list(APPEND DINGUS_NET_EXTRA_LIBS ${SLIRP_LIBRARIES})
    target_link_directories(dingusppc PRIVATE ${SLIRP_LIBRARY_DIRS})
endif()
if (DINGUS_NET_ENABLE_PCAP AND PCAP_FOUND)
    list(APPEND DINGUS_NET_EXTRA_LIBS ${PCAP_LIBRARIES})
    target_link_directories(dingusppc PRIVATE ${PCAP_LIBRARY_DIRS})
endif()
if (DINGUS_NET_EXTRA_LIBS)
    target_link_libraries(dingusppc PRIVATE ${DINGUS_NET_EXTRA_LIBS})
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
    if (APPLE AND DINGUS_VMNET_FRAMEWORK)
        target_link_libraries(testppc PRIVATE ${DINGUS_VMNET_FRAMEWORK})
    endif()
    if (DINGUS_NET_EXTRA_LIBS)
        target_link_libraries(testppc PRIVATE ${DINGUS_NET_EXTRA_LIBS})
    endif()

    enable_testing()
    add_test(NAME testppc COMMAND testppc WORKING_DIRECTORY $<TARGET_FILE_DIR:testppc>)
endif()

option(DPPC_BUILD_CUDA_TESTS "Build ViaCuda tests" OFF)

if (DPPC_BUILD_CUDA_TESTS)
    add_executable(testcuda tests/test_viacuda.cpp
                                           $<TARGET_OBJECTS:core>
                                           $<TARGET_OBJECTS:cpu_ppc>
                                           $<TARGET_OBJECTS:debugger>
                                           $<TARGET_OBJECTS:devices>
                                           $<TARGET_OBJECTS:machines>
                                           $<TARGET_OBJECTS:utils>
                                           $<TARGET_OBJECTS:loguru>)

    if (WIN32)
        target_link_libraries(testcuda PRIVATE SDL2::SDL2 cubeb)
        target_compile_definitions(testcuda PRIVATE SDL_MAIN_HANDLED)
    else()
        target_link_libraries(testcuda PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb
                                    ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    endif()

    if (DPPC_68K_DEBUGGER)
        target_link_libraries(testcuda PRIVATE capstone)
    endif()

    add_test(NAME testcuda COMMAND testcuda)
endif()

option(DPPC_BUILD_NET_TESTS "Build networking tests" OFF)

if (DPPC_BUILD_NET_TESTS)
    file(GLOB NET_TEST_SOURCES "${PROJECT_SOURCE_DIR}/tests/net/*.cpp")
    add_executable(testnet ${NET_TEST_SOURCES} $<TARGET_OBJECTS:core>
                                               $<TARGET_OBJECTS:cpu_ppc>
                                               $<TARGET_OBJECTS:debugger>
                                               $<TARGET_OBJECTS:devices>
                                               $<TARGET_OBJECTS:machines>
                                               $<TARGET_OBJECTS:utils>
                                               $<TARGET_OBJECTS:loguru>)

    if (WIN32)
        target_link_libraries(testnet PRIVATE SDL2::SDL2 cubeb)
        target_compile_definitions(testnet PRIVATE SDL_MAIN_HANDLED)
    else()
        target_link_libraries(testnet PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb
                                    ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    endif()

    if (DPPC_68K_DEBUGGER)
        target_link_libraries(testnet PRIVATE capstone)
    endif()
    if (APPLE AND DINGUS_VMNET_FRAMEWORK)
        target_link_libraries(testnet PRIVATE ${DINGUS_VMNET_FRAMEWORK})
    endif()
    if (DINGUS_NET_EXTRA_LIBS)
        target_link_libraries(testnet PRIVATE ${DINGUS_NET_EXTRA_LIBS})
    endif()

    enable_testing()
    add_test(NAME testnet COMMAND testnet WORKING_DIRECTORY $<TARGET_FILE_DIR:testnet>)
endif()

option(DPPC_BUILD_THREAD_TESTS "Build thread safety tests" OFF)

if (DPPC_BUILD_THREAD_TESTS)
    add_executable(testthread tests/test_thread_safety.cpp
                                           $<TARGET_OBJECTS:core>
                                           $<TARGET_OBJECTS:cpu_ppc>
                                           $<TARGET_OBJECTS:debugger>
                                           $<TARGET_OBJECTS:devices>
                                           $<TARGET_OBJECTS:machines>
                                           $<TARGET_OBJECTS:utils>
                                           $<TARGET_OBJECTS:loguru>)

    if (WIN32)
        target_link_libraries(testthread PRIVATE SDL2::SDL2 cubeb)
        target_compile_definitions(testthread PRIVATE SDL_MAIN_HANDLED)
    else()
        target_link_libraries(testthread PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb
                                    ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    endif()

    if (DPPC_68K_DEBUGGER)
        target_link_libraries(testthread PRIVATE capstone)
    endif()

    add_test(NAME testthread COMMAND testthread)
endif()

option(DPPC_BUILD_DBDMA_TESTS "Build DBDMA tests" OFF)

if (DPPC_BUILD_DBDMA_TESTS)
    add_executable(testdbdma tests/test_dbdma.cpp
                                           $<TARGET_OBJECTS:core>
                                           $<TARGET_OBJECTS:cpu_ppc>
                                           $<TARGET_OBJECTS:debugger>
                                           $<TARGET_OBJECTS:devices>
                                           $<TARGET_OBJECTS:machines>
                                           $<TARGET_OBJECTS:utils>
                                           $<TARGET_OBJECTS:loguru>)

    if (WIN32)
        target_link_libraries(testdbdma PRIVATE SDL2::SDL2 cubeb)
        target_compile_definitions(testdbdma PRIVATE SDL_MAIN_HANDLED)
    else()
        target_link_libraries(testdbdma PRIVATE SDL2::SDL2main SDL2::SDL2 cubeb
                                    ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
    endif()

    if (DPPC_68K_DEBUGGER)
        target_link_libraries(testdbdma PRIVATE capstone)
    endif()

    enable_testing()
    add_test(NAME testdbdma COMMAND testdbdma)
endif()

if (DPPC_BUILD_BENCHMARKS)
    add_compile_options("-DPPC_BENCHMARKS")

    # Gather CPU/PPC sources explicitly (benchmarks live under benchmark/)
    # PPC_SOURCES is provided by cpu/ppc/CMakeLists.txt

    add_executable(benchmarks "${PROJECT_SOURCE_DIR}/benchmark/bench_driver.cpp"
                               "${PROJECT_SOURCE_DIR}/benchmark/bench_common.cpp"
                               "${PROJECT_SOURCE_DIR}/benchmark/bench_checksum.cpp"
                               "${PROJECT_SOURCE_DIR}/benchmark/bench_dispatch.cpp"
                               ${PPC_SOURCES}
                               $<TARGET_OBJECTS:core>
                               $<TARGET_OBJECTS:debugger>
                               $<TARGET_OBJECTS:devices>
                               $<TARGET_OBJECTS:machines>
                               $<TARGET_OBJECTS:utils>
                               $<TARGET_OBJECTS:loguru>)

    target_link_libraries(benchmarks PRIVATE cubeb SDL2::SDL2 ${CMAKE_DL_LIBS}
            ${CMAKE_THREAD_LIBS_INIT})
    if (WIN32)
        target_compile_definitions(benchmarks PRIVATE SDL_MAIN_HANDLED)
    endif()

    if (DPPC_68K_DEBUGGER)
        target_link_libraries(benchmarks PRIVATE capstone)
    endif()
    if (APPLE AND DINGUS_VMNET_FRAMEWORK)
        target_link_libraries(benchmarks PRIVATE ${DINGUS_VMNET_FRAMEWORK})
    endif()
endif()

if (DPPC_BUILD_PPC_TESTS)
    add_custom_command(
        TARGET testppc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcinttests.csv"
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcfloattests.csv"
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcdisasmtest.csv"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>)
    # Also copy test CSVs to testppc binary directory for convenience
    add_custom_command(
        TARGET testppc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcinttests.csv"
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcfloattests.csv"
        "${PROJECT_SOURCE_DIR}/cpu/ppc/test/ppcdisasmtest.csv"
        $<TARGET_FILE_DIR:testppc>)
endif()

if (DPPC_BUILD_FUZZ)
    # Using PPC_SOURCES from cpu/ppc/CMakeLists.txt to avoid duplicate globs

    add_library(fuzz_shims OBJECT fuzz/libcxx_shims.cpp)
    target_compile_options(fuzz_shims PRIVATE -fno-omit-frame-pointer)

    set(FUZZ_COMMON_OBJECTS $<TARGET_OBJECTS:core>
                            $<TARGET_OBJECTS:debugger>
                            $<TARGET_OBJECTS:devices>
                            $<TARGET_OBJECTS:machines>
                            $<TARGET_OBJECTS:utils>
                            $<TARGET_OBJECTS:loguru>
                            $<TARGET_OBJECTS:fuzz_shims>)

    set(FUZZ_COMMON_LIBS SDL2::SDL2main SDL2::SDL2 cubeb
                         ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})

    # Helper: declare a fuzz target with the standard flags.
    # Combines libFuzzer with ASan + UBSan for maximum bug detection.
    function(add_fuzz_target TARGET_NAME)
        target_compile_definitions(${TARGET_NAME} PRIVATE PPC_TESTS DPPC_BUILD_FUZZ=1)
        # Default: rely on compiler-rt's libFuzzer (provides main())
        target_compile_options(${TARGET_NAME} PRIVATE
            -fsanitize=fuzzer,address,undefined
            -fno-sanitize-recover=undefined
            -fno-omit-frame-pointer)
        target_link_options(${TARGET_NAME} PRIVATE
            -fsanitize=fuzzer,address,undefined)
        target_link_libraries(${TARGET_NAME} PRIVATE ${FUZZ_COMMON_LIBS})
        # Homebrew LLVM on Apple Silicon ships compiler-rt but may lack
        # libclang_rt.fuzzer_osx.a or newer libc++ ABI bits (__hash_memory).
        # Provide optional fallback linking with libFuzzer.a if available
        # (keeps Linux CI untouched) and shim missing symbols in fuzz_shims.
        find_library(LIB_FUZZER NAMES libFuzzer.a PATHS ${LLVM_LIBRARY_DIRS} /opt/homebrew/opt/llvm/lib /usr/lib/llvm*/lib /usr/lib/clang/*/lib/linux NO_DEFAULT_PATH)
        if (LIB_FUZZER)
            target_link_libraries(${TARGET_NAME} PRIVATE ${LIB_FUZZER})
        endif()
        if (DPPC_68K_DEBUGGER)
            target_link_libraries(${TARGET_NAME} PRIVATE capstone)
        endif()
    endfunction()

    # 1) Single-instruction PPC fuzzer
    add_executable(fuzz_ppc_single_instruction
        "${PROJECT_SOURCE_DIR}/fuzz/fuzz_ppc_single_instruction.cpp"
        ${PPC_SOURCES}
        ${FUZZ_COMMON_OBJECTS})
    add_fuzz_target(fuzz_ppc_single_instruction)

    # 2) PPC disassembler fuzzer
    add_executable(fuzz_ppc_disasm
        "${PROJECT_SOURCE_DIR}/fuzz/fuzz_ppc_disasm.cpp"
        ${PPC_SOURCES}
        ${FUZZ_COMMON_OBJECTS})
    add_fuzz_target(fuzz_ppc_disasm)

    # 3) MMIO device register fuzzer
    add_executable(fuzz_mmio_device
        "${PROJECT_SOURCE_DIR}/fuzz/fuzz_mmio_device.cpp"
        "${PROJECT_SOURCE_DIR}/fuzz/fuzz_mmio_extras.cpp"
        ${PPC_SOURCES}
        ${FUZZ_COMMON_OBJECTS})
    add_fuzz_target(fuzz_mmio_device)

    # 4) PCI config space fuzzer
    add_executable(fuzz_pci_cfg
        "${PROJECT_SOURCE_DIR}/fuzz/fuzz_pci_cfg.cpp"
        ${PPC_SOURCES}
        ${FUZZ_COMMON_OBJECTS})
    add_fuzz_target(fuzz_pci_cfg)
endif()

install (TARGETS dingusppc DESTINATION ${PROJECT_SOURCE_DIR}/build)
