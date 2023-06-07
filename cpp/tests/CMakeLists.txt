set(GO_LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../src")

set(CMAKE_CXX_FLAGS_RELEASE "/MT")
set(CMAKE_CXX_FLAGS_DEBUG "/MTd")
set(SPDLOG_LIBS fmt spdlog)

set(LOGGING_FILES ${SRC_ROOT}/logging.h ${SRC_ROOT}/logging.cpp)
set(UTILS_FILES ${SRC_ROOT}/utils.h ${SRC_ROOT}/utils.cpp)
set(FRAME_IMG_CALLBACK_FILES ${SRC_ROOT}/frame_img_callback.h ${SRC_ROOT}/frame_img_callback.cpp)
set(SCRCPY_CTRL_HANDLE_FILES ${SRC_ROOT}/scrcpy_ctrl_handler.h ${SRC_ROOT}/scrcpy_ctrl_handler.cpp)

set(SRC_LIB_FILES "${SRC_ROOT}/scrcpy_support.h" "${SRC_ROOT}/scrcpy_support.cpp"
    "${SRC_ROOT}/socket_lib.h" "${SRC_ROOT}/socket_lib.cpp"
    "${SRC_ROOT}/model.h" "${SRC_ROOT}/logging.h" "${SRC_ROOT}/logging.cpp"
    "${SRC_ROOT}/scrcpy_video_decoder.h" "${SRC_ROOT}/scrcpy_video_decoder.cpp"
    "${SRC_ROOT}/frame_img_callback.h" "${SRC_ROOT}/frame_img_callback.cpp"
    "${SRC_ROOT}/utils.h" "${SRC_ROOT}/utils.cpp"
    "${SRC_ROOT}/scrcpy_ctrl_handler.h" "${SRC_ROOT}/scrcpy_ctrl_handler.cpp"
    "${GO_LIB_ROOT}/scrcpy_recv/scrcpy_recv.h")

set(TEST_SVR_FILES test_svr.h test_svr.cpp test_client.h test_client.cpp)

add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG -DLOG_FILENAME="scrcpy_tests.log")

include_directories(${VCPKG_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR}
    ${GO_LIB_ROOT} ${SRC_ROOT})

add_executable(test_utils test_utils.cpp ${UTILS_FILES} ${LOGGING_FILES})
target_link_libraries(test_utils ${SPDLOG_LIBS})

add_executable(test_frame_img_callback test_frame_img_callback.cpp ${UTILS_FILES} ${LOGGING_FILES} ${FRAME_IMG_CALLBACK_FILES})
target_link_libraries(test_frame_img_callback ${SPDLOG_LIBS})

add_executable(test_scrcpy_ctrl_handler test_scrcpy_ctrl_handler.cpp ${UTILS_FILES} ${LOGGING_FILES} ${TEST_SVR_FILES} ${SCRCPY_CTRL_HANDLE_FILES})
target_link_libraries(test_scrcpy_ctrl_handler ${SPDLOG_LIBS} wsock32 ws2_32)

add_executable(test_scrcpy_support test_scrcpy_support.cpp ${SRC_LIB_FILES} ${TEST_SVR_FILES})
target_link_libraries(test_scrcpy_support ${SCRCPY_LINK_LIBS})

if(CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET test_scrcpy_support PROPERTY CXX_STANDARD 20)
endif()

add_test(NAME test_utils COMMAND $<TARGET_FILE:test_utils>)
add_test(NAME test_frame_img_callback COMMAND $<TARGET_FILE:test_frame_img_callback>)
add_test(NAME test_scrcpy_ctrl_handler COMMAND $<TARGET_FILE:test_scrcpy_ctrl_handler>)
add_test(NAME test_scrcpy_support COMMAND $<TARGET_FILE:test_scrcpy_support> ${CMAKE_CURRENT_SOURCE_DIR}/data.h264)


