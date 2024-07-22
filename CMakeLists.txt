cmake_minimum_required(VERSION 3.10)

project(coro_actor)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose Release or Debug" FORCE)
endif()

option(SPDLOG_INSTALL "Generate the spdlog install target" ON)
option(YAML_CPP_INSTALL "Generate the yaml-cpp install target" ON)

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb -Wall -Wno-unused-variable -Wno-sign-compare -Wno-unused-local-typedefs")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wno-unused -Wno-sign-compare -Wno-unused-variable")

add_subdirectory(third-party/spdlog)
add_subdirectory(third-party/yalantinglibs)
add_subdirectory(third-party/yaml-cpp)

file(GLOB_RECURSE srcs ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE hrds ${PROJECT_SOURCE_DIR}/include/*.h)

include(cmake/search_path.cmake)

add_library(${PROJECT_NAME}d SHARED ${srcs})
target_include_directories(${PROJECT_NAME}d PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third-party/yalantinglibs/include
    ${PROJECT_SOURCE_DIR}/third-party/yalantinglibs/include/ylt/thirdparty
)

target_link_libraries(${PROJECT_NAME}d PUBLIC
    yalantinglibs::yalantinglibs
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    pthread
    dl
)

add_library(coro_actor::coro_actor ALIAS ${PROJECT_NAME}d)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PUBLIC coro_actor::coro_actor)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

#
# Clang-Format
#
find_program(CLANG_FORMAT_EXECUTABLE
    NAME "clang-format"
    PATHS "/usr/bin" "/usr/local/bin"
)

if (CLANG_FORMAT_EXECUTABLE)
    message(STATUS "Successfully find program `clang-format`")
    message(STATUS "You can use the `make clang-format` command to automatically format the code style")
    add_custom_target(actor-format ALL
    COMMAND
        ${CLANG_FORMAT_EXECUTABLE} --style=file -i ${hdrs};${srcs};${PROJECT_SOURCE_DIR}/main.cpp
    COMMENT
        "Automatically format the code style"
    )
endif()

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}d ${PROJECT_NAME}
    LIBRARY
        DESTINATION lib
    RUNTIME
        DESTINATION bin
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include/)