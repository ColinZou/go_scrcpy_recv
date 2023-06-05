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


add_definitions(-DWIN_DLL=1 -D_WIN32_WINNT=0x0601)

project("scrcpy_recv" LANGUAGES CXX C)

# static build

set(VCPKG_INCLUDE ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include)
set(VCPKG_LD_DIR ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib)

link_directories(${VCPKG_LD_DIR})

set(OPENCV_LIB bcrypt fmt spdlog lzma tiff libpng16 turbojpeg jpeg
    opencv_core4 opencv_imgcodecs4 opencv_imgproc4)

set(FFMPEG_LD_LIBS mfuuid strmbase zlib libwebp
    libsharpyuv avcodec avutil libx264 swscale swresample)

if(WIN32)
    set(SCRCPY_LINK_LIBS wsock32 ws2_32 ${OPENCV_LIB} ${FFMPEG_LD_LIBS})
else()
    set(SCRCPY_LINK_LIBS ${OPENCV_LIB} ${FFMPEG_LD_LIBS})
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)


