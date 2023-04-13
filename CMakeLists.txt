cmake_minimum_required(VERSION 3.16)
project(OS2023_Practices C)

set(CMAKE_C_STANDARD 11)

include_directories(practice-1/coroutine)

add_executable(OS2023_Practices
        practice-1/coroutine/coroutine.c
        practice-1/coroutine/coroutine.h
        practice-1/coroutine/main.c
        practice-1/coroutine/utils.h)
