# Converts a text file into a C array: cmake -DINPUT=... -DOUTPUT=... -DNAME=... -P embed_file.cmake
file(READ "${INPUT}" hex HEX)
# CMake regexes have no {n} quantifier, so build "32 bytes" (64 hex chars) explicitly.
set(line_pattern "")
foreach(i RANGE 63)
  string(APPEND line_pattern "[0-9a-f]")
endforeach()
string(REGEX REPLACE "(${line_pattern})" "\\1\n" hex "${hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
get_filename_component(out_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${out_dir}")
file(WRITE "${OUTPUT}"
  "// Generated from ${INPUT}. Do not edit.\n"
  "#pragma once\n"
  "static const unsigned char ${NAME}[] = {\n${bytes}0x00};\n")
