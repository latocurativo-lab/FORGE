# FORGE Agent C -- build of Agente C only (shared/, agent/, tests/).
#
# Usage (from the project root FORGE/):
#   cmake -S agent -B build-agent -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-agent --config Release
#   ctest --test-dir build-agent --output-on-failure
#
# Or run the binaries directly: build-agent/agent/test_toolcall_parser etc.
cmake_minimum_required(VERSION 3.16)
project(forge_agent_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra)
endif()

set(FORGE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# --- shared library --------------------------------------------------------
add_library(forge_shared STATIC
    ${FORGE_ROOT}/shared/src/forge_util.c
    ${FORGE_ROOT}/shared/src/forge_events.c
    ${FORGE_ROOT}/shared/src/forge_json.c
)
target_include_directories(forge_shared PUBLIC ${FORGE_ROOT}/shared/src)

# --- agent library ---------------------------------------------------------
add_library(forge_agent STATIC
    src/toolcall_parser.c
    src/state_machine.c
    src/agent_loop.c
)
target_include_directories(forge_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(forge_agent PUBLIC forge_shared)

# --- platform/runtime integrations ----------------------------------------
# Keep the runtime adapter and the Windows workspace tools as distinct
# libraries. In particular, do not pull their implementation into agent_loop.
add_library(forge_runtime_adapter STATIC
    ${FORGE_ROOT}/runtime-adapter/forge_runtime.c
)
target_include_directories(forge_runtime_adapter PUBLIC
    ${FORGE_ROOT}/runtime-adapter
    ${FORGE_ROOT}/shared/src
)
target_link_libraries(forge_runtime_adapter PUBLIC winhttp forge_shared)

add_library(forge_tools STATIC
    ${FORGE_ROOT}/tools/forge_tools.c
)
target_include_directories(forge_tools PUBLIC ${FORGE_ROOT}/tools)
target_compile_definitions(forge_tools PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)

add_library(forge_mcp STATIC ${FORGE_ROOT}/mcp/forge_mcp.c)
target_include_directories(forge_mcp PUBLIC ${FORGE_ROOT}/mcp ${FORGE_ROOT}/shared/src)
target_link_libraries(forge_mcp PUBLIC forge_shared)

add_executable(forge src/main.c src/coding_agent.c)
target_link_libraries(forge PRIVATE forge_agent forge_runtime_adapter forge_tools forge_mcp)

# --- unit tests ------------------------------------------------------------
enable_testing()

foreach(t
    test_toolcall_parser
    test_state_machine
    test_agent_loop)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE forge_agent)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

foreach(t
    test_forge_events
    test_forge_json)
    add_executable(${t} ${FORGE_ROOT}/tests/unit/${t}.c)
    target_link_libraries(${t} PRIVATE forge_shared)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

add_executable(test_cli_protocol tests/test_cli_protocol.c src/coding_agent.c)
target_include_directories(test_cli_protocol PRIVATE ${FORGE_ROOT}/runtime-adapter ${FORGE_ROOT}/tools)
target_link_libraries(test_cli_protocol PRIVATE forge_agent forge_runtime_adapter forge_tools forge_mcp)
add_dependencies(test_cli_protocol forge)
add_test(NAME test_cli_protocol COMMAND test_cli_protocol)

if(WIN32)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(test_cancellation tests/test_cancellation.c src/coding_agent.c)
    target_include_directories(test_cancellation PRIVATE src ${FORGE_ROOT}/runtime-adapter ${FORGE_ROOT}/tools)
    target_link_libraries(test_cancellation PRIVATE forge_agent forge_runtime_adapter forge_tools forge_mcp ws2_32)
    add_test(NAME test_cancellation COMMAND test_cancellation)
    add_executable(test_studio_start tests/test_studio_start.c)
    add_test(NAME test_studio_start COMMAND test_studio_start $<TARGET_FILE:forge>)
    add_executable(test_context_fields tests/test_context_fields.c src/coding_agent.c)
    target_include_directories(test_context_fields PRIVATE ${FORGE_ROOT}/runtime-adapter ${FORGE_ROOT}/tools)
    target_link_libraries(test_context_fields PRIVATE forge_agent forge_runtime_adapter forge_tools forge_mcp)
    add_test(NAME test_context_fields COMMAND test_context_fields)
    add_test(NAME test_agent_mcp_integration COMMAND ${Python3_EXECUTABLE}
        ${FORGE_ROOT}/tests/mcp/test_agent_integration.py
        $<TARGET_FILE:forge> $<TARGET_FILE:test_cli_protocol>)
    add_test(NAME test_agentic_policy COMMAND ${Python3_EXECUTABLE}
        ${FORGE_ROOT}/tests/mcp/test_agentic_policy.py
        $<TARGET_FILE:forge>)
endif()
