cmake_minimum_required(VERSION 3.20)

project(JUCEQNXHelloWorld LANGUAGES C CXX)

set(JUCE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
get_filename_component(JUCE_ROOT_DIR "${JUCE_ROOT_DIR}" ABSOLUTE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(generated_build_version_header "${CMAKE_CURRENT_BINARY_DIR}/GeneratedBuildVersion.h")

add_custom_command(
    OUTPUT "${generated_build_version_header}"
    COMMAND "${CMAKE_COMMAND}"
            -DROOT="${JUCE_ROOT_DIR}"
            -DOUTPUT_HEADER="${generated_build_version_header}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildVersion.cmake"
    VERBATIM
)

add_custom_target(juce_qnx_hello_world_build_version DEPENDS "${generated_build_version_header}")

add_executable(JUCEQNXHelloWorld
    "${JUCE_ROOT_DIR}/modules/juce_core/juce_core.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_core/juce_core_CompilationTime.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_events/juce_events.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_graphics/juce_graphics.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_graphics/juce_graphics_Harfbuzz.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_graphics/unicode/sheenbidi/Source/SheenBidi.c"
    "${JUCE_ROOT_DIR}/modules/juce_data_structures/juce_data_structures.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_gui_basics/juce_gui_basics.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_gui_extra/juce_gui_extra.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_audio_basics/juce_audio_basics.cpp"
    "${JUCE_ROOT_DIR}/modules/juce_audio_devices/juce_audio_devices.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Main.cpp"
)

add_dependencies(JUCEQNXHelloWorld juce_qnx_hello_world_build_version)

target_include_directories(JUCEQNXHelloWorld PRIVATE
    "${JUCE_ROOT_DIR}"
    "${JUCE_ROOT_DIR}/modules"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_compile_definitions(JUCEQNXHelloWorld PRIVATE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_JACK=0
    JUCE_ALSA=1
    JUCE_USE_FONTCONFIG=0
    SB_CONFIG_UNITY=1
)

target_link_libraries(JUCEQNXHelloWorld PRIVATE
    screen
    asound
    socket
    z
    expat
)
