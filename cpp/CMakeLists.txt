# CMakeList.txt : CMake project for scrcpy_desktop, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../lib/ffmpeg-n5.1.3-win64-gpl-shared-5.1")
set(OPENCV_PREBUILT_HOME "${CMAKE_CURRENT_SOURCE_DIR}/../lib/opencv")
set(OPENCV_CUSTOM_HOME "${CMAKE_CURRENT_SOURCE_DIR}/../lib/opencv-customized")
set(GO_LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." )


add_definitions(-DWIN_DLL=1)

IF (DEFINED ENV{OPENCV_CUSTOMIZED})
    SET (OPENCV_VC vc17)
    SET (OPENCV_DLL opencv_core470 opencv_imgcodecs470)
    SET (OPENCV_DLL_DEBUG opencv_core470d opencv_imgcodecs470d)
    SET(OPENCV_HOME ${OPENCV_CUSTOM_HOME})
ELSE()
    SET (OPENCV_VC vc16)
    SET (OPENCV_DLL opencv_world470)
    SET (OPENCV_DLL_DEBUG opencv_world470d)
    SET(OPENCV_HOME ${OPENCV_PREBUILT_HOME})
ENDIF()

IF (${CMAKE_BUILD_TYPE} STREQUAL Debug)
    SET(DEBUG_LIB_POSTFIX d)
    SET(OPENCV_LIB ${OPENCV_DLL_DEBUG})
ELSE()
    SET(OPENCV_LIB ${OPENCV_DLL})
    SET(DEBUG_LIB_POSTFIX )
ENDIF()

MESSAGE("OpenCV home will be ${OPENCV_HOME}")

include_directories(${FFMPEG_ROOT}/include ${CMAKE_CURRENT_SOURCE_DIR} ${OPENCV_HOME}/include ${GO_LIB_ROOT})

link_directories(${FFMPEG_ROOT}/lib ${OPENCV_HOME}/x64/${OPENCV_VC}/lib)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()

project ("scrcpy_desktop" LANGUAGES CXX C)

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
  target_link_libraries(scrcpy_desktop PUBLIC wsock32 ws2_32 avcodec avutil swscale ${OPENCV_LIB})
  target_link_libraries(scrcpy_recv PUBLIC wsock32 ws2_32 avcodec avutil swscale ${OPENCV_LIB})
else()
  target_link_libraries(scrcpy_desktop PUBLIC avcodec avutil swscale ${OPENCV_LIB})
  target_link_libraries(scrcpy_recv PUBLIC avcodec avutil swscale ${OPENCV_LIB})
endif()

add_custom_command(TARGET scrcpy_desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${FFMPEG_ROOT}/bin/avcodec-59.dll"
        $<TARGET_FILE_DIR:scrcpy_desktop>)
add_custom_command(TARGET scrcpy_desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${FFMPEG_ROOT}/bin/avutil-57.dll"
        $<TARGET_FILE_DIR:scrcpy_desktop>)
add_custom_command(TARGET scrcpy_desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${FFMPEG_ROOT}/bin/swscale-6.dll"
        $<TARGET_FILE_DIR:scrcpy_desktop>)
add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${FFMPEG_ROOT}/bin/swresample-4.dll"
        $<TARGET_FILE_DIR:scrcpy_desktop>)

add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${FFMPEG_ROOT}/bin/avcodec-59.dll"
        $<TARGET_FILE_DIR:scrcpy_recv>)
add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${FFMPEG_ROOT}/bin/avutil-57.dll"
        $<TARGET_FILE_DIR:scrcpy_recv>)
add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${FFMPEG_ROOT}/bin/swscale-6.dll"
        $<TARGET_FILE_DIR:scrcpy_recv>)
add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${FFMPEG_ROOT}/bin/swresample-4.dll"
        $<TARGET_FILE_DIR:scrcpy_recv>)


IF (DEFINED ENV{OPENCV_CUSTOMIZED})
add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${OPENCV_HOME}/x64/${OPENCV_VC}/bin/opencv_core470${DEBUG_LIB_POSTFIX}.dll"
        $<TARGET_FILE_DIR:scrcpy_desktop>)
add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${OPENCV_HOME}/x64/${OPENCV_VC}/bin/opencv_imgcodecs470${DEBUG_LIB_POSTFIX}.dll"
        $<TARGET_FILE_DIR:scrcpy_desktop>)

add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${OPENCV_HOME}/x64/${OPENCV_VC}/bin/opencv_core470${DEBUG_LIB_POSTFIX}.dll"
        $<TARGET_FILE_DIR:scrcpy_recv>)
add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${OPENCV_HOME}/x64/${OPENCV_VC}/bin/opencv_imgcodecs470${DEBUG_LIB_POSTFIX}.dll"
        $<TARGET_FILE_DIR:scrcpy_recv>)
ELSE()
add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${OPENCV_HOME}/x64/${OPENCV_VC}/bin/${OPENCV_LIB}.dll"
        $<TARGET_FILE_DIR:scrcpy_desktop>)

add_custom_command(TARGET scrcpy_recv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${OPENCV_HOME}/x64/${OPENCV_VC}/bin/${OPENCV_LIB}.dll"
        $<TARGET_FILE_DIR:scrcpy_recv>)
ENDIF()
