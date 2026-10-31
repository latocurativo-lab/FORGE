cmake_minimum_required(VERSION 3.16)
project(forge_runtime_adapter C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT WIN32)
  message(FATAL_ERROR "The current adapter transport uses the Windows WinHTTP system API.")
endif()
add_library(forge_runtime_adapter
  forge_runtime.c
  ../shared/src/forge_json.c
  ../shared/src/forge_util.c
)
target_link_libraries(forge_runtime_adapter PRIVATE winhttp)
target_include_directories(forge_runtime_adapter PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../shared/src
)
add_executable(adapter_test test_adapter.c)
target_link_libraries(adapter_test PRIVATE forge_runtime_adapter)
enable_testing()
add_test(NAME adapter_unit COMMAND adapter_test --unit)
add_test(NAME adapter_live COMMAND adapter_test http://127.0.0.1:18932)
