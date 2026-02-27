list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(PlatformGlob)

include_directories("${PROJECT_SOURCE_DIR}"
                    "${PROJECT_SOURCE_DIR}/thirdparty/loguru/"
                    )

platform_glob(SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB NET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp")
list(APPEND SOURCES ${NET_SOURCES})

add_library(utils OBJECT ${SOURCES})

# Optional net backends
if (DINGUS_NET_ENABLE_SLIRP)
    target_compile_definitions(utils PRIVATE DINGUS_NET_ENABLE_SLIRP)
    find_package(PkgConfig)
    if (PkgConfig_FOUND)
        pkg_check_modules(SLIRP slirp)  # Debian/Ubuntu ships as 'slirp.pc'
        if (SLIRP_FOUND)
            target_include_directories(utils PRIVATE ${SLIRP_INCLUDE_DIRS})
            target_link_directories(utils INTERFACE ${SLIRP_LIBRARY_DIRS})
            target_link_libraries(utils INTERFACE ${SLIRP_LIBRARIES})
        else()
            message(WARNING "libslirp not found; slirp backend will be stubbed")
        endif()
    endif()
endif()

if (DINGUS_NET_ENABLE_PCAP)
    target_compile_definitions(utils PRIVATE DINGUS_NET_ENABLE_PCAP)
    find_package(PCAP)
    if (PCAP_FOUND)
        target_include_directories(utils PRIVATE ${PCAP_INCLUDE_DIRS})
        target_link_libraries(utils PRIVATE ${PCAP_LIBRARIES})
    else()
        message(WARNING "libpcap not found; pcap backend will be stubbed")
    endif()
endif()

if (DINGUS_NET_ENABLE_TAP AND UNIX AND NOT APPLE)
    target_compile_definitions(utils PRIVATE DINGUS_NET_ENABLE_TAP)
    # TUN/TAP uses only Linux kernel headers; no external library needed.
endif()

if (APPLE AND DINGUS_NET_ENABLE_VMNET)
    target_compile_definitions(utils PRIVATE DINGUS_NET_ENABLE_VMNET)
    # Explicitly search SDK path for vmnet.tbd
    find_library(VMNET_FRAMEWORK vmnet
        PATHS
          ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks
          /System/Library/Frameworks
    )
    if (VMNET_FRAMEWORK)
        # Add framework include path explicitly for some SDKs
        find_path(VMNET_INCLUDE_DIR vmnet/vmnet.h
            HINTS
              ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/vmnet.framework/Headers
              /System/Library/Frameworks/vmnet.framework/Headers
        )
        if (VMNET_INCLUDE_DIR)
            target_include_directories(utils PRIVATE ${VMNET_INCLUDE_DIR})
        endif()
        # Link against vmnet; since utils is an OBJECT library, propagate via INTERFACE
        target_link_libraries(utils INTERFACE ${VMNET_FRAMEWORK})
        set(DINGUS_VMNET_FRAMEWORK ${VMNET_FRAMEWORK} CACHE INTERNAL "vmnet framework path")
    else()
        message(WARNING "vmnet.framework not found; vmnet backend will be stubbed")
    endif()
endif()

target_link_libraries(utils PRIVATE)
