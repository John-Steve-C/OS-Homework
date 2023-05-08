cmake_minimum_required(VERSION 3.16)
project(OS2023_Practices C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_FLAGS -g)

include_directories(practice-1/coroutine)

add_executable(test
        practice-1/coroutine/coroutine.c
        practice-1/coroutine/coroutine.h
        practice-1/coroutine/main.c
        practice-1/coroutine/utils.h)

target_link_libraries(test pthread)     # 将 pthread 的外部库链接到 project 中

#include_directories(practice-2)
#
#add_executable(test
#        practice-2/practice_2-1/buddy.c
#        practice-2/practice_2-1/buddy.h
#        practice-2/practice_2-1/main.c
#        practice-2/practice_2-1/utils.h
#        )