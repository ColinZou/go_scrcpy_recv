set(GO_LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
message("GO_LIB_ROOT=${GO_LIB_ROOT}")
include_directories(${VCPKG_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR} ${GO_LIB_ROOT})

set(CMAKE_CXX_FLAGS_RELEASE "/MT")
set(CMAKE_CXX_FLAGS_DEBUG "/MTd")

set(LIB_FILES "scrcpy_support.h" "scrcpy_support.cpp"
    "socket_lib.h" "socket_lib.cpp"
    "model.h" "logging.h" "logging.cpp"
    "scrcpy_video_decoder.h" "scrcpy_video_decoder.cpp"
    "frame_img_callback.h" "frame_img_callback.cpp"
    "utils.h" "utils.cpp"
    "scrcpy_ctrl_handler.h" "scrcpy_ctrl_handler.cpp"
    "${GO_LIB_ROOT}/scrcpy_recv/scrcpy_recv.h")

add_library(scrcpy_recv SHARED ${LIB_FILES})

install(TARGETS scrcpy_recv DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
install(FILES "${GO_LIB_ROOT}/scrcpy_recv/scrcpy_recv.h"
    DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/scrcpy_recv/scrcpy_recv.h")

# Add source to this project's executable.
add_executable(scrcpy_demo_app
    "scrcpy_demo_app.cpp" "scrcpy_demo_app.h"
    ${LIB_FILES})

option(CMAKE_USE_WIN32_THREADS_INIT "using WIN32 threads" ON)
option(gtest_disable_pthreads "Disable uses of pthreads in gtest." ON)

if(CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET scrcpy_demo_app PROPERTY CXX_STANDARD 20)
  set_property(TARGET scrcpy_recv PROPERTY CXX_STANDARD 20)
endif()

target_link_libraries(scrcpy_demo_app PUBLIC scrcpy_recv)
target_link_libraries(scrcpy_recv PUBLIC ${SCRCPY_LINK_LIBS})

