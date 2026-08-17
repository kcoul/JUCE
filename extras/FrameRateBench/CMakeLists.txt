cmake_minimum_required(VERSION 3.22)

# VERSION is required: juce_add_gui_app falls back to PROJECT_VERSION and errors
# out without one. Keep in step with getApplicationVersion() in Source/Main.cpp.
project(JUCEFrameRateBench VERSION 0.1.0 LANGUAGES C CXX)

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

add_custom_target(juce_frame_rate_bench_build_version ALL
    # No quotes around the -D values: with VERBATIM, CMake quotes each argument
    # for the shell itself, so literal quotes here become part of the value and
    # the header gets written to a directory actually named '"'.
    COMMAND "${CMAKE_COMMAND}"
            -DROOT=${JUCE_ROOT_DIR}
            -DOUTPUT_HEADER=${generated_build_version_header}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildVersion.cmake"
    BYPRODUCTS "${generated_build_version_header}"
    VERBATIM)

juce_add_gui_app(JUCEFrameRateBench
    PRODUCT_NAME "JUCE Frame Rate Bench"
    BUNDLE_ID com.juce.FrameRateBench)

# No juce_generate_juce_header() on purpose: Source/Main.cpp includes the module
# headers directly, and generating JuceHeader.h would pull in the juceaide helper
# tool — which cross-compiles for the target and then cannot run on the build
# host, breaking build_frame_rate_bench_linux_cross.sh.

add_dependencies(JUCEFrameRateBench juce_frame_rate_bench_build_version)

target_sources(JUCEFrameRateBench PRIVATE
    Source/Main.cpp)

target_include_directories(JUCEFrameRateBench PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(JUCEFrameRateBench PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_JACK=0
    SB_CONFIG_UNITY=1)

# Fontconfig is disabled on QNX only, where fonts come from FreeType directly.
# On Linux it must stay ENABLED: with JUCE_USE_FONTCONFIG=0 the freetype backend's
# findSystemTypeface() simply returns nullptr — there is no directory-scan
# fallback — so no font is ever found and all text renders blank, including the
# on-screen FPS overlay.
if(DEFINED ENV{QNX_TARGET})
    target_compile_definitions(JUCEFrameRateBench PRIVATE JUCE_USE_FONTCONFIG=0)
endif()

if(DEFINED ENV{QNX_TARGET})
    target_include_directories(JUCEFrameRateBench PRIVATE
        "$ENV{QNX_TARGET}/usr/include/freetype2")

    target_link_libraries(JUCEFrameRateBench PRIVATE
        "$ENV{QNX_TARGET}/aarch64le/usr/lib/libfreetype.so.24")
endif()

target_link_libraries(JUCEFrameRateBench PRIVATE
    juce::juce_gui_basics
    juce::juce_opengl        # BENCH_RENDERER=opengl; defines JUCE_MODULE_AVAILABLE_juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Deliberately NOT juce::juce_recommended_lto_flags. The QNX side is built by
# build_frame_rate_bench_qnx.{bat,sh} at plain -O2 with no LTO, and a benchmark
# comparing two operating systems is only meaningful if both binaries were built
# with the same optimisation settings — otherwise it measures the toolchain.
# (LTO also makes the link single-threaded and slow.) Keep these in step: if the
# QNX scripts change optimisation level, change it here too.
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG" CACHE STRING "" FORCE)

# QNX needs the Screen/EGL libraries explicitly; on Linux JUCE pulls X11/GL itself.
if(DEFINED ENV{QNX_TARGET})
    target_link_libraries(JUCEFrameRateBench PRIVATE
        screen socket EGL GLESv2 z expat)
endif()

set_target_properties(JUCEFrameRateBench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    # Match the QNX unity-script output name (PRODUCT_NAME would give a
    # space-containing filename), so deploy.py and the runner scripts refer to
    # one binary name on every platform.
    OUTPUT_NAME "JUCEFrameRateBench")
