cmake_minimum_required(VERSION 3.20)
project(Kindlescript LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# --- pocketpy (vendored + patched, see third_party/pocketpy/PATCHES.md) ---
add_library(pocketpy STATIC third_party/pocketpy/pocketpy.c)
target_include_directories(pocketpy PUBLIC third_party/pocketpy)
# PK_ENABLE_OS=0 removes os/io modules and the `open` builtin.
target_compile_definitions(pocketpy PUBLIC PK_ENABLE_OS=0 PK_MAX_VM_COUNT=128)
if(MSVC)
  target_compile_options(pocketpy PRIVATE /utf-8 /W0)
else()
  target_compile_options(pocketpy PRIVATE -w)
endif()

# --- minicoro (stackful coroutines: one per character script) ---
add_library(minicoro STATIC core/minicoro_impl.c)
target_include_directories(minicoro PUBLIC third_party/minicoro)

# --- prelude.py embedded into the binary ---
set(KINDLE_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${KINDLE_GEN_DIR}/prelude_py.h
  COMMAND ${CMAKE_COMMAND}
          -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/core/prelude.py
          -DOUTPUT=${KINDLE_GEN_DIR}/prelude_py.h
          -DNAME=kPreludeSource
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
  DEPENDS core/prelude.py cmake/embed_file.cmake
  COMMENT "Embedding core/prelude.py")

# --- simulation core: world + script runtime (shared by the console prototype and, later, the GDExtension) ---
add_library(kindle_core STATIC
  core/balance.cpp
  core/world.cpp
  core/runtime.cpp
  core/simulation.cpp
  ${KINDLE_GEN_DIR}/prelude_py.h)
target_include_directories(kindle_core PUBLIC core PRIVATE ${KINDLE_GEN_DIR})
target_link_libraries(kindle_core PUBLIC pocketpy minicoro)
if(MSVC)
  target_compile_options(kindle_core PUBLIC /utf-8 /W3)
else()
  target_compile_options(kindle_core PRIVATE -Wall)
endif()

# --- console prototype: run scripts, scenario tests, benchmark ---
add_executable(kindle_proto proto/main.cpp proto/tests.cpp)
target_link_libraries(kindle_proto PRIVATE kindle_core)
target_compile_definitions(kindle_proto PRIVATE KINDLE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
  target_link_libraries(kindle_proto PRIVATE psapi)
endif()

# --- tests: every scenario from proto/tests.cpp is a separate CTest test ---
enable_testing()
set(KINDLE_TESTS
  smoke_chop_sleep
  time_flow
  action_limits
  rain_interrupt
  handler_priority
  handler_exception
  exceptions_handled
  unhandled_exception
  reserved_tool
  restart_on_code_change
  syntax_error
  empty_loop
  sandbox
  wait_until
  tempo_starvation
  tempo_no_sleep
  tempo_workday
  load_50)
foreach(t IN LISTS KINDLE_TESTS)
  add_test(NAME ${t} COMMAND kindle_proto test ${t})
endforeach()
