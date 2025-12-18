cmake_minimum_required(VERSION 3.16)

project(webserver)

set(CMAKE_CXX_STANDER 11)

add_executable(webserver
    src/cow_locker.cpp
    src/cow_threadpool.cpp
    src/main.cpp
)
target_include_directories(webserver
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)