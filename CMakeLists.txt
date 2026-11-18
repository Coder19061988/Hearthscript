cmake_minimum_required(VERSION 3.20)
project(Kindlescript LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# godot-cpp links the static MSVC runtime; every target in the GDExtension DLL must use the same one.
# Set before any target is created (godot-cpp's own cache default does not override this).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime for all targets")

option(KINDLE_GDEXTENSION "Build the Godot extension (needs the third_party/godot-cpp submodule)" ON)

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
  target_compile_options(kindle_core PUBLIC /utf-8 PRIVATE /W3)  # /W3 only for our code, not godot-cpp headers
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
  load_50
  seasons
  cold_and_clothes
  house_heating
  field_cycle
  mvp_rules
  mvp_winter_survival
  mvp_bad_code
  construction
  construction_rules
  globals
  workplace_busy)
foreach(t IN LISTS KINDLE_TESTS)
  add_test(NAME ${t} COMMAND kindle_proto test ${t})
endforeach()

# --- Godot GDExtension: kindle_core exposed to GDScript as KindleSimulation ---
if(KINDLE_GDEXTENSION)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/godot-cpp/CMakeLists.txt)
    message(FATAL_ERROR "godot-cpp is missing: run `git submodule update --init` (or configure with -DKINDLE_GDEXTENSION=OFF)")
  endif()
  # Godot 4.6.x is installed; an extension built for a newer API would not load in it.
  set(GODOTCPP_API_VERSION "4.6" CACHE STRING "Godot API version for godot-cpp")
  # Keep C++ exceptions enabled: otherwise godot-cpp defines _HAS_EXCEPTIONS=0 for the DLL only and
  # the STL would be compiled differently in kindle_core and in the extension.
  set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "")
  set(GODOTCPP_USE_HOT_RELOAD ON CACHE BOOL "" FORCE)  # rebuild the DLL while the editor is open (reloadable = true)
  add_subdirectory(third_party/godot-cpp EXCLUDE_FROM_ALL)

  add_library(kindlescript SHARED gdextension/register_types.cpp gdextension/kindle_simulation.cpp)
  target_link_libraries(kindlescript PRIVATE kindle_core godot::cpp)
  set_target_properties(kindlescript PROPERTIES OUTPUT_NAME kindlescript)

  set(KINDLE_GODOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/godot)
  add_custom_command(TARGET kindlescript POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KINDLE_GODOT_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:kindlescript> ${KINDLE_GODOT_DIR}/bin/kindlescript.dll
    COMMENT "Copying kindlescript.dll into godot/bin")

  # Data the game reads at runtime: one source of truth in config/ and scripts/.
  add_custom_target(kindle_godot_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KINDLE_GODOT_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/config/balance.json ${KINDLE_GODOT_DIR}/data/balance.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/scripts/woodcutter.py ${KINDLE_GODOT_DIR}/data/woodcutter.py
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mvp/settlement.py ${KINDLE_GODOT_DIR}/data/settlement.py
    COMMENT "Copying balance.json and example scripts into godot/data")

  # Headless smoke test of the Godot layer (skipped when the Godot executable is not found).
  set(KINDLE_GODOT_EXE "D:/Godot4/Godot_v4.6.2-stable_win64_console.exe" CACHE FILEPATH "Godot console executable for tests")
  if(EXISTS "${KINDLE_GODOT_EXE}")
    add_test(NAME godot_smoke
      COMMAND ${CMAKE_COMMAND} -DGODOT=${KINDLE_GODOT_EXE} -DPROJECT=${KINDLE_GODOT_DIR}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/godot_smoke.cmake)
  endif()
endif()
