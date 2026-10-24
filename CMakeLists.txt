cmake_minimum_required(VERSION 3.14)
# Prefer g++ when available (clang may lack a linkable libstdc++ in some images).
if(NOT CMAKE_CXX_COMPILER AND EXISTS /usr/bin/g++)
    set(CMAKE_CXX_COMPILER /usr/bin/g++ CACHE FILEPATH "" FORCE)
endif()
project(http_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(http_server
    src/main.cpp
    src/util.cpp
    src/http_request.cpp
    src/http_response.cpp
    src/connection.cpp
    src/server.cpp
)

target_include_directories(http_server PRIVATE include)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(http_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
