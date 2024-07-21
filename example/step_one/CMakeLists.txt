cmake_minimum_required(VERSION 3.10)

project(svr_step_one)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../../../lib)

include_directories(
    ../../include
    ../../third-party/yalantinglibs/include
    ../../third-party/yalantinglibs/include/ylt/thirdparty
)

add_library(${PROJECT_NAME} SHARED
    svc_step_one.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)