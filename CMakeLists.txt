cmake_minimum_required(VERSION 3.16)
project(HttpServer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults to Release for performance
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

# Warnings
add_compile_options(
  -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)

# ──────────────────────────────────────────────
# Core server library (shared between binary + tests)
# ──────────────────────────────────────────────
add_library(server_lib STATIC
  src/server/TcpServer.cpp
  src/concurrency/ThreadPool.cpp
  src/http/HttpRequest.cpp
  src/http/HttpResponse.cpp
  src/http/HttpParser.cpp
  src/handlers/FileHandler.cpp
  src/logging/Logger.cpp
)

if(APPLE)
  target_sources(server_lib PRIVATE src/server/KqueueServer.cpp)
endif()

target_include_directories(server_lib PUBLIC src)

# ──────────────────────────────────────────────
# Main executable
# ──────────────────────────────────────────────
add_executable(http_server src/main.cpp)
target_link_libraries(http_server PRIVATE server_lib)

# ──────────────────────────────────────────────
# Tests
# ──────────────────────────────────────────────
add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE server_lib)

add_executable(test_lru tests/test_lru.cpp)
target_link_libraries(test_lru PRIVATE server_lib)

# ──────────────────────────────────────────────
# Install target: copy www/ next to binary
# ──────────────────────────────────────────────
add_custom_command(TARGET http_server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/www
    ${CMAKE_BINARY_DIR}/www
  COMMENT "Copying www/ to build directory"
)

message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
