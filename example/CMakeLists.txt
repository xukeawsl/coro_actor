cmake_minimum_required(VERSION 3.10)

project(all_example)

# 获取当前目录下的所有子目录
file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

# 遍历每个子目录并添加到构建系统中
foreach(subdir ${SUBDIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${subdir} AND NOT ${subdir} STREQUAL "build")
        add_subdirectory(${subdir})
    endif()
endforeach()