# CMake toolchain for cross-building the bench for aarch64 Linux (Raspberry Pi
# OS / Ubuntu) from an x86_64 host — see build_frame_rate_bench_linux_cross.sh.
#
# Assumes a Debian/Ubuntu multiarch host:
#   sudo dpkg --add-architecture arm64
#   sudo apt install g++-aarch64-linux-gnu
#   sudo apt install libx11-dev:arm64 libxext-dev:arm64 libxinerama-dev:arm64 \
#        libxrandr-dev:arm64 libxcursor-dev:arm64 libxcomposite-dev:arm64 \
#        libxrender-dev:arm64 libfreetype-dev:arm64 libgl-dev:arm64 \
#        libasound2-dev:arm64
#
# Under multiarch the headers are shared in /usr/include and the libraries live
# in /usr/lib/aarch64-linux-gnu, so the find root stays "/" and the search is
# steered by CMAKE_LIBRARY_ARCHITECTURE instead of a separate sysroot tree.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# AARCH64_GCC_SUFFIX pins a specific GCC version (e.g. "-12"), so the Linux
# binary can be built with the same compiler generation as the QNX SDP's GCC
# 12.2. Comparing a GCC 13 Linux build against a GCC 12 QNX build measures the
# toolchain as much as the OS.
set(_gcc_suffix "$ENV{AARCH64_GCC_SUFFIX}")
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc${_gcc_suffix})
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++${_gcc_suffix})

set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

# Host binaries must stay host binaries; libraries and headers must come from
# the arm64 side. NEVER for PROGRAM stops CMake picking up arm64 helper exes it
# cannot execute.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(CMAKE_FIND_ROOT_PATH /usr/lib/aarch64-linux-gnu /usr)

# pkg-config must read the arm64 .pc files, not the host's, or JUCE's module
# dependency checks will silently resolve to x86_64 libraries and fail at link.
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "/usr/lib/aarch64-linux-gnu/pkgconfig")
set(PKG_CONFIG_EXECUTABLE pkg-config)
