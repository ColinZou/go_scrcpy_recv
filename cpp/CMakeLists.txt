# CMakeList.txt : CMake project for scrcpy_desktop, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

IF (DEFINED ENV{VCPKG_ROOT})
    SET(VCPKG_ROOT $ENV{VCPKG_ROOT})
ELSE()
    SET(VCPKG_ROOT D:/vcpkg)
ENDIF()

SET(VCPKG_TARGET_TRIPLET "x64-windows-static")
SET(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

set(GO_LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." )

add_definitions(-DWIN_DLL=1)

project ("scrcpy_desktop" LANGUAGES CXX C)

# static build
set(CMAKE_CXX_FLAGS_RELEASE "/MT")
set(CMAKE_CXX_FLAGS_DEBUG "/MTd")

SET(VCPKG_INCLUDE ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include)
SET(VCPKG_LD_DIR ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib)

include_directories(${VCPKG_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR} ${GO_LIB_ROOT})

link_directories(${VCPKG_LD_DIR})

SET(OPENCV_LIB bcrypt fmt spdlog lzma tiff libpng16 turbojpeg jpeg opencv_core4 opencv_imgcodecs4 opencv_imgproc4)
SET(FFMPEG_LD_LIBS mfuuid strmbase zlib libwebp libsharpyuv avcodec avutil libx264 swscale swresample)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()


add_library(scrcpy_recv SHARED
    "scrcpy_support.h" "scrcpy_support.cpp"
    "socket_lib.h" "socket_lib.cpp"
    "model.h" "logging.h" "logging.cpp"
    "scrcpy_video_decoder.h" "scrcpy_video_decoder.cpp"
    "frame_img_callback.h" "frame_img_callback.cpp"
    "utils.h" "utils.cpp"
    "scrcpy_ctrl_handler.h" "scrcpy_ctrl_handler.cpp"
    "${GO_LIB_ROOT}/scrcpy_recv/scrcpy_recv.h")

install(TARGETS scrcpy_recv DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
install(FILES "${GO_LIB_ROOT}/scrcpy_recv/scrcpy_recv.h" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/scrcpy_recv/scrcpy_recv.h")

# Add source to this project's executable.
add_executable (scrcpy_desktop
    "scrcpy_desktop.cpp" "scrcpy_desktop.h"
    "scrcpy_support.h" "scrcpy_support.cpp"
    "socket_lib.h" "socket_lib.cpp"
    "model.h" "logging.h" "logging.cpp"
    "scrcpy_video_decoder.h" "scrcpy_video_decoder.cpp"
    "frame_img_callback.h" "frame_img_callback.cpp"
    "utils.h" "utils.cpp"
    "scrcpy_ctrl_handler.h" "scrcpy_ctrl_handler.cpp"
    "${GO_LIB_ROOT}/scrcpy_recv/scrcpy_recv.h")

option(CMAKE_USE_WIN32_THREADS_INIT "using WIN32 threads" ON)
option(gtest_disable_pthreads "Disable uses of pthreads in gtest." ON)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET scrcpy_desktop PROPERTY CXX_STANDARD 20)
endif()

if(WIN32)
    target_link_libraries(scrcpy_desktop PUBLIC wsock32 ws2_32 ${FFMPEG_LD_LIBS} ${OPENCV_LIB})
    target_link_libraries(scrcpy_recv PUBLIC wsock32 ws2_32 ${FFMPEG_LD_LIBS} ${OPENCV_LIB})
else()
    target_link_libraries(scrcpy_desktop PUBLIC ${FFMPEG_LD_LIBS} ${OPENCV_LIB})
    target_link_libraries(scrcpy_recv PUBLIC ${FFMPEG_LD_LIBS} ${OPENCV_LIB})
endif()

