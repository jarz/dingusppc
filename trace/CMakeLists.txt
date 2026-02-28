include_directories("${PROJECT_SOURCE_DIR}"
                    "${PROJECT_SOURCE_DIR}/thirdparty/loguru/")

set(TRACE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tracemanager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tracemanager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ppc_trace.h"
)

add_library(trace OBJECT ${TRACE_SOURCES})

if (DPPC_TRACE_PPC)
    target_compile_definitions(trace PUBLIC DPPC_TRACE_PPC=1)
endif()
