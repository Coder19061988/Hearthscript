# Imports the Godot project (registers the extension) and runs the headless smoke test.
# cmake -DGODOT=<godot console exe> -DPROJECT=<godot project dir> -P godot_smoke.cmake
#
# Known issue (Godot 4.6.2 + godot-cpp 10.0.0): the very first import of a project whose extension
# registers a class crashes on exit (signal 11) after everything is imported. It reproduces with an
# empty RefCounted class too, so it is not Kindlescript code. A second import then succeeds.
execute_process(COMMAND "${GODOT}" --headless --path "${PROJECT}" --import RESULT_VARIABLE import_result)
if(NOT import_result EQUAL 0)
  message(STATUS "First Godot import exited with ${import_result}; retrying (see the known issue above)")
  execute_process(COMMAND "${GODOT}" --headless --path "${PROJECT}" --import RESULT_VARIABLE import_result)
endif()
if(NOT import_result EQUAL 0)
  message(FATAL_ERROR "Godot import failed: ${import_result}")
endif()
execute_process(COMMAND "${GODOT}" --headless --path "${PROJECT}" --script res://tests/smoke_test.gd RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "Godot smoke test failed: ${run_result}")
endif()
