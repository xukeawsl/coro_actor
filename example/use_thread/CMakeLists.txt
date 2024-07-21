cmake_minimum_required(VERSION 3.10)

project(svr_use_thread)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../../../lib)

include_directories(
    ../../include
    ../../third-party/yalantinglibs/include
    ../../third-party/yalantinglibs/include/ylt/thirdparty
)

add_library(${PROJECT_NAME} SHARED
    svc_use_thread.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pthread
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)