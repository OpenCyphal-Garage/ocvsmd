#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

set(local_demos_dir "${submodules_dir}/demos")
set(local_libcyphal_demo_dir "${local_demos_dir}/libcyphal_demo")
# ${submodules_dir}/demos/libcyphal_demo/

option(CETL_ENABLE_DEBUG_ASSERT "Enable or disable runtime CETL asserts." ON)

if (CETL_ENABLE_DEBUG_ASSERT)
    add_compile_definitions("CETL_ENABLE_DEBUG_ASSERT=1")
endif()

# Define the LibUDPard static library build target.
add_library(integration-udpard STATIC ${submodules_dir}/libudpard/libudpard/udpard.c)
target_include_directories(integration-udpard INTERFACE SYSTEM ${submodules_dir}/libudpard/libudpard)
include(${local_demos_dir}/shared/udp/udp.cmake)

if (${PLATFORM_OS_TYPE} STREQUAL "linux")
    # Define the LibCANard static library build target.
    add_library(integration-canard STATIC ${submodules_dir}/libcanard/libcanard/canard.c)
    target_include_directories(integration-canard INTERFACE SYSTEM ${submodules_dir}/libcanard/libcanard)
    include(${local_demos_dir}/shared/socketcan/socketcan.cmake)
endif ()


# Define type generation and header library all in one go.
#
set(dsdl_types_in_demo # List all the DSDL types used in the engine
        uavcan/file/405.GetInfo.0.2.dsdl
        uavcan/file/408.Read.1.1.dsdl
        uavcan/node/430.GetInfo.1.0.dsdl
        uavcan/node/435.ExecuteCommand.1.3.dsdl
        uavcan/node/7509.Heartbeat.1.0.dsdl
        uavcan/register/384.Access.1.0.dsdl
        uavcan/register/385.List.1.0.dsdl
)

add_cyphal_library(
        NAME integration-demo
        DSDL_FILES ${dsdl_types_in_demo}
        ALLOW_EXPERIMENTAL_LANGUAGES
        LANGUAGE cpp
        LANGUAGE_STANDARD ${CYPHAL_LANGUAGE_STANDARD}
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/dsdl_transpiled
        OUT_LIBRARY_TARGET demo_transpiled
)

# Define the demo application build target and link it with the library.
add_executable(integration-demo
        ${local_libcyphal_demo_dir}/src/application.cpp
        ${local_libcyphal_demo_dir}/src/main.cpp
        ${local_libcyphal_demo_dir}/src/no_cpp_heap.cpp
        ${submodules_dir}/o1heap/o1heap/o1heap.c
)

target_link_libraries(integration-demo
        PRIVATE integration-udpard shared_udp
        PRIVATE ${demo_transpiled}
)

if (${PLATFORM_OS_TYPE} STREQUAL "linux")
    target_link_libraries(integration-demo
            PRIVATE integration-canard shared_socketcan
    )
endif ()

target_include_directories(integration-demo PRIVATE ${local_libcyphal_demo_dir}/src)
target_include_directories(integration-demo PRIVATE ${submodules_dir}/cetl/include)
target_include_directories(integration-demo PRIVATE ${submodules_dir}/libcyphal/include)
target_include_directories(integration-demo PRIVATE ${submodules_dir}/o1heap/o1heap)
