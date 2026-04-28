set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SIZEOF_VOID_P 8 CACHE INTERNAL "")
set(JUCE_TARGET_ARCHITECTURE aarch64 CACHE INTERNAL "QNX target architecture")
set(CMAKE_C_COMPILE_FEATURES c_std_11 CACHE INTERNAL "")
set(CMAKE_CXX_COMPILE_FEATURES
    cxx_alias_templates
    cxx_alignas
    cxx_alignof
    cxx_attributes
    cxx_auto_type
    cxx_binary_literals
    cxx_constexpr
    cxx_contextual_conversions
    cxx_decltype
    cxx_decltype_incomplete_return_types
    cxx_default_function_template_args
    cxx_defaulted_functions
    cxx_defaulted_move_initializers
    cxx_delegating_constructors
    cxx_deleted_functions
    cxx_explicit_conversions
    cxx_final
    cxx_func_identifier
    cxx_generalized_initializers
    cxx_generic_lambdas
    cxx_inheriting_constructors
    cxx_lambdas
    cxx_lambda_init_captures
    cxx_local_type_template_args
    cxx_long_long_type
    cxx_noexcept
    cxx_nonstatic_member_init
    cxx_nullptr
    cxx_override
    cxx_range_for
    cxx_raw_string_literals
    cxx_reference_qualified_functions
    cxx_relaxed_constexpr
    cxx_return_type_deduction
    cxx_right_angle_brackets
    cxx_rvalue_references
    cxx_sizeof_member
    cxx_static_assert
    cxx_strong_enums
    cxx_thread_local
    cxx_trailing_return_types
    cxx_unicode_literals
    cxx_uniform_initialization
    cxx_unrestricted_unions
    cxx_user_literals
    cxx_variable_templates
    cxx_variadic_macros
    cxx_variadic_templates
    cxx_std_11
    cxx_std_14
    cxx_std_17
    cxx_std_20
    CACHE INTERNAL "")

if(NOT DEFINED ENV{QNX_HOST} OR NOT DEFINED ENV{QNX_TARGET})
    message(FATAL_ERROR "QNX_HOST and QNX_TARGET must be set before loading this toolchain file.")
endif()

set(QNX_TARGET_TRIPLE "12.2.0,gcc_ntoaarch64le" CACHE STRING "QNX SDP compiler target")
set(QNX_HOST "$ENV{QNX_HOST}" CACHE PATH "QNX host tools directory")
set(QNX_TARGET "$ENV{QNX_TARGET}" CACHE PATH "QNX target sysroot")
set(QNX_BIN_DIR "${QNX_HOST}/usr/bin" CACHE INTERNAL "")
set(QNX_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/qnx_compat/include" CACHE INTERNAL "")
set(QNX_FREETYPE_INCLUDE_DIR "${QNX_TARGET}/usr/include/freetype2" CACHE INTERNAL "")
set(QNX_FREETYPE_LIBRARY "${QNX_TARGET}/aarch64le/usr/lib/libfreetype.so.24" CACHE INTERNAL "")

if(CMAKE_HOST_WIN32)
    set(QNX_HOST_EXECUTABLE_SUFFIX ".exe" CACHE INTERNAL "")
else()
    set(QNX_HOST_EXECUTABLE_SUFFIX "" CACHE INTERNAL "")
endif()

set(CMAKE_SYSROOT "${QNX_TARGET}")
set(CMAKE_STAGING_PREFIX "${CMAKE_BINARY_DIR}/stage")

set(CMAKE_C_COMPILER qcc CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER q++ CACHE FILEPATH "")
set(CMAKE_AR "${QNX_BIN_DIR}/ntoaarch64-ar${QNX_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${QNX_BIN_DIR}/ntoaarch64-ranlib${QNX_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_NM "${QNX_BIN_DIR}/ntoaarch64-nm${QNX_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${QNX_BIN_DIR}/ntoaarch64-objcopy${QNX_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_OBJDUMP "${QNX_BIN_DIR}/ntoaarch64-objdump${QNX_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_STRIP "${QNX_BIN_DIR}/ntoaarch64-strip${QNX_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_AR "${CMAKE_AR}" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_RANLIB "${CMAKE_RANLIB}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER_AR "${CMAKE_AR}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER_RANLIB "${CMAKE_RANLIB}" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
set(CMAKE_C_COMPILER_WORKS TRUE CACHE BOOL "" FORCE)
set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE BOOL "" FORCE)

set(CMAKE_C_COMPILER_TARGET "${QNX_TARGET_TRIPLE}" CACHE STRING "")
set(CMAKE_CXX_COMPILER_TARGET "${QNX_TARGET_TRIPLE}" CACHE STRING "")

set(QNX_COMMON_DEFINES "-D_QNX_SOURCE -D__EXT_QNX -D__EXT_UNIX_MISC" CACHE INTERNAL "")
set(QNX_C_FLAGS
    "-V${QNX_TARGET_TRIPLE} -I\"${QNX_COMPAT_INCLUDE_DIR}\" -I\"${QNX_FREETYPE_INCLUDE_DIR}\" ${QNX_COMMON_DEFINES}"
    CACHE INTERNAL "")
set(QNX_CXX_FLAGS
    "-V${QNX_TARGET_TRIPLE} -I\"${QNX_COMPAT_INCLUDE_DIR}\" -I\"${QNX_FREETYPE_INCLUDE_DIR}\" ${QNX_COMMON_DEFINES} -include strings.h -DM_PI=3.14159265358979323846"
    CACHE INTERNAL "")
set(QNX_LINKER_FLAGS "-V${QNX_TARGET_TRIPLE} \"${QNX_FREETYPE_LIBRARY}\"" CACHE INTERNAL "")

set(CMAKE_C_FLAGS_INIT
    "${QNX_C_FLAGS}"
    CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_INIT
    "${QNX_CXX_FLAGS}"
    CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${QNX_LINKER_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${QNX_LINKER_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${QNX_LINKER_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS "${QNX_C_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${QNX_CXX_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "${QNX_LINKER_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS "${QNX_LINKER_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS "${QNX_LINKER_FLAGS}" CACHE STRING "" FORCE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_HAVE_LIBC_PTHREAD 1 CACHE INTERNAL "")
set(Threads_FOUND TRUE CACHE INTERNAL "")
set(CMAKE_USE_PTHREADS_INIT 1 CACHE INTERNAL "")
set(CMAKE_THREAD_LIBS_INIT "" CACHE INTERNAL "")

set(CMAKE_FIND_ROOT_PATH
    "${QNX_TARGET}"
    "${QNX_HOST}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
