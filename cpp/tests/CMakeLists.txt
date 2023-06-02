set(GO_LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../src)

set(CMAKE_CXX_FLAGS_RELEASE "/MT")
set(CMAKE_CXX_FLAGS_DEBUG "/MTd")
set(SPDLOG_LIBS fmt spdlog)
set(LOGGING_FILES ${SRC_ROOT}/logging.h ${SRC_ROOT}/logging.cpp)

add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG -DLOG_FILENAME="scrcpy_tests.log")

include_directories(${VCPKG_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR}
    ${GO_LIB_ROOT} ${SRC_ROOT})

add_executable(test_utils test_utils.cpp ${SRC_ROOT}/utils.h ${SRC_ROOT}/utils.cpp ${LOGGING_FILES})
target_link_libraries(test_utils ${SPDLOG_LIBS})

add_test(NAME test_utils COMMAND $<TARGET_FILE:test_utils>)
