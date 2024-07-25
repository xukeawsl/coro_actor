cmake_minimum_required(VERSION 3.10)

project(svr_use_sqlite)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose Release or Debug" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb -Wall -Wno-unused-variable -Wno-sign-compare -Wno-unused-local-typedefs")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wno-unused -Wno-sign-compare -Wno-unused-variable")

add_definitions(-DSQLITE_THREADSAFE=0)
add_definitions(-DSQLITE_DISABLE_LOAD_EXTENSION)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../../../lib)

include_directories(
    ../../include
    ../../third-party/yalantinglibs/include
    ../../third-party/yalantinglibs/include/ylt/thirdparty
)

add_library(${PROJECT_NAME} SHARED
    svc_use_sqlite.cpp
    sqlite3.c
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)