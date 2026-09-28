cmake_minimum_required(VERSION 3.20)
project(mafia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_executable(mafia src/main.cpp)
target_include_directories(mafia PRIVATE include)
target_link_libraries(mafia PRIVATE Threads::Threads)

enable_testing()
add_subdirectory(tests)
