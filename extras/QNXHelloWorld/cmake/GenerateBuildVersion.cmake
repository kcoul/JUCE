cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT must point at the JUCE repository root")
endif()

if (NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "OUTPUT_HEADER must point at the generated header path")
endif()

set(version_file "${ROOT}/extras/QNXHelloWorld/BuildVersion.txt")

if (EXISTS "${version_file}")
    file(READ "${version_file}" current_build_number)
    string(STRIP "${current_build_number}" current_build_number)
else()
    set(current_build_number "0")
endif()

if (current_build_number STREQUAL "")
    set(current_build_number "0")
endif()

math(EXPR next_build_number "${current_build_number} + 1")
set(next_build_suffix "${next_build_number}")

while (TRUE)
    string(LENGTH "${next_build_suffix}" next_build_suffix_length)

    if (next_build_suffix_length GREATER_EQUAL 5)
        break()
    endif()

    set(next_build_suffix "0${next_build_suffix}")
endwhile()

set(next_build_version "0.1.${next_build_suffix}")

file(WRITE "${version_file}" "${next_build_number}\n")
get_filename_component(output_dir "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT_HEADER}"
"#pragma once\n"
"#define JUCE_QNX_HELLO_WORLD_BUILD_NUMBER ${next_build_number}\n"
"#define JUCE_QNX_HELLO_WORLD_BUILD_VERSION \"${next_build_version}\"\n")

message(STATUS "Generated JUCE QNX Hello World build version ${next_build_version}")
