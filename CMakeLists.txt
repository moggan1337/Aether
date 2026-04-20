cmake_minimum_required(VERSION 3.14)
project(Aether VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable color output
if(NOT CMAKE_COLOR_DIAGNOSTICS)
    set(CMAKE_COLOR_DIAGNOSTICS ON)
endif()

# Options
option(AETHER_BUILD_TESTS "Build tests" ON)
option(AETHER_BUILD_BENCHMARKS "Build benchmarks" ON)
option(AETHER_BUILD_DOCS "Build documentation" OFF)
option(AETHER_USE_HUGEPAGES "Use hugepages for memory allocations" ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -DAETHER_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")

# Find RDMA libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIBVERBS REQUIRED libibverbs)
pkg_check_modules(LIBRDMACM REQUIRED librdmacm)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${LIBIBVERBS_INCLUDE_DIRS}
    ${LIBRDMACM_INCLUDE_DIRS}
)

# Source groups
set(CORE_SOURCES
    src/core/rdma_device.c
    src/core/rdma_memory.c
    src/core/rdma_qp.c
    src/core/rdma_cq.c
    src/core/rdma_ops.c
)

set(DSM_SOURCES
    src/dsm/dsm.c
    src/dsm/dsm_array.c
)

set(RPC_SOURCES
    src/rpc/rpc.c
)

set(STRUCTURES_SOURCES
    src/structures/lockfree.c
)

set(PROFILING_SOURCES
    src/profiling/profiler.c
)

# Main library
add_library(aether STATIC
    ${CORE_SOURCES}
    ${DSM_SOURCES}
    ${RPC_SOURCES}
    ${STRUCTURES_SOURCES}
    ${PROFILING_SOURCES}
)

target_include_directories(aether PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(aether
    ${LIBIBVERBS_LIBRARIES}
    ${LIBRDMACM_LIBRARIES}
    pthread
    rt
    m
)

# Enable -fPIC for shared library
set_target_properties(aether PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Shared library version
set_target_properties(aether PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Installation
install(TARGETS aether
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Tests
if(AETHER_BUILD_TESTS)
    enable_testing()
    
    add_executable(test_lockfree tests/test_lockfree.c)
    target_link_libraries(test_lockfree aether pthread)
    add_test(NAME LockFreeTests COMMAND test_lockfree)
    
    add_executable(test_rpc tests/test_rpc.c)
    target_link_libraries(test_rpc aether pthread)
    add_test(NAME RPCTests COMMAND test_rpc)
endif()

# Benchmarks
if(AETHER_BUILD_BENCHMARKS)
    add_executable(bandwidth_bench benchmarks/bandwidth_bench.c)
    target_link_libraries(bandwidth_bench aether pthread)
    
    add_executable(latency_bench benchmarks/latency_bench.c)
    target_link_libraries(latency_bench aether pthread)
    
    add_executable(dsm_bench benchmarks/dsm_bench.c)
    target_link_libraries(dsm_bench aether pthread)
endif()

# Documentation
if(AETHER_BUILD_DOCS)
    find_package(Doxygen)
    if(Doxygen_FOUND)
        add_custom_target(doc
            ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/docs/Doxyfile
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Generating API documentation"
        )
    endif()
endif()

# pkg-config file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/aether.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/aether.pc"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/aether.pc"
    DESTINATION lib/pkgconfig
)

# Summary
message(STATUS "")
message(STATUS "=== Aether Configuration Summary ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "RDMA support: Yes")
message(STATUS "  - libibverbs: ${LIBIBVERBS_VERSION}")
message(STATUS "  - librdmacm: ${LIBRDMACM_VERSION}")
message(STATUS "Build tests: ${AETHER_BUILD_TESTS}")
message(STATUS "Build benchmarks: ${AETHER_BUILD_BENCHMARKS}")
message(STATUS "Build docs: ${AETHER_BUILD_DOCS}")
message(STATUS "Hugepages: ${AETHER_USE_HUGEPAGES}")
message(STATUS "")
