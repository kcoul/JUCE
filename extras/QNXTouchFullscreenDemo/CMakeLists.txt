cmake_minimum_required(VERSION 3.22)

project(JUCEQNXTouchFullscreenDemo LANGUAGES C CXX)

set(JUCE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
get_filename_component(JUCE_ROOT_DIR "${JUCE_ROOT_DIR}" ABSOLUTE)

list(APPEND CMAKE_MODULE_PATH
    "${JUCE_ROOT_DIR}/extras/Build/CMake")

include(JUCEModuleSupport)
include(JUCEUtils)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory("${JUCE_ROOT_DIR}/modules" "${CMAKE_CURRENT_BINARY_DIR}/juce_modules")

set(generated_build_version_header "${CMAKE_CURRENT_BINARY_DIR}/GeneratedBuildVersion.h")

add_custom_target(juce_qnx_touch_fullscreen_demo_build_version ALL
    COMMAND "${CMAKE_COMMAND}"
            -DROOT="${JUCE_ROOT_DIR}"
            -DOUTPUT_HEADER="${generated_build_version_header}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildVersion.cmake"
    BYPRODUCTS "${generated_build_version_header}"
    VERBATIM)

juce_add_gui_app(JUCEQNXTouchFullscreenDemo
    PRODUCT_NAME "JUCE QNX Touch Fullscreen Demo"
    BUNDLE_ID com.juce.QNXTouchFullscreenDemo)

juce_generate_juce_header(JUCEQNXTouchFullscreenDemo)

add_dependencies(JUCEQNXTouchFullscreenDemo juce_qnx_touch_fullscreen_demo_build_version)

target_sources(JUCEQNXTouchFullscreenDemo PRIVATE
    Source/Main.cpp)

target_include_directories(JUCEQNXTouchFullscreenDemo PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(JUCEQNXTouchFullscreenDemo PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_JACK=0
    JUCE_ALSA=1
    JUCE_USE_FONTCONFIG=0
    SB_CONFIG_UNITY=1)

if(DEFINED ENV{QNX_TARGET})
    target_include_directories(JUCEQNXTouchFullscreenDemo PRIVATE
        "$ENV{QNX_TARGET}/usr/include/freetype2")

    target_link_libraries(JUCEQNXTouchFullscreenDemo PRIVATE
        "$ENV{QNX_TARGET}/aarch64le/usr/lib/libfreetype.so.24")
endif()

target_link_libraries(JUCEQNXTouchFullscreenDemo PRIVATE
    juce::juce_audio_devices
    juce::juce_gui_basics
    juce::juce_opengl
    juce::juce_osc
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    screen
    asound
    socket
    EGL
    GLESv2
    z
    expat)

set_target_properties(JUCEQNXTouchFullscreenDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
