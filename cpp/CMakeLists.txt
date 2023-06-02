# CMakeList.txt : CMake project for scrcpy_demo_app, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.8)

if(DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT $ENV{VCPKG_ROOT})
else()
    set(VCPKG_ROOT D:/vcpkg)
endif()

set(VCPKG_TARGET_TRIPLET "x64-windows-static")
set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

message("SCRCPY_DEBUG_ENABLED = " $ENV{SCRCPY_DEBUG_ENABLED})
if(DEFINED ENV{SCRCPY_DEBUG_ENABLED})
    if("2" STREQUAL $ENV{SCRCPY_DEBUG_ENABLED})
        add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
        message("ADDING -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
    else()
        add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
        message("ADDING -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG")
    endif()
endif()

set(GO_LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_definitions(-DWIN_DLL=1)

project("scrcpy_recv" LANGUAGES CXX C)

# static build
set(CMAKE_CXX_FLAGS_RELEASE "/MT")
set(CMAKE_CXX_FLAGS_DEBUG "/MTd")

set(VCPKG_INCLUDE ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include)
set(VCPKG_LD_DIR ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib)

link_directories(${VCPKG_LD_DIR})

set(OPENCV_LIB bcrypt fmt spdlog lzma tiff libpng16 turbojpeg jpeg opencv_core4 opencv_imgcodecs4 opencv_imgproc4)
set(FFMPEG_LD_LIBS mfuuid strmbase zlib libwebp libsharpyuv avcodec avutil libx264 swscale swresample)

# Enable Hot Reload for MSVC compilers if supported.
if(POLICY CMP0141)
  cmake_policy(set CMP0141 NEW)
endif()

add_subdirectory(src)
add_subdirectory(tests)

enable_testing()


